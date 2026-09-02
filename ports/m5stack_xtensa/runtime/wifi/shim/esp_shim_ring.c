/*
 *  TOPPERS/FMP3 ESP32-S3 port
 *
 *  シム所有のリングバッファ（**ISR 文脈から受信できる**キュー）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  設計・AC・実測は `非公開作業記録/20260804-bthal-ring/DESIGN.md`。
 *  「なぜ要るか」「分岐を作らない理由」「単一コア前提」は `esp_shim_ring.h` 冒頭。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "esp_shim.h"
#include "esp_shim_ring.h"

/*
 *  ========================================================================
 *  単一コア前提の**コンパイル時**検査（黙って壊れさせない）
 *  ========================================================================
 *  本実装の排他は `SIL_LOC_INT()`/`SIL_UNL_INT()`＝**自コアの PS.INTLEVEL** だけ。
 *  他コアを止める手段（スピンロック）も、コア間可視性の順序付け（`memw`）も持たない。
 *  ⇒ `TNUM_PRCID >= 2` では **head/tail/count/waiter が壊れる**。
 *  `#ifndef` も検査する: `#if 未定義マクロ` は 0 として通ってしまい、
 *    「検査したつもり」で素通りするため（fail-closed）。
 */
#ifndef TNUM_PRCID
#error "esp_shim_ring.c: TNUM_PRCID が見えていない（単一コア前提を検査できない）"
#endif

/*
 *  ========================================================================
 *  段16（2026-08-05）: SMP 対応（上の `#error` が要求していた作り替え）
 *  ========================================================================
 *  **この検査は正しく働いた。** `seam-s3-ble` を 2 コアで建てようとした瞬間に
 *    ビルドを止め、「黙って壊れる」ことを防いだ。段10 の穴の数え上げ（5 つ）にも、
 *    段13 が見つけた H6 にも本ファイルは入っていなかった——
 *    **コード側の fail-closed だけが気付いていた**。
 *
 *  排他の作り: `SIL_LOC_INT()`（自コアの `PS.INTLEVEL`）→ `s32c1i` の CAS。
 *  この順序は `core_cas()` の不変条件 (a)（`PS.INTLEVEL=15` 下でのみ呼ぶ。
 *  SCOMPARE1 を退避していないため。`chip_kernel_impl.h:91-108`）を満たす。
 *
 *  **ロックは `esp_shim.c` の `SHIM_LOCK` とは別の変数**にした。理由は
 *    **入れ子が無いから**である（実測）:
 *      - `esp_shim.c` から `esp_shim_ring_*` の呼出しは **0 件**。
 *      - 本ファイルの `esp_shim_malloc`/`esp_shim_free` 呼出し（3 箇所）は
 *        **すべて臨界区間の外**（186 行の malloc は `SIL_LOC_INT` の前、
 *        210/241 行の free は `SIL_UNL_INT` の後）。
 *    ⇒ 双方向は存在せず、別ロックで順序問題は起きない。共有ロックにすると
 *      リング操作とヒープ操作が不要に直列化するので分けた。
 *
 *  `memw` は**取得後にも置いた**（`chip_kernel_impl.h` の `acquire_lock()` と
 *    同じ形）。上の旧コメントが「`memw` も持たない」を欠陥として挙げていたため、
 *    可視性の順序付けを明示する。`esp_shim.c` 側（段12）の取得は
 *    `memw` を持たない——`s32c1i` 自体が同期命令であることに依っている。
 *    **どちらが正しいかは本段では判定していない**（本ファイルは強い側を採った）。
 *
 *  2 段ゲート（段15 の作法）: `TNUM_PRCID >= 2` かつ `ESP_SHIM_XCORE_LOCK`。
 *  1 コアでは下の `#else` に落ち、**旧実装とバイト同一**である。
 *
 *  フェーズ1 1-1（2026-08-06）: `ESP_SHIM_XCORE_LOCK` が無い状態で
 *  `TNUM_PRCID >= 2` に入ると、下の `#else` が**黙って**単一コア専用の
 *  `SIL_LOC_INT()` を選ぶ（esp_shim.c の C-1 と同型）。本ファイルは現状
 *  `ble` variant の `seam_objs` にしか居らず、そこでは `ESP_SHIM_XCORE_LOCK`
 *  が段15 の既定 ON で届くため今は安全だが、それは**配線側の事情**であって
 *  ソース側の保証ではない。§10 規則2 に従い fail-closed にする。
 */
#ifndef TNUM_PRCID
#error "esp_shim_ring.c: TNUM_PRCID が見えていない（単一コア前提を検査できない）"
#endif
#if (TNUM_PRCID >= 2) && !defined(ESP_SHIM_XCORE_LOCK) && !defined(ESP_SHIM_ALLOW_XCORE_UNSAFE)
#error "esp_shim_ring.c: 2コアなのにESP_SHIM_XCORE_LOCKが無効(RING_LOCKがSILのみに落ちクロスコア非安全)。NC/意図的ならESP_SHIM_ALLOW_XCORE_UNSAFEを渡すこと"
#endif
#if (TNUM_PRCID > 1) && defined(ESP_SHIM_XCORE_LOCK)
#include <xtensa_cas.h>		/* 2026-08-06（フェーズ1 1-3）: CAS実体を1本化 */

static volatile uint32_t	ring_xcore_word;

Inline bool_t
ring_xcore_cas(volatile uint32_t *p, uint32_t cmp, uint32_t newv)
{
	return xtensa_core_cas(p, cmp, newv);
}

Inline uint32_t
ring_xcore_acquire(void)
{
	uint32_t	ps, me;

	Asm("rsr.prid %0" : "=a"(me));
	me |= 1U;						/*  0 は「空き」なので必ず非 0 にする  */
	ps = esp_shim_int_disable();	/*  先に自コアを閉じる（不変条件 (a)）  */
	while (!ring_xcore_cas(&ring_xcore_word, 0U, me)) {
		/*  他コアが保持中。保持側は割込みを閉じているので待ちは有界  */
	}
	Asm("memw" ::: "memory");
	return(ps);
}

Inline void
ring_xcore_release(uint32_t ps)
{
	Asm("memw" ::: "memory");
	ring_xcore_word = 0U;
	esp_shim_int_restore(ps);
}

#define RING_PRE_LOC	uint32_t ring_ps_
#define RING_LOCK()		(ring_ps_ = ring_xcore_acquire())
#define RING_UNLOCK()	ring_xcore_release(ring_ps_)

#else /* 1 コア: 旧実装のまま（バイト同一） */

#define RING_PRE_LOC	SIL_PRE_LOC
#define RING_LOCK()		SIL_LOC_INT()
#define RING_UNLOCK()	SIL_UNL_INT()

#endif

/*
 *  ============================================================================
 *  同時に存在できるリングの数（`ESP_SHIM_RING_MAX`。2026-08-15・C-2 拡張）
 *  ============================================================================
 *  **旧値 8 は「BL-G-3 段2 完遂後、S3 factory の実測 `create=8`＝プール上限
 *    ちょうど・余裕 0」だった**（`非公開作業記録/20260814-ring-unification/RESULT.md`
 *    §20-1）。BLE+WiFi 合成等 1 本増える変更で即座に `create_fail` になる状態で、
 *    見積り漏れで golden 構成が壊れた BL-H-9 の前例（LX6 割込みスロット）と
 *    同型のリスクだった。C-1（`esp/shim/esp_shim_intr_clic_lines.h`）が採った
 *    「利用者を列挙し、各行に同時本数と出典を書く」作法をここでも踏む。
 *
 *  【利用者の列挙（この表が MAX の根拠である）】
 *
 *  | 利用者                          | 同時本数 | 出典（一次情報）                        |
 *  |----------------------------------|---------|------------------------------------------|
 *  | Wi-Fi（`esp_wifi_adapter.c`、    | 4       | 旧 `ESP_SHIM_NUM_DTQ` 既定値（`esp_shim_cfg.h`|
 *  | wifi_os_adapter 経由）           |         | 旧版。WiFi 単体ビルドで枯渇報告なし＝実績） |
 *  | SD-over-SPI（SPI Master 2 本＋   | 3       | `esp-idf/.../gpspi/spi_master.c:537,543`  |
 *  | SDMMC host event 1 本）          |         | （trans_queue/ret_queue）＋               |
 *  |                                   |         | `esp-idf/.../sdmmc_host.c:528`（event_queue）|
 *  | I2S（msg_queue）                 | 1       | `esp-idf/.../i2s_common.c:329`            |
 *  |                                   |         | （xQueueCreateWithCaps→リング経由、       |
 *  |                                   |         | `libi2s.a` nm実測で参照確認）              |
 *  | **現在の実測最大**               | **8**   | S3 factory ＝ Wi-Fi+SD+I2S 合成。          |
 *  | （S3 factory=WiFi+SD+I2S）       |         | `create=8`・`create_fail=0`（20260814-ring-|
 *  |                                   |         | unification/RESULT.md §20。上記内訳4+3+1=8|
 *  |                                   |         | と一致＝構造分析の裏取り）                  |
 *  | NimBLE（npl eventq 1＋           | 2       | `npl_os_freertos.c:303`（`nimble_port.c:192`|
 *  | コントローラ側 1）                |         | で1回のみ）＋`esp/bt/hal/bt.c:1031`        |
 *  |                                   |         | （queue_create_wrapper、コントローラosi）  |
 *  | BT Classic Bluedroid（HCI host 2 | 6       | `hci_layer.c:48`(WORKQUEUE_NUM=2)／        |
 *  | ＋BTU 1＋BTC 2 workqueue＋       |         | `btu_init.c:51`(=1)／`btc_task.c:100`(=2)／|
 *  | コントローラ 1）                 |         | `bt.c:1031`。fixed_queue（RFCOMM/SPP接続時）|
 *  |                                   |         | は list_new+セマフォで xQueueCreate を     |
 *  |                                   |         | 使わない（`fixed_queue.c`実測）ので接続数分 |
 *  |                                   |         | は増えない（推定・未実機測定）              |
 *  | **近未来: BLE+WiFi 合成**        | **+2**  | 現行 preset に無い組合せ。NimBLE単体の      |
 *  | （未実装）                       |         | 寄与（上記2）をWiFi単体（4）に追加する想定  |
 *  | **近未来: 段7a hosted系**        | **+4**  | `BL-I-1`（P4+C6 統合計画）§8 段7a。          |
 *  | （P4+C6 SDIO、未実装）           |         | esp-hosted transport は典型的に TX/RX/ctrl |
 *  |                                   |         | 等の追加キューを持つ。未実装のため実測不能  |
 *  |                                   |         | ＝保守的な見積り                            |
 *  | **明示的な余裕**                 | **+2**  | BL-H-9／C-1 の教訓（列挙し切っても見積り漏れ|
 *  |                                   |         | は起きる）を踏まえた定数マージン            |
 *  | **合計 = `ESP_SHIM_RING_MAX`**   | **16**  | 8（現行実測最大）+2（BLE近未来）+4（hosted |
 *  |                                   |         | 見積り）+2（余裕）= 16                     |
 *
 *  【構造上の上限】`ring_wake_pending` を 32bit ビットマップで持つので 32 が
 *    上限。16 はこれに安全に収まる。
 *
 *  【メモリ単価（実測）】`sizeof(ESP_SHIM_RING)` = **32 バイト**
 *    （`xtensa-esp32s3-elf-nm -S` で `ring_pool` の合計サイズ 0x100(=256B) を
 *    旧枠 8 で割って確認）。8→16 で `.bss` は **+256 バイト**（`esp_shim_ring.c`
 *    が届く 9 構成すべてで一律。既存のヒープ予算——S3 factory の
 *    `[SHIM-HEAP/postSD]` ピーク実測 `0x107e8`≈66,536B——に対し無視できる規模）。
 *
 *  枯渇したら**黙って切り詰めず失敗を返す**（下の create）。
 */
#define ESP_SHIM_RING_MAX		16

/*
 *  保留起床の取りこぼしに対する**安全網**の上限（ms）。
 *  これは「主たる起床機構」ではない。主機構は `wup_tsk`＋
 *    `esp_shim_ring_flush_wakes()`（`esp_shim_bt_exit_critical` から）である。
 *  無限待ち（`ESP_SHIM_BLOCK_FOREVER`）でも**この刻みで必ず目を覚まして
 *    自分でキューを見に行く**ので、起床を 1 回落としても
 *    「永久に還らない」にはならない（H4 の帰結を構造的に防ぐ）。
 */
#define ESP_SHIM_RING_WAKE_SAFETY_MS	50U

typedef struct {
	bool_t				used;
	uint32_t			len;		/* 要素数                       */
	uint32_t			item_size;	/* 1 要素のバイト数             */
	uint32_t			head;
	uint32_t			tail;
	volatile uint32_t	count;
	uint8_t				*buf;		/* len*item_size（生成時に 1 回だけ確保） */
	ID					waiter;		/* 受信待ちタスク（TSK_NONE=無し） */
} ESP_SHIM_RING;

static ESP_SHIM_RING	ring_pool[ESP_SHIM_RING_MAX];

/*  起床を保留したリングのビットマップ（index は ring_pool の添字）。 */
static volatile uint32_t	ring_wake_pending;

volatile uint32_t	esp_shim_ring_n_create;
volatile uint32_t	esp_shim_ring_n_create_fail;
volatile uint32_t	esp_shim_ring_n_send_full;
volatile uint32_t	esp_shim_ring_n_recv_isrctx;
volatile uint32_t	esp_shim_ring_n_wake_deferred;
volatile uint32_t	esp_shim_ring_n_wake_flushed;
volatile uint32_t	esp_shim_ring_n_waiter_conflict;
volatile uint32_t	esp_shim_ring_n_recv_nowait_ctx;

/*
 *  ------------------------------------------------------------------------
 *  起床（送信側 → 受信待ちタスク）
 *  ------------------------------------------------------------------------
 *  FMP3 の `wup_tsk` は `CHECK_UNL_MYSTATE`（`fmp3/fmp3_core/kernel/task_sync.c:225`）
 *    ＝ `sense_lock()`（本ポートでは PS.INTLEVEL != 0）**だけ**で弾く。
 *    コンテキストは判定に使わない（`fmp3_core/kernel/check.h` の `check_unl_mystate()`）。
 *  ⇒ **ISR からは通る**（本ポートの L1/L3 は C ハンドラを INTLEVEL=0 で走らせる）。
 *    通らないのは **CPU ロック中**＝BT クリティカルセクション（`rsil` 保持）である。
 *  `iwup_tsk` は `wup_tsk` の別名（`fmp3_core/include/kernel.h:371`）なので
 *    文脈で呼び分ける必要が無い。
 *
 *  臨界区間（SIL 区間）の**外**でサービスコールを呼ぶ（m5 側と同じ作法）。
 */
static void
ring_wake(uint_t idx)
{
	ESP_SHIM_RING	*q = &ring_pool[idx];
	ID				w;
	RING_PRE_LOC;

	RING_LOCK();
	w = q->waiter;
	RING_UNLOCK();
	if (w == TSK_NONE) {
		return;
	}
	if (sns_loc()) {
		/*  今は起こせない（必ず E_CTX）。黙って諦めず**保留する**。 */
		RING_LOCK();
		ring_wake_pending |= (1U << idx);
		esp_shim_ring_n_wake_deferred++;
		RING_UNLOCK();
		return;
	}
	/*  E_QOVR（既に起床要求がラッチ済み）は正常。E_OBJ（相手が休止）も無視でよい。 */
	if (wup_tsk(w) == E_CTX) {
		RING_LOCK();
		ring_wake_pending |= (1U << idx);
		esp_shim_ring_n_wake_deferred++;
		RING_UNLOCK();
	}
}

void
esp_shim_ring_flush_wakes(void)
{
	uint32_t	pend;
	uint_t		i;
	ID			w;
	RING_PRE_LOC;

	if (sns_loc()) {
		return;		/*  まだ `wup_tsk` を出せない。保留は残したまま返る。 */
	}
	RING_LOCK();
	pend = ring_wake_pending;
	ring_wake_pending = 0U;
	RING_UNLOCK();
	if (pend == 0U) {
		return;
	}
	for (i = 0U; i < (uint_t) ESP_SHIM_RING_MAX; i++) {
		if ((pend & (1U << i)) == 0U) {
			continue;
		}
		RING_LOCK();
		w = ring_pool[i].waiter;
		RING_UNLOCK();
		if (w != TSK_NONE) {
			(void) wup_tsk(w);
			esp_shim_ring_n_wake_flushed++;
		}
	}
}

/*
 *  ------------------------------------------------------------------------
 *  生成・破棄
 *  ------------------------------------------------------------------------
 *  **黙って切り詰めない**（fail-closed）。旧 `esp_shim_queue_create()` は
 *    `len > ESP_SHIM_DTQ_CNT` を syslog 1 行で切り詰めていたが、
 *    切り詰めたキューは「たまに標本が落ちる」という**分かりにくい形**で表面化する。
 */
void *
esp_shim_ring_create(uint32_t len, uint32_t item_size)
{
	uint_t			i;
	uint8_t			*buf;
	ESP_SHIM_RING	*q = NULL;
	RING_PRE_LOC;

	if ((len == 0U) || (item_size == 0U)
		|| (len > (0xFFFFFFFFU / item_size))) {
		syslog(LOG_ERROR, "esp_shim_ring: create 引数が不正 len=%u item=%u",
			   (uint_t) len, (uint_t) item_size);
		esp_shim_ring_n_create_fail++;
		return(NULL);
	}
	buf = (uint8_t *) esp_shim_malloc((size_t) len * (size_t) item_size);
	if (buf == NULL) {
		syslog(LOG_ERROR, "esp_shim_ring: create ヒープ確保失敗 len=%u item=%u",
			   (uint_t) len, (uint_t) item_size);
		esp_shim_ring_n_create_fail++;
		return(NULL);
	}
	RING_LOCK();
	for (i = 0U; i < (uint_t) ESP_SHIM_RING_MAX; i++) {
		if (!ring_pool[i].used) {
			q = &ring_pool[i];
			q->used = true;
			q->len = len;
			q->item_size = item_size;
			q->head = 0U;
			q->tail = 0U;
			q->count = 0U;
			q->buf = buf;
			q->waiter = TSK_NONE;
			break;
		}
	}
	RING_UNLOCK();
	if (q == NULL) {
		esp_shim_free(buf);
		syslog(LOG_ERROR, "esp_shim_ring: プール枯渇（上限 MAX=%d）",
			   ESP_SHIM_RING_MAX);
		esp_shim_ring_n_create_fail++;
		return(NULL);
	}
	esp_shim_ring_n_create++;
	syslog(LOG_NOTICE, "esp_shim_ring: create len=%u item=%u buf=%uB",
		   (uint_t) len, (uint_t) item_size, (uint_t) (len * item_size));
	return((void *) q);
}

void
esp_shim_ring_delete(void *que)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	uint8_t			*buf;
	RING_PRE_LOC;

	if (q == NULL) {
		return;
	}
	RING_LOCK();
	buf = q->buf;
	q->buf = NULL;
	q->count = 0U;
	q->head = 0U;
	q->tail = 0U;
	q->waiter = TSK_NONE;
	q->used = false;
	RING_UNLOCK();
	esp_shim_free(buf);
}

void
esp_shim_ring_reset(void *que)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	RING_PRE_LOC;

	if (q == NULL) {
		return;
	}
	RING_LOCK();
	q->head = 0U;
	q->tail = 0U;
	q->count = 0U;
	RING_UNLOCK();
}

uint32_t
esp_shim_ring_msg_waiting(void *que)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	uint32_t		n;
	RING_PRE_LOC;

	if (q == NULL) {
		return(0U);
	}
	RING_LOCK();
	n = q->count;
	RING_UNLOCK();
	return(n);
}

uint32_t
esp_shim_ring_spaces_available(void *que)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	uint32_t		n;
	RING_PRE_LOC;

	if (q == NULL) {
		return(0U);
	}
	RING_LOCK();
	n = q->len - q->count;
	RING_UNLOCK();
	return(n);
}

/*
 *  ------------------------------------------------------------------------
 *  投入（共通本体）
 *  ------------------------------------------------------------------------
 *  戻り値: 1=入れた / 0=満杯。満杯なら**古いものを捨てない**（捨てると
 *  呼び手からは「入った」と区別が付かない）。
 */
static int32_t
ring_put(ESP_SHIM_RING *q, const void *item, int to_front)
{
	RING_PRE_LOC;

	RING_LOCK();
	if ((q->buf == NULL) || (q->count >= q->len)) {
		RING_UNLOCK();
		return(0);
	}
	if (to_front) {
		q->head = (q->head + q->len - 1U) % q->len;
		memcpy(&q->buf[q->head * q->item_size], item, q->item_size);
	}
	else {
		memcpy(&q->buf[q->tail * q->item_size], item, q->item_size);
		q->tail = (q->tail + 1U) % q->len;
	}
	q->count++;
	RING_UNLOCK();
	return(1);
}

static int32_t
ring_get(ESP_SHIM_RING *q, void *item)
{
	RING_PRE_LOC;

	RING_LOCK();
	if ((q->buf == NULL) || (q->count == 0U)) {
		RING_UNLOCK();
		return(0);
	}
	memcpy(item, &q->buf[q->head * q->item_size], q->item_size);
	q->head = (q->head + 1U) % q->len;
	q->count--;
	RING_UNLOCK();
	return(1);
}

static uint_t
ring_index(const ESP_SHIM_RING *q)
{
	return((uint_t) (q - &ring_pool[0]));
}

/*
 *  診断の氾濫よけ。「出す回数を絞る」ことと「出さない」ことは違う——
 *  カウンタ（`esp_shim_ring_n_*`）は常に進む。最初の 4 回と以後 1024 回ごと。
 */
static bool_t
ring_should_report(volatile uint32_t *p_cnt)
{
	uint32_t	n;
	RING_PRE_LOC;

	RING_LOCK();
	n = *p_cnt + 1U;
	*p_cnt = n;
	RING_UNLOCK();
	return((n <= 4U) || ((n % 1024U) == 0U));
}

/*
 *  ------------------------------------------------------------------------
 *  送信（ISR 文脈）
 *  ------------------------------------------------------------------------
 *  `SIL_*` は PS を退避・復元するので、既に INTLEVEL=15 の文脈から呼ばれても
 *    **退避値へ戻す**＝呼出し文脈に中立である。
 */
int32_t
esp_shim_ring_send_from_isr(void *que, void *item)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	int32_t			r;

	if ((q == NULL) || (item == NULL)) {
		return(0);
	}
	r = ring_put(q, item, 0);
	if (r == 0) {
		if (ring_should_report(&esp_shim_ring_n_send_full)) {
			syslog(LOG_ERROR, "esp_shim_ring: send_from_isr 満杯で入れられず"
				   "失敗を返す（古いものは捨てていない）n=%d",
				   (int_t) esp_shim_ring_n_send_full);
		}
		return(0);
	}
	ring_wake(ring_index(q));
	return(1);
}

/*
 *  ------------------------------------------------------------------------
 *  送信（タスク文脈・ブロッキング可）
 *  ------------------------------------------------------------------------
 *  満杯待ちの起床通知は持っていないので、短く寝て再試行する（m5 側と同じ）。
 *    待てない文脈（ISR／CPU ロック中）から `block_time_tick != 0` で呼ばれたら
 *    **黙って 0 を返さず言う**——「満杯だった」と「待てなかった」は別物である。
 */
int32_t
esp_shim_ring_send(void *que, void *item, uint32_t block_time_tick, int to_front)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	uint32_t		remain = block_time_tick;
	ER				er;

	if ((q == NULL) || (item == NULL)) {
		return(0);
	}
	esp_shim_ring_flush_wakes();	/*  機会的 flush（保留残の滞留防止） */
	for (;;) {
		if (ring_put(q, item, to_front) != 0) {
			ring_wake(ring_index(q));
			return(1);
		}
		if (remain == 0U) {
			if (ring_should_report(&esp_shim_ring_n_send_full)) {
				syslog(LOG_ERROR, "esp_shim_ring: send 満杯（非ブロッキング）"
					   " n=%d", (int_t) esp_shim_ring_n_send_full);
			}
			return(0);
		}
		if (sns_ctx() || sns_loc()) {
			if (ring_should_report(&esp_shim_ring_n_send_full)) {
				syslog(LOG_ERROR, "esp_shim_ring: 満杯だが待てない文脈"
					   "（ctx=%d lock=%d）＝0 を返す（空きを待っていない）",
					   (int_t) (sns_ctx() ? 1 : 0),
					   (int_t) (sns_loc() ? 1 : 0));
			}
			return(0);
		}
		er = tslp_tsk((RELTIM) 1000U);		/* 1ms */
		if ((er != E_OK) && (er != E_TMOUT)) {
			return(0);
		}
		if (remain != ESP_SHIM_BLOCK_FOREVER) {
			remain = (remain > 1U) ? (remain - 1U) : 0U;
		}
	}
}

/*
 *  ------------------------------------------------------------------------
 *  受信（**本ファイルの存在理由**）
 *  ------------------------------------------------------------------------
 *  `block_time_tick == 0` の即時取得は **`get_tid` も含めサービスコールを 1 つも
 *  呼ばない**。⇒ ISR 文脈からも CPU ロック中からも**正しく成功する**。
 *  旧写像先（`prcv_dtq`）はここで必ず E_CTX になり、それが 0＝「空」に畳まれて
 *    いた（H4 と同型）。**本実装ではその畳み込みが構造的に存在しない。**
 *
 *  `get_tid()` は**待ちに登録する直前でだけ**呼ぶ（`CHECK_UNL_MYSTATE`＝
 *    CPU ロック中は E_CTX。入口で呼ぶと「取れるのに空と答える」穴になる。
 *    m5 側で 2026-08-03 に同じ誤りを直している）。
 */
int32_t
esp_shim_ring_recv(void *que, void *item, uint32_t block_time_tick)
{
	ESP_SHIM_RING	*q = (ESP_SHIM_RING *) que;
	uint32_t		remain = block_time_tick;
	uint32_t		slice;
	ID				self;
	ER				er;
	RING_PRE_LOC;

	if ((q == NULL) || (item == NULL)) {
		return(0);
	}
	for (;;) {
		if (ring_get(q, item) != 0) {
			if (sns_ctx() || sns_loc()) {
				/*  ここが「以前は必ず失敗していた」経路である（計数する）。 */
				esp_shim_ring_n_recv_isrctx++;
			}
			RING_LOCK();
			if (q->waiter != TSK_NONE) {
				q->waiter = TSK_NONE;
			}
			RING_UNLOCK();
			return(1);
		}
		if (remain == 0U) {
			return(0);		/*  本当に空（「問えなかった」ではない） */
		}
		/*
		 *  ここから先は「待つ」＝タスク文脈でしか成立しない。
		 *  待てない文脈で待ちを頼まれたら**黙って 0 を返さない**。
		 */
		if (sns_ctx() || (get_tid(&self) != E_OK)) {
			if (ring_should_report(&esp_shim_ring_n_recv_nowait_ctx)) {
				syslog(LOG_ERROR, "esp_shim_ring: 待てない文脈で timeout>0 を"
					   "要求された（空ではない）ctx=%d lock=%d",
					   (int_t) (sns_ctx() ? 1 : 0),
					   (int_t) (sns_loc() ? 1 : 0));
			}
			return(0);
		}
		RING_LOCK();
		if ((q->waiter != TSK_NONE) && (q->waiter != self)) {
			/*
			 *  受信待ちは 1 枠しか持たない。2 人目は**登録せずに**待つ
			 *   （安全網の刻みで自分で見に来る）。黙って上書きすると
			 *   1 人目の起床が永久に失われる。
			 */
			RING_UNLOCK();
			esp_shim_ring_n_waiter_conflict++;
			self = TSK_NONE;
		}
		else {
			q->waiter = self;
			RING_UNLOCK();
		}
		/*  上の窓でデータが入っていたら取り直す。 */
		if (q->count > 0U) {
			continue;
		}
		slice = (remain == ESP_SHIM_BLOCK_FOREVER)
				? ESP_SHIM_RING_WAKE_SAFETY_MS
				: ((remain < ESP_SHIM_RING_WAKE_SAFETY_MS)
				   ? remain : ESP_SHIM_RING_WAKE_SAFETY_MS);
		er = tslp_tsk((RELTIM) (slice * 1000U));
		if (self != TSK_NONE) {
			RING_LOCK();
			if (q->waiter == self) {
				q->waiter = TSK_NONE;
			}
			RING_UNLOCK();
		}
		if ((er != E_OK) && (er != E_TMOUT)) {
			return(0);
		}
		if ((er == E_TMOUT) && (remain != ESP_SHIM_BLOCK_FOREVER)) {
			/*  起こされた（E_OK）ときは減らさない——起床は「待ち時間の消費」
			 *  ではない。時間切れ（E_TMOUT）のときだけ予算を減らす。 */
			remain = (remain > slice) ? (remain - slice) : 0U;
		}
	}
}
