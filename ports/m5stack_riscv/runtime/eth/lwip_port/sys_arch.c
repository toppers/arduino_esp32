/*
 *  ESP32-P4 Wi-Fi モジュール対応 - lwIP sys_arch FMP3 実装
 *  sys_arch.c 本体
 *
 *  lwIP の OS 抽象層 `sys_arch`（NO_SYS=0, OS 有り構成）を FMP3 プリミティブで
 *  実装する。実体（静的プールからの確保/返却）は `fmp3_lwip_pools.c` に委譲し、
 *  本ファイルは lwIP 契約（型変換・timeout単位変換・エラーコード変換）に
 *  専念する。
 *
 *  【重要・独立性】本ファイルは `wifi_p4_module/os_adapter/`（esp-hosted 用
 *  hosted_osi）の一切を include/リンクしない。lwIP は esp-hosted に依存させ
 *  ない独立モジュールとして実装している（S3 等、esp-hosted を使わない構成
 *  でも host 側 TCP/IP として共有できるようにするため）。
 *
 *  参照:
 *    docs/esp32p4_wifi_module_plan.md §6（lwIP sys_arch の設計方針）
 *    wifi_p4_module/os_adapter/       （様式・FMP3事実を踏襲した姉妹実装）
 *    wifi_p4_module/lwip_port/mapping.md （本ファイルの関数対応表・設計判断）
 *
 *  【統合時の差し替え注意（2026-07-10 方式(D)統合で完了済み。以下は歴史的経緯）】
 *  当初（フェーズ0、lwIP 本体ソース未取得の段階）は、下記の #include を
 *  本物の lwIP ヘッダへ後日差し替える前提で、`_lwip_min_shim.h` が
 *      #include "lwip/opt.h"
 *      #include "lwip/sys.h"
 *      #include "lwip/mem.h"      (必要なら)
 *      #include "lwip/stats.h"    (必要なら)
 *  相当の型・プロトタイプを最小限で前方定義していた。
 *
 *  【2026-07-10 方式(D)統合】上記の差し替えを完了し、下記の #include は
 *  実 lwIP ヘッダ本体を直接指すようになった（`_lwip_min_shim.h` はフェーズ0の
 *  -fsyntax-only 治具であり実ビルドには関与しない）。lwip/opt.h が lwipopts.h
 *  と arch/cc.h を、lwip/sys.h が arch/sys_arch.h（sys_sem_t 等の型）と
 *  sys_* プロトタイプ・SYS_ARCH_TIMEOUT・lwip_thread_fn を、lwip/err.h が
 *  err_t/ERR_* を供給する。`_lwip_min_shim.h` はどこからも参照されない
 *  死んだファイルとなったため 2026-07-16 に削除済み。
 */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "fmp3_lwip_pools.h"

#include <kernel.h>
#include <sil.h>		/* SIL_LOC_INT/SIL_UNL_INT（diag リングのスロット払い出し保護） */
#include <t_syslog.h>	/* sys_thread_new のプール枯渇検知用（下記コメント参照） */
#include <stdarg.h>
#include <stdio.h>		/* vsnprintf（LWIP_PLATFORM_DIAG の整形用） */

/*
 *  =====================================================================
 *  プラットフォーム診断・アサート（arch/cc.h が extern 宣言している実体）
 *  =====================================================================
 *  lwIP コア（LWIP_PLATFORM_DIAG/LWIP_PLATFORM_ASSERT 経由）および本ポートが
 *  参照する。cc.h は独立モジュール方針で「宣言のみ・実体は統合時にアプリ側」と
 *  していたため、ここ（FMP3 側でコンパイルされる sys_arch.c）で FMP3 syslog へ
 *  橋渡しする実体を定義する。
 *
 *  syslog は遅延整形（書式文字列ポインタを保存し logtask が後で整形）のため、
 *  スタック上の可変引数を直接渡すと化ける（os_adapter の hosted_osi_printf で
 *  確認済みの罠）。よって一旦静的バッファへ完全整形してから syslog("%s") で出す。
 */
/*
 *  【2026-07-16 修正（レビュー指摘 M-1）】旧実装は `static char buf[160]` の
 *  【単一】バッファを全呼出しで共有していた。syslog は遅延整形（logtask が
 *  約10ms周期で後から %s を解決）のため、logtask が整形する前に別の呼出しが
 *  同じ buf を上書きすると、表示が別呼出しの内容に化けるか、書き換え途中の
 *  状態が出る（スタックバッファの罠を避けたつもりが、共有バッファのレースに
 *  すり替わっていた）。os_adapter の hosted_osi_printf が採用済みの
 *  「静的リングバッファからスロットを払い出す」方式に揃えて解消する。
 *
 *  現状 lwipopts.h は LWIP_DEBUG=0 のため LWIP_DEBUGF はコンパイル時に消え、
 *  本関数へはほぼ到達しない。しかし LWIP_DEBUG=1 にしてデバッグする際に
 *  真っ先に踏む罠なので、その時に困らないよう直しておく。
 */
#define LWIP_DIAG_RING_SLOTS	4U
#define LWIP_DIAG_RING_WIDTH	160U

static char	lwip_diag_ring[LWIP_DIAG_RING_SLOTS][LWIP_DIAG_RING_WIDTH];
static uint_t	lwip_diag_ring_next;

void
fmp3_lwip_platform_diag(const char *fmt, ...)
{
	va_list	ap;
	uint_t	slot;
	char	*buf;
	SIL_PRE_LOC;		/* SIL_LOC_INT/UNL_INT が使う退避変数を宣言（core_sil.h） */

	/*  スロット払い出しのみ自コアの割込み禁止で保護する（hosted_osi_printf と
	 *  同じ方式・同じ制約。lwIP 関連タスクは cfg で CLS_PRC1 固定＝単一 PE
	 *  前提のためコア間排他は持たない。fmp3_lwip_pools.cfg のコメント参照）。 */
	SIL_LOC_INT();
	slot = lwip_diag_ring_next;
	lwip_diag_ring_next = (slot + 1U) % LWIP_DIAG_RING_SLOTS;
	SIL_UNL_INT();
	buf = lwip_diag_ring[slot];

	va_start(ap, fmt);
	(void) vsnprintf(buf, LWIP_DIAG_RING_WIDTH, fmt, ap);
	va_end(ap);
	syslog(LOG_NOTICE, "lwip: %s", buf);
}

void
fmp3_lwip_platform_assert_fail(const char *msg, const char *file, int line)
{
	syslog(LOG_EMERG, "lwip ASSERT: %s (%s:%d)", msg, file, (int_t) line);
	for (;;) {
		(void) dly_tsk(1000U * 1000U);	/* 1s。意図的停止（noreturn 契約） */
	}
}

/*
 *  =====================================================================
 *  init
 *  =====================================================================
 */
void
sys_init(void)
{
	fmp3_lwip_pools_init();
}

/*
 *  =====================================================================
 *  時刻
 *  =====================================================================
 *  FMP3 の get_tim() が返す SYSTIM はマイクロ秒単位
 *  （RELTIM/TMO がマイクロ秒であることを os_adapter 実装時に確認済みで、
 *  同じ時刻ドメインを共有する SYSTIM も同様にマイクロ秒である）。
 */
u32_t
sys_now(void)
{
	SYSTIM systim = 0;

	(void) get_tim(&systim);
	return (u32_t) (systim / 1000U);	/* us -> ms */
}

u32_t
sys_jiffies(void)
{
	/*
	 *  lwIP契約: "Ticks/jiffies since power up"（単位は問わず、単調増加で
	 *  あればよい）。sys_now() と同じ get_tim() ベースで、us 単位のまま
	 *  下位32bitを返す（sys_now()よりも高分解能）。オーバーフロー
	 *  （約71分でラップ）は lwIP 側が差分計算にのみ使うため実害は無い想定。
	 */
	SYSTIM systim = 0;

	(void) get_tim(&systim);
	return (u32_t) systim;
}

/*
 *  =====================================================================
 *  クリティカルセクション保護 (SYS_ARCH_PROTECT/UNPROTECT)
 *  =====================================================================
 *  【設計判断】dis_dsp/ena_dsp（タスクディスパッチ抑止のみ）ではなく
 *  loc_cpu/unl_cpu（CPUロック＝割込みも抑止）を選ぶ。理由:
 *  lwIP の SYS_LIGHTWEIGHT_PROT は mem.c/memp.c 等、短時間のバッファ
 *  割当て/解放カウンタ操作を保護する用途であり、これらはドライバの
 *  受信割込みハンドラから直接触られる可能性を排除できない
 *  （lwIPの設計上ISRからのpbuf操作は非推奨だが、保護機構自体は
 *  "ISRからの割込みでも安全"であることが望ましい）。dis_dspはタスク
 *  ディスパッチしか止めないためISR自体は防げず、loc_cpuの方が安全側。
 *
 *  loc_cpu()/unl_cpu() はネスト呼び出しに対して "idempotent"
 *  （FMP3 kernel/sys_manage.c: `if (!sense_lock()) { lock_cpu(); }` /
 *  `if (sense_lock() && ...) { unlock_cpu(); }`）であり、内側の
 *  unl_cpu() を先に呼ぶと外側の保護区間もそこで解除されてしまう。
 *  そこで sns_loc() で「呼び出し前に既にロック済みだったか」を記録し、
 *  それを sys_prot_t として返す。unprotect 側は「呼び出し前が非ロックで
 *  あった場合（＝このprotectが実際にロックを取った場合）」のみ
 *  unl_cpu() する。これにより sys_arch_protect/unprotect のネスト呼び出し
 *  （lwIP の契約上許容される "SYS_ARCH_PROTECT を保護済み状態から呼んでも
 *  良い"）に対して正しく振る舞う。
 *
 *  【SMP に関する重要な注意（未解決課題・TODO）】
 *  FMP3 の loc_cpu/unl_cpu は **呼び出し元プロセッサ(PE)のみ**を保護する。
 *  ESP32-P4 は 2コアSMPであり、もう一方のPEで動く別タスク（例:
 *  SDIOドライバの受信タスクが tcpip_thread と異なるPEで動く場合）は
 *  この保護の影響を受けず、同時に保護対象データへアクセスしうる。
 *  つまり本実装は **単一PE内の直列化のみ**を保証し、**クロスコアの
 *  真の排他性は提供しない**。
 *  【2026-07-10 更新】この単一PE前提を構成レベルで強制するため、lwIP に
 *  触れるタスク群（fmp3_lwip_pools.cfg のスレッドプール・os_adapter の
 *  fmp3_hosted_osi.cfg のプールタスク群）を CLS_PRC1（PRC1 固定・
 *  マイグレーション不可）へ変更済み。lwIP をマルチ PE で使う構成に
 *  拡張する場合は、FMP3 のスピンロック（`loc_spn`/`unl_spn`。PE間の
 *  真の排他用に設計されたプリミティブ）を本 protect の実装に組み込む
 *  見直しが先に必要。
 */
sys_prot_t
sys_arch_protect(void)
{
	sys_prot_t was_already_locked = (sys_prot_t) sns_loc();

	(void) loc_cpu();

	return was_already_locked;
}

void
sys_arch_unprotect(sys_prot_t pval)
{
	if (!pval) {
		/* このprotect呼び出しが実際にロックを取った（ネストしていない）
		 * 場合のみ、ここでアンロックする。 */
		(void) unl_cpu();
	}
	/* pval != 0 の場合（呼び出し前から既にロック済み＝ネスト呼び出し）は
	 * 何もしない。外側の sys_arch_unprotect() が最終的に解除する。 */
}

/*
 *  =====================================================================
 *  セマフォ
 *  =====================================================================
 */
err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
	void *h;

	if (sem == NULL) {
		return ERR_ARG;
	}

	h = fmp3_lwip_pool_sem_new(count);
	if (h == NULL) {
		/* プール枯渇。fmp3_lwip_pools.h の FMP3_LWIP_POOL_NUM_SEM を
		 * 調整すること(TODO)。 */
		return ERR_MEM;
	}

	*sem = h;
	return ERR_OK;
}

void
sys_sem_free(sys_sem_t *sem)
{
	if (sem == NULL) {
		return;
	}
	fmp3_lwip_pool_sem_free(*sem);
	*sem = SYS_SEM_NULL;
}

void
sys_sem_signal(sys_sem_t *sem)
{
	if (sem == NULL) {
		return;
	}
	/* ISR文脈から呼んでもよい(sig_sem。fmp3_lwip_pools.c 冒頭コメント参照)。 */
	fmp3_lwip_pool_sem_signal(*sem);
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
	SYSTIM t0 = 0, t1 = 0;
	int st;

	if (sem == NULL) {
		return SYS_ARCH_TIMEOUT;
	}

	(void) get_tim(&t0);
	st = fmp3_lwip_pool_sem_wait(*sem, timeout_ms);
	(void) get_tim(&t1);

	if (st != FMP3_LWIP_POOL_OK) {
		/* タイムアウトに加え、想定外エラーもタイムアウト扱いに丸める
		 * （lwIP契約はタイムアウト以外の失敗コードを持たないため）。 */
		return SYS_ARCH_TIMEOUT;
	}

	/* 経過時間(ms)。0でも「タイムアウトではない」ことは SYS_ARCH_TIMEOUT
	 * (=0xffffffffUL)と衝突しないため成功として扱って問題ない。 */
	return (u32_t) ((t1 - t0) / 1000U);
}

int
sys_sem_valid(sys_sem_t *sem)
{
	return (sem != NULL && *sem != SYS_SEM_NULL) ? 1 : 0;
}

void
sys_sem_set_invalid(sys_sem_t *sem)
{
	if (sem == NULL) {
		return;
	}
	*sem = SYS_SEM_NULL;
}

/*
 *  =====================================================================
 *  ミューテックス (LWIP_COMPAT_MUTEX=0 前提)
 *  =====================================================================
 */
err_t
sys_mutex_new(sys_mutex_t *mutex)
{
	void *h;

	if (mutex == NULL) {
		return ERR_ARG;
	}

	h = fmp3_lwip_pool_mutex_new();
	if (h == NULL) {
		return ERR_MEM;
	}

	*mutex = h;
	return ERR_OK;
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
	int st;

	if (mutex == NULL) {
		return;
	}

	st = fmp3_lwip_pool_mutex_lock(*mutex);
	if (st != FMP3_LWIP_POOL_OK) {
		/*
		 *  lwIP契約: sys_mutex_lock() は失敗してはならない
		 *  （戻り値がvoidであることがそれを示す）。ここに来るのは
		 *  ハンドル不正等の設定不備であり、本来は起こらない想定。
		 *  TODO(統合時): syslog等の診断出力へ接続し、検知できるようにする
		 *  （本ファイルは独立モジュール方針のためsyslog直呼びは避け、
		 *  cc.hのfmp3_lwip_platform_diag経由にする案も検討可）。
		 */
	}
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
	if (mutex == NULL) {
		return;
	}
	(void) fmp3_lwip_pool_mutex_unlock(*mutex);
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
	if (mutex == NULL) {
		return;
	}
	fmp3_lwip_pool_mutex_free(*mutex);
	*mutex = NULL;
}

int
sys_mutex_valid(sys_mutex_t *mutex)
{
	return (mutex != NULL && *mutex != NULL) ? 1 : 0;
}

void
sys_mutex_set_invalid(sys_mutex_t *mutex)
{
	if (mutex == NULL) {
		return;
	}
	*mutex = NULL;
}

/*
 *  =====================================================================
 *  メールボックス（dtq 1本 = mbox 1個。fmp3_lwip_pools.h 冒頭コメント参照）
 *  =====================================================================
 */
err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
	void *h;

	if (mbox == NULL) {
		return ERR_ARG;
	}

	h = fmp3_lwip_pool_mbox_new(size);
	if (h == NULL) {
		return ERR_MEM;
	}

	*mbox = h;
	return ERR_OK;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
	if (mbox == NULL) {
		return;
	}
	fmp3_lwip_pool_mbox_free(*mbox);
	*mbox = SYS_MBOX_NULL;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
	int st;

	if (mbox == NULL) {
		return;
	}

	st = fmp3_lwip_pool_mbox_post(*mbox, msg);
	if (st != FMP3_LWIP_POOL_OK) {
		/* lwIP契約上 "may not fail" だが、ハンドル不正等の場合はここに
		 * 来うる。TODO(統合時): 診断出力に接続。 */
	}
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	int st;

	if (mbox == NULL) {
		return ERR_ARG;
	}

	/* タスク文脈専用（fmp3_lwip_pools.c 冒頭コメント参照。ISRからは
	 * sys_mbox_trypost_fromisr() を使うこと）。 */
	st = fmp3_lwip_pool_mbox_trypost(*mbox, msg);
	return (st == FMP3_LWIP_POOL_OK) ? ERR_OK : ERR_MEM;
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	int st;

	if (mbox == NULL) {
		return ERR_ARG;
	}

	/* ISR文脈から呼んでよい(ipsnd_dtq。fmp3_lwip_pools.c 冒頭コメント参照)。 */
	st = fmp3_lwip_pool_mbox_trypost_fromisr(*mbox, msg);
	return (st == FMP3_LWIP_POOL_OK) ? ERR_OK : ERR_MEM;
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
	SYSTIM t0 = 0, t1 = 0;
	void *tmp = NULL;
	int st;

	if (mbox == NULL) {
		return SYS_ARCH_TIMEOUT;
	}

	(void) get_tim(&t0);
	st = fmp3_lwip_pool_mbox_fetch(*mbox, &tmp, timeout_ms);
	(void) get_tim(&t1);

	if (st != FMP3_LWIP_POOL_OK) {
		return SYS_ARCH_TIMEOUT;
	}

	if (msg != NULL) {
		/* lwIP契約: msgはNULL可（メッセージを読み捨てる指示）。 */
		*msg = tmp;
	}

	return (u32_t) ((t1 - t0) / 1000U);
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	void *tmp = NULL;
	int st;

	if (mbox == NULL) {
		return SYS_ARCH_TIMEOUT;	/* == SYS_MBOX_EMPTY */
	}

	st = fmp3_lwip_pool_mbox_tryfetch(*mbox, &tmp);
	if (st != FMP3_LWIP_POOL_OK) {
		return SYS_ARCH_TIMEOUT;	/* == SYS_MBOX_EMPTY */
	}

	if (msg != NULL) {
		*msg = tmp;
	}

	return 0;
}

int
sys_mbox_valid(sys_mbox_t *mbox)
{
	return (mbox != NULL && *mbox != SYS_MBOX_NULL) ? 1 : 0;
}

void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
	if (mbox == NULL) {
		return;
	}
	*mbox = SYS_MBOX_NULL;
}

/*
 *  =====================================================================
 *  スレッド
 *  =====================================================================
 */
sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
	void *h;

	h = fmp3_lwip_pool_thread_new(name, thread, arg, stacksize, prio);

	/*
	 *  lwIP契約: 「ATTENTION: although this function returns a value,
	 *  it MUST NOT FAIL (ports have to assert this!)」。呼び出し元
	 *  （tcpip_init 等）は戻り値を検査しないため、NULL を返すと「存在しない
	 *  スレッド」への mbox post が満杯→無限ブロックという遠隔地ハングとして
	 *  顕在化する（レビュー指摘）。プール枯渇は静的コンフィギュレーション
	 *  不足（FMP3_LWIP_POOL_NUM_THREAD 不足）という設定バグなので、契約
	 *  どおりここで停止して即座に検知させる（独立モジュール方針の例外として
	 *  syslog を直接使う。書式・name はいずれも静的文字列で logtask の遅延
	 *  整形にも安全）。
	 */
	if (h == NULL) {
		syslog(LOG_EMERG,
				"sys_thread_new: thread pool exhausted (increase FMP3_LWIP_POOL_NUM_THREAD)");
		for (;;) {
			(void) dly_tsk(1000U * 1000U);	/* 1s。意図的な停止（MUST NOT FAIL 契約） */
		}
	}
	return (sys_thread_t) h;
}
