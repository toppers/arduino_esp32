/*
 *  TOPPERS/FMP3 Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  2026-07-22 訂正（外部からの指摘。`../esp32_s31/docs/reference_repo_issues.md` S3-2）:
 *  本ファイルは長らく冒頭が「TOPPERS/ASP Kernel」だったが、**実際に使われているのは FMP3**
 *  である。`esp/wifi/net/` 一式が ESP32-C3/ASP3 の祖先から移植されたとき、
 *  **コードは移植されたがコメントが祖先のまま残った**もの。
 *  同じ理由で `port/include/arch/cc.h` は「NO_SYS=1」と誤記していた（実際は 0）。
 *    そちらは動作モデルを決める設定なので、より危険な誤りだった。
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  lwIP sys_arch実装（ASP3用．NO_SYS=0）
 *
 *  sys_sem_t／sys_mbox_tはcfg（net.cfg）で静的生成したASP3セマフォ／
 *  データキューのプールから割り当てる（Wi-Fi shim＝esp/esp_shim.cの
 *  静的プール方式を踏襲．
 *  docs/tcpip-integration.md は**存在しない**（2026-07-22 実測。git 履歴にも一度も無い。外部指摘 `../esp32_s31/docs/reference_repo_issues.md` S3-1）。設計・経緯は `esp/debug/JTAG_DEBUG.md` と `docs/status.md` の Wi-Fi/lwIP 節を参照）．
 *  ただしメールボックスはWi-Fi shimのキューと異なりヒープ確保での
 *  ボックス化が不要：lwIPのsys_mbox_*は常に「ポインタ1個」しか運ば
 *  ないため，ASP3のCRE_DTQ（item＝intptr_t）にそのまま1:1で対応する．
 *
 *  sys_thread_new()はlwIPの生涯で一度だけ（tcpip_init()内部から
 *  tcpip_thread生成のために）呼ばれる前提．cfgで休止状態生成した
 *  NET_TSKを起動するだけの単発実装とする（複数スレッド生成が必要に
 *  なった場合は別途プール化が要る）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include "kernel_cfg.h"

#include "lwip/sys.h"
#include "lwip/err.h"

#include "net_cfg.h"
#include "esp_shim.h"		/* esp_shim_int_disable/restore・esp_shim_time_us */

#define NET_LOCK()		uint32_t net_lock_ = esp_shim_int_disable()
#define NET_UNLOCK()	esp_shim_int_restore(net_lock_)

/*
 *  2026-08-14 BL-H-8（LX6 の A-4 R2b 以降で NET_TSK が CPU を離さない）の
 *  切り分け計装（opt-in・既定 OFF＝golden 構成には 1 バイトも届かない）。
 *
 *  ここが要るのは、tcpip_thread の待ち合わせが
 *    sys_timeouts_sleeptime() -> (0 なら) sys_check_timeouts() -> goto again
 *    sys_timeouts_sleeptime() -> (非 0 なら) sys_arch_mbox_fetch(tmo)
 *  の 2 経路に分かれており、**空転しているのがどちらか**でしか原因が分かれ
 *  ないからである。両経路とも sys_now() を必ず通るので、
 *    sys_now 呼出し数が爆発 ＆ mbox_fetch 数は据置き -> sleeptime==0 の空転
 *    sys_now 呼出し数が爆発 ＆ mbox_fetch 数も爆発   -> 待ちが即時 return
 *    どちらも据置き                                  -> もっと深い所で停止
 *  と一義に読める。記録: .steering/20260814-lx6-r2b-hang/
 */
#ifdef M5_A4_HANGDIAG
volatile uint32_t	net_hd_now_calls;
volatile uint32_t	net_hd_now_last_ms;
volatile uint32_t	net_hd_mboxfetch_calls;
volatile uint32_t	net_hd_mboxfetch_tmo_ms;
volatile int32_t	net_hd_mboxfetch_er;
volatile uint32_t	net_hd_semwait_calls;
volatile uint32_t	net_hd_tmo_over_max;	/* TMAX_RELTIM 超過を渡した回数 */
#endif /* M5_A4_HANGDIAG */

/*
 *  ミリ秒タイムアウト→ASP3 TMO（μs．0＝永久待ち）変換
 */
static TMO
ms_to_tmo(u32_t timeout_ms)
{
	if (timeout_ms == 0U) {
		return(TMO_FEVR);
	}
#ifdef M5_A4_HANGDIAG
	if ((uint64_t) timeout_ms * 1000U > (uint64_t) TMAX_RELTIM) {
		net_hd_tmo_over_max++;
	}
#endif
	return((TMO)((uint64_t) timeout_ms * 1000U));
}

/*
 *  ------------------------------------------------------------------
 *  lwIP assert の後始末（cc.h の LWIP_PLATFORM_ASSERT から呼ばれる）
 *
 *  2026-08-14 BL-H-8（現行バグの修正）．旧実装は cc.h のマクロで
 *      esp_shim_log_write("lwip: assertion ...");  for (;;) { }
 *  だった．これには独立した 2 つの欠陥があった：
 *
 *  (1) **申告が届かない**．`esp_shim_log_write()` は `syslog()`＝logtask
 *      経由であり，LX6 の m5+wifi 構成では実測で**1 行も UART に出ない**．
 *      ⇒ ここでは `target_fput_log()`（ポーリング UART・同期）を直に叩く．
 *        `~/.claude` の教訓「syslog() は実機診断で信用しない」そのもの．
 *  (2) **タスク優先度のまま無限ループする**．発火場所は普通 tcpip_thread
 *      （NET_TSK・優先度 4）なので，**それより下の全タスク（MAIN_TASK は
 *      優先度 10）が永久に走れなくなる**．割込みは入り続けるので観測像は
 *      「カーネルは生きているのに MAIN_TASK が二度とスケジュールされない」
 *      になり，原因を示す行は 1 行も残らない．実際 BL-H-8 はこれで
 *      3 回とも「NET_TSK が CPU を離さない」としか見えていなかった．
 *      ⇒ **待って明け渡す**（`dly_tsk`）．壊れたのは lwIP の不変条件なので
 *        この文脈は戻さない（＝この関数は戻らない）が，**系の他の部分は
 *        動き続ける**——コンソールも，判定 probe も，他タスクも．
 *
 *  非タスク文脈（`sns_ctx()` が真）からは待てないので，その場合だけは
 *  回るしかない．ただし毎周回で同期出力するため UART 速度に律速され，
 *  40 万回/秒級の空転にはならない．
 *  ------------------------------------------------------------------
 */
extern void	target_fput_log(char c);

static void
lwip_port_puts(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

static void
lwip_port_put_dec(int v)
{
	char	buf[12];
	int		i = 0;
	unsigned int	u = (v < 0) ? (unsigned int)(-v) : (unsigned int) v;

	if (v < 0) {
		target_fput_log('-');
	}
	do {
		buf[i++] = (char)('0' + (u % 10U));
		u /= 10U;
	} while ((u != 0U) && (i < (int) sizeof(buf)));
	while (i > 0) {
		target_fput_log(buf[--i]);
	}
}

void
lwip_port_assert_fail(const char *msg, int line, const char *file)
{
	uint_t	round = 0U;

	for (;;) {
		/*  最初の 4 回は毎回，以後は 30 周（≒30 秒）ごとに再申告する．  */
		if ((round < 4U) || ((round % 30U) == 0U)) {
			lwip_port_puts("[LWIP-ASSERT] \"");
			lwip_port_puts((msg != NULL) ? msg : "(null)");
			lwip_port_puts("\" at ");
			lwip_port_puts((file != NULL) ? file : "(null)");
			lwip_port_puts(":");
			lwip_port_put_dec(line);
			lwip_port_puts(" — このタスクは停止する（他タスクは動く）\n");
		}
		round++;
		if (!sns_ctx()) {
			(void) dly_tsk(1000000U);	/* 1 秒．CPU を明け渡す */
		}
	}
}

/*
 *  ポート初期化（lwip_init()の先頭で一度だけ呼ばれる．cfgの静的
 *  プールはBSS初期化済みのため特にすることはない）
 */
void
sys_init(void)
{
}

/*
 *  時刻（起動からのms．NO_SYS=0でも常時必要）
 */
u32_t
sys_now(void)
{
	u32_t	ms = (u32_t) (esp_shim_time_us() / 1000);

#ifdef M5_A4_HANGDIAG
	net_hd_now_calls++;
	net_hd_now_last_ms = ms;
#endif
	return(ms);
}

/*
 *  SYS_ARCH_PROTECT/UNPROTECT（pbuf/memp等の保護．net_task
 *  （tcpip_thread）と他タスク／Wi-Fi rxコールバックとの排他に使う）
 */
sys_prot_t
sys_arch_protect(void)
{
	return((sys_prot_t) esp_shim_int_disable());
}

void
sys_arch_unprotect(sys_prot_t pval)
{
	esp_shim_int_restore((uint32_t) pval);
}

/*
 *  ------------------------------------------------------------------
 *  セマフォプール（LWIP_COMPAT_MUTEX=1のためミューテックスもこれを
 *  介する．sys_mutex_new(mutex)→sys_sem_new(mutex,1)等，lwip/sys.hが
 *  マクロで読み替える）
 *  ------------------------------------------------------------------
 */
static const ID net_sem_id[SYS_ARCH_NUM_SEM] = {
	NET_SEM1, NET_SEM2, NET_SEM3, NET_SEM4,
	NET_SEM5, NET_SEM6, NET_SEM7, NET_SEM8,
};
static bool_t net_sem_used[SYS_ARCH_NUM_SEM];

err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
	uint_t	i;
	ID		semid = 0;

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_SEM; i++) {
		if (!net_sem_used[i]) {
			net_sem_used[i] = true;
			semid = net_sem_id[i];
			break;
		}
	}
	NET_UNLOCK();

	if (semid == 0) {
		syslog(LOG_ERROR, "net: sem pool exhausted");
		*sem = SYS_SEM_NULL;
		return(ERR_MEM);
	}

	/*  再利用時のカウントクリア（cfg初期値も0）  */
	while (pol_sem(semid) == E_OK) {
		;
	}
	while (count-- > 0U) {
		(void) sig_sem(semid);
	}
	*sem = (sys_sem_t) semid;
	return(ERR_OK);
}

void
sys_sem_free(sys_sem_t *sem)
{
	ID		semid = (ID) *sem;
	uint_t	i;

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_SEM; i++) {
		if (net_sem_id[i] == semid) {
			net_sem_used[i] = false;
			break;
		}
	}
	NET_UNLOCK();
	*sem = SYS_SEM_NULL;
}

void
sys_sem_signal(sys_sem_t *sem)
{
	(void) sig_sem((ID) *sem);
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
	ER	er;

	er = twai_sem((ID) *sem, ms_to_tmo(timeout_ms));
#ifdef M5_A4_HANGDIAG
	net_hd_semwait_calls++;
#endif
	return((er == E_OK) ? 1U : SYS_ARCH_TIMEOUT);
}

int
sys_sem_valid(sys_sem_t *sem)
{
	return(*sem != SYS_SEM_NULL);
}

void
sys_sem_set_invalid(sys_sem_t *sem)
{
	*sem = SYS_SEM_NULL;
}

/*
 *  ------------------------------------------------------------------
 *  メールボックスプール（ASP3 CRE_DTQに1:1対応．メッセージはポインタ
 *  1個＝ボックス化不要）
 *  ------------------------------------------------------------------
 */
static const ID net_mbox_id[SYS_ARCH_NUM_MBOX] = {
	NET_MBOX1, NET_MBOX2, NET_MBOX3, NET_MBOX4,  NET_MBOX5,
	NET_MBOX6, NET_MBOX7, NET_MBOX8, NET_MBOX9,  NET_MBOX10,
};
static bool_t net_mbox_used[SYS_ARCH_NUM_MBOX];

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
	uint_t	i;
	ID		dtqid = 0;

	(void) size;	/* 深さは固定（SYS_ARCH_MBOX_DEPTH．net_cfg.h参照） */

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_MBOX; i++) {
		if (!net_mbox_used[i]) {
			net_mbox_used[i] = true;
			dtqid = net_mbox_id[i];
			break;
		}
	}
	NET_UNLOCK();

	if (dtqid == 0) {
		syslog(LOG_ERROR, "net: mbox pool exhausted");
		*mbox = SYS_MBOX_NULL;
		return(ERR_MEM);
	}
	(void) ini_dtq(dtqid);		/* 再利用時の残留メッセージクリア */
	*mbox = (sys_mbox_t) dtqid;
	return(ERR_OK);
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
	ID		dtqid = (ID) *mbox;
	uint_t	i;

	(void) ini_dtq(dtqid);

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_MBOX; i++) {
		if (net_mbox_id[i] == dtqid) {
			net_mbox_used[i] = false;
			break;
		}
	}
	NET_UNLOCK();
	*mbox = SYS_MBOX_NULL;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
	(void) tsnd_dtq((ID) *mbox, (intptr_t) msg, TMO_FEVR);
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	return((psnd_dtq((ID) *mbox, (intptr_t) msg) == E_OK) ? ERR_OK : ERR_MEM);
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	return(sys_mbox_trypost(mbox, msg));
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
	intptr_t	data;
	ER			er;

	er = trcv_dtq((ID) *mbox, &data, ms_to_tmo(timeout_ms));
#ifdef M5_A4_HANGDIAG
	net_hd_mboxfetch_calls++;
	net_hd_mboxfetch_tmo_ms = (uint32_t) timeout_ms;
	net_hd_mboxfetch_er = (int32_t) er;
#endif
	if (er != E_OK) {
		return(SYS_ARCH_TIMEOUT);
	}
	if (msg != NULL) {
		*msg = (void *) data;
	}
	return(1U);
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	intptr_t	data;
	ER			er;

	er = prcv_dtq((ID) *mbox, &data);
	if (er != E_OK) {
		return(SYS_MBOX_EMPTY);
	}
	if (msg != NULL) {
		*msg = (void *) data;
	}
	return(0U);
}

int
sys_mbox_valid(sys_mbox_t *mbox)
{
	return(*mbox != SYS_MBOX_NULL);
}

void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
	*mbox = SYS_MBOX_NULL;
}

/*
 *  ------------------------------------------------------------------
 *  スレッド（単発実装．lwIPはtcpip_init()の中でtcpip_thread生成の
 *  ために一度だけsys_thread_new()を呼ぶ．cfgで休止生成したNET_TSKを
 *  起動するだけで足りる．複数回呼ばれた場合は静的アサート的に
 *  ログを出して黙って上書きする（"MUST NOT FAIL"契約のため戻り値で
 *  失敗を伝える手段が無い）
 *  ------------------------------------------------------------------
 */
static lwip_thread_fn	net_thread_fn;
static void				*net_thread_arg;
static bool_t			net_thread_started;

void
net_task_entry(EXINF exinf)
{
	(void) exinf;
	net_thread_fn(net_thread_arg);		/* 通常は戻らない（tcpip_thread） */
}

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
			   int stacksize, int prio)
{
	(void) name; (void) stacksize; (void) prio;

	if (net_thread_started) {
		syslog(LOG_ERROR,
			   "net: sys_thread_new() called more than once (unsupported)");
	}
	net_thread_fn = thread;
	net_thread_arg = arg;
	net_thread_started = true;
	(void) act_tsk(NET_TSK);
	return((sys_thread_t) NET_TSK);
}
