/*
 *  TOPPERS/FMP3 Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  esp_timer_* の共通実装（Wi-Fi構成・BT/BLE構成の両方でリンクする）
 *
 *  2026-07-27 レビュー esp-2 対処：本ファイルは esp/bt/bt_shim.c から
 *  esp_timer_* 群とタイマタスクをそのまま切り出したものである（実装は
 *  無改変．識別子のみ BT_/bt_ 接頭辞から ESP_TIMER_/esp_timer_ へ改名）．
 *
 *  切り出しの理由：
 *   - 従来 Wi-Fi 構成は esp/shim/wifi_stubs.c の「ESP_OK を返す no-op」
 *     を esp_timer_* の実体としていた．このため esp/wifi/hal_src/
 *     phy_common.c の phy_track_pll_init() が ESP_ERROR_CHECK 付きで
 *     成功を受け取りながら，PHY PLL トラッキング（既定 1Hz）が一度も
 *     走らない状態になっていた（S3-wifi 構成のみ．LX6-wifi は
 *     phy_init.c の `#if !CONFIG_IDF_TARGET_ESP32`，S3-BLE は
 *     -DCONFIG_ESP_PHY_DISABLE_PLL_TRACK=1 により呼出し経路が無い）．
 *   - BLE 構成は bt_shim.o（本物）と wifi_stubs.o（no-op）の両方を
 *     リンクし，-Wl,--allow-multiple-definition ＋ リンク行のオブジェクト
 *     順（`ls objs` の *.o 辞書順で bt_shim.o が先）で本物が選ばれていた．
 *     実装を1本に集約することで，この偶然依存を構造的に解消する．
 *
 *  cfg オブジェクト（ESP_TIMER_SEM／ESP_TIMER_TSK）は esp/shim/esp_shim.cfg
 *  で定義する（従来は esp/bt/bt.cfg の BT_TIMER_SEM／BT_TIMER_TSK）．
 *  クラスは CLS_ALL_PRC1 のまま（BLE の従来挙動を保つ）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdint.h>
#include "kernel_cfg.h"

#include "esp_timer.h"

#include "esp_shim.h"
#include "esp_shim_cfg.h"

/*
 *  排他：bt_shim.c の BT_LOCK()/BT_UNLOCK() と同一（割込み禁止）．
 *  プール操作はいずれも数命令で終わるため，これで足りる．
 */
#define ETMR_LOCK()		uint32_t etmr_lock_ = esp_shim_int_disable()
#define ETMR_UNLOCK()	esp_shim_int_restore(etmr_lock_)

/*
 *  ------------------------------------------------------------------
 *  esp_timer_*（固定プール＋専用タイマタスクによる実装）
 *
 *  esp_shim.cのets_timer機構（SHIM_TIMER_TSK／key=blob側ETSTimer*の
 *  動的リスト）とは別の，esp_timer_handle_t向けの小さな専用実装．
 *  ------------------------------------------------------------------
 */
struct esp_timer {
	bool_t			used;
	bool_t			active;
	int64_t			deadline_us;
	uint64_t		period_us;	/* 0=one-shot */
	esp_timer_cb_t	callback;
	void			*arg;
};

static struct esp_timer	esp_timer_pool[ESP_TIMER_NUM];

/*
 *  ============================================================================
 *  part1-8 検証計装（ESP_TIMER_LATE_PROBE）— 既定ビルドには1バイトも入らない
 *  ----------------------------------------------------------------------------
 *  【何を測るか】未解決の「初回発火が期限より 533 ms 遅い」
 *  （非公開作業記録/20260727-fable-fixes-batch1/part1-7-result.md §6）を、
 *  **時間区間の分割**で切り分ける。期限 D から実際に cb() へ到達した時刻 F
 *  までを、唯一の観測可能な中間点 W（twai_sem から戻った瞬間）で割る：
 *
 *      over = (F - D) = (W - D) + (F - W) = late + gap        … 恒等式
 *
 *  さらに (W - D) は、カーネルがタイムイベントを処理した時刻 H で割れる：
 *
 *      (W - D) = (H - D)          … タイムイベント配送の遅れ
 *              + (W - H)          … 実行可能→実行（ディスパッチ）の遅れ
 *
 *  (H - D) は本タスクからは直接見えないので、**同じ signal_time() ループで
 *  呼ばれる周期通知**（esp_timer_witness_cyc・esp_shim.cfg）を目撃者に使う。
 *  根拠：fmp3_core/kernel/cyclic.c:315 call_cyclic() は
 *    fmp3_core/kernel/time_event.c:709 signal_time() の
 *    「期限到来タイムイベントを順に callback する」ループから呼ばれる。
 *    twai_sem のタイムアウトも同じヒープの同じループで処理される。
 *    ⇒ 目撃者が定刻に走ったなら (H - D) ≈ 0 である。
 *
 *  【出力を有界化してある】閾値 ESP_TIMER_LATE_TH_US 以上のときだけ出し、
 *  種別ごとに ESP_TIMER_LATE_MAX_LOG 行で打ち切る。先頭 ESP_TIMER_LATE_HEAD_N
 *  回だけは閾値未満でも出す（「小さいとき」の対照が無いと大きさを論じられない）。
 *
 *  【行番号中立は不要】本 TU には __LINE__／__FILE__ を即値化する経路が無い
 *  （ESP_ERROR_CHECK も assert も使っていない）。esp/wifi/hal_src/phy_common.c
 *  の #line トリックが要ったのはそちらが ESP_ERROR_CHECK(__LINE__) を持つため。
 *  ただし「不要」は**推測ではなく実測で確かめること**＝フラグ OFF での
 *    golden 6 構成バイト不変（part1-8-ac.md §2-2 / part1-8-result.md §8）。
 *
 *  ============================================================================
 *  2026-07-28 実測後の修正 2 点（part1-8-result.md §5・§6）
 *  ----------------------------------------------------------------------------
 *  (A) **目撃者に生存証明が無かった**（コーディネータの指摘。真）。
 *      初版は「閾値超のときだけ出す」設計だったため、Run D/E の
 *      「[ETMRW] 0 行」が **「定刻だった」とも「一度も走っていない」とも
 *      読めた**＝GATE-3 が判定に使えなかった。⇒ `[ETMR] wake`/`fire` と
 *      **同じ形**（先頭 N 回は無条件）に揃え、さらに
 *      **ESP_TIMER_WITNESS_BEAT 回ごとの生存拍**を足した。拍が観測窓を
 *      またいで並ぶので、「窓の中に行が無い」が初めて意味を持つ。
 *      教訓：**沈黙を根拠にする計装には、沈黙していないことの証明が要る。**
 *
 *  (B) **目撃者は本フラグから分離した**（ESP_TIMER_WITNESS）。
 *      Run D/E は `esp_wifi_init_internal()` に **26.9 秒**を要し
 *      （run C は 0.22 秒未満）、しかも D/E 2 本が **2.5 ms 以内で一致**した＝
 *      **決定論的**。バイナリ側の差分は本計装しかない。⇒ 100 ms 周期通知が
 *      第一容疑者だが、初版は probe と目撃者を 1 フラグに束ねていたため
 *      **犯人を切り分けられなかった**（設計ミス）。分離したので次の 1 run で
 *      切り分けられる。因果は**未確定（推測）**である。
 *  ============================================================================
 */

/*  共通の定数（probe / 目撃者のどちらか一方だけでも使う）  */
#if defined(ESP_TIMER_LATE_PROBE) || defined(ESP_TIMER_WITNESS)
#ifndef ESP_TIMER_LATE_TH_US
#define ESP_TIMER_LATE_TH_US	10000	/* 10ms 以上の遅れだけ出す */
#endif
#ifndef ESP_TIMER_LATE_MAX_LOG
#define ESP_TIMER_LATE_MAX_LOG	64U		/* 種別ごとの出力上限 */
#endif
#ifndef ESP_TIMER_LATE_HEAD_N
#define ESP_TIMER_LATE_HEAD_N	3U		/* 先頭は閾値未満でも出す */
#endif
#endif

#ifdef ESP_TIMER_LATE_PROBE

/*
 *  直近の「眠り」の記録。ESP_TIMER_TSK は cfg 上 1 本だけなので static で足りる
 *  （複数タスクが本関数へ入ることは無い）。
 */
static int64_t	etmr_p_t_before;	/* twai_sem 直前の時刻 */
static int64_t	etmr_p_t_after;		/* twai_sem 直後の時刻 */
static TMO		etmr_p_tmo;			/* twai_sem に渡した TMO */
static int64_t	etmr_p_late;		/* (t_after - t_before) - tmo */
static uint32_t	etmr_p_wake_n;
static uint32_t	etmr_p_fire_n;
static uint32_t	etmr_p_wake_logged;
static uint32_t	etmr_p_fire_logged;

/*
 *  期限到来を検出した瞬間（再武装の**前**、cb() の直前）に呼ぶ。
 *  deadline は再武装前の期限。
 */
static void
etmr_probe_fire(int64_t deadline)
{
	int64_t		t_fire = esp_shim_time_us();
	int64_t		over = t_fire - deadline;
	int64_t		gap = t_fire - etmr_p_t_after;

	etmr_p_fire_n++;
	if ((over >= (int64_t) ESP_TIMER_LATE_TH_US
					|| etmr_p_fire_n <= ESP_TIMER_LATE_HEAD_N)
				&& etmr_p_fire_logged < ESP_TIMER_LATE_MAX_LOG) {
		etmr_p_fire_logged++;
		/*  over = late + gap（恒等式）．tmo は直前の眠りへ渡した値  */
		syslog(LOG_NOTICE, "[ETMR] fire m=%u over=%d late=%d gap=%u tmo=%u",
			   (uint32_t) etmr_p_fire_n, (int32_t) over,
			   (int32_t) etmr_p_late, (uint32_t) gap,
			   (uint32_t) etmr_p_tmo);
	}
}

/*
 *  twai_sem を挟んで「要求した tmo」と「実際に眠った時間」を測る。
 *  OFF のときは呼出し側が素の (void) twai_sem(...) を通る（本関数は消える）。
 */
static void
etmr_probe_wait(TMO tmo)
{
	int64_t		waited;
	ER			ercd;

	etmr_p_tmo = tmo;
	etmr_p_t_before = esp_shim_time_us();
	ercd = twai_sem(ESP_TIMER_SEM, tmo);
	etmr_p_t_after = esp_shim_time_us();

	waited = etmr_p_t_after - etmr_p_t_before;
	if (tmo == TMO_FEVR) {
		etmr_p_late = 0;	/*  無限待ちに「遅れ」は定義できない  */
	}
	else {
		etmr_p_late = waited - (int64_t) tmo;
	}
	etmr_p_wake_n++;
	if ((etmr_p_late >= (int64_t) ESP_TIMER_LATE_TH_US
					|| etmr_p_wake_n <= ESP_TIMER_LATE_HEAD_N)
				&& etmr_p_wake_logged < ESP_TIMER_LATE_MAX_LOG) {
		etmr_p_wake_logged++;
		/*  er=0(E_OK)は sig_sem による早期復帰＝異常ではない．
		 *  er=-50(E_TMOUT)が本来のタイムアウト．                */
		syslog(LOG_NOTICE, "[ETMR] wake k=%u tmo=%u waited=%u late=%d er=%d",
			   (uint32_t) etmr_p_wake_n, (uint32_t) tmo,
			   (uint32_t) waited, (int32_t) etmr_p_late, (int32_t) ercd);
	}
}

#endif	/* ESP_TIMER_LATE_PROBE */

#ifdef ESP_TIMER_WITNESS

#ifndef ESP_TIMER_WITNESS_HEAD_N
#define ESP_TIMER_WITNESS_HEAD_N	3U		/* 先頭は無条件（生存証明） */
#endif
#ifndef ESP_TIMER_WITNESS_BEAT
#define ESP_TIMER_WITNESS_BEAT		100U	/* 100 回＝10s ごとの生存拍 */
#endif

static volatile bool_t	etmr_w_armed;
static bool_t	etmr_w_init;
static int64_t	etmr_w_next;		/* 次に走るべき時刻（HRT μs） */
static uint32_t	etmr_w_n;
static uint32_t	etmr_w_logged;

/*
 *  目撃者：周期通知ハンドラ（**非タスクコンテキスト**・signal_time() から）
 *
 *  FMP3 の call_cyclic は evttim を cyctim ずつ進める固定グリッドなので、
 *    こちらも etmr_w_next += period とすれば位相がずれない。遅延で n 周期
 *    取りこぼした場合はカーネル側が連続して呼び直すので、
 *    late が period ずつ減っていく「階段」が出る＝配送遅れの指紋になる。
 *  syslog は非タスクコンテキストから呼んでよい（syslog_wri_log は
 *    SIL_LOC_SPN のみ・fmp3_core/syssvc/syslog.c:120。fmp3_core の
 *    sample1.c:602 cyclic_handler も syslog を呼んでいる）。
 *
 *  出力条件（2026-07-28 修正・上の (A)）：
 *      late >= 閾値  … 本来の目的（配送遅れの検出）
 *    ｜ w <= HEAD_N   … **目撃者が生きている**ことの positive control
 *    ｜ w % BEAT == 0 … **生存拍**。観測窓の前後に拍が並ぶことで、
 *                      「窓の中に late 行が無い＝定刻だった」が初めて成立する。
 *    初版はこの 2 つが無く、「0 行」が「定刻」とも「死んでいた」とも読めた。
 */
void
esp_timer_witness_cyc(EXINF exinf)
{
	int64_t		now;
	int64_t		late;

	(void) exinf;
	if (!etmr_w_armed) {
		return;		/*  esp_timer 利用者が現れるまで黙る  */
	}
	now = esp_shim_time_us();
	if (!etmr_w_init) {
		etmr_w_init = true;
		etmr_w_next = now;		/*  原点を置くだけ（この回は late=0）  */
	}
	late = now - etmr_w_next;
	etmr_w_next += (int64_t) ESP_TIMER_WITNESS_PERIOD_US;
	etmr_w_n++;
	if ((late >= (int64_t) ESP_TIMER_LATE_TH_US
					|| etmr_w_n <= ESP_TIMER_WITNESS_HEAD_N
					|| (etmr_w_n % ESP_TIMER_WITNESS_BEAT) == 0U)
				&& etmr_w_logged < ESP_TIMER_LATE_MAX_LOG) {
		etmr_w_logged++;
		syslog(LOG_NOTICE, "[ETMRW] cyc w=%u t=%u late=%d",
			   (uint32_t) etmr_w_n, (uint32_t) now, (int32_t) late);
	}
}

#define ETMR_PROBE_ARM()		(etmr_w_armed = true)

#else	/* !ESP_TIMER_WITNESS */

#define ETMR_PROBE_ARM()		((void) 0)

#endif	/* ESP_TIMER_WITNESS */

#ifdef ESP_TIMER_LATE_PROBE
#define ETMR_PROBE_FIRE(dl)		etmr_probe_fire(dl)
#else
#define ETMR_PROBE_FIRE(dl)		((void) 0)
#endif

esp_err_t
esp_timer_create(const esp_timer_create_args_t *create_args,
				  esp_timer_handle_t *out_handle)
{
	uint_t	i;
	struct esp_timer	*t = NULL;

	ETMR_LOCK();
	for (i = 0U; i < ESP_TIMER_NUM; i++) {
		if (!esp_timer_pool[i].used) {
			esp_timer_pool[i].used = true;
			t = &esp_timer_pool[i];
			break;
		}
	}
	ETMR_UNLOCK();

	if (t == NULL) {
		syslog(LOG_ERROR, "esp_timer_shim: pool exhausted");
		return(ESP_ERR_NO_MEM);
	}
	t->active = false;
	t->callback = create_args->callback;
	t->arg = create_args->arg;
	t->period_us = 0U;
	*out_handle = (esp_timer_handle_t) t;
	return(ESP_OK);
}

esp_err_t
esp_timer_delete(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	ETMR_LOCK();
	t->active = false;
	t->used = false;
	ETMR_UNLOCK();
	return(ESP_OK);
}

static esp_err_t
timer_start(struct esp_timer *t, uint64_t timeout_us, uint64_t period_us)
{
	ETMR_LOCK();
	t->deadline_us = esp_shim_time_us() + (int64_t) timeout_us;
	t->period_us = period_us;
	t->active = true;
	ETMR_UNLOCK();
	ETMR_PROBE_ARM();				/* part1-8 計装。既定は空マクロ */
	(void) sig_sem(ESP_TIMER_SEM);	/* タイマタスクへ再計算を促す */
	return(ESP_OK);
}

esp_err_t
esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
	return(timer_start((struct esp_timer *) timer, timeout_us, 0U));
}

esp_err_t
esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
	return(timer_start((struct esp_timer *) timer, period, period));
}

esp_err_t
esp_timer_stop(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	ETMR_LOCK();
	t->active = false;
	ETMR_UNLOCK();
	return(ESP_OK);
}

/*
 *  esp_timer_get_time()はesp_shim_libc.cで既に実装済み（Wi-Fi統合時に
 *  phy_init.c用として追加．BTも同じPHYを使うためそのまま共用する．
 *  ここでは重複定義しない）．
 */

/*
 *  NimBLE NPL（npl_os_freertos.cのコールアウト＝esp_timer経路．Phase
 *  BT-2）が要求する参照系API．struct esp_timerのdeadline_us/activeを
 *  そのまま返す．
 */
esp_err_t
esp_timer_get_expiry_time(esp_timer_handle_t timer, uint64_t *expiry)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	if (t == NULL || expiry == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (!t->active) {
		return(ESP_ERR_INVALID_STATE);
	}
	*expiry = (uint64_t) t->deadline_us;
	return(ESP_OK);
}

bool
esp_timer_is_active(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	return(t != NULL && t->active);
}

/*
 *  タイマタスク：期限順の線形走査（ESP_TIMER_NUM が小さい
 *  （BT=16／Wi-Fi=4）ためソート済みリストは使わず毎回全走査で十分）
 *
 *  2026-07-28 修正（part1-7）：周期タイマが1回しか発火しない欠陥の修正
 *
 *  修正前の走査は
 *      if (期限到来) { 再武装して cb() }
 *      else if (nearest 更新)
 *    という形だったため，**発火したタイマの「再武装後の新しい期限」が
 *    nearest の候補に入らなかった**．他に active なタイマが無い構成
 *    （Wi-Fi 構成＝phy_track_pll の1本だけ）では nearest が -1 のままとなり，
 *    tmo = TMO_FEVR ＝ twai_sem で**永久に眠る**．sig_sem(ESP_TIMER_SEM) を
 *    出すのは timer_start() だけなので，以後だれも起こさない．
 *    ⇒ 周期タイマが**実質ワンショット**になっていた（実機 S3-wifi で
 *      「fired n=1 の1行だけ・アプリは 180 秒生存」を観測．
 *      非公開作業記録/20260727-fable-fixes-batch1/part1-6-esp2-result.md）．
 *
 *  修正の要点は3つ：
 *   (1) 再武装後の期限を **cb() を呼ぶ前に** ローカルへ確保して nearest に
 *       畳み込む．cb() の中から esp_timer_stop/delete/start が呼ばれ得る
 *       （IDF の実例：phy_track_pll_deinit）ので，cb() の**後で**
 *       t->deadline_us を読み直してはならない．
 *       ・cb() がそのタイマを止めた場合：nearest は「早すぎる」値になるが，
 *         その時刻に空振りで起きて走査し直すだけ（無害）．
 *       ・cb() が別のタイマを張った場合：timer_start() が sig_sem を出すので
 *         直後の twai_sem が即復帰し，走査し直す（取りこぼさない）．
 *   (2) 取りこぼした周期は**まとめて追いつかない**（バーストさせない）．
 *       位相（開始時刻＋n*period）は保ったまま，次の期限が必ず now より
 *       未来になるところまで飛ばす．これにより periodic では常に
 *       nearest > now ＝ tmo >= 1 が保証され，「tmo=0 で無限に回り続ける」
 *       暴走が構造的に起きない．
 *       ESP-IDF 本家（esp-idf/components/esp_timer/src/esp_timer.c:412-421）は
 *         既定で `it->alarm += it->period`（＝取りこぼした分だけ連続発火）で，
 *         ESP_TIMER_FLAG_SKIP_UNHANDLED_EVENTS を付けたときだけ
 *         `it->alarm = now + it->period` に切り替える．本実装は
 *         **常に後者側（スキップ）**を採る＝本家既定からの意図的な逸脱である．
 *         理由：本実装のディスパッチャは優先度5のタスク1本であり，遅延の
 *         原因（上位優先度タスクによる横取り）がそのまま連続発火の駆動力に
 *         なるため，追いつき発火が負のフィードバックになる．また現在の
 *         周期タイマ利用者は phy_track_pll（べき等な「今すぐ追従」動作）
 *         1本だけで，取りこぼし分の再実行に意味が無い．
 *       本家との違いとして，位相は now 基準に置き直さず元の位相を保つ
 *         （IDF の SKIP は now + period にリセットするので位相が流れる）．
 *   (3) TMO は uint32_t（μs 単位・t_stddef.h）で TMO_FEVR == UINT32_MAX．
 *       (TMO)(nearest - now) を素で書くと，差が 2^32 μs（≒71.6 分）以上の
 *       ときに切り捨てで別の値になり，差がちょうど UINT32_MAX のときは
 *       **偶然 TMO_FEVR（永久待ち）になる**．クランプして両方を塞ぐ．
 *
 *  本関数はロックを取らずに走査する（cb() もロック外で呼ぶ）．これは
 *    移設元 bt_shim.c からの既存仕様で，本修正でも変えていない．
 *    ETMR_LOCK() は rsil 15 ＝**自コアの割込み禁止だけ**でありコア間排他
 *    ではないため，走査に被せても SMP 安全にはならない（別コアの
 *    esp_timer_start と競合しうる）．この点は本件とは別の既知の限界として
 *    part1-7-timertask-design.md §6 に記す．
 */
void
esp_timer_shim_task(EXINF exinf)
{
	(void) exinf;

	for (;;) {
		int64_t		now;
		int64_t		nearest = -1;
		uint_t		i;
		TMO			tmo;

		now = esp_shim_time_us();
		for (i = 0U; i < ESP_TIMER_NUM; i++) {
			struct esp_timer	*t = &esp_timer_pool[i];

			if (t->used && t->active) {
				if (t->deadline_us <= now) {
					esp_timer_cb_t	cb = t->callback;
					void			*arg = t->arg;
					int64_t			period = (int64_t) t->period_us;

					/*  part1-8 計装。既定は空マクロ（引数も評価されない）．
					 *  再武装で t->deadline_us を書き換える**前**に測る．     */
					ETMR_PROBE_FIRE(t->deadline_us);
					if (period > 0) {
						int64_t		next = t->deadline_us + period;

						if (next <= now) {
							/*  遅延で周期を取りこぼした：位相を保ったまま
							 *  now より未来へ飛ばす（連続発火はしない）  */
							next += ((now - next) / period + 1) * period;
						}
						t->deadline_us = next;
						/*  cb() を呼ぶ**前に**畳み込む（(1) の理由）  */
						if (nearest < 0 || next < nearest) {
							nearest = next;
						}
					}
					else {
						t->active = false;
					}
					if (cb != NULL) {
						cb(arg);
					}
				}
				else if (nearest < 0 || t->deadline_us < nearest) {
					nearest = t->deadline_us;
				}
			}
		}

		if (nearest < 0) {
			tmo = TMO_FEVR;
		}
		else {
			int64_t		diff;

			now = esp_shim_time_us();
			diff = nearest - now;
			if (diff <= 0) {
				tmo = TMO_POL;
			}
			else if (diff >= (int64_t)(TMO_FEVR - 1U)) {
				tmo = (TMO)(TMO_FEVR - 1U);	/*  永久待ちへ落とさない  */
			}
			else {
				tmo = (TMO) diff;
			}
		}
		/*  part1-8 計装。OFF 側は素の (void) twai_sem(...) のままにして
		 *  あり（マクロで括ると「マクロが呼出しごと落とす」事故が起きる）．  */
#ifdef ESP_TIMER_LATE_PROBE
		etmr_probe_wait(tmo);
#else
		(void) twai_sem(ESP_TIMER_SEM, tmo);
#endif
	}
}
