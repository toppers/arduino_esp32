/*
 *  ESP32-P4 Wi-Fi モジュール対応 - lwIP sys_arch FMP3 実装
 *  静的プール実装（詳細設計は fmp3_lwip_pools.h 冒頭コメント参照）
 */
#include "fmp3_lwip_pools.h"

/*
 *  =====================================================================
 *  cfg 生成 ID のフォールバック前方宣言
 *  =====================================================================
 *  【重要・2026-07-10 修正（実機で確定した致命バグ）】本ファイルは実ビルドでも
 *  kernel_cfg.h / kernel_id.h を #include しない（os_adapter の
 *  fmp3_hosted_pools.c と同じ設計。実機で確認済み）。したがって configure.rb が
 *  生成する本物の ID 定数は本 TU には見えず、**常にこのフォールバック値が
 *  使われる**。ゆえにフォールバック値は kernel_cfg.h の実 ID と一致していなければ
 *  ならない。旧値（101/121/131/141 系）は全て範囲外で、act_tsk(141) 等が E_ID で
 *  黙って失敗し、tcpip_thread が起動せず DHCP が回らなかった（fmp_wifi_connect_probe
 *  で [TTSP_RESULT: FAIL(no ip)]、netif ログ皆無として顕在化。HOSTED_TMR_TSK の
 *  同種バグと同一クラス）。
 *
 *  下記の値は fmp_wifi_connect_probe のビルド（syslog/serial/logtask +
 *  fmp3_hosted_osi.cfg + fmp3_lwip_pools.cfg）での kernel_cfg.h 実 ID に一致させて
 *  ある。FMP3 は ID をオブジェクト種別ごと（task/mutex/semaphore/dataqueue）に
 *  独立採番するため、TSK1=11 と MTX1=12、SEM6=26 と MBOX1=26 のように種別を
 *  跨いだ数値の重なりは正常（各サービスコールは自種別のテーブルを引く）。
 *
 *  【重要】プールサイズ（FMP3_LWIP_POOL_NUM_*）や、これを INCLUDE するアプリ cfg の
 *  構成（先行して生成されるオブジェクト数）を変更すると実 ID がずれるため、
 *  その都度 kernel_cfg.h を再確認して本一覧を追従させること（動的追随しない）。
 */
#ifndef FMP3_LWIP_SEM1
#define FMP3_LWIP_SEM1		21
#define FMP3_LWIP_SEM2		22
#define FMP3_LWIP_SEM3		23
#define FMP3_LWIP_SEM4		24
#define FMP3_LWIP_SEM5		25
#define FMP3_LWIP_SEM6		26
#define FMP3_LWIP_SEM7		27
#define FMP3_LWIP_SEM8		28
#define FMP3_LWIP_SEM9		29
#define FMP3_LWIP_SEM10		30
#define FMP3_LWIP_SEM11		31
#define FMP3_LWIP_SEM12		32
#define FMP3_LWIP_SEM13		33
#define FMP3_LWIP_SEM14		34
#define FMP3_LWIP_SEM15		35
#define FMP3_LWIP_SEM16		36
#endif

#ifndef FMP3_LWIP_MTX1
#define FMP3_LWIP_MTX1		12
#define FMP3_LWIP_MTX2		13
#define FMP3_LWIP_MTX3		14
#define FMP3_LWIP_MTX4		15
#define FMP3_LWIP_MTX5		16
#define FMP3_LWIP_MTX6		17
#define FMP3_LWIP_MTX7		18
#define FMP3_LWIP_MTX8		19
#endif

#ifndef FMP3_LWIP_POOL_META_MTX
#define FMP3_LWIP_POOL_META_MTX	20
#endif

#ifndef FMP3_LWIP_MBOX1
#define FMP3_LWIP_MBOX1		26
#define FMP3_LWIP_MBOX2		27
#define FMP3_LWIP_MBOX3		28
#define FMP3_LWIP_MBOX4		29
#define FMP3_LWIP_MBOX5		30
#define FMP3_LWIP_MBOX6		31
#define FMP3_LWIP_MBOX7		32
#define FMP3_LWIP_MBOX8		33
#define FMP3_LWIP_MBOX9		34
#define FMP3_LWIP_MBOX10	35
#define FMP3_LWIP_MBOX11	36
#define FMP3_LWIP_MBOX12	37
#endif

#ifndef FMP3_LWIP_TSK1
#define FMP3_LWIP_TSK1		11
#define FMP3_LWIP_TSK2		12
#define FMP3_LWIP_TSK3		13
#define FMP3_LWIP_TSK4		14
#define FMP3_LWIP_TSK5		15
#define FMP3_LWIP_TSK6		16
#define FMP3_LWIP_TSK7		17
#define FMP3_LWIP_TSK8		18
#endif

/*
 *  =====================================================================
 *  タイムアウト変換（lwIP ミリ秒 → FMP3 TMO マイクロ秒）
 *  =====================================================================
 *  【重要・os_adapter との違い】lwIP 契約では sys_arch_sem_wait/mbox_fetch の
 *  timeout=0 は「無限待ち」を意味する（os_adapter の hosted_pool_map_timeout_ms
 *  とは 0 の解釈が逆なので注意。fmp3_lwip_pools.h 冒頭コメントにも明記）。
 *    timeout_ms == 0 : 無限待ち → TMO_FEVR
 *    timeout_ms >  0 : ミリ秒 → マイクロ秒(×1000)
 *  TMAX_RELTIM(us) を超えるミリ秒指定はクランプする（呼び出し側がミリ秒換算で
 *  桁あふれさせないための安全策。TMAX_RELTIM=4000000000U ≒ 66分40秒なので
 *  実用上ほぼ問題にならない）。
 */
static TMO
fmp3_lwip_ms_to_tmo(uint32_t timeout_ms)
{
	uint32_t max_ms = TMAX_RELTIM / 1000U;

	if (timeout_ms == 0U) {
		return TMO_FEVR;
	}
	if (timeout_ms > max_ms) {
		timeout_ms = max_ms;
	}
	return (TMO) (timeout_ms * 1000U);
}

static int
fmp3_lwip_map_ercd(ER ercd)
{
	if (ercd == E_OK) {
		return FMP3_LWIP_POOL_OK;
	} else if (ercd == E_TMOUT) {
		return FMP3_LWIP_POOL_TIMEOUT;
	}
	return FMP3_LWIP_POOL_ERROR;
}

/*
 *  =====================================================================
 *  セマフォプール
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		semid;
} fmp3_lwip_sem_slot_t;

static fmp3_lwip_sem_slot_t g_fmp3_lwip_sem_pool[FMP3_LWIP_POOL_NUM_SEM] = {
	{ false, FMP3_LWIP_SEM1 },  { false, FMP3_LWIP_SEM2 },  { false, FMP3_LWIP_SEM3 },
	{ false, FMP3_LWIP_SEM4 },  { false, FMP3_LWIP_SEM5 },  { false, FMP3_LWIP_SEM6 },
	{ false, FMP3_LWIP_SEM7 },  { false, FMP3_LWIP_SEM8 },  { false, FMP3_LWIP_SEM9 },
	{ false, FMP3_LWIP_SEM10 }, { false, FMP3_LWIP_SEM11 }, { false, FMP3_LWIP_SEM12 },
	{ false, FMP3_LWIP_SEM13 }, { false, FMP3_LWIP_SEM14 }, { false, FMP3_LWIP_SEM15 },
	{ false, FMP3_LWIP_SEM16 },
};

void *
fmp3_lwip_pool_sem_new(uint8_t count)
{
	int i;
	fmp3_lwip_sem_slot_t *slot = NULL;

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	for (i = 0; i < FMP3_LWIP_POOL_NUM_SEM; i++) {
		if (!g_fmp3_lwip_sem_pool[i].used) {
			g_fmp3_lwip_sem_pool[i].used = true;
			slot = &g_fmp3_lwip_sem_pool[i];
			break;
		}
	}
	unl_mtx(FMP3_LWIP_POOL_META_MTX);

	if (slot == NULL) {
		/* プール枯渇。FMP3_LWIP_POOL_NUM_SEM を調整すること(TODO)。 */
		return NULL;
	}

	/*
	 *  静的 CRE_SEM は初期資源数0で生成しているため、要求された count 分だけ
	 *  sig_sem してから返す。CRE_SEM の maxsem(=FMP3_LWIP_SEM_STATIC_MAXCNT=255)
	 *  は count(u8_t, 最大255)の理論上限に一致させてあるのでクランプ不要。
	 */
	for (i = 0; i < (int) count; i++) {
		(void) sig_sem(slot->semid);
	}

	return (void *) slot;
}

void
fmp3_lwip_pool_sem_free(void *sem_handle)
{
	fmp3_lwip_sem_slot_t *slot = (fmp3_lwip_sem_slot_t *) sem_handle;

	if (slot == NULL) {
		return;
	}

	/* 次回利用者に前回の残数を持ち越さないよう、0まで空読みする。 */
	while (pol_sem(slot->semid) == E_OK) {
		/* drain */
	}

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	slot->used = false;
	unl_mtx(FMP3_LWIP_POOL_META_MTX);
}

void
fmp3_lwip_pool_sem_signal(void *sem_handle)
{
	fmp3_lwip_sem_slot_t *slot = (fmp3_lwip_sem_slot_t *) sem_handle;

	if (slot == NULL) {
		return;
	}

	/*
	 *  ISR 文脈からも呼ばれる(sys_sem_signal 自体は lwIP 契約上 ISR 制限は
	 *  無いが、ドライバの受信ISRがsys_sem_signal相当の合図に使うケースを
	 *  想定)。kernel.h は `#define isig_sem(semid) sig_sem(semid)` であり、
	 *  タスク文脈/非タスク文脈で実装が分かれる専用版は無い(os_adapterで確認済み)。
	 */
	(void) sig_sem(slot->semid);
}

int
fmp3_lwip_pool_sem_wait(void *sem_handle, uint32_t timeout_ms)
{
	fmp3_lwip_sem_slot_t *slot = (fmp3_lwip_sem_slot_t *) sem_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	ercd = twai_sem(slot->semid, fmp3_lwip_ms_to_tmo(timeout_ms));
	return fmp3_lwip_map_ercd(ercd);
}

/*
 *  =====================================================================
 *  ミューテックスプール
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		mtxid;
} fmp3_lwip_mutex_slot_t;

static fmp3_lwip_mutex_slot_t g_fmp3_lwip_mutex_pool[FMP3_LWIP_POOL_NUM_MUTEX] = {
	{ false, FMP3_LWIP_MTX1 }, { false, FMP3_LWIP_MTX2 }, { false, FMP3_LWIP_MTX3 },
	{ false, FMP3_LWIP_MTX4 }, { false, FMP3_LWIP_MTX5 }, { false, FMP3_LWIP_MTX6 },
	{ false, FMP3_LWIP_MTX7 }, { false, FMP3_LWIP_MTX8 },
};

void *
fmp3_lwip_pool_mutex_new(void)
{
	int i;
	fmp3_lwip_mutex_slot_t *slot = NULL;

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	for (i = 0; i < FMP3_LWIP_POOL_NUM_MUTEX; i++) {
		if (!g_fmp3_lwip_mutex_pool[i].used) {
			g_fmp3_lwip_mutex_pool[i].used = true;
			slot = &g_fmp3_lwip_mutex_pool[i];
			break;
		}
	}
	unl_mtx(FMP3_LWIP_POOL_META_MTX);

	return (void *) slot;
}

void
fmp3_lwip_pool_mutex_free(void *mutex_handle)
{
	fmp3_lwip_mutex_slot_t *slot = (fmp3_lwip_mutex_slot_t *) mutex_handle;

	if (slot == NULL) {
		return;
	}

	/* 呼び出し元がロック保持中に destroy するのは規約違反だが、防御的に
	 * unl_mtx を試みる（既にアンロック済みなら失敗するだけで無害）。 */
	(void) unl_mtx(slot->mtxid);

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	slot->used = false;
	unl_mtx(FMP3_LWIP_POOL_META_MTX);
}

int
fmp3_lwip_pool_mutex_lock(void *mutex_handle)
{
	fmp3_lwip_mutex_slot_t *slot = (fmp3_lwip_mutex_slot_t *) mutex_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	/* lwIP の sys_mutex_lock() は無限待ち契約（timeout引数自体を持たない）
	 * なので tloc_mtx ではなく loc_mtx を使う。 */
	ercd = loc_mtx(slot->mtxid);
	return fmp3_lwip_map_ercd(ercd);
}

int
fmp3_lwip_pool_mutex_unlock(void *mutex_handle)
{
	fmp3_lwip_mutex_slot_t *slot = (fmp3_lwip_mutex_slot_t *) mutex_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	ercd = unl_mtx(slot->mtxid);
	return fmp3_lwip_map_ercd(ercd);
}

/*
 *  =====================================================================
 *  メールボックスプール（dtq 1本 = mbox 1個）
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		dtqid;
} fmp3_lwip_mbox_slot_t;

/*  【診断 2026-07-10】sys_mbox_trypost（=psnd_dtq）の呼出し総数/失敗数/最終エラー。
 *  TCP 受信不達（recv_tcp の trypost 失敗説）の実測用。getter は下部。 */
static volatile uint32_t s_mbox_trypost_total;
static volatile uint32_t s_mbox_trypost_fail;
static volatile ER       s_mbox_trypost_last_ercd;

static fmp3_lwip_mbox_slot_t g_fmp3_lwip_mbox_pool[FMP3_LWIP_POOL_NUM_MBOX] = {
	{ false, FMP3_LWIP_MBOX1 }, { false, FMP3_LWIP_MBOX2 }, { false, FMP3_LWIP_MBOX3 },
	{ false, FMP3_LWIP_MBOX4 }, { false, FMP3_LWIP_MBOX5 }, { false, FMP3_LWIP_MBOX6 },
	{ false, FMP3_LWIP_MBOX7 }, { false, FMP3_LWIP_MBOX8 }, { false, FMP3_LWIP_MBOX9 },
	{ false, FMP3_LWIP_MBOX10 }, { false, FMP3_LWIP_MBOX11 }, { false, FMP3_LWIP_MBOX12 },
};

void *
fmp3_lwip_pool_mbox_new(int size_hint)
{
	int i;
	fmp3_lwip_mbox_slot_t *slot = NULL;
	intptr_t dummy;

	/*
	 *  全 mbox プールスロットは cfg で固定容量 FMP3_LWIP_MBOX_MAX_ELEMS の
	 *  dtq として生成済み。size_hint がこれを超えていても静的にしか確保
	 *  できないため、単にクランプする（TODO: 統合時に実際の要求サイズ
	 *  (TCPIP_MBOX_SIZE 等)を確認し、必要なら FMP3_LWIP_MBOX_MAX_ELEMS を
	 *  引き上げること）。
	 */
	(void) size_hint;

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	for (i = 0; i < FMP3_LWIP_POOL_NUM_MBOX; i++) {
		if (!g_fmp3_lwip_mbox_pool[i].used) {
			g_fmp3_lwip_mbox_pool[i].used = true;
			slot = &g_fmp3_lwip_mbox_pool[i];
			break;
		}
	}
	unl_mtx(FMP3_LWIP_POOL_META_MTX);

	if (slot == NULL) {
		return NULL;
	}

	/* 生成直後は空のはず（destroy時にdrain済み）だが、防御的に空読みしておく。 */
	while (trcv_dtq(slot->dtqid, &dummy, TMO_POL) == E_OK) {
		/* drain */
	}

	return (void *) slot;
}

void
fmp3_lwip_pool_mbox_free(void *mbox_handle)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	intptr_t dummy;

	if (slot == NULL) {
		return;
	}

	/*
	 *  lwIP 契約上、destroy 時にメッセージが残っているのはプログラミング
	 *  エラーとされる（本来は上位が空にしてから呼ぶ）。本実装は防御的に
	 *  残留メッセージを drain して捨てる（TODO: syslog へ警告を出す）。
	 */
	while (trcv_dtq(slot->dtqid, &dummy, TMO_POL) == E_OK) {
		/* drain */
	}

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	slot->used = false;
	unl_mtx(FMP3_LWIP_POOL_META_MTX);
}

int
fmp3_lwip_pool_mbox_post(void *mbox_handle, void *msg)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	/* lwIP契約: 「may not fail」＝満杯なら空くまで無限待ち。ISRから呼んではならない。 */
	ercd = tsnd_dtq(slot->dtqid, (intptr_t) msg, TMO_FEVR);
	return fmp3_lwip_map_ercd(ercd);
}

int
fmp3_lwip_pool_mbox_trypost(void *mbox_handle, void *msg)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	/*
	 *  【2026-07-10 修正】psnd_dtq（非ブロッキング送信。ipsnd_dtq はこの
	 *  単純な別名）を使う。psnd_dtq はタスク文脈・非タスク文脈の両方から
	 *  呼べるため、lwIP コアが tcpip_input() 等を経由して非タスク文脈から
	 *  sys_mbox_trypost() を呼ぶ構成でも安全になる。旧実装の
	 *  tsnd_dtq(TMO_POL) はタスク文脈専用で、非タスク文脈では E_CTX →
	 *  ERR_MEM となり、lwIP コアはこれを「mbox 満杯」と区別できず受信
	 *  パケットを黙って捨て続ける（レビュー指摘）。
	 */
	ercd = psnd_dtq(slot->dtqid, (intptr_t) msg);
	/*  【診断 2026-07-10】TCP 受信不達の切り分け: trypost 失敗（満杯/文脈エラー等）を数える。
	 *  recv_tcp が失敗すると lwIP は ERR_MEM 扱いでセグメントを ACK せず、
	 *  アプリの recv に永遠に届かない。ここが 0 でなければ本経路が原因。 */
	s_mbox_trypost_total++;
	if (ercd != E_OK) {
		s_mbox_trypost_fail++;
		s_mbox_trypost_last_ercd = ercd;
	}
	return fmp3_lwip_map_ercd(ercd);
}

int
fmp3_lwip_pool_mbox_trypost_fromisr(void *mbox_handle, void *msg)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	ER ercd;

	if (slot == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	/*
	 *  ISR文脈から呼んでよい唯一の送信手段。kernel.h の
	 *  `#define ipsnd_dtq(dtqid, data) psnd_dtq(dtqid, data)` を使う
	 *  （非ブロッキング送信。満杯ならE_TMOUTではなくpsnd_dtq相当のエラーに
	 *  なる点はtsnd_dtq(TMO_POL)と同じ扱いでよい）。
	 */
	ercd = ipsnd_dtq(slot->dtqid, (intptr_t) msg);
	return fmp3_lwip_map_ercd(ercd);
}

int
fmp3_lwip_pool_mbox_fetch(void *mbox_handle, void **p_msg, uint32_t timeout_ms)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	intptr_t data = 0;
	ER ercd;

	if (slot == NULL || p_msg == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	ercd = trcv_dtq(slot->dtqid, &data, fmp3_lwip_ms_to_tmo(timeout_ms));
	if (ercd == E_OK) {
		*p_msg = (void *) data;
	}
	return fmp3_lwip_map_ercd(ercd);
}

int
fmp3_lwip_pool_mbox_tryfetch(void *mbox_handle, void **p_msg)
{
	fmp3_lwip_mbox_slot_t *slot = (fmp3_lwip_mbox_slot_t *) mbox_handle;
	intptr_t data = 0;
	ER ercd;

	if (slot == NULL || p_msg == NULL) {
		return FMP3_LWIP_POOL_INVALID;
	}

	/* タスク文脈専用（trypost同様、trcv_dtqにi系別名が無いため）。 */
	ercd = trcv_dtq(slot->dtqid, &data, TMO_POL);
	if (ercd == E_OK) {
		*p_msg = (void *) data;
	}
	return fmp3_lwip_map_ercd(ercd);
}

/*
 *  【診断 2026-07-10】sys_mbox_trypost（=psnd_dtq）の呼出し総数/失敗数/最終 ER を
 *  返す。TCP 受信不達（recv_tcp の trypost 失敗＝refused_data 退避）の原因段特定用。
 *  last_ercd の値で一意に決まる: E_TMOUT=満杯 / E_ID・E_PAR=ハンドル不正 /
 *  E_CTX=非タスク文脈。
 */
void
fmp3_lwip_pool_mbox_get_trypost_stats(uint32_t *total, uint32_t *fail, int *last_ercd)
{
	if (total != NULL)     { *total = s_mbox_trypost_total; }
	if (fail != NULL)      { *fail = s_mbox_trypost_fail; }
	if (last_ercd != NULL) { *last_ercd = (int) s_mbox_trypost_last_ercd; }
}

/*
 *  =====================================================================
 *  タスクプール
 *  =====================================================================
 */
typedef struct {
	bool_t	used;
	ID		tskid;
	void	(*thread_fn)(void *arg);
	void	*arg;
	PRI		pri;
	char	name[FMP3_LWIP_THREAD_NAME_MAX];
} fmp3_lwip_task_slot_t;

static fmp3_lwip_task_slot_t g_fmp3_lwip_task_pool[FMP3_LWIP_POOL_NUM_THREAD] = {
	{ false, FMP3_LWIP_TSK1, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK2, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK3, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK4, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK5, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK6, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK7, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
	{ false, FMP3_LWIP_TSK8, NULL, NULL, FMP3_LWIP_THREAD_PRI_DEFAULT, { 0 } },
};

static void
fmp3_lwip_copy_name(char *dst, const char *src)
{
	size_t i;

	for (i = 0; i + 1 < FMP3_LWIP_THREAD_NAME_MAX && src != NULL && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
	dst[i] = '\0';
}

static PRI
fmp3_lwip_clamp_pri(int prio)
{
	if (prio < (int) TMIN_TPRI || prio > (int) TMAX_TPRI) {
		/* 範囲外は既定優先度にフォールバックする(TODO: 統合時にlwipopts.h側の
		 * 実際の優先度定義を確認し、想定外の値が来ていないか確認すること)。 */
		return FMP3_LWIP_THREAD_PRI_DEFAULT;
	}
	return (PRI) prio;
}

void
fmp3_lwip_pool_task_trampoline(EXINF exinf)
{
	uint_t idx = (uint_t) exinf;
	fmp3_lwip_task_slot_t *slot = &g_fmp3_lwip_task_pool[idx];
	void (*fn)(void *arg);
	void *arg;

	/*
	 *  act_tsk 直後に生成側で chg_pri すると起動とchg_priの間に競合窓が
	 *  生じるため、タスク自身の先頭(ここ)で優先度を書き換える
	 *  （os_adapter の hosted_pool_task_trampoline と同じ設計）。
	 */
	(void) chg_pri(TSK_SELF, slot->pri);

	fn = slot->thread_fn;
	arg = slot->arg;

	if (fn != NULL) {
		fn(arg);
	}

	/*
	 *  lwIP のスレッド（tcpip_thread 等）は通常無限ループで戻らないが、
	 *  万一戻ってきた場合はプールへ返却してダーマント状態に戻る。
	 */
	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	slot->used = false;
	unl_mtx(FMP3_LWIP_POOL_META_MTX);

	(void) ext_tsk();
	/* ext_tsk は戻らない */
}

void *
fmp3_lwip_pool_thread_new(const char *name, void (*thread_fn)(void *arg),
		void *arg, int tstack_size, int prio)
{
	int i;
	fmp3_lwip_task_slot_t *slot = NULL;

	if (thread_fn == NULL) {
		return NULL;
	}

	/* 静的スタック(FMP3_LWIP_THREAD_STACK_SIZE)に対して無視する。
	 * TODO(統合時): 実際の要求スタックサイズを確認し調整すること。 */
	(void) tstack_size;

	loc_mtx(FMP3_LWIP_POOL_META_MTX);
	for (i = 0; i < FMP3_LWIP_POOL_NUM_THREAD; i++) {
		if (!g_fmp3_lwip_task_pool[i].used) {
			g_fmp3_lwip_task_pool[i].used = true;
			slot = &g_fmp3_lwip_task_pool[i];
			slot->thread_fn = thread_fn;
			slot->arg = arg;
			slot->pri = fmp3_lwip_clamp_pri(prio);
			fmp3_lwip_copy_name(slot->name, name);
			break;
		}
	}
	unl_mtx(FMP3_LWIP_POOL_META_MTX);

	if (slot == NULL) {
		/* プール枯渇。FMP3_LWIP_POOL_NUM_THREAD を調整すること(TODO)。
		 * lwIP契約上 sys_thread_new は失敗してはならないため、これは
		 * 設定不足を示す致命的状況（呼び出し元 sys_arch.c でログを出す）。 */
		return NULL;
	}

	(void) act_tsk(slot->tskid);

	return (void *) slot;
}

/*
 *  =====================================================================
 *  初期化
 *  =====================================================================
 */
void
fmp3_lwip_pools_init(void)
{
	/*
	 *  各静的プール要素は cfg 側で CRE_* により生成済み・"used" は初期値
	 *  false（配列初期化子どおり）なので、通常は何もする必要が無い。
	 *  将来リセット等に備えて明示的な再初期化フックとして残す。
	 */
}
