/*
 *  TOPPERS/FMP3 ESP32-S3 移植 — ESP-IDF 割込み確保 API のシム（G-1）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ============================================================================
 *  設計メモ
 *  ============================================================================
 *
 *  【何のためのものか】
 *  ESP32-S3 の **GDMA を使うために必要な唯一の欠けピース**。
 *  カメラ（LCD_CAM）と audio（I2S）は S3 では **GDMA を通らないとデータが動かない**
 *  （I2S に CPU から書ける FIFO が無い＝`soc_caps.h:229`／LCD_CAM も同様）。
 *  そして GDMA を使う IDF 側コードは `esp_intr_alloc_intrstatus()` を要求する。
 *  本ファイルはそれを FMP3 の静的割込み機構の上に実装する。
 *
 *  【前提】FMP3 は `ATT_ISR` を廃止し **`CRE_ISR`** を持つ。同一 `intno` の ISR を
 *  `isrpri` 順に呼ぶ `_kernel_inthdr_<intno>` を生成する
 *  （`fmp3_core/kernel/interrupt.py:370-417`）。★**本ポートで既に現役**——
 *  シリアルコンソールがこの機構で動く（`chip_serial.cfg:16-17`）。
 *  したがって残るのは **静的 API と動的 API のギャップだけ**であり、それは
 *  「静的プールで動的生成を模倣する」型そのものである。本ファイルはそれ。
 *
 *  【方式】
 *  cfg 側に `CFG_INT` + `CRE_ISR` のスロットを**事前宣言**しておき、実行時の
 *  `esp_intr_alloc_intrstatus()` が
 *    (1) 空きスロットを確保し
 *    (2) **割込みマトリクス**の MAP レジスタへスロットの CPU 線番号を書いて
 *        ペリフェラルのソースをその線へ配線し
 *    (3) `intrstatus` フィルタとハンドラを覚える
 *  という形で「動的確保」を模倣する。ISR は 1 本（`esp_shim_intr_isr`）で、
 *  `CRE_ISR` の `exinf` にスロット番号を載せて区別する。
 *
 *  【★CPU 割込み線の選び方】
 *  本ポートの `INTNO` は **Xtensa の CPU 割込み線番号(0〜31)** であって、
 *  ESP32-S3 のペリフェラル割込みソース番号ではない。マトリクスがソース→線を写す。
 *  `XCHAL_INT*_TYPE` が `EXTERN_LEVEL`（＝ペリフェラルを載せられる）でレベル 1 の線は
 *    0,1,2,3,4,5,8,9,12,13,17,18
 *  【`esp-idf/components/xtensa/esp32s3/include/xtensa/config/core-isa.h:383-414,421-`】
 *  このうち使用中:
 *    6=tick(TIMER型) / 7=SW / 13=IPI / 5 or 17=コンソール /
 *    0-3=Wi-Fi/BT blob 用(esp/shim/esp_shim.cfg:255-262) / 23,27=BT Level3
 *  ⇒ ★**4, 8, 9, 12 を使う。** m5 単体では 0〜3 も空いているが、将来 wifi/ble と
 *  統合したときに衝突しないよう**安全側**に寄せる。
 *
 *  【MAP レジスタ番地】
 *  `DR_REG_INTERRUPT_CORE0_BASE(0x600C2000) + source*4`
 *  【`esp-idf/components/soc/esp32s3/register/soc/interrupt_core0_reg.h`】
 *    LCD_CAM(24)→0x060(:209) / I2S0(25)→0x064(:217) /
 *    DMA_IN_CH0(66)→0x108(:545) / DMA_OUT_CH0(71)→0x11C(:585)
 *  いずれも `source*4` と一致する。
 *
 *  【この版でやらないこと（意図的な限定）】
 *   - `esp_intr_alloc()`（フィルタ無し版）は**定義しない**。
 *     `esp/bt/bt_shim.c:775` が BT 専用実装を持っており、将来 ble 構成と統合する
 *     ときに**多重定義になる**。統合方針が決まるまで踏み込まない。
 *   - `ESP_INTR_FLAG_SHARED` は受け取るが**特別扱いしない**（上記(b)のとおり本ポートで
 *     共有は起きない）。★共有が必要になったら、同じ `intno` に `CRE_ISR` を
 *     複数並べれば FMP3 側が `isrpri` 順に呼ぶ——機構は既にある。
 *   - IRAM 常駐（`ESP_INTR_FLAG_IRAM`）は**未対応**。cache 無効期間が本ポートに
 *     存在するかを確かめていないため（設計 D-3）。フラグは無視する。
 *  ★これらは「黙って無視」ではなく、**呼ばれた事実をカウンタに残す**（診断可能にする）。
 *
 *  ============================================================================
 *  ★★使用上の制約 — **PRC1 タスク文脈専用**
 *  ============================================================================
 *  本シムは以下の 3 点で「どのコアから呼んでもよい API」には**なっていない**。
 *  現用途（m5 構成・PRC1 の単一タスクから確保して使う）では成立するが、
 *  用途を広げるときは**必ずここを直すこと**。
 *
 *   (1) **コア親和性が非対称**。`ena_int`/`dis_int` は **呼んだコアの INTENABLE** を
 *       操作するのに対し、割込みマトリクスの MAP は **CORE0 固定**
 *       （`INTR_CORE0_MAP_BASE`）である。⇒ PRC2 から `esp_intr_disable()` を呼んでも
 *       **止まらない**（PRC2 の INTENABLE を落とすだけで、配送先は CORE0 のまま）。
 *   (2) **スロット確保に排他が無い**。`alloc` の空きスロット探索〜`in_use=true` は
 *       read-modify-write であり、2 者が同時に呼べば同じスロットを掴む。
 *   (3) 上記より、**確保・解放・enable/disable はすべて PRC1 のタスク文脈から**
 *       行うこと。ISR からは呼ばない（`ena_int`/`dis_int` はサービスコール）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>

#include <stdint.h>
#include <esp_err.h>			/* esp_err_t / ESP_OK / ESP_ERR_* */

/*
 *  スロット数と、各スロットに割り当てる CPU 割込み線。
 *  ★cfg 側（CFG_INT/CRE_ISR）と**必ず一致**させること。片方だけ変えると
 *  「配線したのに誰も受けない」割込みができる。
 */
#define ESP_SHIM_INTR_NSLOT		4

static const uint32_t	esp_shim_intr_cpu_line[ESP_SHIM_INTR_NSLOT] = {
	4U, 8U, 9U, 12U
};

/*  割込みマトリクス（コア0）の MAP レジスタ。source 番号 * 4 が番地。 */
#define INTR_CORE0_MAP_BASE		0x600C2000U
#define INTR_MAP_REG(src)		((uint32_t *)(uintptr_t)(INTR_CORE0_MAP_BASE + (uint32_t)(src) * 4U))

/*
 *  マトリクスから外すときに書く値。
 *
 *  ★★2026-07-24 訂正（レビュー Part C-2）。旧コメントは
 *  「0 を書くと**どの CPU 線にも繋がない**」と書いていたが、**事実と逆**である。
 *  ESP32-S3 の割込みマトリクスに「非接続」を表す符号は無く、**0 は CPU 線 0 への
 *  配線**である。本 m5 構成の cfg には `CFG_INT(0)` が無いので、外したはずの
 *  ペリフェラルが線 0 で**未処理割込み**になり得る。さらに wifi/ble 構成では
 *  線 0〜3 が blob 用に**現役**（`esp/shim/esp_shim.cfg:255-262`）なので、
 *  統合したら他人の線へ配線することになる。
 *
 *  ⇒ IDF と同じく**内部型（マトリクスから駆動できない）の線へ退避する**。
 *     `ETS_INVALID_INUM` = **6**
 *     【`esp-idf/components/soc/esp32s3/include/soc/soc.h:242`】
 *     線 6 は Xtensa の TIMER 型（本ポートでは tick が載る内部割込み）で、
 *     マトリクスの出力は繋がっていない＝ペリフェラル信号は配送されない。
 *  ★これは `esp_intr_free()` 後に要因が起きても未処理割込みにならないことを
 *    意味する。AC `reallocdel` の相 A（free 後に実際に転送を起こす）は
 *    **この変更が入っていないと安全に実施できない**。
 */
#define INTR_MAP_DISABLED		6U		/* = ETS_INVALID_INUM */

typedef void (*esp_shim_intr_handler_t)(void *arg);

/*
 *  ★IDF の `intr_handle_t` は `struct intr_handle_data_t *`。
 *  その実体をここで定義する（IDF 本体はリンクしない）。
 */
struct intr_handle_data_t {
	uint32_t					idx;		/* スロット番号 */
	bool_t						valid;
};

struct esp_shim_intr_slot {
	struct intr_handle_data_t	handle;
	int							source;			/* ETS_*_INTR_SOURCE */
	volatile uint32_t			*status_reg;	/* intrstatus フィルタ（NULL=無条件） */
	uint32_t					status_mask;
	esp_shim_intr_handler_t		handler;
	void						*arg;
	bool_t						in_use;
	bool_t						enabled;
};

static struct esp_shim_intr_slot	esp_shim_intr_slot[ESP_SHIM_INTR_NSLOT];

/*
 *  ★診断カウンタ（外から読めるように非 static）。
 *  「黙って無視した」を残さないためのもの。AC でこれを見る。
 */
volatile uint32_t	esp_shim_intr_n_alloc;			/* 確保に成功した回数       */
volatile uint32_t	esp_shim_intr_n_alloc_fail;		/* 空きが無くて失敗した回数 */
volatile uint32_t	esp_shim_intr_n_isr[ESP_SHIM_INTR_NSLOT];	/* ISR 到達回数（フィルタ前） */
volatile uint32_t	esp_shim_intr_n_call[ESP_SHIM_INTR_NSLOT];	/* handler 呼出し回数（フィルタ後） */
volatile uint32_t	esp_shim_intr_n_flag_iram;		/* IRAM 要求を受けた回数（未対応） */
volatile uint32_t	esp_shim_intr_n_flag_shared;	/* SHARED 要求を受けた回数   */

/*
 *  ISR 本体。`CRE_ISR` の exinf にスロット番号が入る。
 *
 *  ★`intrstatus` フィルタ: IDF ではこれが「共有線上で自分宛かを判定する」機構だが、
 *  本ポートでは共有が起きないので**必須ではない**。それでも実装するのは
 *  (a) IDF 側コードの意味論をそのまま保つため
 *  (b) ★AC の negative control（決してセットされないビットを指定したスロットの
 *      handler が 1 回も呼ばれないこと）が**この分岐を実際に通る**ため
 */
void
esp_shim_intr_isr(intptr_t exinf)
{
	uint32_t					idx = (uint32_t) exinf;
	struct esp_shim_intr_slot	*p;

	if (idx >= (uint32_t) ESP_SHIM_INTR_NSLOT) {
		return;						/* 起こり得ないが黙って落ちない */
	}
	p = &esp_shim_intr_slot[idx];
	esp_shim_intr_n_isr[idx]++;

	if (!p->in_use || !p->enabled || (p->handler == NULL)) {
		return;
	}
	if (p->status_reg != NULL) {
		if ((*(p->status_reg) & p->status_mask) == 0U) {
			return;					/* ★自分宛ではない */
		}
	}
	esp_shim_intr_n_call[idx]++;
	(*(p->handler))(p->arg);
}

/*
 *  ESP-IDF 互換 API
 */

esp_err_t
esp_intr_alloc_intrstatus(int source, int flags,
						  uint32_t intrstatusreg, uint32_t intrstatusmask,
						  void (*handler)(void *), void *arg,
						  struct intr_handle_data_t **ret_handle)
{
	uint32_t					idx;
	struct esp_shim_intr_slot	*p;
	bool_t						found = false;

	/*  ★未対応フラグを黙って捨てない。回数を残す。  */
	if ((flags & 0x00000400) != 0) {		/* ESP_INTR_FLAG_IRAM */
		esp_shim_intr_n_flag_iram++;
	}
	if ((flags & 0x00000200) != 0) {		/* ESP_INTR_FLAG_SHARED */
		esp_shim_intr_n_flag_shared++;
	}

	for (idx = 0U; idx < (uint32_t) ESP_SHIM_INTR_NSLOT; idx++) {
		if (!esp_shim_intr_slot[idx].in_use) {
			found = true;
			break;
		}
	}
	if (!found) {
		esp_shim_intr_n_alloc_fail++;
		syslog_1(LOG_ERROR, "esp_shim_intr: スロット枯渇 (source=%d)", source);
		return(ESP_ERR_NO_MEM);
	}

	p = &esp_shim_intr_slot[idx];
	p->source      = source;
	p->status_reg  = (intrstatusreg != 0U)
					 ? (volatile uint32_t *)(uintptr_t) intrstatusreg : NULL;
	p->status_mask = intrstatusmask;
	p->handler     = handler;
	p->arg         = arg;
	p->enabled     = true;
	p->in_use      = true;
	p->handle.idx  = idx;
	p->handle.valid = true;

	/*  ★割込みマトリクス：ペリフェラルのソースをこのスロットの CPU 線へ配線する。
	 *  ここを書かないと、ISR は登録されているのに**永久に呼ばれない**。  */
	sil_wrw_mem(INTR_MAP_REG(source), esp_shim_intr_cpu_line[idx]);

	/*
	 *  ★★2026-07-24 追加（レビュー Part C-1）: **CPU 割込み線を有効化する。**
	 *
	 *  【欠けていて何が起きるか】`esp_intr_free()` は `dis_int()` で線を止める。
	 *  そのスロット（＝同じ CPU 線）を後から再確保しても、線は止まったままなので
	 *  **ハードウェアからの配送が死ぬ**。初回だけは cfg の `TA_ENAINT` で
	 *  たまたま有効だったため露出していなかった。
	 *  IDF の `esp_intr_alloc*()` は確保した割込みを有効状態で返すので、
	 *  ここで有効化するのが**意味論としても正しい**。
	 *  ★初回（TA_ENAINT 済み）に対しては冪等。
	 *  ★タスク文脈からのみ呼ぶこと（サービスコール。冒頭の制約 (3)）。
	 */
	(void) ena_int((INTNO) esp_shim_intr_cpu_line[idx]);

	esp_shim_intr_n_alloc++;
	syslog_2(LOG_NOTICE, "esp_shim_intr: source=%d -> CPU線=%d",
			 source, (int) esp_shim_intr_cpu_line[idx]);

	if (ret_handle != NULL) {
		*ret_handle = &p->handle;
	}
	return(ESP_OK);
}

/*
 *  ★2026-07-22 修正: enable/disable は **CPU 割込み線そのもの**を操作する。
 *
 *  【初版の誤りと、実機で起きたこと】
 *  初版はソフトウェアフラグ `enabled` を落とすだけだった。しかし GDMA の割込みは
 *  **レベル割込み**なので、ISR が起動して「無効だから」と何もせずに戻ると
 *  **要因がクリアされないまま再入し続ける＝storm** になり、タスクが飢餓に陥る。
 *  実際に AC-G5（disable 中は呼ばれないこと）の試験で run が完走しなかった
 *  （`build/g1m2m2/s6c-run1.log`）。
 *
 *  ⇒ IDF の `esp_intr_disable()` と同じく **割込み線を止める**（`dis_int`）。
 *  こうすれば要因は保留されたまま CPU へ届かず、storm しない。
 *  ★フラグも併せて維持する（`esp_intr_free` 後の取りこぼし防止）。
 */
esp_err_t
esp_intr_enable(struct intr_handle_data_t *handle)
{
	if ((handle == NULL) || !handle->valid
		|| (handle->idx >= (uint32_t) ESP_SHIM_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	esp_shim_intr_slot[handle->idx].enabled = true;
	(void) ena_int((INTNO) esp_shim_intr_cpu_line[handle->idx]);
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(struct intr_handle_data_t *handle)
{
	if ((handle == NULL) || !handle->valid
		|| (handle->idx >= (uint32_t) ESP_SHIM_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	/*  ★線を止めるのが先。フラグだけ落として線を開けたままにすると
	 *  レベル割込みが storm する（上記）。 */
	(void) dis_int((INTNO) esp_shim_intr_cpu_line[handle->idx]);
	esp_shim_intr_slot[handle->idx].enabled = false;
	return(ESP_OK);
}

/*
 *  ★実際に解放する。no-op にしない（「機能が無いものは失敗を返す」原則。
 *  黙って成功を返す no-op は、後で「解放したのに使えない」を生む）。
 */
esp_err_t
esp_intr_free(struct intr_handle_data_t *handle)
{
	struct esp_shim_intr_slot	*p;

	if ((handle == NULL) || !handle->valid
		|| (handle->idx >= (uint32_t) ESP_SHIM_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	p = &esp_shim_intr_slot[handle->idx];

	/*  マトリクスを外してから状態を落とす（順序が逆だと、外す前に
	 *  割込みが来て in_use=false のスロットを踏む）。 */
	sil_wrw_mem(INTR_MAP_REG(p->source), INTR_MAP_DISABLED);
	/*  ★線も止める（マトリクスを外しただけでは、保留中の要因が残り得る）。 */
	(void) dis_int((INTNO) esp_shim_intr_cpu_line[handle->idx]);

	p->enabled      = false;
	p->in_use       = false;
	p->handler      = NULL;
	p->arg          = NULL;
	p->status_reg   = NULL;
	p->status_mask  = 0U;
	p->handle.valid = false;
	return(ESP_OK);
}

/*
 *  ★★診断アクセサ: **その割込み源が実際に発火したか**を外から数える。
 *
 *  【なぜ要るか】音声の段 D-0 で `micraw=FAIL_ALL_ZERO_SAMPLES` が出た——
 *  ES7210 の初期化はレジスタ読み返しで確認でき（`es7210=PASS_INIT_REGS_READBACK`）、
 *  `mic_task` も走っている（`isRecording()` が 1 を経由）のに **256 標本すべて 0**。
 *  ⇒ 残る筋の一つが「**I2S の割込みが上がらず `i2s_channel_read` が時間切れし、
 *  calloc されたままの 0 が返っている**」である。
 *  ★`esp_shim_intr_n_isr[]`／`_n_call[]` は既に数えているが、**スロット番号でしか
 *  引けない**ので外から使えなかった。source で引ける口を開ける。
 *
 *  ★見つからないときは **-1 を返す**（0 を返すと「割当てられているが 0 回」と
 *  区別できず、**未割当てを「発火しなかった」と誤読する**）。
 */
int32_t	esp_shim_intr_nisr_by_source(int source);
int32_t
esp_shim_intr_nisr_by_source(int source)
{
	int	i;

	for (i = 0; i < ESP_SHIM_INTR_NSLOT; i++) {
		if (esp_shim_intr_slot[i].in_use && (esp_shim_intr_slot[i].source == source)) {
			return((int32_t) esp_shim_intr_n_isr[i]);
		}
	}
	return(-1);
}

int32_t	esp_shim_intr_ncall_by_source(int source);
int32_t
esp_shim_intr_ncall_by_source(int source)
{
	int	i;

	for (i = 0; i < ESP_SHIM_INTR_NSLOT; i++) {
		if (esp_shim_intr_slot[i].in_use && (esp_shim_intr_slot[i].source == source)) {
			return((int32_t) esp_shim_intr_n_call[i]);
		}
	}
	return(-1);
}

/*
 *  ★スロットの総覧（source を**当てずに**全部出すため）。
 *  ★「source=66 は I2S1 だろう」と**推測して数えるのをやめる**ための口である。
 *  in_use なら true を返し、source と 2 つのカウンタを書く。
 */
/*  ★戻り値は **int**（C++ 側から extern "C" で引くので `bool_t` の実体に依存しない）。 */
int	esp_shim_intr_slot_info(int idx, int *source, uint32_t *n_isr, uint32_t *n_call);
int
esp_shim_intr_slot_info(int idx, int *source, uint32_t *n_isr, uint32_t *n_call)
{
	if ((idx < 0) || (idx >= ESP_SHIM_INTR_NSLOT) || !esp_shim_intr_slot[idx].in_use) {
		return(0);
	}
	if (source != NULL) { *source = esp_shim_intr_slot[idx].source; }
	if (n_isr  != NULL) { *n_isr  = esp_shim_intr_n_isr[idx]; }
	if (n_call != NULL) { *n_call = esp_shim_intr_n_call[idx]; }
	return(1);
}

int	esp_shim_intr_nslot(void);
int
esp_shim_intr_nslot(void)
{
	return(ESP_SHIM_INTR_NSLOT);
}
