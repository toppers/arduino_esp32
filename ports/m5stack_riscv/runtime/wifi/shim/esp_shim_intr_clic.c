/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  ESP-IDF 割込み確保 API の CLIC 版シム（C-1）
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
 *  何のためのものか（2026-08-15・C-1）
 *  ============================================================================
 *  seam 版 Ethernet（PoE-P4）と Tab5 表示（DSI）の**共通前提**。
 *  どちらの IDF ドライバも `esp_intr_alloc()` を呼ぶが、seam は FreeRTOS も
 *  IDF の割込みアロケータもリンクしない。⇒ FMP3 の静的割込み機構の上に
 *  「動的確保に見えるもの」を作る必要がある。本ファイルがそれである。
 *
 *  設計の選定と根拠（スロット数の決め方・線の選び方・線17 の食い違いの決着）は
 *  **`esp/shim/esp_shim_intr_clic_lines.h` の冒頭**に書いた。ここには書かない
 *  （Xtensa 側で「.c と .cfg の両方に線番号がベタ書きされていて実際に食い違った」
 *   という事故があったため、線に関する記述は 1 箇所に集める）。
 *
 *  ============================================================================
 *  Xtensa 版（`esp/shim/esp_shim_intr.c`）との差
 *  ============================================================================
 *  形は同じ（cfg にスロットを事前宣言し、実行時に空きを払い出す）。違うのは 2 点:
 *
 *  (1) **配線の書き方**。Xtensa は ROM 関数 `intr_matrix_set()` を使う。
 *      P4 は ROM を使わず**レジスタへ直接書く**:
 *          *(0x500D6000 + 4*source) = <CLIC 線番号（+16 込みの絶対値）>
 *      根拠は 3 つとも一次情報で揃っている:
 *        - `esp-idf/components/hal/include/hal/interrupt_clic_ll.h` の
 *          `interrupt_clic_ll_route()` が同じ番地へ、`HAL_ASSERT(intr_num < 48)` の
 *          値（＝絶対 CLIC 線番号）を書く。
 *        - 本 repo の `fmp3/arch/riscv_gcc/esp32p4/chip_serial.c` が
 *          `INTMTX_MAP(0, USB_SERIAL_JTAG_INTR_SOURCE) = INTNO_SIO(16)` を書き、
 *          **実機でコンソール割込みが動いている**。
 *        - 方式(a) の `esp/eth/os/eth_os_fmp3.c` が
 *          `P4_INTMTX_CORE0(92) = INTNO_EMAC(46)` を書き、**実機で EMAC が動いた**（段E）。
 *      ⇒ 「MAP に書く値は絶対 CLIC 線番号」は推測ではなく、実機 2 例で裏が取れている。
 *      ROM 関数（`esp_rom_route_intr_matrix` ＝ ROM の `intr_matrix_set`）が内部で
 *      +16 するかどうかは**未確認**（`idf_riscv_intr_impl.md` §8 が「要確認」に挙げている）
 *      が、本ファイルは ROM を通らないのでその不確実性を持ち込まない。
 *
 *  (2) **tick/IPI と構造的に衝突しない**。P4 の tick(mtime=線7)・IPI(msip=線3) は
 *      **内部線**で、割込みマトリクスからは駆動されない。Xtensa 版が
 *      検査(A)(B) を必要としたのは、あちらでは tick/IPI が同じ 0..31 の空間に
 *      居たからである。P4 でも検査は張るが（線表ヘッダの (A)(C)）、それは
 *      「誰かが 16 未満を書いた」を捕まえるためである。
 *
 *  ============================================================================
 *  使用上の制約 — **PRC1 のタスク文脈専用**
 *  ============================================================================
 *  Xtensa 版と同じ 3 点で、「どのコアから呼んでもよい API」には**なっていない**。
 *
 *   (1) **コア親和性**。割込みマトリクスは **CORE0 側**（`INTMTX_MAP(0, ...)`）
 *       にしか書かない。cfg の `CFG_INT`/`CRE_ISR` も `CLASS(CLS_PRC1)` に置く
 *       （`clic_kernel.trb` の `TargetCheckCfgInt` が、複数 PE への affinity を持つ
 *        クラスの `CFG_INT` を拒否する）。⇒ **受けるコアは PRC1 固定**である。
 *   (2) **スロット確保に排他が無い**。空きスロット探索から `in_use=true` までが
 *       read-modify-write であり、2 者が同時に呼べば同じスロットを掴む。
 *   (3) 上記より、**確保・解放・enable/disable はすべて PRC1 のタスク文脈から**。
 *       ISR からは呼ばない（`ena_int`/`dis_int` はサービスコール）。
 *  用途を広げるときは必ずここを直すこと。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>

#include <stdint.h>

#include "esp32p4.h"					/* INTMTX_MAP / CLIC_* */
#include "esp_shim_intr_clic.h"
#include "esp_shim_intr_clic_lines.h"	/* 線表の単一真実源（NSLOT/LINEn/INTPRI） */

/*
 *  各スロットの CLIC 線。**定義は線表ヘッダにしか無い**（cfg も同じものを読む）。
 */
static const uint32_t	esp_shim_clic_intr_line[ESP_SHIM_CLIC_INTR_NSLOT] = {
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE0,
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE1,
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE2,
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE3,
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE4,
	(uint32_t) ESP_SHIM_CLIC_INTR_LINE5
};

/*
 *  割込みマトリクスから外すときに MAP へ書く値。
 *
 *  IDF は `esp_intr_disable()` で `esp_rom_route_intr_matrix(cpu, source,
 *  INT_MUX_DISABLED_INTNO)` を呼ぶ（`esp-idf/components/esp_hw_support/intr_alloc.c`。
 *  `INT_MUX_DISABLED_INTNO` は同ファイル内の `#define ... 6`）。C 実装の経路
 *  （`components/riscv/interrupt_clic.c` の `intr_matrix_route()`）はこれに
 *  `RV_EXTERNAL_INT_OFFSET(16)` を足して `interrupt_clic_ll_route()` へ渡すので、
 *  **CLIC 線 22** に着地する。本ファイルは MAP へ直接書くので、その 22 を書く。
 *
 *  **なぜ 22 が安全か**（推測ではなく構造から）:
 *   - 線 22 は本 repo の予約表に既に載っており（`cmake/a1_p4_intno_audit.sh`）、
 *     `CFG_INT` が無い。⇒ ISR が登録されていない。
 *   - `clic_context_initialize()`（`fmp3_core/arch/riscv_gcc/common/clic_kernel_impl.c`）
 *     が起動時に全 48 本の CTRL レジスタを 0 にするので **IE=0**。
 *     ⇒ ここへ配線された要因は CPU へ配送されない（＝未処理割込みにならない）。
 *  Xtensa 版が「0 を書くと非接続」という**事実と逆**のコメントを持っていて
 *  2026-07-24 に訂正された前例があるので、根拠を明示して置く。
 *
 *  **未確認**: ROM 関数側（`esp_rom_route_intr_matrix` ＝ ROM `intr_matrix_set`）が
 *  内部で +16 するかは確かめていない。本ファイルは ROM を呼ばないので影響しない。
 */
#define ESP_SHIM_CLIC_INTR_MAP_DISABLED		22U

/*
 *  割込みソース番号の上限（範囲外の MAP 書込みで無関係なレジスタを壊さないため）。
 *  `esp-idf/components/soc/esp32p4/include/soc/interrupts.h` の enum 末尾
 *  `ETS_MAX_INTR_SOURCE` が **136**（enum を機械的に評価して得た値。
 *  `cmake/a1_p4_intr_source_audit.sh` が configure 時に再導出して照合する）。
 */
#define ESP_SHIM_CLIC_INTR_NUM_SOURCE		136

/*
 *  IDF の `intr_handle_t` は `struct intr_handle_data_t *`。
 *  その実体をここで定義する（IDF 本体はリンクしない）。
 */
struct intr_handle_data_t {
	uint32_t	idx;		/* スロット番号 */
	uint8_t		valid;
};

struct esp_shim_clic_intr_slot {
	struct intr_handle_data_t	handle;
	int							source;			/* ETS_*_INTR_SOURCE */
	volatile uint32_t			*status_reg;	/* intrstatus フィルタ（NULL=無条件） */
	uint32_t					status_mask;
	void						(*handler)(void *);
	void						*arg;
	uint8_t						in_use;
	uint8_t						enabled;
};

static struct esp_shim_clic_intr_slot	esp_shim_clic_intr_slot[ESP_SHIM_CLIC_INTR_NSLOT];

/*
 *  診断カウンタ。「黙って無視した」を残さないためのもの。AC でこれを見る。
 */
volatile uint32_t	esp_shim_clic_intr_n_alloc;
volatile uint32_t	esp_shim_clic_intr_n_alloc_fail;
volatile uint32_t	esp_shim_clic_intr_n_free;
volatile uint32_t	esp_shim_clic_intr_n_flag_iram;
volatile uint32_t	esp_shim_clic_intr_n_flag_shared;
volatile uint32_t	esp_shim_clic_intr_n_flag_level;
volatile uint32_t	esp_shim_clic_intr_n_flag_edge;
volatile uint32_t	esp_shim_clic_intr_n_ena_fail;
volatile uint32_t	esp_shim_clic_intr_n_dis_fail;
volatile uint32_t	esp_shim_clic_intr_n_clr_fail;

static volatile uint32_t	esp_shim_clic_intr_n_isr[ESP_SHIM_CLIC_INTR_NSLOT];
static volatile uint32_t	esp_shim_clic_intr_n_call[ESP_SHIM_CLIC_INTR_NSLOT];

/*
 *  ============================================================================
 *  `ena_int()`/`dis_int()` の失敗を**捨てない**
 *  ============================================================================
 *  `ena_int(intno)` は `check_intno_cfg()` が偽なら `E_OBJ` を返す。
 *  **それはまさに、本ファイルの線表と cfg の `CFG_INT`/`CRE_ISR` がずれた状態**
 *  である。線表の単一真実源化で「両方を直し忘れる」形のずれは塞いだが、
 *  「cfg 側だけスロットを 1 本消す」等のずれはまだ作れる。
 *  そのとき実機ログ **1 行**で分かるようにしておく。
 *
 *  「失敗しても先へ進む」のは意図的である。ここで確保ごと失敗させると、
 *  「線が開かないだけ（＝割込みが来ないが他は動く）」を、より広い停止に格上げして
 *  しまう。**診断は出す・カウンタに残す・進む。**
 */
/*
 *  線を開ける前に、**溜まっているペンディングを落とす**。
 *
 *  【この関数を足した経緯（推測が外れた記録として残す）】
 *  C-1 の probe（run2）で slot0 の `n_isr` が期待 256 に対して **257** だった。
 *  最初に立てた読みは「`free` で無効化している間に溜まった CLIC ペンディングが
 *  `ena_int()` で 1 回配送された」で、その対策としてこの `clr_int()` を入れた。
 *  **run3 で反証された**——`clr_int` を入れても 257 のまま、`n_clr_fail=0`
 *  （クリア自体は成功している）。⇒ 推測は誤りだった。
 *
 *  【本当の原因（run4 で測って確定した事実）】
 *  probe に「確保**前**の要因レジスタ」と「確保直後の到達回数」を出させたところ
 *      CLICINTR I-1a ... pre_from_cpu1=1 n_a_after_alloc=1
 *  ——**実 ESP-IDF 2nd-stage bootloader から制御を受け取った時点で、
 *  `HP_SYSTEM_CPU_INT_FROM_CPU_1` の BIT(0) が既に立っていた**。
 *  つまり 257 = 1（確保した瞬間に、既に鳴っていた要因が 1 回配送された）
 *  + 128（I-2）+ 128（I-3b）で、**すべて説明がつく**。レベル要因なので
 *  `clr_int()` では消えない（クリアしても即座に再 assert する）のが道理である。
 *
 *  【それでもこの `clr_int()` を残す理由】
 *  IDF も同じ場所で同じことをしている——`esp_intr_alloc*()` は
 *  `ESP_INTR_FLAG_EDGE` のとき `esp_cpu_intr_edge_ack(intr)` を呼んでから
 *  有効化する（`esp-idf/components/esp_hw_support/intr_alloc.c`）。
 *  本シムは線をスロットとして**使い回す**ので、前の利用者が残したエッジの
 *  残骸が次の利用者へ配送される筋は実在する。トリガ型に依らず落としておく。
 *  **「これが 257 を直した」とは言わない**（直していない）。
 *
 *  【利用者への含意】`esp_intr_alloc()` は、**確保した瞬間に既に鳴っている要因**を
 *  1 回配送し得る。ハンドラは「まだ何も起きていないはず」を前提にしてはならない。
 *
 *  `clr_int()` は `check_intno_cfg()` を通る（＝cfg に `CFG_INT` のある線でのみ
 *  効く）。ここは必ずスロットの線なので通る。失敗は `ena_int` と同じ扱いにする。
 */
static void
esp_shim_clic_intr_clr_line(uint32_t line)
{
	ER		ercd;

	ercd = clr_int((INTNO) line);
	if (ercd < 0) {
		esp_shim_clic_intr_n_clr_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_clic: clr_int(CLIC線=%d) 失敗 ercd=%d",
				 (int) line, (int) ercd);
	}
}

static void
esp_shim_clic_intr_ena_line(uint32_t line)
{
	ER		ercd;

	esp_shim_clic_intr_clr_line(line);
	ercd = ena_int((INTNO) line);
	if (ercd < 0) {
		esp_shim_clic_intr_n_ena_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_clic: ena_int(CLIC線=%d) 失敗 ercd=%d"
				 " (cfg の CFG_INT/CRE_ISR がこの線を登録していない疑い)",
				 (int) line, (int) ercd);
	}
}

static void
esp_shim_clic_intr_dis_line(uint32_t line)
{
	ER		ercd;

	ercd = dis_int((INTNO) line);
	if (ercd < 0) {
		esp_shim_clic_intr_n_dis_fail++;
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_clic: dis_int(CLIC線=%d) 失敗 ercd=%d"
				 " (cfg の CFG_INT/CRE_ISR がこの線を登録していない疑い)",
				 (int) line, (int) ercd);
	}
}

/*
 *  割込みマトリクス配線。`pid` は **0 起点の hart id**（`INTMTX_MAP` の引数。
 *  `CLINT_CORE_BASE(pid)` の方は 1 起点の FMP3 prcid なので、混同しないこと
 *  ——`esp32p4.h` の中でこの 2 つは非対称である）。
 *  本シムは PRC1（＝hart 0）固定。
 */
static void
esp_shim_clic_intr_route(int source, uint32_t line)
{
	sil_wrw_mem(INTMTX_MAP(0, (uint32_t) source), line);
}

/*
 *  ISR 本体。`CRE_ISR` の `exinf` にスロット番号が入る。
 */
void
esp_shim_clic_intr_isr(intptr_t exinf)
{
	uint32_t						idx = (uint32_t) exinf;
	struct esp_shim_clic_intr_slot	*p;

	if (idx >= (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT) {
		return;						/* 起こり得ないが黙って落ちない */
	}
	p = &esp_shim_clic_intr_slot[idx];
	esp_shim_clic_intr_n_isr[idx]++;

	if ((p->in_use == 0U) || (p->enabled == 0U) || (p->handler == NULL)) {
		return;
	}
	if (p->status_reg != NULL) {
		if ((*(p->status_reg) & p->status_mask) == 0U) {
			return;					/* 自分宛ではない */
		}
	}
	esp_shim_clic_intr_n_call[idx]++;
	(*(p->handler))(p->arg);
}

/*
 *  ============================================================================
 *  ESP-IDF 互換 API
 *  ============================================================================
 */

esp_err_t
esp_intr_alloc_intrstatus(int source, int flags,
						  uint32_t intrstatusreg, uint32_t intrstatusmask,
						  void (*handler)(void *), void *arg,
						  struct intr_handle_data_t **ret_handle)
{
	uint32_t						idx;
	uint32_t						found = (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT;
	struct esp_shim_clic_intr_slot	*p;

	/*
	 *  未対応・非対応のフラグを**黙って捨てない**。回数を残し、ログにも出す。
	 *   - IRAM        … cache 無効期間の扱いを P4 で確かめていない（未対応）
	 *   - SHARED      … 本シムは 1 線 1 ソース。共有が要るなら同じ `intno` に
	 *                   `CRE_ISR` を複数並べれば FMP3 が `isrpri` 順に呼ぶ（機構は在る）
	 *   - LEVELn      … `CFG_INT` が静的なので実行時には効かせられない
	 *                   （全スロットが `ESP_SHIM_CLIC_INTR_INTPRI`＝CLIC level 4）
	 *   - EDGE        … 同上（トリガ型も `CFG_INT` の `TA_EDGE` で静的に決まる）
	 */
	if ((flags & ESP_INTR_FLAG_IRAM) != 0) {
		esp_shim_clic_intr_n_flag_iram++;
	}
	if ((flags & ESP_INTR_FLAG_SHARED) != 0) {
		esp_shim_clic_intr_n_flag_shared++;
	}
	if ((flags & (ESP_INTR_FLAG_LEVEL1 | ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_LEVEL3
				  | ESP_INTR_FLAG_LEVEL4 | ESP_INTR_FLAG_LEVEL5 | ESP_INTR_FLAG_LEVEL6
				  | ESP_INTR_FLAG_NMI)) != 0) {
		esp_shim_clic_intr_n_flag_level++;
	}
	if ((flags & ESP_INTR_FLAG_EDGE) != 0) {
		esp_shim_clic_intr_n_flag_edge++;
	}

	if ((source < 0) || (source >= ESP_SHIM_CLIC_INTR_NUM_SOURCE)) {
		syslog_1(LOG_ERROR, "esp_shim_intr_clic: source=%d は範囲外", source);
		return(ESP_ERR_INVALID_ARG);
	}
	if (handler == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}

	/*
	 *  **同じソースの二重確保は fail-closed で拒否する**（AC I-4a の「定義」）。
	 *
	 *  【なぜ黙って 2 本目のスロットを渡さないか】割込みマトリクスの MAP は
	 *  ソース 1 個につき**行き先 1 本**である。2 本目を確保して MAP を上書きすると、
	 *  1 本目のハンドルは「有効に見えるのに二度と呼ばれない」死んだ状態になる。
	 *  それは黙って起きるので、実機では「たまに割込みが来ない」という最悪の形で出る。
	 *  ⇒ **成功を返さない。**IDF も共有指定なしの二重確保は許さない
	 *    （共有は `ESP_INTR_FLAG_SHARED` を要求する）。
	 */
	for (idx = 0U; idx < (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT; idx++) {
		if ((esp_shim_clic_intr_slot[idx].in_use != 0U)
			&& (esp_shim_clic_intr_slot[idx].source == source)) {
			syslog_2(LOG_ERROR,
					 "esp_shim_intr_clic: source=%d は既にスロット%d で確保済み"
					 " (二重確保は MAP の上書きになるので拒否する)",
					 source, (int) idx);
			return(ESP_ERR_INVALID_STATE);
		}
	}

	for (idx = 0U; idx < (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT; idx++) {
		if (esp_shim_clic_intr_slot[idx].in_use == 0U) {
			found = idx;
			break;
		}
	}
	if (found >= (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT) {
		esp_shim_clic_intr_n_alloc_fail++;
		/*  BL-H-9 は実機ログのこの 1 行から診断できた。文面を弱めないこと。  */
		syslog_2(LOG_ERROR,
				 "esp_shim_intr_clic: スロット枯渇 (source=%d, NSLOT=%d)"
				 " -> esp_shim_intr_clic_lines.h の利用者表に行を足すこと",
				 source, ESP_SHIM_CLIC_INTR_NSLOT);
		return(ESP_ERR_NO_MEM);
	}
	idx = found;

	p = &esp_shim_clic_intr_slot[idx];
	p->source      = source;
	p->status_reg  = (intrstatusreg != 0U)
					 ? (volatile uint32_t *)(uintptr_t) intrstatusreg : NULL;
	p->status_mask = intrstatusmask;
	p->handler     = handler;
	p->arg         = arg;
	p->enabled     = 1U;
	p->in_use      = 1U;
	p->handle.idx  = idx;
	p->handle.valid = 1U;

	/*
	 *  配線してから線を開ける（逆順だと、配線前に残留ペンディングを拾い得る）。
	 */
	esp_shim_clic_intr_route(source, esp_shim_clic_intr_line[idx]);

	/*
	 *  **CLIC 線を有効化する。** cfg は `TA_NULL`（起動時は開けない）で宣言して
	 *  あるので、ここが唯一の開け口である。
	 *  Xtensa 版が 2026-07-24 に踏んだ欠陥（`esp_intr_free` が線を止めた後、
	 *  同じスロットを再確保しても線が止まったままでハードウェアからの配送が死ぬ）
	 *  の再発防止でもある。IDF の `esp_intr_alloc*()` も確保した割込みを
	 *  有効状態で返すので、意味論としても正しい。
	 */
	esp_shim_clic_intr_ena_line(esp_shim_clic_intr_line[idx]);

	esp_shim_clic_intr_n_alloc++;
	syslog_3(LOG_NOTICE, "esp_shim_intr_clic: source=%d -> CLIC線=%d (slot=%d)",
			 source, (int) esp_shim_clic_intr_line[idx], (int) idx);

	if (ret_handle != NULL) {
		*ret_handle = &p->handle;
	}
	return(ESP_OK);
}

esp_err_t
esp_intr_alloc(int source, int flags,
			   void (*handler)(void *), void *arg,
			   struct intr_handle_data_t **ret_handle)
{
	/*  フィルタ無し版は `intrstatusreg = 0` の薄い包み（IDF と同じ関係）。  */
	return(esp_intr_alloc_intrstatus(source, flags, 0U, 0U, handler, arg, ret_handle));
}

/*
 *  enable/disable は **CLIC 線そのもの**を操作する。
 *
 *  【フラグだけ落とすのでは駄目な理由】Xtensa 版の初版はソフトウェアフラグ
 *  `enabled` を落とすだけだった。しかし**レベル割込み**では、ISR が起動して
 *  「無効だから」と何もせずに戻ると**要因がクリアされないまま再入し続ける＝storm**
 *  になり、タスクが飢餓に陥る（実機で run が完走しなかった）。
 *  ⇒ IDF の `esp_intr_disable()` と同じく**線を止める**。
 *  P4 の主な利用者（EMAC・DSI ブリッジ・GPIO）はいずれもレベル割込みなので、
 *  この教訓はそのまま効く。
 */
esp_err_t
esp_intr_enable(struct intr_handle_data_t *handle)
{
	if ((handle == NULL) || (handle->valid == 0U)
		|| (handle->idx >= (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	esp_shim_clic_intr_slot[handle->idx].enabled = 1U;
	/*  disable でマトリクスを外している可能性があるので、配線し直してから開ける。  */
	esp_shim_clic_intr_route(esp_shim_clic_intr_slot[handle->idx].source,
							 esp_shim_clic_intr_line[handle->idx]);
	esp_shim_clic_intr_ena_line(esp_shim_clic_intr_line[handle->idx]);
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(struct intr_handle_data_t *handle)
{
	if ((handle == NULL) || (handle->valid == 0U)
		|| (handle->idx >= (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	/*  線を止めるのが先。フラグだけ落として線を開けたままにすると storm する。  */
	esp_shim_clic_intr_dis_line(esp_shim_clic_intr_line[handle->idx]);
	/*  IDF の `esp_intr_disable()` と同じくマトリクスも外す（要因が保留されない）。  */
	esp_shim_clic_intr_route(esp_shim_clic_intr_slot[handle->idx].source,
							 ESP_SHIM_CLIC_INTR_MAP_DISABLED);
	esp_shim_clic_intr_slot[handle->idx].enabled = 0U;
	return(ESP_OK);
}

/*
 *  実際に解放する。no-op にしない（「機能が無いものは失敗を返す」原則。
 *  黙って成功を返す no-op は、後で「解放したのに使えない」を生む）。
 */
esp_err_t
esp_intr_free(struct intr_handle_data_t *handle)
{
	struct esp_shim_clic_intr_slot	*p;

	if ((handle == NULL) || (handle->valid == 0U)
		|| (handle->idx >= (uint32_t) ESP_SHIM_CLIC_INTR_NSLOT)) {
		return(ESP_ERR_INVALID_ARG);
	}
	p = &esp_shim_clic_intr_slot[handle->idx];

	/*  マトリクスを外してから状態を落とす（順序が逆だと、外す前に割込みが
	 *  来て in_use=0 のスロットを踏む）。  */
	esp_shim_clic_intr_route(p->source, ESP_SHIM_CLIC_INTR_MAP_DISABLED);
	/*  線も止める（マトリクスを外しただけでは、保留中の要因が残り得る）。  */
	esp_shim_clic_intr_dis_line(esp_shim_clic_intr_line[handle->idx]);

	p->enabled      = 0U;
	p->in_use       = 0U;
	p->handler      = NULL;
	p->arg          = NULL;
	p->status_reg   = NULL;
	p->status_mask  = 0U;
	p->handle.valid = 0U;
	esp_shim_clic_intr_n_free++;
	syslog_2(LOG_NOTICE, "esp_shim_intr_clic: free source=%d (slot=%d)",
			 p->source, (int) handle->idx);
	return(ESP_OK);
}

/*
 *  ============================================================================
 *  診断アクセサ
 *  ============================================================================
 */

int
esp_shim_clic_intr_nslot(void)
{
	return(ESP_SHIM_CLIC_INTR_NSLOT);
}

int
esp_shim_clic_intr_line_of(int idx)
{
	if ((idx < 0) || (idx >= ESP_SHIM_CLIC_INTR_NSLOT)) {
		return(-1);				/* 0 は線 0 と区別できないので -1 */
	}
	return((int) esp_shim_clic_intr_line[idx]);
}

int
esp_shim_clic_intr_slot_of_source(int source)
{
	int		i;

	for (i = 0; i < ESP_SHIM_CLIC_INTR_NSLOT; i++) {
		if ((esp_shim_clic_intr_slot[i].in_use != 0U)
			&& (esp_shim_clic_intr_slot[i].source == source)) {
			return(i);
		}
	}
	return(-1);
}

int
esp_shim_clic_intr_slot_info(int idx, int *source, int *line,
							 uint32_t *n_isr, uint32_t *n_call)
{
	if ((idx < 0) || (idx >= ESP_SHIM_CLIC_INTR_NSLOT)
		|| (esp_shim_clic_intr_slot[idx].in_use == 0U)) {
		return(0);
	}
	if (source != NULL) { *source = esp_shim_clic_intr_slot[idx].source; }
	if (line   != NULL) { *line   = (int) esp_shim_clic_intr_line[idx]; }
	if (n_isr  != NULL) { *n_isr  = esp_shim_clic_intr_n_isr[idx]; }
	if (n_call != NULL) { *n_call = esp_shim_clic_intr_n_call[idx]; }
	return(1);
}

uint32_t
esp_shim_clic_intr_read_map(int source)
{
	if ((source < 0) || (source >= ESP_SHIM_CLIC_INTR_NUM_SOURCE)) {
		return(0xFFFFFFFFU);
	}
	return(sil_rew_mem(INTMTX_MAP(0, (uint32_t) source)));
}
