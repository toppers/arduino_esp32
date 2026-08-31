/*
 *  TOPPERS/FMP3 ESP32-S3 / ESP32(LX6) port
 *
 *  シムのミューテックス — **動的生成（`acre_mtx`/`del_mtx`）へ写像する**
 *
 *  ========================================================================
 *  方式
 *  ========================================================================
 *  カーネルの動的生成サービスコール `acre_mtx` / `del_mtx` を直接使う。
 *
 *  ★**静的版と動的版の分岐は作らない**（`#ifdef` も実行時分岐も）。
 *    無条件に動的生成である。写像先を構成で切り替える分岐は、過去に
 *    不具合を再発させた形である。
 *
 *  **ミューテックスはカーネルメモリプール（`DEF_MPK`）を使わない**
 *    （`fmp3_core/docs/dynamic-creation.md` §3.3「`acre_*`：`E_NOID` のみ
 *     （メモリプール不使用）」）。⇒ bump アロケータが単調に痩せる問題とは
 *    無関係で、`DEF_MPK` を足す必要も無い。
 *
 *  ========================================================================
 *  ハンドルの表現 — **ID を裸で返してはいけない**
 *  ========================================================================
 *  ハンドルは `&shim_mtx_state[mtxid - SHIM_MTX_TMIN_ID]`＝**実アドレス**を返す。
 *  `(void *)(intptr_t) mtxid` として ID を裸で返す案は**採らなかった**。
 *    理由は仮定ではなく実在するコードである:
 *      `esp-idf/components/bt/porting/npl/freertos/src/npl_os_freertos.c:395`
 *      `npl_freertos_mutex_deinit()` は **`vSemaphoreDelete(mutex->handle)`** を呼ぶ
 *      ＝ ミューテックスのハンドルが `esp_shim_sem_delete()` へ渡る
 *      （`esp/bt/stub/include/freertos/semphr.h:54`）。
 *    `esp_shim_sem_delete()` はハンドルを `(ID)(intptr_t)` として `shim_sem_id[]` と
 *    突き合わせる（`esp/shim/esp_shim.c`）。⇒ **ID を裸で返すと、
 *    ミューテックス削除がセマフォのスロットを解放してしまう。**
 *    実アドレスなら（従来と同じく）どのセマフォ ID とも一致せず無害な no-op になる。
 *  これは本 TU が導入し得た**新規の欠陥**であって、既存の欠陥ではない。
 *    ★ハンドル表現を変える人は、この段落を先に読むこと。
 *
 *  ========================================================================
 *  `shim_mtx_state[]` は「プール」ではない
 *  ========================================================================
 *  FMP3 のミューテックスは**再帰ロックを持たない**（`loc_mtx` は同一タスクの
 *  再取得を `E_ILUSE` で拒む）。NimBLE NPL・newlib の `_lock_*_recursive`・
 *  Wi-Fi blob の `_recursive_mutex_create` はいずれも再帰を要求するので、
 *  **所有者と再帰カウントだけはシムが持つ必要がある。**
 *
 *  そのための表が `shim_mtx_state[]` である。**旧プールとの違い**:
 *    - 空きスロットの**線形探索が無い**（添字は `acre_mtx` が返した ID で決まる）
 *    - `used[]` フラグが無い／`shim_mtx_id[]` のような **ID 対応表が無い**
 *      （＝「配列が 8 個しか初期化されず 9 個目以降がエイリアスする」型の
 *        欠陥が**構造的に作れない**）
 *    - 大きさは `TNUM_MTXID`（cfg が生成する総数）から**導出される**。
 *      シムが独自に個数を書く場所は無い（＝`ESP_SHIM_NUM_MTX` は廃止した）。
 *
 *  ========================================================================
 *  文脈の制約
 *  ========================================================================
 *  `acre_mtx`/`del_mtx` は `CHECK_TSKCTX_UNL`＝**タスク文脈かつ CPU ロック外**
 *  でしか成功しない。
 *  ⇒ **本 TU はサービスコールを CPU ロックの外で呼ぶ**（`SHIM_LOCK` を使わない）。
 *    ID は `acre_mtx` が返した時点でこの呼出しに専有されているので、
 *    `shim_mtx_state[]` の初期化に排他は要らない。
 *  ⇒ 禁止文脈から呼ばれた場合は `E_CTX` になる。**畳まず診断に残す**。
 *    呼出し側から見た戻り値は `NULL`（境界は畳まざるを得ない）。
 *
 *  ========================================================================
 *  エラー値の扱い
 *  ========================================================================
 *  `acre_mtx` は `E_NOID`（予約枯渇）/ `E_CTX`（文脈）/ `E_RSATR` / `E_PAR` を
 *  区別して返す。**値を意味へ翻訳した分岐表は作らない**（pin 更新で腐る）。
 *  **値をそのまま診断へ通す**（`ercd` を数値で出し、直近値を公開変数に残す）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "kernel_cfg.h"			/* TNUM_MTXID（cfg 生成。静的＋AID_MTX の総数） */
#include "esp_shim.h"

/*
 *  ID の下限。
 *
 *  カーネルの `TMIN_MTXID` は**内部ヘッダ**（`fmp3/fmp3_core/kernel/kernel_impl.h:190`）
 *  にあり、シム層（カーネル外）から include してはいけない。値は FMP3 の全
 *  オブジェクト種で 1 固定である（同 :185-190）。よってここで自前に定義する。
 *  写しが腐ることに対する備えは**コメントではなく実行時の範囲検査**である——
 *  `esp_shim_mutex_create()` は `acre_mtx` が返した ID が
 *  `[SHIM_MTX_TMIN_ID, SHIM_MTX_TMIN_ID + TNUM_MTXID)` に入ることを毎回確かめ、
 *  外れたら**配列外を触らずに fail-closed する**（`del_mtx` して NULL を返す）。
 *  ⇒ 仮に上流が下限を変えても、黙って壊れるのではなく診断が出る。
 */
#define SHIM_MTX_TMIN_ID	1

/*  cfg が生成する唯一の静的ミューテックス（`AID_MTX` のガード）が
 *  存在すること自体は、cfg 側（`esp_shim.cfg`）が保証する。 */
_Static_assert(SHIM_MTX_AID_GUARD >= SHIM_MTX_TMIN_ID,
			   "SHIM_MTX_AID_GUARD must be a valid mutex ID");

/*
 *  ID で引く状態表。添字は `mtxid - SHIM_MTX_TMIN_ID`。空き探索はしない。
 *  `mtxid == 0` は「このスロットに生きたミューテックスは無い」。
 *  `TNUM_MTXID` が 0 の構成で本 TU をリンクすると**配列長 0 でコンパイルが
 *    落ちる**。それは正しい（ミューテックスが 1 本も無い構成が
 *    ミューテックス実装をリンクしている＝構成の誤り）。
 */
typedef struct {
	ID			mtxid;			/* 0 なら未使用スロット             */
	ID			owner;			/* 再帰用：ロック保持タスク         */
	uint32_t	count;			/* 再帰カウント                     */
	bool_t		recursive;		/* 再帰ミューテックスとして作られた */
} SHIM_MTX;

static SHIM_MTX shim_mtx_state[TNUM_MTXID];

/*
 *  診断カウンタ（JTAG／将来のダンプから読めるように公開する）。
 *  `ercd` は**翻訳せずそのまま**残す（E_NOID=-33 / E_CTX=-25 など）。
 */
volatile uint32_t	esp_shim_mtx_acre_fail;		/* acre_mtx が失敗した回数     */
volatile int32_t	esp_shim_mtx_acre_last_ercd;/* 直近の acre_mtx のエラー値  */
volatile uint32_t	esp_shim_mtx_del_fail;		/* del_mtx が失敗した回数      */
volatile int32_t	esp_shim_mtx_del_last_ercd;	/* 直近の del_mtx のエラー値   */
volatile uint32_t	esp_shim_mtx_stale_handle;	/* 死んだハンドルで呼ばれた回数 */
volatile uint32_t	esp_shim_mtx_lock_fail;		/* loc_mtx が失敗した回数      */
volatile int32_t	esp_shim_mtx_lock_last_ercd;/* 直近の loc_mtx のエラー値   */

/*
 *  診断（沈黙させない）。氾濫を避けるため最初の 4 回とその後 1024 回ごとに出す。
 *  「出す回数を絞る」ことと「出さない」ことは違う——カウンタは常に進む
 *  （`esp_shim_isr_ctx.c` と同じ作法）。
 */
static void
shim_mtx_report(const char *what, ER ercd, volatile uint32_t *p_cnt)
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

/*
 *  ハンドル → 状態スロット。死んだ／偽のハンドルは NULL を返して数える。
 *  ★死んだハンドルを黙って受け取ると `loc_mtx(0)` が必ず失敗し、
 *    **排他が黙って消える**。
 */
static SHIM_MTX *
shim_mtx_live(void *mtx, const char *what)
{
	SHIM_MTX	*m = (SHIM_MTX *) mtx;
	size_t		off;
	uint_t		idx;

	if (m == NULL) {
		return(NULL);			/* 従来どおり静かに 0 を返す経路 */
	}
	off = (size_t)((const char *) m - (const char *) &shim_mtx_state[0]);
	idx = (uint_t)(off / sizeof(SHIM_MTX));
	if ((m < &shim_mtx_state[0]) || (idx >= (uint_t) TNUM_MTXID)
		|| (&shim_mtx_state[idx] != m) || (m->mtxid == 0)) {
		shim_mtx_report(what, E_NOEXS, &esp_shim_mtx_stale_handle);
		return(NULL);
	}
	return(m);
}

void *
esp_shim_mutex_create(bool_t recursive)
{
	T_CMTX		cmtx;
	ER_ID		ercd;
	SHIM_MTX	*m;

	/*  属性は静的生成時（`CRE_MTX(..., { TA_NULL })`）と同じにする。
	 *  `ceilpri` は `TA_CEILING` のときしか検査されない
	 *  （`fmp3/fmp3_core/docs/dynamic-creation.md` §3.3）。 */
	cmtx.mtxatr = TA_NULL;
	cmtx.ceilpri = TMIN_TPRI;

	/*  CPU ロックの外・タスク文脈で呼ぶこと（本ファイル冒頭「文脈の制約」）。 */
	ercd = acre_mtx(&cmtx);
	if (ercd <= 0) {
		esp_shim_mtx_acre_last_ercd = (int32_t) ercd;
		shim_mtx_report("acre_mtx", (ER) ercd, &esp_shim_mtx_acre_fail);
		return(NULL);
	}
	if ((ercd < SHIM_MTX_TMIN_ID) || (ercd >= (SHIM_MTX_TMIN_ID + (ID) TNUM_MTXID))) {
		/*  起こり得ないが、起きたら黙って配列外を触るより落とす方がよい。 */
		esp_shim_mtx_acre_last_ercd = (int32_t) ercd;
		shim_mtx_report("acre_mtx(range)", E_SYS, &esp_shim_mtx_acre_fail);
		(void) del_mtx((ID) ercd);
		return(NULL);
	}

	m = &shim_mtx_state[ercd - SHIM_MTX_TMIN_ID];
	m->owner = 0;
	m->count = 0U;
	m->recursive = recursive;
	m->mtxid = (ID) ercd;		/* 最後に立てる（生存フラグ兼用） */
	return((void *) m);
}

void
esp_shim_mutex_delete(void *mtx)
{
	SHIM_MTX	*m = shim_mtx_live(mtx, "mutex_delete(stale)");
	ID			mtxid;
	ER			ercd;

	if (m == NULL) {
		return;
	}
	mtxid = m->mtxid;
	m->mtxid = 0;				/* 先に落とす（以後このハンドルは死ぬ） */
	m->owner = 0;
	m->count = 0U;

	/*  `del_mtx` はロック中のミューテックスでも成功する（同 §3.3）。 */
	ercd = del_mtx(mtxid);
	if (ercd != E_OK) {
		esp_shim_mtx_del_last_ercd = (int32_t) ercd;
		shim_mtx_report("del_mtx", ercd, &esp_shim_mtx_del_fail);
	}
}

/*
 *  生きているシムミューテックスの本数（診断用）。
 *
 *  名前は `..._pool_used` のまま残した（呼び手は `esp/app/wifi_sta.c` の
 *  `-DF1_LOCK_RACE` 試験だけで、API 表面を変えないため）。**もう「プール」は
 *  無い**ので、意味は「生きているミューテックスの本数」である。
 *  migration-defect-review F-1（`_lock_t` 遅延生成のコア間レースによる二重生成）の
 *  リーク定量観測に使う。
 *
 *  サービスコールを呼ばない（`ref_mtx` はタスク文脈専用なので、
 *  診断がさらに文脈へ依存するのを避ける）。
 */
uint_t
esp_shim_mutex_pool_used(void)
{
	uint_t	i;
	uint_t	n = 0U;

	for (i = 0U; i < TNUM_MTXID; i++) {
		if (shim_mtx_state[i].mtxid != 0) {
			n++;
		}
	}
	return(n);
}

int32_t
esp_shim_mutex_lock(void *mtx)
{
	SHIM_MTX	*m = shim_mtx_live(mtx, "mutex_lock(stale)");
	ID			self = 0;
	ER			ercd;

	if (m == NULL) {
		return(0);
	}
	if (get_tid(&self) != E_OK) {
		self = 0;				/* 失敗時に未初期化値で owner 比較しない */
	}
	if (m->recursive && (m->owner == self) && (self != 0)) {
		m->count++;
		return(1);
	}
	ercd = loc_mtx(m->mtxid);
	if (ercd != E_OK) {
		/*  ここは**数えるだけ**にする（syslog を出さない）。
		 *  `loc_mtx` の失敗は禁止文脈（ISR／CPU ロック中）から呼ばれれば
		 *  毎回起きる——NimBLE の `npl_freertos_mutex_pend()` は `IRAM_ATTR` で
		 *  その形を持つ。旧実装もここは `-DF1_LOCK_RACE_DIAG` を付けない限り
		 *  黙っていた。**本段は生成／削除の失敗を可視化する段**であって、
		 *  ロック取得の失敗まで喋らせる段ではない（射程を広げない）。
		 *  沈黙ではなくカウンタで残す＝JTAG／ダンプから読める。 */
		SIL_PRE_LOC;

		SIL_LOC_INT();
		esp_shim_mtx_lock_fail++;
		esp_shim_mtx_lock_last_ercd = (int32_t) ercd;
		SIL_UNL_INT();
		return(0);
	}
	m->owner = self;
	m->count = 1U;
	return(1);
}

/*
 *  非ブロッキング版（newlib `_lock_try_acquire` 委譲用）。
 *  `loc_mtx`（タスクブロッキング可能）ではなく `ploc_mtx`（ポーリング版）を使う
 *  ——`_lock_try_acquire` は非ブロッキング契約（レビュー指摘#10）。
 */
int32_t
esp_shim_mutex_trylock(void *mtx)
{
	SHIM_MTX	*m = shim_mtx_live(mtx, "mutex_trylock(stale)");
	ID			self = 0;

	if (m == NULL) {
		return(0);
	}
	if (get_tid(&self) != E_OK) {
		self = 0;
	}
	if (m->recursive && (m->owner == self) && (self != 0)) {
		m->count++;
		return(1);
	}
	if (ploc_mtx(m->mtxid) != E_OK) {
		return(0);				/* 取れなかった＝正常な戻り。数えない */
	}
	m->owner = self;
	m->count = 1U;
	return(1);
}

int32_t
esp_shim_mutex_unlock(void *mtx)
{
	SHIM_MTX	*m = shim_mtx_live(mtx, "mutex_unlock(stale)");

	if (m == NULL) {
		return(0);
	}
	if (m->recursive && (m->count > 1U)) {
		m->count--;
		return(1);
	}
	m->owner = 0;
	m->count = 0U;
	return((unl_mtx(m->mtxid) == E_OK) ? 1 : 0);
}
