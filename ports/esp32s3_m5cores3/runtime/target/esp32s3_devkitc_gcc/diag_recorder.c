/*
 *  TOPPERS/FMP Kernel
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
 *  クラッシュ/ハング診断用の常設recorder 実装（diag_recorder.h参照）
 */
#include "kernel_impl.h"
#include "task.h"
#include "diag_recorder.h"
#include <string.h>

/*
 *  診断領域を実DRAM上のnoinitセクションへ置くかどうか
 *
 *  esp/build_incflags.txt（build_wifi_xip.sh/build_bt_xip.sh経由）は
 *  ESP_PLATFORMを定義するが、QEMU/実機カーネルオブジェクトテストビルド
 *  （scripts/run_tests.sh＝TOPPERS_QEMU定義、scripts/run_tests_realhw.sh＝
 *  どちらも未定義）は定義しない。esp32s3_xip.ldにのみ.diag_noinit出力
 *  セクションを追加しesp32s3_devkitc.ldは触らない方針（コーディネータ
 *  承認済み設計）のため、.diag_noinit属性は「esp32s3_xip.ldでリンクされる
 *  ことが既知」なESP_PLATFORMビルドに限定する。他のビルドでは通常の
 *  .bss変数にフォールバックし、diag_noinitセクション自体を参照しない
 *  （orphan section問題を避け、esp32s3_devkitc.ldを不変に保つ）。
 */
#if defined(ESP_PLATFORM) && !defined(TOPPERS_QEMU)
#define DIAG_NOINIT_ATTR	__attribute__((section(".diag_noinit")))
#else
#define DIAG_NOINIT_ATTR
#endif

#define DIAG_MAGIC		0x44494147UL	/* "DIAG" */
#define DIAG_VERSION	1UL
#define DIAG_MAX_CORES	2U
#define DIAG_RING_LEN	64U				/* 64エントリ*16B=1024B */

/*
 *  イベントリング1エントリ（16バイト固定）
 */
typedef struct {
	uint32_t	ts;			/* CCOUNT下位32bit */
	uint8_t		id;			/* DIAG_EV_* */
	uint8_t		core;		/* コアID(0/1) */
	uint16_t	reserved;
	uint32_t	arg1;
	uint32_t	arg2;
} diag_ring_entry_t;

/*
 *  パニック/致命例外スナップショット（15ワード=60バイト）
 */
typedef struct {
	uint32_t	valid;
	uint32_t	core;
	uint32_t	pc;
	uint32_t	ps;
	uint32_t	exccause;
	uint32_t	excvaddr;
	uint32_t	sar;
	uint32_t	windowbase;
	uint32_t	windowstart;
	uint32_t	intnest;
	uint32_t	p_runtsk;
	uint32_t	task_sp;
	uint32_t	stack_lo;
	uint32_t	stack_hi;
	uint32_t	hrt;
} diag_snapshot_t;

/*
 *  診断領域本体
 *
 *  checksumは「ringの各エントリ + snapshot」全ワードのXOR折り畳みを
 *  常に正確に反映する running checksum（diag_xor_update参照）。
 *  heartbeat/boot_count/magic/versionはchecksum対象外（heartbeatの
 *  tickフックは「1ストアのみ」の制約があるため）。
 */
typedef struct {
	uint32_t			magic;
	uint32_t			version;
	uint32_t			boot_count;
	uint32_t			checksum;
	uint32_t			heartbeat[DIAG_MAX_CORES];	/* 最終生存tick(CCOUNT) */
	uint32_t			ring_head;					/* 次に書き込むエントリ番号 */
	diag_ring_entry_t	ring[DIAG_RING_LEN];
	diag_snapshot_t		snapshot;
} diag_region_t;

static diag_region_t diag_region DIAG_NOINIT_ATTR;

/*
 *  診断領域が初期化済み（診断領域の有効性チェック・初期化を実施済み）
 *
 *  診断領域自体（DIAG_NOINIT_ATTR時は.diag_noinit＝BSS初期化対象外）とは
 *  独立に、通常.bssへ置く（起動毎に確実に0で始まる）。diag_boot_check_
 *  and_dump()呼び出し前にdiag_event等が呼ばれても書き込まないための
 *  ガード。
 */
static volatile bool_t diag_ready = false;

#if TNUM_PRCID >= 2
/*
 *  診断領域のコア間排他用スピンロック［レビュー指摘#5］
 *
 *  diag_event()/diag_panic_snapshot()の`Asm("rsil %0,15")`は自コアの
 *  割込みマスクのみで、TNUM_PRCID>=2では他コアが同時にdiag_event()等を
 *  呼ぶのを止められない。ring_head/checksum/snapshotへのRMW（差分更新の
 *  XOR含む）がコア間で競合すると診断データ自体が破壊される（ハング解析
 *  の生命線のため致命的）。chip_kernel_impl.hのLOCK/acquire_lock/
 *  release_lock（S32C1Iベースのソフトウェアスピンロックで、giant_lock/
 *  ネイティブspnと同じ実体）でコア間排他を追加する。
 *
 *  ロック順序：rsil(15)で自コア割込みを止めてからacquire_lock()する
 *  （wai_sem等のlock_cpu_dsp()→acquire_glock()と同じ順序）。臨界区間は
 *  メモリ操作のみの数命令なので、他コアがロック保持中にfatal例外で
 *  停止して解放できなくなる残存リスクは無視できるほど小さいと判断した
 *  （giant_lock等、既存カーネルのロックも同じ前提で運用されている）。
 */
static LOCK diag_lock;
#endif /* TNUM_PRCID >= 2 */

/*
 *  CCOUNT（Xtensaサイクルカウンタ）下位32bit読み出し
 */
static uint32_t
diag_get_ccount(void)
{
	uint32_t val;
	Asm("rsr.ccount %0" : "=a"(val));
	return val;
}

/*
 *  自コアID（0/1）読み出し（PRIDの第13bit。chip_kernel_impl.hの
 *  get_my_prcidxと同じ手法。diag_recorder.hをkernel_impl.hに依存させ
 *  ないため、ここで直接読む）
 */
static uint32_t
diag_get_core(void)
{
	uint32_t id;
	Asm("rsr.prid %0 \n\t"
		"extui %0, %0, 13, 1" : "=a"(id));
	return id;
}

/*
 *  nワード（uint32_t単位）のXOR折り畳み
 */
static uint32_t
diag_xor_words(const void *p, uint32_t nwords)
{
	const uint32_t	*w = (const uint32_t *) p;
	uint32_t		acc = 0U;
	uint32_t		i;

	for (i = 0U; i < nwords; i++) {
		acc ^= w[i];
	}
	return acc;
}

/*
 *  checksumの差分更新：old_contentをXORで抜き、new_contentをXORで足す。
 *  region->checksumが常に「現在のring+snapshot全体のXOR」と一致するよう
 *  維持する（diag_init時のフル計算を基準に、以後はO(1)差分更新のみ）。
 *  呼び出し側で診断領域ロックを取得済みであること。
 */
static void
diag_checksum_replace(const void *old_content, const void *new_content, uint32_t nwords)
{
	diag_region.checksum ^= diag_xor_words(old_content, nwords);
	diag_region.checksum ^= diag_xor_words(new_content, nwords);
}

/*
 *  診断領域の新規初期化（ダンプ後、または無効データ検出時に呼ぶ）
 *
 *  呼び出し元（diag_boot_check_and_dump）は起動シーケンス中の
 *  シングルスレッド文脈（マスタプロセッサのみ、他コアはbarrier待ち）
 *  のため排他不要。
 */
static void
diag_region_init(uint32_t boot_count)
{
	memset(&diag_region, 0, sizeof(diag_region));
	diag_region.magic = DIAG_MAGIC;
	diag_region.version = DIAG_VERSION;
	diag_region.boot_count = boot_count;
	/* ring/snapshotは全ゼロなのでchecksum=0が整合する */
	diag_region.checksum = 0U;
#if TNUM_PRCID >= 2
	/* 他コアがまだ起動していないシングルスレッド文脈での初期化（呼出元コメント参照） */
	initialize_lock(&diag_lock);
#endif /* TNUM_PRCID >= 2 */
	diag_ready = true;
}

/*
 *  軽量イベント記録（診断領域.hで宣言）
 */
void
diag_event(uint8_t id, uint32_t arg1, uint32_t arg2)
{
	diag_ring_entry_t	new_entry;
	diag_ring_entry_t	old_entry;
	uint32_t			idx;
	uint32_t			ps;

	if (!diag_ready) {
		return;
	}

	new_entry.ts = diag_get_ccount();
	new_entry.id = id;
	new_entry.core = (uint8_t) diag_get_core();
	new_entry.reserved = 0U;
	new_entry.arg1 = arg1;
	new_entry.arg2 = arg2;

	/* 短い割込み禁止区間（printf/syslog/カーネルAPI非依存） */
	Asm("rsil %0, 15" : "=a"(ps));
#if TNUM_PRCID >= 2
	acquire_lock(&diag_lock);	/*［#5］他コアとのコア間排他 */
#endif /* TNUM_PRCID >= 2 */

	idx = diag_region.ring_head;
	old_entry = diag_region.ring[idx];
	diag_region.ring[idx] = new_entry;
	diag_checksum_replace(&old_entry, &new_entry, sizeof(new_entry) / sizeof(uint32_t));
	diag_region.ring_head = (idx + 1U) % DIAG_RING_LEN;

#if TNUM_PRCID >= 2
	release_lock(&diag_lock);
#endif /* TNUM_PRCID >= 2 */
	Asm("wsr.ps %0; rsync" :: "a"(ps) : "memory");
}

/*
 *  per-coreハートビート（tickハンドラから1ストアのみ想定）
 */
void
diag_heartbeat(void)
{
	uint32_t core;

	if (!diag_ready) {
		return;
	}
	core = diag_get_core();
	if (core < DIAG_MAX_CORES) {
		diag_region.heartbeat[core] = diag_get_ccount();	/* 1ストア */
	}
}

/*
 *  パニック/致命例外時のCPUスナップショット保存
 */
void
diag_panic_snapshot(const void *p_excinf)
{
	const T_EXCINF	*p_ei = (const T_EXCINF *) p_excinf;
	diag_snapshot_t	new_snap;
	diag_snapshot_t	old_snap;
	uint32_t		excvaddr, windowbase, windowstart;
	uint32_t		ps;
	TCB				*p_tcb;

	if (!diag_ready) {
		return;
	}

	/*
	 *  T_EXCINFに含まれないSFR（core_kernel.hのフィールド一覧参照）は
	 *  例外発生直後のこの時点で直接読む。
	 */
	Asm("rsr.excvaddr %0" : "=a"(excvaddr));
	Asm("rsr.windowbase %0" : "=a"(windowbase));
	Asm("rsr.windowstart %0" : "=a"(windowstart));

	memset(&new_snap, 0, sizeof(new_snap));
	new_snap.valid = 1U;
	new_snap.core = diag_get_core();
	new_snap.pc = p_ei->pc;
	new_snap.ps = p_ei->ps;
	new_snap.exccause = p_ei->exccause;
	new_snap.excvaddr = excvaddr;
	new_snap.sar = p_ei->sar;
	new_snap.windowbase = windowbase;
	new_snap.windowstart = windowstart;
	new_snap.intnest = p_ei->exncnt;
	new_snap.hrt = diag_get_ccount();

	/*
	 *  p_runtsk・スタック範囲：get_my_pcb()は例外発生コアのPCBを返す
	 *  （割込み/例外コンテキストからも安全に呼べる）。
	 */
	p_tcb = get_my_pcb()->p_runtsk;
	new_snap.p_runtsk = (uint32_t)(uintptr_t) p_tcb;
	new_snap.task_sp = p_ei->oldsp;
	if (p_tcb != NULL) {
		new_snap.stack_lo = (uint32_t)(uintptr_t) p_tcb->p_tinib->tskinictxb.stk_top;
		new_snap.stack_hi = (uint32_t)(uintptr_t) p_tcb->p_tinib->tskinictxb.stk_bottom;
	}

	Asm("rsil %0, 15" : "=a"(ps));
#if TNUM_PRCID >= 2
	acquire_lock(&diag_lock);	/*［#5］他コアとのコア間排他 */
#endif /* TNUM_PRCID >= 2 */
	old_snap = diag_region.snapshot;
	diag_region.snapshot = new_snap;
	diag_checksum_replace(&old_snap, &new_snap, sizeof(new_snap) / sizeof(uint32_t));
#if TNUM_PRCID >= 2
	release_lock(&diag_lock);
#endif /* TNUM_PRCID >= 2 */
	Asm("wsr.ps %0; rsync" :: "a"(ps) : "memory");
}

/*
 *  診断領域の有効性チェック用：現在のring+snapshot全体のXOR
 *  （diag_checksum_replaceの差分更新とは独立に、フルスキャンで
 *  検証する。ブート時1回だけの処理なのでコストは問題ない）。
 */
static uint32_t
diag_full_checksum(void)
{
	uint32_t acc = 0U;
	uint32_t i;

	for (i = 0U; i < DIAG_RING_LEN; i++) {
		acc ^= diag_xor_words(&diag_region.ring[i], sizeof(diag_ring_entry_t) / sizeof(uint32_t));
	}
	acc ^= diag_xor_words(&diag_region.snapshot, sizeof(diag_snapshot_t) / sizeof(uint32_t));
	return acc;
}

static const char *
diag_ev_name(uint8_t id)
{
	switch (id) {
	case DIAG_EV_BOOT:            return("BOOT");
	case DIAG_EV_SEM_TAKE:        return("SEM_TAKE");
	case DIAG_EV_SEM_GIVE:        return("SEM_GIVE");
	case DIAG_EV_Q_SEND:          return("Q_SEND");
	case DIAG_EV_Q_SEND_ISR:      return("Q_SEND_ISR");
	case DIAG_EV_Q_RECV:          return("Q_RECV");
	case DIAG_EV_NET_TX:          return("NET_TX");
	case DIAG_EV_NET_RX:          return("NET_RX");
	case DIAG_EV_PBUF_ALLOC_FAIL: return("PBUF_ALLOC_FAIL");
	default:                      return("?");
	}
}

/*
 *  ブート時の診断領域チェック・前回内容ダンプ・再初期化
 *
 *  ATT_INI（target_diag.cfg、マスタプロセッサのみ・カーネルオブジェクト
 *  初期化済みの文脈）から呼ばれる。syslogを使ってよい。
 */
void
diag_boot_check_and_dump(intptr_t exinf)
{
	uint32_t	i, idx;
	bool_t		valid;
	uint32_t	expect_cksum;

	(void) exinf;

	valid = (diag_region.magic == DIAG_MAGIC) && (diag_region.version == DIAG_VERSION);

	if (valid) {
		expect_cksum = diag_full_checksum();
		syslog(LOG_NOTICE,
			   "diag: previous boot data found (boot_count=%u, checksum=%s)",
			   (uint_t) diag_region.boot_count,
			   (expect_cksum == diag_region.checksum) ? "OK" : "MISMATCH(dump anyway)");
		for (i = 0U; i < DIAG_MAX_CORES; i++) {
			syslog(LOG_NOTICE, "diag: core%u last heartbeat ccount=%u",
				   (uint_t) i, (uint_t) diag_region.heartbeat[i]);
		}
		if (diag_region.snapshot.valid != 0U) {
			syslog(LOG_NOTICE,
				   "diag: panic snapshot core=%u pc=%#x ps=%#x exccause=%u excvaddr=%#x",
				   (uint_t) diag_region.snapshot.core,
				   (uint_t) diag_region.snapshot.pc,
				   (uint_t) diag_region.snapshot.ps,
				   (uint_t) diag_region.snapshot.exccause,
				   (uint_t) diag_region.snapshot.excvaddr);
			syslog(LOG_NOTICE,
				   "diag: panic sar=%#x windowbase=%#x windowstart=%#x intnest=%u hrt=%u",
				   (uint_t) diag_region.snapshot.sar,
				   (uint_t) diag_region.snapshot.windowbase,
				   (uint_t) diag_region.snapshot.windowstart,
				   (uint_t) diag_region.snapshot.intnest,
				   (uint_t) diag_region.snapshot.hrt);
			syslog(LOG_NOTICE,
				   "diag: panic p_runtsk=%#x sp=%#x stack=[%#x,%#x)",
				   (uint_t) diag_region.snapshot.p_runtsk,
				   (uint_t) diag_region.snapshot.task_sp,
				   (uint_t) diag_region.snapshot.stack_lo,
				   (uint_t) diag_region.snapshot.stack_hi);
		}
		else {
			syslog(LOG_NOTICE, "diag: no panic snapshot (silent hang or clean reset)");
		}
		/* リング内容：ring_headから古い順（=次に上書きされる位置から） */
		for (i = 0U; i < DIAG_RING_LEN; i++) {
			idx = (diag_region.ring_head + i) % DIAG_RING_LEN;
			if (diag_region.ring[idx].id == DIAG_EV_NONE) {
				continue;	/* 未使用（まだ一周していない） */
			}
			syslog(LOG_INFO, "diag: ring[%u] ts=%u core=%u ev=%s arg1=%#x arg2=%#x",
				   (uint_t) idx,
				   (uint_t) diag_region.ring[idx].ts,
				   (uint_t) diag_region.ring[idx].core,
				   diag_ev_name(diag_region.ring[idx].id),
				   (uint_t) diag_region.ring[idx].arg1,
				   (uint_t) diag_region.ring[idx].arg2);
		}
	}
	else {
		syslog(LOG_NOTICE, "diag: no valid previous boot data (cold boot or first run)");
	}

	/*
	 *  LCOUNT暴走ガード（実験W、core_support.S .Lret_do_restore、追記53/54）の
	 *  診断カウンタ表示。ガード変数は通常の.bss（起動毎に0クリア）のため、
	 *  ここで非ゼロが見えるのは「本関数呼び出し（起動シーケンス中のATT_INI）
	 *  より前にガードが発動した」場合のみだが、要求仕様通りブート時ダンプにも
	 *  表示しておく（ゼロ時は静か）。実行中の発動はwifi_sta.cの周期出力で確認する。
	 */
	if (_kernel_lcount_guard_cnt[0] != 0U) {
		syslog(LOG_WARNING,
			   "diag: ★LCOUNTガード発動 cnt=%u last_val=0x%08x last_pc=0x%08x（実験W追記54）",
			   (uint_t) _kernel_lcount_guard_cnt[0],
			   (uint_t) _kernel_lcount_guard_val[0],
			   (uint_t) _kernel_lcount_guard_pc[0]);
	}

	diag_region_init(valid ? (diag_region.boot_count + 1U) : 1U);
	diag_event(DIAG_EV_BOOT, (uint32_t) diag_region.boot_count, 0U);
}
