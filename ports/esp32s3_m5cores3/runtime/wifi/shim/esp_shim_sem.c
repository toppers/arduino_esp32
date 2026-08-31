/*
 *  TOPPERS/FMP3 ESP32-S3 / ESP32(LX6) port
 *
 *  シムのセマフォプール — **動的生成（`acre_sem`/`del_sem`）へ写像する**
 *
 *  ========================================================================
 *  何が変わったか
 *  ========================================================================
 *  従来は `esp_shim.cfg` の `CRE_SEM(SHIM_SEM1..96)`（構成により 24/28/96 本が
 *  有効）を静的プールとして持ち、`esp_shim.c` の `shim_sem_id[]` /
 *  `shim_sem_used[]` が空きスロットを線形探索して配っていた。
 *  **本 TU はそのプール管理を廃し、カーネルの動的生成サービスコール
 *  `acre_sem` / `del_sem` を直接使う。**
 *
 *  **静的版と動的版の分岐は無い**（`#ifdef` も実行時分岐も）。無条件に動的生成。
 *    理由は `1765759`（キュー写像先の分岐を廃止）と、その分岐が実際に H4 を
 *    再発させた `。
 *
 *  **セマフォはカーネルメモリプール（`DEF_MPK`）を使わない**
 *    （`fmp3/fmp3_core/docs/dynamic-creation.md` §3.3、および
 *     `grep -c "alloc_mpk\|free_mpk" fmp3_core/kernel/semaphore.c` = 0）。
 *    ⇒ bump アロケータが単調に痩せる問題
 *    （` §4-4b/§4-4e）
 *    とは無関係で、`DEF_MPK` を足す必要も無い。
 *
 *  ========================================================================
 *  ハンドルの表現 — **裸の ID のまま変えない**（mtx と逆の判断。理由が違う）
 *  ========================================================================
 *  ハンドルは従来どおり `(void *)(intptr_t) semid`＝**セマフォ ID を裸で返す**。
 *  段1（mtx）は逆に「ID を裸で返さない」ことを選んだ。判断が割れるのは
 *    **同じ理由（ハンドルが型を跨いで流れる）から出発して、制約が違う**からである:
 *
 *    - `esp_shim_sem_take` / `_give` / `_get_count` / `_take_from_isr` /
 *      `_give_from_isr` は **ハンドルを `(ID)(intptr_t)` として直に
 *      サービスコールへ渡す**（`esp/shim/esp_shim.c`・`esp_shim_isr_ctx.c`）。
 *      これらは**本段の射程外**（プール管理を含まない）。
 *      ⇒ 表現を実アドレスへ変えると、**射程外の 5 関数を同時に書き換える**ことになる。
 *      本段は機構だけを移す段なので、**表現は動かさない。**
 *    - mtx 側は再帰カウントのために**どのみち状態表が要った**ので、
 *      その表のアドレスをハンドルにするのが自然だった。sem には状態が要らない
 *      （初期値は `acre_sem` の `isemcnt` が直接立てる）。
 *
 *  **ミューテックスのハンドルがここへ来る**（AC の M-9）:
 *      `esp-idf/components/bt/porting/npl/freertos/src/npl_os_freertos.c`
 *      `npl_freertos_mutex_deinit()` は **`vSemaphoreDelete(mutex->handle)`** を呼び、
 *      `esp/bt/stub/include/freertos/semphr.h` がそれを `esp_shim_sem_delete()` へ
 *      写像する。**mutex->handle は段1 以降 `&shim_mtx_state[i]`＝DRAM の実アドレス**
 *      （`esp/shim/esp_shim_mtx.c`）。
 *      ⇒ `(ID)(intptr_t)` に落とすと **10 億級の値**になり、下の範囲検査
 *        `[SHIM_SEM_TMIN_ID, SHIM_SEM_TMIN_ID + TNUM_SEMID)` に**絶対に入らない**。
 *        **カーネルへは 1 回も渡らない**ので誤削除は起こらない。
 *      ⇒ この経路は `esp_shim_sem_del_foreign` に**数えるが syslog は出さない**
 *        （BLE 構成で正規に毎回通る経路なので、出すと診断が氾濫して
 *         本物の失敗が埋もれる。「絞る」のであって「黙る」のではない＝
 *         カウンタは常に進む。`esp_shim_isr_ctx.c` と同じ作法）。
 *      **旧実装との差は無い**——旧実装も `shim_sem_id[]` との突合に失敗して
 *        no-op になっていた（＝NimBLE のミューテックスは今も昔も
 *        `vSemaphoreDelete` では消えない。これは既知の漏れであって本段の新規欠陥
 *        ではなく、本段の射程でもない）。
 *
 *  **裸 ID の代償（正直に書く）**: **stale ハンドルを検出できない。**
 *    削除済み ID がすぐ再利用されると（カーネルの再利用は FIFO。同 §4）、
 *    古いハンドルでの `delete` が**別のセマフォを消し得る**。
 *    これは**旧実装と同じ**（旧も ID 一致でスロットを返していた）ので
 *    **退行ではない**が、**改善もしていない。**改善するには表現を太らせる必要があり、
 *    それは射程外の 5 関数を巻き込む。**次に触る人はここを読むこと。**
 *
 *  ========================================================================
 *  文脈の制約
 *  ========================================================================
 *  `acre_sem`/`del_sem` は `CHECK_TSKCTX_UNL`＝**タスク文脈かつ CPU ロック外**
 *  でしか成功しない。旧実装は `SHIM_LOCK`（`rsil 15`）の中でスロットを配り、
 *  その後の `pol_sem`/`sig_sem` の `E_CTX` を**捨てていた**ので、
 *  **どの文脈からでも「非 NULL のハンドル」が返っていた**。
 *  ⇒ **本 TU はサービスコールを CPU ロックの外で呼ぶ**（`SHIM_LOCK` を使わない）。
 *    ID は `acre_sem` が返した時点でこの呼出しに専有される。
 *  ⇒ 禁止文脈から呼ばれた場合は `E_CTX` になる。**畳まず診断に残す**（§7-4）。
 *    呼出し側から見た戻り値は従来と同じ `NULL`（境界は畳まざるを得ない）。
 *
 *  ========================================================================
 *  意味論は変えない（射程を広げない）
 *  ========================================================================
 *  `max`（FreeRTOS の `uxMaxCount`）は**従来どおり無視する**。
 *    旧実装は `CRE_SEM(..., TMAX_MAXSEM)` 固定で `(void) max;` していた。
 *    honor すると **binary セマフォの二重 give が `E_QOVR` で硬く失敗する**ように
 *    なる——それは動的化とは**別の意味論変更**であって、本段の射程ではない。
 *    `acre_sem` は `1 <= maxsem` と `isemcnt <= maxsem` を検査するので、
 *    `TMAX_MAXSEM`（= `UINT_MAX`）を渡せば旧挙動と一致する。
 *  初期値 `init` は `acre_sem` の `isemcnt` が**直接**立てる。
 *    旧実装の「`pol_sem` で空にしてから `sig_sem` を init 回」は
 *    **スロット再利用のための後始末**で、動的生成では生成物が常に新品なので要らない。
 *    ⇒ **生成経路からサービスコールが 2 種消えた**（`pol_sem`/`sig_sem`）。
 *
 *  ========================================================================
 *  エラー値の扱い（§7-4）
 *  ========================================================================
 *  `acre_sem` は `E_NOID`（予約枯渇）/ `E_CTX`（文脈）/ `E_RSATR` / `E_PAR` を
 *  区別して返す。**値を意味へ翻訳した分岐表は作らない**（pin 更新で腐る）。
 *  **値をそのまま診断へ通す**（`ercd` を数値で出し、直近値を公開変数に残す）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "kernel_cfg.h"			/* TNUM_SEMID（cfg 生成。静的＋AID_SEM の総数） */
#include "esp_shim.h"

/*
 *  ID の下限。
 *
 *  カーネルの `TMIN_SEMID` は**内部ヘッダ**（`fmp3/fmp3_core/kernel/kernel_impl.h`）
 *  にあり、シム層（カーネル外）から include してはいけない。値は FMP3 の全
 *  オブジェクト種で 1 固定である。よってここで自前に定義する。
 *  写しが腐ることに対する備えは**コメントではなく実行時の範囲検査**である——
 *  `esp_shim_sem_create()` は `acre_sem` が返した ID が
 *  `[SHIM_SEM_TMIN_ID, SHIM_SEM_TMIN_ID + TNUM_SEMID)` に入ることを毎回確かめ、
 *  外れたら **fail-closed する**（`del_sem` して NULL を返す）。
 */
#define SHIM_SEM_TMIN_ID	1

/*
 *  診断カウンタ（JTAG／将来のダンプから読めるように公開する）。
 *  `ercd` は**翻訳せずそのまま**残す（E_NOID=-33 / E_CTX=-25 など）。
 */
volatile uint32_t	esp_shim_sem_acre_fail;		/* acre_sem が失敗した回数      */
volatile int32_t	esp_shim_sem_acre_last_ercd;/* 直近の acre_sem のエラー値   */
volatile uint32_t	esp_shim_sem_del_fail;		/* del_sem が失敗した回数       */
volatile int32_t	esp_shim_sem_del_last_ercd;	/* 直近の del_sem のエラー値    */
volatile uint32_t	esp_shim_sem_del_foreign;	/* 範囲外ハンドルで呼ばれた回数 */
volatile uint32_t	esp_shim_sem_live;			/* 生きている本数（+1/-1）      */

/*
 *  診断（沈黙させない）。氾濫を避けるため最初の 4 回とその後 1024 回ごとに出す。
 *  「出す回数を絞る」ことと「出さない」ことは違う——カウンタは常に進む
 *  （`esp_shim_isr_ctx.c`・`esp_shim_mtx.c` と同じ作法）。
 */
static void
shim_sem_report(const char *what, ER ercd, volatile uint32_t *p_cnt)
{
	uint32_t	n;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	n = *p_cnt + 1U;
	*p_cnt = n;
	SIL_UNL_INT();

	if ((n <= 4U) || ((n % 1024U) == 0U)) {
		syslog(LOG_ERROR,
			   "esp_shim: %s failed ercd=%d (ctx=%d lock=%d) n=%d",
			   what, (int_t) ercd,
			   (int_t) (sns_ctx() ? 1 : 0), (int_t) (sns_loc() ? 1 : 0),
			   (int_t) n);
	}
}

static void
shim_sem_count(volatile uint32_t *p_cnt, int32_t delta)
{
	SIL_PRE_LOC;

	SIL_LOC_INT();
	*p_cnt = (uint32_t)((int32_t) *p_cnt + delta);
	SIL_UNL_INT();
}

void *
esp_shim_sem_create(uint32_t max, uint32_t init)
{
	T_CSEM		csem;
	ER_ID		ercd;

	/*  属性・上限は静的生成時（`CRE_SEM(..., { TA_NULL, 0, TMAX_MAXSEM })`）と
	 *  同じにする。`max` を無視するのも旧実装と同じ（本ファイル冒頭「意味論は
	 *  変えない」）。初期値だけは `isemcnt` へ直接渡す。 */
	csem.sematr = TA_NULL;
	csem.isemcnt = (uint_t) init;
	csem.maxsem = TMAX_MAXSEM;
	(void) max;

	/*  CPU ロックの外・タスク文脈で呼ぶこと（本ファイル冒頭「文脈の制約」）。 */
	ercd = acre_sem(&csem);
	if (ercd <= 0) {
		esp_shim_sem_acre_last_ercd = (int32_t) ercd;
		shim_sem_report("acre_sem", (ER) ercd, &esp_shim_sem_acre_fail);
		return(NULL);
	}
	if ((ercd < SHIM_SEM_TMIN_ID)
		|| (ercd >= (SHIM_SEM_TMIN_ID + (ID) TNUM_SEMID))) {
		/*  起こり得ないが、起きたら黙って配るより落とす方がよい。 */
		esp_shim_sem_acre_last_ercd = (int32_t) ercd;
		shim_sem_report("acre_sem(range)", E_SYS, &esp_shim_sem_acre_fail);
		(void) del_sem((ID) ercd);
		return(NULL);
	}
	shim_sem_count(&esp_shim_sem_live, 1);
	return((void *)(intptr_t) ercd);
}

void
esp_shim_sem_delete(void *sem)
{
	ID		semid = (ID)(intptr_t) sem;
	ER		ercd;

	if ((semid < SHIM_SEM_TMIN_ID)
		|| (semid >= (SHIM_SEM_TMIN_ID + (ID) TNUM_SEMID))) {
		/*  セマフォ ID ではないハンドル。数えるが喋らない（冒頭 M-9 の段落）。
		 *  NULL もここ（旧実装も静かに no-op だった）。 */
		shim_sem_count(&esp_shim_sem_del_foreign, 1);
		return;
	}

	ercd = del_sem(semid);
	if (ercd != E_OK) {
		/*  静的セマフォ（`SHIM_TIMER_SEM` 等）を渡されると `E_OBJ`、
		 *  既に消えた ID なら `E_NOEXS`。**翻訳せず生値で**残す。 */
		esp_shim_sem_del_last_ercd = (int32_t) ercd;
		shim_sem_report("del_sem", ercd, &esp_shim_sem_del_fail);
		return;
	}
	shim_sem_count(&esp_shim_sem_live, -1);
}
