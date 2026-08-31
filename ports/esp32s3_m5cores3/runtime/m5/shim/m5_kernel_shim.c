/*
 *  M5（CoreS3）用 esp_shim カーネルラッパ（自己完結・M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ------------------------------------------------------------------
 *  なぜ esp/shim/esp_shim.c を再利用しないか
 *  ------------------------------------------------------------------
 *  M5GFX の compat freertos 経路が要求する esp_shim_* シンボルは次の 5 個だけ：
 *    esp_shim_sem_create / esp_shim_sem_take / esp_shim_sem_give
 *    esp_shim_task_delay / esp_shim_task_yield
 *  （esp_shim_mutex_* / queue_* / task_create は S3 経路では未参照＝
 *   Panel_CVBS/EPD の xTaskCreate 等は CONFIG_IDF_TARGET_ESP32 ガードで
 *   S3 では空コンパイルされるため）。
 *
 *  一方 esp/shim/esp_shim.c は Wi-Fi/BT 用の巨大シムで，本 M5 ビルドは
 *  --gc-sections が効かないため，esp_shim.c を丸ごとリンクすると
 *  esp_wifi_* 等の未定義参照を芋づるで引き込む。**したがって M5 では上記 5 個の
 *  カーネルラッパだけを自己完結で提供する。** 写像先の FMP3 サービスコールは
 *  esp_shim.c と同一（sig_sem/twai_sem/pol_sem/dly_tsk/rot_rdq）。
 *
 *  セマフォは wifi 側と同じ動的生成で、実体は wifi/shim/esp_shim_sem.c。
 *  cfg の AID_SEM(4) を上限に acre_sem する。M5 は単一コア・単一 main_task
 *  （SHIM_LOCK 規約）で，唯一の使用箇所 common.cpp:1086 の相互排他は実質
 *  競合しないが，take が count=0 で呼ばれても正しくブロックできるよう
 *  実カーネルセマフォで裏打ちする（「動くふり」を避ける）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>		/* ★段 C-2 のキュー実装で memcpy/memset を使う */
#include "kernel_cfg.h"
/*  ★`M5_AUDIO_STACK_SIZE` は `m5_display_smoke.h` にあるが、本 TU の include パスに
 *  アプリのディレクトリが無い。値を二重管理しないよう、**cfg と同じ定数名で
 *  ここに再定義せず**、アプリ側と同じ値を使うことだけを約束する。
 *  ⇒ 依存を増やさないため、ここでは**要求サイズの警告だけ**を出す形にした。 */

/*
 *  ★セマフォの静的プール（CRE_SEM(M5_SEM1..4) と枯渇時の syslog、および
 *    12 本構成の判定）はここに在ったが、削除した。
 *    実装は wifi/shim/esp_shim_sem.c（acre_sem／del_sem）に一本化してある。
 *    「4 本では足りない」という 2026-07-25 の実機所見は、
 *    プールの本数ではなく cfg の AID_SEM() の値の問題になった。
 */
/*  ★UART 直書きのログ（★`#if` の中に入れると構成次第で消える——
 *  実際に QEMU ビルドが `implicit declaration` で落ちた）。 */
extern void	m5_log_now(const char *msg);
extern void	m5_log_now_u32(const char *msg, unsigned int v);


/*  FMP3 の待ちタイムアウト（μs 単位 TMO）へ換算（tick=1ms 前提）。 */
static TMO
m5_tick_to_tmo(uint32_t tick)
{
	if (tick == 0xffffffffU) {		/* portMAX_DELAY */
		return(TMO_FEVR);
	}
	if (tick == 0U) {
		return(TMO_POL);
	}
	if (tick > 2000000U) {			/* TMO(μs・32bit) オーバフロー回避 */
		tick = 2000000U;
	}
	return((TMO)(tick * 1000U));
}

/*
 *  ★1-13：シム呼出し回数カウンタ（m5_display_smoke.c の m5_ctr[]）。
 *  周期ハンドラが毎秒ダンプする。「どのシムも呼ばない単一箇所での固着」か
 *  「micros()/taskYIELD() のポーリングループを回り続けるライブロック」かを、
 *  スピン中のタスクに触れずに弁別するための計装。
 *  ★esp_timer_get_time はホットパスなので出力はせず加算のみ（安い）。
 */
extern volatile unsigned int m5_ctr[];
extern void m5_ps_snapshot(unsigned int site);
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);
extern void m5_ckpt_set(unsigned int id);
#define M5_SEM_MARK_MAX		48U
static unsigned int	m5_sem_marks;
#define M5_CTR_MICROS		2
#define M5_CTR_YIELD		3
#define M5_CTR_DELAY		4
#define M5_CTR_SEMTAKE		5
#define M5_CTR_SEMGIVE		6
#define M5_PSSITE_SEMTAKE	0x03U
#define M5_PSSITE_YIELD		0x04U
#define M5_PSSITE_DELAY		0x05U

/*
 *		セマフォ生成・削除 — 動的生成へ移した
 *
 *  実体は wifi/shim/esp_shim_sem.c（acre_sem / del_sem）で、wifi 側の
 *  プロファイルと同じものを使う。ここに在った静的プール
 *  （m5_sem_id[] / m5_sem_used[]、cfg の CRE_SEM ×4）は廃止した。
 *
 *  ハンドル表現は変わらない（セマフォIDを void* へ入れる）ので、下の
 *  take / give はそのまま使える。
 *
 *  旧実装には「プール枯渇で NULL を返し、呼び側（ESP-IDF）が NULL に
 *  気づかず永久に待つことがある」という失敗モードがコメントで
 *  記録されていた。上限は AID_SEM の宣言値になり、acre_sem の失敗は
 *  esp_shim_sem.c が数える。
 */

/*
 * ★These two stay OUTSIDE the M5_USE_ESP_SHIM guard below: they are only
 *   declarations, and heap_caps_aligned_calloc - which the combined build
 *   still needs - calls both. Guarding them turned that into an implicit-declaration error.
 */
extern void *esp_shim_malloc(size_t size);
extern void  esp_shim_free(void *ptr);

#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
int32_t
esp_shim_sem_take(void *sem, uint32_t block_time_tick)
{
	ER	er;

	m5_ctr[M5_CTR_SEMTAKE]++;
	m5_ps_snapshot(M5_PSSITE_SEMTAKE);
	/*  ★lgfx の i2c_context_t::lock() は既定 msec=portMAX_DELAY で呼ぶ
	 *  （common.cpp:1084）。ここが TMO_FEVR の twai_sem になると、対になる
	 *  unlock() を通らない早期 return 経路が一度でもあれば**永久ブロック**する
	 *  （スピンではなくデッドロック＝CPU はアイドル）。入口/出口を必ず出す。 */
	if (m5_sem_marks < M5_SEM_MARK_MAX) {
		m5_sem_marks++;
		m5_mark_u32("[MK] sem_take in  tmo(tick)=", 0x50U,
					(unsigned int) block_time_tick);
	} else {
		m5_ckpt_set(0x50U);		/* 予算超過後は無出力で ckpt だけ更新 */
	}
	er = twai_sem((ID)(intptr_t)sem, m5_tick_to_tmo(block_time_tick));
	if (m5_sem_marks < M5_SEM_MARK_MAX) {
		m5_sem_marks++;
		m5_mark_u32("[MK] sem_take out er=", 0x51U, (unsigned int) er);
	} else {
		m5_ckpt_set(0x51U);
	}
	if (er == E_CTX) {
		/*  ディスパッチ保留文脈では twai_sem が E_CTX（NGKI0181）。
		 *  非ブロッキング取得へフォールバック（esp_shim.c と同型）。 */
		er = pol_sem((ID)(intptr_t)sem);
	}
	return(er == E_OK ? 1 : 0);
}

int32_t
esp_shim_sem_give(void *sem)
{
	m5_ctr[M5_CTR_SEMGIVE]++;
	return(sig_sem((ID)(intptr_t)sem) == E_OK ? 1 : 0);
}

void
esp_shim_task_delay(uint32_t tick)
{
	m5_ctr[M5_CTR_DELAY]++;
	m5_ps_snapshot(M5_PSSITE_DELAY);
	(void) dly_tsk((RELTIM)(tick * 1000U));		/* tick(ms)→μs */
}

void
esp_shim_task_yield(void)
{
	/*  ★lgfx の I2C 完了待ちは `do { taskYIELD(); } while(…micros()…)` である
	 *  （common.cpp:1306/1347/1688/1832）。ここが毎秒増え続けるなら
	 *  「固着」ではなく「ポーリングを回り続けるライブロック」であり、
	 *  弁別の主役になる。ホットパスなので出力はしない。 */
	m5_ctr[M5_CTR_YIELD]++;
	m5_ps_snapshot(M5_PSSITE_YIELD);
	(void) rot_rdq(TPRI_SELF);
}

/*
 *  ------------------------------------------------------------------
 *  esp_timer_get_time：単調増加 μs（M5GFX の millis()/micros() の土台）
 *  ------------------------------------------------------------------
 *  ★重要（1-5 実測）：M5GFX の register 直 I2C は完了待ちを
 *    `while(!done && micros()-ms < 14)` 等の**有限タイムアウト**で回す。
 *    esp_timer_get_time が進まないと QEMU で AXP2101 無応答時に無限ループ＝
 *    ハングする。したがって確実に進む値を返す必要がある。
 *
 *  実装：CCOUNT（CPU サイクルカウンタ）を読み，32bit ラップをソフトウェアで
 *    64bit へ拡張し，CPU 周波数[MHz]で割って μs にする。kernel HRT の Inline
 *    （target_hrt_get_current）は TU を跨ぐと確実にリンクできない実績（1-5 の
 *    get_my_prcidx 参照）があるため使わず，CCOUNT 直読で自己完結させる。
 *    M5 は単一コア・main_task 単一文脈なのでラップ拡張の状態に排他は不要。
 */
#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#define TOPPERS_S3_CPU_FREQ_MHZ	160		/* プロジェクト既定＝160MHz */
#endif

static uint32_t	m5_ccount_last;
static uint64_t	m5_ccount_hi;			/* 累積したラップ分（<<32 前の上位） */

int64_t
esp_timer_get_time(void)
{
	uint32_t	now;
	uint64_t	cycles;

	m5_ctr[M5_CTR_MICROS]++;
	__asm__ __volatile__ ("rsr %0, ccount" : "=a"(now));
	if (now < m5_ccount_last) {
		m5_ccount_hi += (uint64_t)1U << 32;		/* 32bit ラップ検出 */
	}
	m5_ccount_last = now;
	cycles = m5_ccount_hi + (uint64_t)now;
	return((int64_t)(cycles / (uint64_t)TOPPERS_S3_CPU_FREQ_MHZ));
}

/*
 *  ------------------------------------------------------------------
 *  heap_caps_malloc/free：M5GFX の DMA/内部 SRAM 確保写像
 *  ------------------------------------------------------------------
 *  本ポートのヒープは内蔵 SRAM なので MALLOC_CAP_DMA/INTERNAL は自然に満たす。
 *  M5 自己完結ヒープ（m5_display_smoke_heap.c の esp_shim_malloc/free）へ写す。
 */

/*  ★1-12：到達点マーカ（m5_display_smoke.c・即時出力）。
 *  reserveDMABuffer（320*2=640B を 2 回）がヒープで止まっていないかを見る。
 *  出力過多で USJ を詰まらせないよう先頭 M5_HEAP_MARK_MAX 回だけ出す。 */
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);
#define M5_HEAP_MARK_MAX	16U
static unsigned int	m5_heap_marks;

void *
heap_caps_malloc(size_t size, uint32_t caps)
{
	void	*p;
	bool_t	mark = (m5_heap_marks < M5_HEAP_MARK_MAX);

	(void) caps;		/* 内蔵 SRAM 単一プールなので caps は無視 */
	if (mark) {
		m5_heap_marks++;
		m5_mark_u32("[MK] heap_caps_malloc in  size=", 0x20U, (unsigned int) size);
	}
	p = esp_shim_malloc(size);
	if (mark) {
		m5_mark_u32("[MK] heap_caps_malloc out ptr=", 0x21U, (unsigned int)(uintptr_t) p);
	}
	return(p);
}

void
heap_caps_free(void *ptr)
{
	esp_shim_free(ptr);
}
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ============================================================================
 *  ★段 C-2: 音声（ESP-IDF I2S ドライバ）が要求するシムの供給
 *  ============================================================================
 *
 *  要求集合は**リンクの実測**で確定した §9-3）。
 *  ★推定で足していない。`undefined reference` に出たものだけを供給する。
 *
 *  ★**なぜ `esp/shim/esp_shim.c` を使わないのか**: m5 ビルドは
 *  `esp_shim.c`（Wi-Fi/BT 用の巨大シム）を**リンクしない**（本ファイル冒頭のコメント参照。
 *  `--gc-sections` が効かないため丸ごと載せられない）。⇒ m5 側で供給する必要がある。
 */

/*
 *  ---- タスクプール（cfg の M5_AUDIO_TSK1/2）----
 *
 *  ★**cfg にスロットが無い構成でも壊れないようにする。**
 *  QEMU ハーネス（`m5_qemu`）は別の cfg を使い `M5_AUDIO_TSK1/2` を持たないので、
 *  無条件に参照すると `M5_AUDIO_TSK1 undeclared` でビルドが落ちる（★実測）。
 *  ⇒ cfg が定義していなければ**プール 0 本**とし、`task_create` は失敗を返す。
 *  ★「音声を使わない構成では音声タスクを作れない」は正しい振る舞いである。
 */
#if defined(M5_AUDIO_TSK1) && defined(M5_AUDIO_TSK2)
#define M5_AUD_HAVE_POOL	1
#define M5_AUD_NTSK		2
#else
#define M5_AUD_HAVE_POOL	0
#define M5_AUD_NTSK		1		/* 配列を 0 要素にしないためのダミー */
#endif
typedef struct {
	ID			tskid;
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	uint32_t	notify;		/* ★FreeRTOS task notification の値 */
} M5_AUD_TSK;
static M5_AUD_TSK	m5_aud_tsk[M5_AUD_NTSK];

/*  ★cfg から呼ばれる共通エントリ（exinf = スロット番号）。 */
void
m5_audio_task_entry(EXINF exinf)
{
	M5_AUD_TSK	*t = &m5_aud_tsk[(uint_t) exinf];

	if (t->fn != NULL) {
		t->fn(t->arg);
	}
	/*  ★FreeRTOS のタスクは vTaskDelete(NULL) で終わるのが作法だが、
	 *  関数リターンで終わった場合もスロットを解放する。 */
	t->used = false;
	(void) ext_tsk();
}

#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	(void) name; (void) freertos_prio;
#if M5_AUD_HAVE_POOL
	static const ID	tbl[M5_AUD_NTSK] = { M5_AUDIO_TSK1, M5_AUDIO_TSK2 };
	uint_t			i;

	/*  ★cfg のスタックは 4,096（M5_AUDIO_STACK_SIZE）。M5Unified の要求は
	 *  `1280 + dma_buf_len*4`（既定で約 2.3KB）なので通常は収まる。
	 *  ★収まらない要求が来たら**黙って通さず警告する**（溢れると実機で静かに壊れる）。 */
	if (stack_size > 4096U) {
		syslog(LOG_NOTICE, "m5_shim: audio task stack %u > provided 4096 (要 cfg 見直し)",
			   (uint_t) stack_size);
	}
	for (i = 0U; i < (uint_t) M5_AUD_NTSK; i++) {
		if (!m5_aud_tsk[i].used) {
			break;
		}
	}
	if (i >= (uint_t) M5_AUD_NTSK) {
		/*  ★プール枯渇を黙って成功にしない（fail-closed）。 */
		syslog(LOG_NOTICE, "m5_shim: audio task pool exhausted (need >%d)", M5_AUD_NTSK);
		return(0);
	}
	m5_aud_tsk[i].tskid  = tbl[i];
	m5_aud_tsk[i].fn     = entry;
	m5_aud_tsk[i].arg    = param;
	m5_aud_tsk[i].notify = 0U;
	m5_aud_tsk[i].used   = true;
	if (task_handle != NULL) {
		*task_handle = (void *) &m5_aud_tsk[i];
	}
	return((act_tsk(tbl[i]) == E_OK) ? 1 : 0);
#else
	/*  ★cfg に音声タスクのスロットが無い構成（QEMU ハーネス等）。
	 *  ★**生成できないのが正しい振る舞い**なので、黙って成功にしない。 */
	(void) entry; (void) stack_size; (void) param; (void) task_handle;
	syslog(LOG_NOTICE, "m5_shim: この構成には音声タスクのスロットが無い（cfg 未定義）");
	return(0);
#endif
}

void
esp_shim_task_delete(void *task_handle)
{
	M5_AUD_TSK	*t = (M5_AUD_TSK *) task_handle;

	if ((t == NULL) || (t == (M5_AUD_TSK *) 0)) {
		/*  NULL = 自タスク  */
		ID	self;

		if (get_tid(&self) == E_OK) {
			uint_t	i;

			for (i = 0U; i < (uint_t) M5_AUD_NTSK; i++) {
				if (m5_aud_tsk[i].used && (m5_aud_tsk[i].tskid == self)) {
					m5_aud_tsk[i].used = false;
					break;
				}
			}
		}
		(void) ext_tsk();
		return;
	}
	t->used = false;
	(void) ter_tsk(t->tskid);
}
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ★task notification（`ulTaskNotifyTake` / `xTaskNotifyGive` の実体）。
 *  設計は `esp/shim/esp_shim.c` の同名関数と同じ:
 *  値の判定を割込み禁止で行い、**解除してから** `slp_tsk`。
 *  その隙に give が来ても `wup_tsk` が**起床要求をラッチする**ので取りこぼさない。
 *  ★`slp_tsk`/`wup_tsk` は本ポートの他の場所で使っていないので、ラッチが他を乱さない。
 */
static M5_AUD_TSK *
m5_aud_self(void)
{
	ID		self;
	uint_t	i;

	if (get_tid(&self) != E_OK) {
		return(NULL);
	}
	for (i = 0U; i < (uint_t) M5_AUD_NTSK; i++) {
		if (m5_aud_tsk[i].used && (m5_aud_tsk[i].tskid == self)) {
			return(&m5_aud_tsk[i]);
		}
	}
	return(NULL);
}

uint32_t
esp_shim_task_notify_take(int clear_on_exit, uint32_t timeout_ms)
{
	M5_AUD_TSK	*t = m5_aud_self();
	uint32_t	v;
	ER			er;

	if (t == NULL) {
		return(0U);		/*  ★プール外タスクは通知値を持たない  */
	}
	for (;;) {
		loc_cpu();
		v = t->notify;
		if (v > 0U) {
			t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
			unl_cpu();
			return(v);
		}
		unl_cpu();
		if (timeout_ms == 0U) {
			return(0U);
		}
		er = (timeout_ms == 0xFFFFFFFFU)
			 ? slp_tsk() : tslp_tsk((RELTIM)(timeout_ms * 1000U));	/* ms→μs */
		if (er == E_TMOUT) {
			loc_cpu();
			v = t->notify;
			if (v > 0U) {
				t->notify = (clear_on_exit != 0) ? 0U : (v - 1U);
			}
			unl_cpu();
			return(v);
		}
	}
}

void
esp_shim_task_notify_give(void *task_handle)
{
	M5_AUD_TSK	*t = (M5_AUD_TSK *) task_handle;

	if (t == NULL) {
		return;
	}
	loc_cpu();
	t->notify++;
	unl_cpu();
	(void) wup_tsk(t->tskid);	/* ★E_QOVR（既にラッチ済み）は正常 */
}

/*
 *  ---- キュー（I2S ドライバが DMA 記述子の受け渡しに使う）----
 *  ★FMP3 の CRE_DTQ を使わず**自前のリングバッファ**にした理由:
 *  m5 の cfg にデータキューが 1 つも無く、しかも I2S は
 *  「ISR から送り、タスクで受ける」形なので、**通知と同じ仕組み**で足りる。
 *  ★容量は I2S の DMA 記述子数に合わせて小さく取る。溢れたら**捨てずに失敗を返す**。
 */
/*
 *  ★★★2026-07-26（実機 p81）: **SPI マスタは 1 要素 20 バイトを要求する。**
 *
 *  【実測】p81 のログに我々自身の fail-closed 報告がそのまま出ていた:
 *      `m5_shim: queue too big len=1 item=20 (cap 8/8)`  ← ★**2 回**
 *  ⇒ `spi_bus_add_device()` が `trans_queue` と `ret_queue` の作成に失敗し
 *    **`ESP_ERR_NO_MEM`(0x101)** を返していた。
 *  ★LGFX は `ESP_LOGW` だけで続行するので（`W (0) LGFX: Failed to spi_bus_add_device.`）
 *  ★**fail-open**——ハンドルが NULL のまま描画へ進む。
 *
 *  ★**20 バイトの内訳は推測しないでソースを読んだ**（`spi_master.c` の
 *  `spi_trans_priv_t`）: ポインタ 3 本（12）＋ `SOC_SPI_SCT_SUPPORTED` のときの
 *  `uint32_t reserved[2]`（8）＝ **20**。
 *  ★私はポインタ 3 本だけ見て「12」と数えそうになった。★実測値が正しかった。
 *
 *  ★**「切り詰めない」判断は正しかった**（`esp_shim_queue_create` の fail-closed）。
 *  8 バイトに詰めていたら、SPI の記述子が壊れて**もっと分かりにくい形**で失敗していた。
 *
 *  【なぜ M5_SDSPI 限定にするか】このファイルは **m5 既定ビルドにも入っている**。
 *  寸法を無条件に変えると表示 A 等級の既定ビルドの .bss と基準 sha が動き、
 *  **表示の非退行を別途実機で確かめる必要が出る**。★1 度に 1 つだけ変える。
 *  ★恒久的には寸法を一本化するのが筋である。
 */
#if defined(M5_SDSPI)
#define M5_QUE_MAX		6		/* ★SPI が 2 本（trans/ret）増やすので +2 */
#define M5_QUE_CAP		8		/* 1 キューの要素数（SPI の queue_size=1 なので十分） */
#define M5_QUE_ITEM		24		/* ★SPI の spi_trans_priv_t = 20 バイト＋余裕 */
#else
#define M5_QUE_MAX		4		/* 同時に存在するキューの数 */
#define M5_QUE_CAP		8		/* 1 キューの要素数 */
#define M5_QUE_ITEM		8		/* 1 要素の最大バイト数（I2S は desc ポインタ） */
#endif
typedef struct {
	bool_t		used;
	uint32_t	len, item_size;
	uint32_t	head, tail, count;
	uint8_t		buf[M5_QUE_CAP * M5_QUE_ITEM];
	ID			waiter;			/* 受信待ちタスク（TSK_NONE=無し） */
} M5_QUE;
static M5_QUE	m5_que[M5_QUE_MAX];

#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
void *
esp_shim_queue_create(uint32_t len, uint32_t item_size)
{
	uint_t	i;

	if ((len > (uint32_t) M5_QUE_CAP) || (item_size > (uint32_t) M5_QUE_ITEM)) {
		/*  ★黙って切り詰めない。要求が大きければ失敗を返す（fail-closed）。 */
		syslog(LOG_NOTICE, "m5_shim: queue too big len=%u item=%u (cap %d/%d)",
			   (uint_t) len, (uint_t) item_size, M5_QUE_CAP, M5_QUE_ITEM);
		return(NULL);
	}
	loc_cpu();
	for (i = 0U; i < (uint_t) M5_QUE_MAX; i++) {
		if (!m5_que[i].used) {
			m5_que[i].used = true;
			m5_que[i].len = len; m5_que[i].item_size = item_size;
			m5_que[i].head = 0U; m5_que[i].tail = 0U; m5_que[i].count = 0U;
			m5_que[i].waiter = TSK_NONE;
			unl_cpu();
			return((void *) &m5_que[i]);
		}
	}
	unl_cpu();
	syslog(LOG_NOTICE, "m5_shim: queue pool exhausted (need >%d)", M5_QUE_MAX);
	return(NULL);
}

void
esp_shim_queue_delete(void *que)
{
	M5_QUE	*q = (M5_QUE *) que;

	if (q != NULL) {
		loc_cpu(); q->used = false; unl_cpu();
	}
}

void
esp_shim_queue_reset(void *que)
{
	M5_QUE	*q = (M5_QUE *) que;

	if (q != NULL) {
		loc_cpu(); q->head = 0U; q->tail = 0U; q->count = 0U; unl_cpu();
	}
}

uint32_t
esp_shim_queue_spaces_available(void *que)
{
	M5_QUE		*q = (M5_QUE *) que;
	uint32_t	n;

	if (q == NULL) { return(0U); }
	loc_cpu(); n = q->len - q->count; unl_cpu();
	return(n);
}

/*  ★ISR から呼ばれる。**割込み禁止にしない**（既に禁止文脈）。 */
int32_t
esp_shim_queue_send_from_isr(void *que, void *item)
{
	M5_QUE	*q = (M5_QUE *) que;

	if ((q == NULL) || (q->count >= q->len)) {
		return(0);		/* ★満杯なら失敗を返す（古いものを捨てない） */
	}
	memcpy(&q->buf[q->tail * q->item_size], item, q->item_size);
	q->tail = (q->tail + 1U) % q->len;
	q->count++;
	if (q->waiter != TSK_NONE) {
		(void) iwup_tsk(q->waiter);		/* ★ISR 版を使う */
	}
	return(1);
}

int32_t
esp_shim_queue_recv(void *que, void *item, uint32_t timeout_ms)
{
	M5_QUE	*q = (M5_QUE *) que;
	ID		self;
	ER		er;

	if (q == NULL) { return(0); }
	if (get_tid(&self) != E_OK) { return(0); }
	for (;;) {
		loc_cpu();
		if (q->count > 0U) {
			memcpy(item, &q->buf[q->head * q->item_size], q->item_size);
			q->head = (q->head + 1U) % q->len;
			q->count--;
			q->waiter = TSK_NONE;
			unl_cpu();
			return(1);
		}
		if (timeout_ms == 0U) { unl_cpu(); return(0); }
		q->waiter = self;		/* ★送信側が起こせるように登録 */
		unl_cpu();
		er = (timeout_ms == 0xFFFFFFFFU)
			 ? slp_tsk() : tslp_tsk((RELTIM)(timeout_ms * 1000U));
		if (er == E_TMOUT) {
			loc_cpu(); q->waiter = TSK_NONE; unl_cpu();
			return(0);
		}
	}
}

/*  ---- heap（I2S の DMA バッファ確保）---- */
void *
heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	void	*p;

	(void) caps;		/* ★内蔵 SRAM 単一プールなので caps は無視 */
	p = esp_shim_malloc(n * size);
	if (p != NULL) {
		memset(p, 0, n * size);
	}
	return(p);
}
#endif /* !M5_USE_ESP_SHIM */

void *
heap_caps_aligned_calloc(size_t align, size_t n, size_t size, uint32_t caps)
{
	/*  ★`esp_shim_malloc` の整列保証を確かめていないので、
	 *  **要求整列より大きく取って自前で切り上げる**。
	 *  ★元ポインタを捨てるので free できない——I2S はチャネル寿命中
	 *  保持し続ける（`i2s_free_dma_desc` は `free` を呼ぶが、本ポートの
	 *  `esp_shim_free` は単一プールで解放しない実装）。
	 *  ★**ここは「解放しない」ことを承知の上での実装である**と明記しておく。 */
	size_t	total = (n * size) + align;
	void	*raw;

	(void) caps;
	raw = esp_shim_malloc(total);
	if (raw == NULL) { return(NULL); }
	{
		uintptr_t	a = (uintptr_t) raw;
		uintptr_t	aligned = (a + (uintptr_t)(align - 1U)) & ~(uintptr_t)(align - 1U);

		memset((void *) aligned, 0, n * size);
		return((void *) aligned);
	}
}

/*
 *  ---- ★RTC クロック校正の**大声で失敗するスタブ** ----
 *
 *  ★**IDF の `rtc_clk.c` を持ち込まない。** 実機でクロックが壊れる。
 *
 *  ★**なぜスタブで足りるか**: I2S の既定クロック源は **PLL_F160M**
 *  （他は XTAL / PLL_D2）で、RTC 校正が要る源（LP_SLOW / RC_FAST / XTAL32K）は
 *  **選ばれない**。⇒ これらは**死んだ分岐**である。
 *
 *  ★**ただし「死んでいる」は前提であって保証ではない。** もし呼ばれたら
 *  前提が崩れているので、**黙って嘘の値を返さず syslog で叫ぶ**。
 *  ★0 を返すのは「明らかに使えない値」だからである（周波数 0 は成立しない）。
 */
uint32_t
rtc_clk_cal(uint32_t cal_clk, uint32_t slowclk_cycles)
{
	(void) cal_clk; (void) slowclk_cycles;
	syslog(LOG_ERROR, "m5_shim: ★rtc_clk_cal が呼ばれた＝I2S が RTC 系クロックを"
			 "選んだ。設計の前提（PLL_F160M のみ）が崩れている");
	return(0U);
}

uint32_t
rtc_clk_freq_cal(uint32_t cal_val)
{
	(void) cal_val;
	syslog(LOG_ERROR, "m5_shim: ★rtc_clk_freq_cal が呼ばれた（前提が崩れている）");
	return(0U);
}

void
esp_clk_slowclk_cal_set(uint32_t value)
{
	(void) value;
	syslog(LOG_ERROR, "m5_shim: ★esp_clk_slowclk_cal_set が呼ばれた（前提が崩れている）");
}

/*
 *  ---- ★64bit アトミック（`__atomic_fetch_and_8` / `__atomic_fetch_or_8`）----
 *
 *  【なぜ要るか】ESP-IDF の I2S ドライバが 64bit のビット操作をアトミックに行う
 *  （チャネル占有ビットマップ）。**Xtensa は 64bit のアトミック命令を持たない**ので
 *  コンパイラが libatomic の関数版を呼ぶ。★しかし本ツールチェーンに
 *  **libatomic が無い**。⇒ 自前で供給する。
 *
 *  ★**割込み禁止で実装するのは、m5 構成が単一コアだから**である
 *  （`portENTER_CRITICAL_SAFE` の実装と同じ前提）。
 *  ★**SMP で使うなら成立しない。** m5 が複数コアで動くようになったら
 *  `s32c1i`（Xtensa の compare-and-swap）を使う実装へ差し替えること。
 *  ★`memorder` は無視する（割込み禁止区間なので順序は保たれる）。
 */
unsigned long long
__atomic_fetch_and_8(volatile void *ptr, unsigned long long val, int memorder)
{
	volatile unsigned long long	*p = (volatile unsigned long long *) ptr;
	unsigned long long			old;

	(void) memorder;
	loc_cpu();
	old = *p;
	*p = old & val;
	unl_cpu();
	return(old);
}

unsigned long long
__atomic_fetch_or_8(volatile void *ptr, unsigned long long val, int memorder)
{
	volatile unsigned long long	*p = (volatile unsigned long long *) ptr;
	unsigned long long			old;

	(void) memorder;
	loc_cpu();
	old = *p;
	*p = old | val;
	unl_cpu();
	return(old);
}

#if defined(M5_SDSPI)
/*  ★既定ビルドを**バイト不変**に保つため、SD 構成のときだけコンパイルする。
 *  ★35 run 比較してきた基準（a9076851…）を動かさない。 */
/*
 *  ============================================================================
 *  ★microSD（ESP-IDF SPI/SD）が要求するシムの供給
 *  ============================================================================
 *  ★要求集合は**リンクの実測**で確定した（`.steading` ではなく
 *  。
 *  ★**リンカが名を挙げたものだけ**を供給している。推定で足していない。
 */

/*  ★本ポートは PRC1 固定で走る（m5 変種は SMP を使わない）。 */
int	xPortGetCoreID(void);
int
xPortGetCoreID(void)
{
	return(0);
}

/*  ★割込みが割り当てられた CPU 番号。本ポートは常に 0。 */
int	esp_intr_get_cpu(void *handle);
int
esp_intr_get_cpu(void *handle)
{
	(void) handle;
	return(0);
}

/*
 *  ★そのポインタが PSRAM 上かどうか。
 *  ★**PSRAM を有効にしていない構成では常に false** が正しい。
 *  ★有効な構成（A1_M5_PSRAM=ON）では窓 0x3C000000-0x3E000000 を見るが、
 *  ★**我々の DROM(0x3C100000) もその窓に居る**ので、番地だけでは区別できない。
 *  ⇒ ★**保守的に false を返す**（「PSRAM ではない」と答える方が安全側:
 *  呼び側は DMA 可否の判断に使うので、内蔵 SRAM 扱いにしておく）。
 *  ★これは割り切りである。PSRAM をヒープへ繋ぐ段で見直すこと。
 */
bool_t	esp_ptr_external_ram(const void *p);
bool_t
esp_ptr_external_ram(const void *p)
{
	(void) p;
	return(false);
}

/*
 *  ★確保済みブロックのサイズ。本ポートのアリーナはヘッダに size を持つ
 *  （`m5_display_smoke_heap.c` の `m5_block_t`）。★正確に返せる。
 */
extern size_t	esp_shim_block_size(void *ptr);
size_t	heap_caps_get_allocated_size(void *ptr);
size_t
heap_caps_get_allocated_size(void *ptr)
{
	return(esp_shim_block_size(ptr));
}

/*
 *  ★RTC GPIO（2 本だけ・2026-07-26）。
 *  ★`sdspi_host.c` が「そのピンは RTC GPIO か」を尋ね、そうなら RTC 側を解除する。
 *  ★CoreS3 の SD ピン（CLK36/MOSI37/MISO35/CS4）は ★**どれも RTC GPIO ではない**
 *  （S3 の RTC GPIO は GPIO0-21）。★**GPIO4 だけは RTC 範囲に入る**が、
 *  本ポートは RTC IO を一切使っていないので**解除の必要が無い**。
 *  ⇒ ★`is_valid` は false を返し、`deinit` は何もしない。
 *  ★**IDF の `rtc_io.c` を入れない**理由: それを入れると `rtc_io_desc` /
 *  `rtc_io_num_map` / `io_mux_force_disable_lp_io_clock` …と
 *  **RTC IO のサブシステム一式**を引き込む。2 関数のために払う代償が大きい。
 */
bool_t	rtc_gpio_is_valid_gpio(int gpio_num);
bool_t
rtc_gpio_is_valid_gpio(int gpio_num)
{
	(void) gpio_num;
	return(false);
}

int	rtc_gpio_deinit(int gpio_num);
int
rtc_gpio_deinit(int gpio_num)
{
	(void) gpio_num;
	return(0);			/* ESP_OK */
}
#endif /* M5_SDSPI */

/*
 * Public FreeRTOS compatibility headers use this function independently of
 * SD-SPI support (for example xTaskGetTickCount). Keep it unconditional.
 */
#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
int64_t
esp_shim_time_us(void)
{
	return((int64_t) esp_timer_get_time());
}
#endif /* !M5_USE_ESP_SHIM */

#if !defined(M5_SDSPI)
#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
/*
 * Public queue.h exposes these operations in every M5 build.  Their original
 * implementations were accidentally inside the M5_SDSPI-only block even
 * though the queue storage and receive path above are unconditional.
 */
int32_t
esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick,
					bool_t to_front)
{
	M5_QUE	*q = (M5_QUE *) que;
	ID		self;
	ER		er;

	if ((q == NULL) || (item == NULL) || to_front) {
		return(0);
	}
	if (get_tid(&self) != E_OK) {
		return(0);
	}
	for (;;) {
		loc_cpu();
		if (q->count < q->len) {
			memcpy(&q->buf[q->tail * q->item_size], item, q->item_size);
			q->tail = (q->tail + 1U) % q->len;
			q->count++;
			unl_cpu();
			return(1);
		}
		if (block_time_tick == 0U) {
			unl_cpu();
			return(0);
		}
		unl_cpu();
		er = tslp_tsk((RELTIM) 1000U);
		if ((er != E_OK) && (er != E_TMOUT)) {
			return(0);
		}
		if (block_time_tick != 0xFFFFFFFFU) {
			block_time_tick--;
		}
	}
}

unsigned int
esp_shim_queue_msg_waiting(void *q)
{
	M5_QUE			*p = (M5_QUE *) q;
	unsigned int	n;

	if (p == NULL) {
		return(0U);
	}
	loc_cpu();
	n = (unsigned int) p->count;
	unl_cpu();
	return(n);
}
#endif /* !M5_USE_ESP_SHIM */
#endif /* !M5_SDSPI */

#if defined(M5_SDSPI)
/*
 *  ★SD（ESP-IDF SPI/SD）が追加で要求したもの（★リンカが名を挙げたものだけ・2026-07-26）。
 */

/*
 *  ★`esp_intr_alloc()`（フィルタ無し版）。
 *  ★`esp/shim/esp_shim_intr.c` は**意図的に定義していない**
 *  （intrstatus フィルタ付きの `esp_intr_alloc_intrstatus()` だけを提供する方針）。
 *  ★IDF の `spi_master.c` はフィルタ無し版を呼ぶので、★**フィルタを NULL にして委譲する**。
 *  ★「無いから作る」ではなく「**同じ物にフィルタ無しで入る口**を開ける」だけである。
 */
extern int	esp_intr_alloc_intrstatus(int source, int flags, uint32_t intrstatus_reg,
									  uint32_t intrstatus_mask, void (*handler)(void *),
									  void *arg, void **ret_handle);
extern int	esp_intr_disable(void *handle);

/*
 *  ★`ESP_INTR_FLAG_INTRDISABLED` を尊重する。★これが無いと **系全体が死ぬ**。
 *
 *  【なぜ死ぬか】`spi_hal_init()`（`hal/spi_hal.c`）は
 *  ★**意図的に「転送完了」割込みを立てる**:
 *      spi_ll_enable_int(hw);   // ペリフェラル側の割込み許可
 *      spi_ll_set_int_stat(hw); // ★要因を立てる
 *  IDF は「`ESP_INTR_FLAG_INTRDISABLED` で確保したので**まだ飛ばない**」ことを
 *  前提にしている（コメントに明記されている）。
 *  ★ところが `esp/shim/esp_shim_intr.c` の `esp_intr_alloc_intrstatus()` は
 *  **`flags` のうち IRAM(0x400) と SHARED(0x200) しか見ておらず**、
 *  `p->enabled = true` と `ena_int()` で ★**無条件に CPU 線を有効化する**。
 *  ⇒ 立てた瞬間に `spi_intr(host)` が**半分しか初期化されていない host** に対して
 *    走る（`bus_driver_ctx[host_id]` はまだ NULL、デバイスは未登録）。
 *    ★SPI の割込みは**レベル**なので要因が落ちず **storm** になり、tick も UART も死ぬ。
 *
 *  ★**`esp_shim_intr.c` 側の穴は残っている。** INTRDISABLED を渡す他の IDF
 *  呼び出し（現状 m5 では無い）が現れたら同じ事故になる。意味論としては
 *  あちらを直すのが正しい。
 *
 *  【この順序（確保→即無効化）が安全な理由】確保した時点では
 *  **ペリフェラル側の割込み許可がまだ 0**（`spi_ll_enable_int` は後で `spi_hal_init` が
 *  立てる）。⇒ 窓の間に要因が立つことはない。★タスク文脈でしか呼ばれない。
 */
#define M5_ESP_INTR_FLAG_INTRDISABLED	(1 << 11)	/* esp_intr_alloc.h:43 と同値 */

unsigned int	m5_intr_alloc_n_disabled_honored;	/* ★効いた回数（0 なら効いていない） */

int	esp_intr_alloc(int source, int flags, void (*handler)(void *), void *arg, void **ret_handle);
int
esp_intr_alloc(int source, int flags, void (*handler)(void *), void *arg, void **ret_handle)
{
	int		er;
	void	*h = NULL;

	er = esp_intr_alloc_intrstatus(source, flags, 0U, 0U, handler, arg, &h);
	if (ret_handle != NULL) {
		*ret_handle = h;
	}
	if ((er == 0) && ((flags & M5_ESP_INTR_FLAG_INTRDISABLED) != 0) && (h != NULL)) {
		(void) esp_intr_disable(h);
		m5_intr_alloc_n_disabled_honored++;
		m5_log_now_u32("[INTR] ★ESP_INTR_FLAG_INTRDISABLED を尊重して即無効化した source=",
					   (unsigned int) source);
	}
	else if ((flags & M5_ESP_INTR_FLAG_INTRDISABLED) != 0) {
		/*  ★黙って落とさない。★「尊重したつもり」を「尊重した」と読まないため。 */
		m5_log_now_u32("[INTR] ★★INTRDISABLED を尊重**できなかった**（er/handle を見よ）"
					   " source=", (unsigned int) source);
		m5_log_now_u32("[INTR]   er=", (unsigned int) er);
		m5_log_now_u32("[INTR]   handle=", (unsigned int)(uintptr_t) h);
	}
	return(er);
}

/*
 *  ============================================================================
 *  ★★案 A（`M5_SDSPI`）で**リンカが名を挙げた**もの
 *  ============================================================================
 *  ★推測で足していない。★`ld` の undefined reference を 1 件ずつ潰した:
 *
 *    libsdspi.a(sd_spi_master.o)      : esp_ptr_dma_ext_capable / heap_caps_aligned_alloc
 *                                      esp_shim_queue_send / esp_shim_time_us
 *    libsdspi.a(sd_sdspi_host.o)      : esp_shim_time_us / esp_log_buffer_hex_internal
 *    libsdspi.a(sd_sdspi_transaction.o): _lock_acquire / _lock_release
 *
 *  ★`esp_shim_queue_send` と `esp_shim_time_us` は名前だけ見ると既存に見えるが、
 *  実体は **`esp/shim/esp_shim.c`** に在り、★**m5 構成はあのファイルをリンクしない**
 *  （`build.ninja` 実測: golden 4 構成のみが持つ）。⇒ m5 側に用意する必要がある。
 *  ★「同じ名前がどこかに在る」を「使える」と読まないこと。
 */

/*  ---- esp_shim_time_us: 本ファイルの esp_timer_get_time と同じ源を使う ---- */
/*
 *  ---- esp_shim_queue_send（タスク文脈・ブロッキング可）----
 *  ★既存の `esp_shim_queue_recv`（:639）と対称に書く。満杯なら**捨てずに待つ**。
 *  ★`to_front` は SPI ドライバが使わない（`xQueueSend`=back のみ）。
 *    ★**黙って無視しない**——要求されたら失敗を返す（fail-closed）。
 */
int32_t	esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick, bool_t to_front);
int32_t
esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick, bool_t to_front)
{
	M5_QUE	*q = (M5_QUE *) que;
	ID		self;
	ER		er;

	if ((q == NULL) || (item == NULL)) { return(0); }
	if (to_front) {
		/*  ★未実装を黙って成功にしない。 */
		m5_log_now("[MK] ★★esp_shim_queue_send(to_front=true) は未実装＝失敗を返す");
		return(0);
	}
	if (get_tid(&self) != E_OK) { return(0); }
	for (;;) {
		loc_cpu();
		if (q->count < q->len) {
			memcpy(&q->buf[q->tail * q->item_size], item, q->item_size);
			q->tail = (q->tail + 1U) % q->len;
			q->count++;
			unl_cpu();
			return(1);
		}
		if (block_time_tick == 0U) { unl_cpu(); return(0); }
		unl_cpu();
		/*  ★満杯待ちの起床通知は持っていないので、短く寝て再試行する。
		 *  ★SPI の `queue_size=1` かつ polling 転送なので、実際には
		 *  ここで待つこと自体が稀である（待つなら設計を見直す合図）。 */
		er = tslp_tsk((RELTIM) 1000U);		/* 1ms */
		if ((er != E_OK) && (er != E_TMOUT)) { return(0); }
		if (block_time_tick != 0xFFFFFFFFU) {
			if (block_time_tick == 0U) { return(0); }
			block_time_tick--;
		}
	}
}

/*  ---- heap_caps_aligned_alloc: 既存の aligned_calloc（:684）と同じ流儀 ---- */
void	*heap_caps_aligned_alloc(size_t align, size_t size, uint32_t caps);
void *
heap_caps_aligned_alloc(size_t align, size_t size, uint32_t caps)
{
	size_t	total = size + align;
	void	*raw;

	(void) caps;			/* ★内蔵 SRAM 単一プールなので caps は無視 */
	if (align == 0U) { return(NULL); }
	raw = esp_shim_malloc(total);
	if (raw == NULL) { return(NULL); }
	/*  ★元ポインタを捨てるので free できない（既存 aligned_calloc と同じ承知の上）。 */
	return((void *)(((uintptr_t) raw + (uintptr_t)(align - 1U))
					& ~(uintptr_t)(align - 1U)));
}

/*
 *  ---- esp_ptr_dma_ext_capable: **保守的に false** ----
 *  ★本構成は PSRAM をヒープに繋いでいない（`A1_M5_PSRAM` は別 opt-in で、
 *  繋いでもいない）。⇒ 外部 RAM を DMA するポインタは存在しない。
 *  ★false は「外部 RAM ではない」＝ドライバに**内蔵 RAM 用の経路**を選ばせる。
 *  ★もし将来 PSRAM をヒープに繋いだら**ここが嘘になる**——その時に直すこと。
 */
bool_t	esp_ptr_dma_ext_capable(const void *p);
bool_t
esp_ptr_dma_ext_capable(const void *p)
{
	(void) p;
	return(false);
}

/*  ---- esp_log_buffer_hex_internal: ESP_LOG_BUFFER_HEX の実体。何もしない ---- */
void	esp_log_buffer_hex_internal(const char *tag, const void *buffer,
									uint16_t buff_len, int level);
void
esp_log_buffer_hex_internal(const char *tag, const void *buffer,
							uint16_t buff_len, int level)
{
	(void) tag; (void) buffer; (void) buff_len; (void) level;
}

/*
 *  ---- _lock_acquire / _lock_release ----
 *  ★`sdspi_transaction.c:18,98,183` が **SD トランザクション全体**を囲む
 *  排他に使う（`static _lock_t s_lock;`）。
 *
 *  ★★`loc_cpu()` で実装してはいけない——区間は**ミリ秒級**（SD の応答待ちを含む）
 *  なので、その間割込みを止めると **tick が死ぬ**（p79/p80 で実際に系全体を殺した型）。
 *
 *  ★本構成では SD を触るのは `m5_sd_idf_probe()` の**単一タスクだけ**なので、
 *  排他は実際には要らない。⇒ **何もしない実装**にする。
 *  ★★ただし「要らない」は前提であって保証ではない。**黙って無視すると、
 *  将来 2 タスクが同時に触ったときに「たまに壊れる」形で出る**。
 *  ⇒ ★**深さを数えて、入れ子／競合が起きたら大声で言う**。
 *  ★これは「no-op だが検出はする」設計であって、「安全だと仮定した」ではない。
 */
static int	m5_sd_lock_depth;
unsigned int	m5_sd_lock_contention;		/* ★0 でなければ前提が崩れている */

void	_lock_acquire(void *lock);
void
_lock_acquire(void *lock)
{
	(void) lock;
	loc_cpu();
	m5_sd_lock_depth++;
	if (m5_sd_lock_depth > 1) {
		m5_sd_lock_contention++;
	}
	unl_cpu();
	if (m5_sd_lock_contention != 0U) {
		m5_log_now_u32("[MK] ★★_lock_acquire が入れ子／競合した（no-op 実装の前提が"
					   "崩れている。単一タスクのはずだった）depth=",
					   (unsigned int) m5_sd_lock_depth);
	}
}

void	_lock_release(void *lock);
void
_lock_release(void *lock)
{
	(void) lock;
	loc_cpu();
	if (m5_sd_lock_depth > 0) { m5_sd_lock_depth--; }
	unl_cpu();
}

/*
 *  ★キューに溜まっている件数。★`esp_shim_queue_*` の実装は本ファイル内にあるが、
 *  `msg_waiting` は音声では要らなかったので出していなかった。★SD が要求したので足す。
 */
unsigned int	esp_shim_queue_msg_waiting(void *q);
unsigned int
esp_shim_queue_msg_waiting(void *q)
{
	M5_QUE	*p = (M5_QUE *) q;
	unsigned int	n;

	if (p == NULL) { return(0U); }
	loc_cpu(); n = (unsigned int) p->count; unl_cpu();
	return(n);
}
#endif /* M5_SDSPI */
