/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 */

/*
 *		コア依存モジュール（Xtensa用）
 *
 *  本作業単位のスコープでは、
 *  ARM-M版にあったNVIC関連処理（set_exc_int_priority/enable_exc/
 *  disable_exc/config_int/core_int_entry等）はXtensaに存在しない
 *  概念のため実装しない。割込みの許可・禁止はcore_kernel_impl.hの
 *  disable_int/enable_int（INTENABLEレジスタ直接操作）で行う。
 */

#include "kernel_impl.h"
#include "check.h"
#include "task.h"
#include "diag_recorder.h"
#include "target_timer.h"	/* TCYC_PER_HRT（sil_dly_nseのcycles_per_usec） */
#if defined(TOPPERS_M5_PANIC_DUMP)
#include <target_syssvc.h>	/* target_fput_log（M5 パニック即時ダンプ・M5 ビルド専用） */
#endif

/*
 *  割込みネストカウンタの実体（宣言はcore_kernel_impl.h）
 *
 *  sense_context()が参照する非タスクコンテキスト判定用のソフトウェア
 *  カウンタ。i_begin_int()/i_end_int()で増減する。
 */
volatile uint_t _kernel_xtos_intnest[TNUM_PRCID] = { 0U };

/*
 *  割込み優先度マスク（IPM）のper-core保持変数（ARM-MのBASEPRI相当）。
 *  chg_ipm(t_set_ipm)で更新し、get_ipm(t_get_ipm)で参照する。CPUロック
 *  （PS.INTLEVEL）とは独立。既定はTIPM_ENAALL（全割込み許可）。
 */
volatile PRI _kernel_ipm[TNUM_PRCID] = { TIPM_ENAALL };

/*
 *  IPMに対応するINTENABLE仮想優先度マスク（宣言はcore_kernel_impl.h）
 *
 *  ESP-IDFの_xt_vpri_mask相当。INTENABLEへ書く実効値は常に
 *  _kernel_intenable_mask[c] & _kernel_vpri_mask[c] とする。t_set_ipmが
 *  _kernel_ipmと対で更新し、enable_int/disable_intと割込み出口（core_support.S）
 *  の復元がこのマスクをANDする。既定はTIPM_ENAALL＝全許可（0xFFFFFFFF）。
 */
volatile uint32_t _kernel_vpri_mask[TNUM_PRCID] = { 0xFFFFFFFFU };

/*
 *  自コアで許可中のLevel-1割込みマスク（宣言はcore_kernel_impl.h）
 *
 *  _kernel_l1int_entry（core_support.S）が割込みディスパッチ復帰時に
 *  INTENABLEへ書き戻す値。enable_int/disable_intとtick/IPI初期化がハードの
 *  INTENABLEと対で更新する。初期値はcore_initializeでtick(INT6)＋
 *  （SMP時）IPI(INT13)をseedする（core_asm.incのINT_KERNEL_MASKと一致）。
 */
volatile uint32_t _kernel_intenable_mask[TNUM_PRCID] = { 0U };

/*
 *  自コアでCFG_INT設定済みの割込み番号マスク（宣言はcore_kernel_impl.h）
 *
 *  check_intno_cfg/check_intno_clear/check_intno_raise
 *  は当初「常にtrue」の簡略実装だった（本作業単位ではCFG_INT/DEF_INHの
 *  汎用テーブル機構を使わない設計のため）が、TTSP3のAPIテストは
 *  「CFG_INT未設定の割込み番号を指定するとE_OBJが返る」ことを期待して
 *  おり（NGKI3098等）、常にtrueだとこの検証ができない
 *  （dis_int/ena_int/clr_int/ras_int/prb_intが軒並みE_OBJの代わりに
 *  E_OKを返し失敗していた）。config_intが呼ばれた時点でこのマスクへ
 *  ビットを立て、check_intno_*系はこれを参照して正しくE_OBJ相当の
 *  判定ができるようにする。
 */
volatile uint32_t _kernel_cfgint_mask[TNUM_PRCID] = { 0U };

/*
 *  LCOUNT暴走ガードの診断用per-core変数（宣言はcore_kernel_impl.h）
 *
 *  実験W：core_support.Sの.Lret_do_restoreが
 *  例外フレームからLCOUNTを復元する際、値が閾値（LCOUNT_GUARD_THRESHOLD、
 *  約100万回転）以上なら破壊された異常値とみなして0へ置換する。その際に
 *  ヒット回数・異常値そのもの・復帰先PC（中断PC）をここへ記録する
 *  （movi/addx4/s32iでアセンブリから直接アクセス）。
 *  guard_cnt[]が非ゼロ＝ガードが発動した実績あり（=破壊は実在した）を示す。
 */
volatile uint32_t _kernel_lcount_guard_cnt[TNUM_PRCID] = { 0U };  /* ヒット回数 */
volatile uint32_t _kernel_lcount_guard_val[TNUM_PRCID] = { 0U };  /* 最後の異常値 */
volatile uint32_t _kernel_lcount_guard_pc[TNUM_PRCID]  = { 0U };  /* そのときの復帰先PC */

#if TNUM_PRCID >= 2
#define CORE_INTENABLE_INIT   ((1U << 6) | (1U << 13))   /* tick(INT6)|IPI(INT13) */
#else
#define CORE_INTENABLE_INIT   (1U << 6)                  /* tick(INT6) */
#endif

/*
 *  intno別のLevel-1割込みハンドラ表（宣言はcore_kernel_impl.h）
 *
 *  define_inhがCRE_ISR/CFG_INTの生成ハンドラ(_kernel_inthdr_N)を登録し、
 *  _kernel_l1int_dispatch（target_timer.c）がペンディング割込みに対して呼ぶ。
 */
void (*_kernel_inh_handler_tbl[TMAX_INTNO + 1])(void) = { 0 };

/*
 *  ナノ秒単位のビジーウェイト（sil.hで宣言される汎用インタフェース）
 *
 *  CCOUNT差分で実装する。
 *
 *  ★2026-07-17：CPUクロック周波数を**ターゲット層の正本から取得する**ように
 *  校正した。従来はここに
 *  「cycles_per_usec = 40U（QEMU実測値を仮定・要調整）」とハードコードされて
 *  おり、実クロックと最大6倍ずれていた：
 *    - S3 seam      : 実CPU 80MHz（IDF 2nd-stage bootloaderがCPU_CLK_FREQ_MHZ_BTLD=80を
 *                     適用）に対し 40U → **2倍**ずれ（要求の1/2しか待たない）
 *    - LX6          : software_init_hook が実際にPLLへ昇圧している（既定160/240MHz）
 *                     のに 40U のまま → **4〜6倍**ずれ
 *    - S3 WiFi/BTビルド: TCYC_PER_HRT だけが 160/240 へ追随し、ここは 40U のまま
 *                     → **4〜6倍**ずれ
 *  本ファイルは S3(LX7)/ESP32(LX6) 共通であり、両者で正しい値が異なるため
 *  定数を直接書くことはできない。TCYC_PER_HRT（target_timer.h）が
 *  「1usあたりのCCOUNTサイクル数」＝まさに cycles_per_usec そのものであり、
 *  各ターゲットが起動経路に応じた正しい値を持っているので、それを参照する。
 *  （arch層からtarget_timer.hを読む前例：arch/xtensa_gcc/esp32/chip_ipi.c、
 *    arch/xtensa_gcc/esp32s3/chip_ipi.c がいずれも同ヘッダをincludeしている）
 */
static uint32_t
core_get_ccount(void)
{
	uint32_t val;
	Asm("rsr.ccount  %0" : "=a"(val));
	return val;
}

void
sil_dly_nse(ulong_t dlytim)
{
	/*
	 * ターゲット層の正本（TCYC_PER_HRT = TOPPERS_S3_CPU_FREQ_MHZ /
	 * TOPPERS_ESP32_CPU_FREQ_MHZ）から取得する。上のコメント参照。
	 */
	const uint32_t cycles_per_usec = TCYC_PER_HRT;
	uint32_t cycles = (uint32_t)(((uint64_t) dlytim * cycles_per_usec) / 1000U);
	uint32_t start = core_get_ccount();

	while ((core_get_ccount() - start) < cycles) {
	}
}

/*
 *  コア依存の初期化
 *
 *  本作業単位では最小限（何もしない）。アイドルスタック・コンテキスト
 *  スイッチ関連の初期化はコンテキストスイッチ実装の作業単位で追加する。
 */
void
core_initialize(PCB *p_my_pcb)
{
	/*
	 *  自コアの許可割込みマスクを既定値（tick＋SMP時IPI）にseedする。
	 *  以降enable_int/disable_intが差分更新する。tick/IPIの実際の許可は
	 *  target_hrt_initialize/IPI初期化が_xtos_ints_onで行う。
	 */
	_kernel_intenable_mask[get_my_prcidx()] = CORE_INTENABLE_INIT;

	/*
	 *  IPM仮想優先度マスクを全許可でseedする。静的初期化子は要素[0]しか
	 *  0xFFFFFFFFにできない（[1]以降は0＝全マスクになる）ため、per-coreで
	 *  実行時に初期化する。以降t_set_ipmが差分更新する。
	 */
	_kernel_vpri_mask[get_my_prcidx()] = 0xFFFFFFFFU;
}

/*
 *  コア依存の終了処理
 */
void
core_terminate(void)
{
	extern void    software_term_hook(void);
	void (*volatile fp)(void) = software_term_hook;

	/*
	 *  software_term_hookへのポインタを，一旦volatile指定のあるfpに代
	 *  入してから使うのは，0との比較が最適化で削除されないようにするた
	 *  めである．
	 */
	if (fp != 0) {
		(*fp)();
	}
}

/*
 *  ディスパッチャ・コンテキストスイッチ関連スタブ
 *
 *  本作業単位のスコープ外。
 *  Window Overflow/Underflow例外ハンドラとの統合が必要な本実装は
 *  コンテキストスイッチ実装の作業単位で行う（design.md参照）。
 *  到達した場合は原因を明示してから無限ループする（サイレントな
 *  ハング・クラッシュを避けるため）。
 */
/*
 *  dispatch / do_dispatch / start_dispatch / exit_and_dispatch は
 *  core_support.S にアセンブリで実装した（同期ディスパッチ、
 *  。do_dispatchはARM-M版内部名で
 *  FMP3カーネルからは参照されないため実体を持たない（ヘッダのextern
 *  宣言のみ残す）。
 */

/*
 *  自タスクのマイグレーション（dispatch_and_migrate / exit_and_migrate）
 *
 *  実装は core_support.S のアセンブリ（FMP3本家riscv_gccポート準拠のプロトコル）。
 *  ★**C で「make_runnable→release_glock→dispatch() 流用」と書いてはならない。**
 *  SMP では「コンテキスト保存前に make_runnable で対象コアへ公開する」順序誤りに
 *  なり、対象コアが未保存の TCB/スタックを走らせ得る。
 *  詳細プロトコル・windowed ABI固有の注意（全窓スピル/破棄、FPU eager
 *  保存の順序）はcore_support.Sの各関数コメントを参照。
 */

/*
 *  call_exit_kernel はアセンブラ（core_support.S）で実装する。
 *  自コアの割込みスタックへ切り替え、非タスクコンテキストで exit_kernel()
 *  を呼び出す（exit_kernelはbarrier_sync後にtarget_exit()へ至り戻らない）。
 */

/*
 *  未登録のCPU例外・割込みが発生した場合に呼び出される
 *
 *  本作業単位ではVECBASEを変更しないため、これらは実際には呼ばれない
 *  （ROM提供のベクタ・ハンドラが使われる）。将来VECBASEを自前化する
 *  時のためのプレースホルダ。
 */
void
default_exc_handler(void *p_excinf)
{
	/*
	 *  常設recorder基盤（diag_recorder.h）：未登録/致命CPU例外のスナップ
	 *  ショットを診断領域へ保存する。syslog（下記）自体がカーネルデータ
	 *  破壊の巻き添えで動かない可能性があるため、必ずsyslogより先に呼ぶ。
	 *  割込み禁止・最小処理・printf非依存で、既存のpanic挙動（停止ループ）
	 *  は変更しない。
	 */
	diag_panic_snapshot(p_excinf);

#if defined(TOPPERS_M5_PANIC_DUMP)
	/*
	 *  ★M5 ハング調査用の即時ダンプ（M5 ビルド専用・golden 5 構成では未定義）。
	 *
	 *  【なぜ生 UART0 直書きではないか（重要）】
	 *    下の TOPPERS_ESP32_LX6 ブロックは UART0 の FIFO を直接叩くが、M5 構成は
	 *    A1_CONSOLE_USJ=ON（CoreS3 は USB-Serial-JTAG 1 ポートで UART ブリッジを
	 *    持たない）なので、生 UART0 へ書いても **TX ピンに出るだけでホストからは
	 *    見えない**。target_fput_log は TOPPERS_S3_CONSOLE_USJ 定義時に出力先を
	 *    USJ へ切り替える（chip_serial.c）ので、実機で実際に観測できる唯一の経路。
	 *    リトライ上限つき（5000 回）で無限には待たないため例外文脈でも安全。
	 *
	 *  【なぜ syslog では駄目か】syslog は logtask 経由の遅延出力で、この直後の
	 *    while(1) では flush されない（同ファイル下のコメント自身が認めている）。
	 *    ＝S3 では例外が起きても完全に無音だった。
	 */
	{
		const T_EXCINF *pe = (const T_EXCINF *) p_excinf;
		static const char hexd[] = "0123456789abcdef";
		/*  ★T_EXCINF（core_kernel.h:91-120）に excvaddr フィールドは無い。
		 *  EXCVADDR は特殊レジスタなので RSR で直接読む（LoadProhibited/
		 *  StoreProhibited のとき「どのアドレスを触って落ちたか」が分かる＝
		 *  ヌルポインタ/未マップ領域の切り分けに最も効く 1 語）。
		 *  スタックポインタは a1 フィールドではなく oldsp（例外発生時の SP）。 */
		uint32_t excvaddr;
		__asm__ volatile ("rsr.excvaddr %0" : "=r" (excvaddr));
		{
			static const char *const lbl[] = {
				"\r\n*** CPU EXCEPTION *** exccause=", " pc=", " ps=", " excvaddr=",
				" sp(oldsp)=", " a0=", " a2=", " a3=", " a8=", " a10=", " a12=", " a13=",
				" exncnt=", " intpri=", "\r\n"
			};
			const uint32_t val[] = {
				pe->exccause, pe->pc, pe->ps, excvaddr,
				pe->oldsp, pe->a0, pe->a2, pe->a3, pe->a8, pe->a10, pe->a12, pe->a13,
				pe->exncnt, pe->intpri
			};
			int i, j;
			for (i = 0; i < 14; i++) {
				const char *s = lbl[i];
				while (*s != '\0') { target_fput_log(*s++); }
				for (j = 28; j >= 0; j -= 4) {
					target_fput_log(hexd[(val[i] >> j) & 0xFU]);
				}
			}
			{ const char *s = lbl[14];
			  while (*s != '\0') { target_fput_log(*s++); } }
		}
	}
#endif /* TOPPERS_M5_PANIC_DUMP */

#if defined(TOPPERS_ESP32_LX6)
	/*  無印ESP32ブリングアップ診断：syslog(logtask経由)は例外ハンドラの
	 *  while(1)文脈でバッファがflushされないことがあるため、EXCCAUSE/PC/PS/
	 *  主要レジスタを生UART0(0x3FF40000)へ直接ダンプする（TOPPERS_ESP32_LX6限定）。 */
	{
		const T_EXCINF *pe = (const T_EXCINF *) p_excinf;
		static const char hexd[] = "0123456789abcdef";
		volatile uint32_t *fifo   = (volatile uint32_t *) 0x3FF40000U;
		volatile uint32_t *status = (volatile uint32_t *) 0x3FF4001CU;
		const char *lbl[] = { "\r\nEXC cause=", " pc=", " ps=", " a2=", " a3=",
		                      " a8=", " a10=", " a12=", " a13=", "\r\n" };
		uint32_t val[] = { pe->exccause, pe->pc, pe->ps, pe->a2, pe->a3,
		                   pe->a8, pe->a10, pe->a12, pe->a13 };
		int i, j;
		for (i = 0; i < 9; i++) {
			const char *s = lbl[i];
			while (*s != '\0') {
				while (((*status >> 16) & 0xFFU) >= 120U) { }
				*fifo = (uint32_t) (uint8_t) *s++;
			}
			for (j = 28; j >= 0; j -= 4) {
				while (((*status >> 16) & 0xFFU) >= 120U) { }
				*fifo = (uint32_t) (uint8_t) hexd[(val[i] >> j) & 0xFU];
			}
		}
		{ const char *s = lbl[9];
		  while (*s != '\0') { while (((*status >> 16) & 0xFFU) >= 120U) { } *fifo = (uint32_t)(uint8_t)*s++; } }
	}
#endif

	syslog(LOG_EMERG, "STUB: default_exc_handler() reached (p_excinf=%p)", p_excinf);
	{
		/*
		 *  未登録CPU例外の一次切り分け情報（EXCCAUSE/PC/PS）を出力する。
		 *  EXCCAUSE=32〜39（CoprocessorNDisabled）はCPENABLE=0状態での
		 *  コプロセッサ命令実行で、本ポートではCP0=FPU：TA_FPU属性の無い
		 *  タスク（またはISR）がFPU命令を実行したバグの検出を意味する
		 *  （FPUはeager保存方式。TA_FPU無しタスクはCPENABLE=0で実行される。
		 *  core_support.Sのdispatcher参照）。
		 */
		const T_EXCINF *p = (const T_EXCINF *) p_excinf;
		uint32_t exccause = p->exccause & 0x3FU;

		syslog(LOG_EMERG, "  EXCCAUSE=%d PC=0x%08x PS=0x%08x",
		       (int_t) exccause, p->pc, p->ps);
		if ((32U <= exccause) && (exccause <= 39U)) {
			syslog(LOG_EMERG,
			       "  = Coprocessor%dDisabled: FPU(CP0) insn without TA_FPU?",
			       (int_t)(exccause - 32U));
		}
	}
	while (1) {
	}
}

void
default_int_handler(void)
{
#if defined(TOPPERS_M5_PANIC_DUMP)
	/*  ★M5 専用（golden 5 構成では未定義＝コードは 1 バイトも変わらない）。
	 *  syslog は logtask 経由で、直後の while(1) では flush されない＝無音だった。
	 *  即時経路で到達を宣言し、併せて現在の INTERRUPT/INTENABLE を出す
	 *  （どの割込み源が未登録のまま上がったのかの一次切り分けになる）。 */
	{
		static const char hexd[] = "0123456789abcdef";
		static const char *const lbl[] = {
			"\r\n*** UNHANDLED INTERRUPT *** interrupt=", " intenable=", " ps=", "\r\n"
		};
		uint32_t v[3];
		int i, j;
		__asm__ volatile ("rsr.interrupt %0" : "=r" (v[0]));
		__asm__ volatile ("rsr.intenable %0" : "=r" (v[1]));
		__asm__ volatile ("rsr.ps        %0" : "=r" (v[2]));
		for (i = 0; i < 3; i++) {
			const char *s = lbl[i];
			while (*s != '\0') { target_fput_log(*s++); }
			for (j = 28; j >= 0; j -= 4) {
				target_fput_log(hexd[(v[i] >> j) & 0xFU]);
			}
		}
		{ const char *s = lbl[3];
		  while (*s != '\0') { target_fput_log(*s++); } }
	}
#endif /* TOPPERS_M5_PANIC_DUMP */

	syslog(LOG_EMERG, "STUB: default_int_handler() reached");
	while (1) {
	}
}

/*
 *  CPU例外ハンドラのディスパッチ機構
 *
 *  ROMの汎用例外ベクタは EXCCAUSE で xtos_exc_handler_table を引いて飛ぶ。
 *  DEF_EXCで登録した各EXCCAUSEに対し、define_exc()が
 *   (1) EXCCAUSE→Cハンドラ対応表 _kernel_exc_handler_tbl に登録し、
 *   (2) xtos_exc_handler_table[EXCCAUSE] に共通の入口 core_exc_entry を書く。
 *  実行時、core_exc_entry（core_support.S）がフレームを構築し、windowed
 *  ABIで _kernel_exc_dispatch() を呼ぶ。ここでEXCCAUSEから登録済みCハンドラ
 *  を引いて呼び出す（登録が無ければ default_exc_handler）。
 *
 *  EXCCAUSEは0〜63。ハンドラはコアローカルに動作するが、DEF_EXCの
 *  ハンドラ番地はコア間で同一のため、対応表はグローバル1本とする。
 */
#define NUM_EXCCAUSE    64U

void (*_kernel_exc_handler_tbl[NUM_EXCCAUSE])(void *p_excinf) = { 0 };

/* core_exc_entry（core_support.S、core_rename.hで_kernel_core_exc_entryへ改名） */
extern void core_exc_entry(void);
/* ROMの例外ハンドラテーブル（実体アドレスはリンカスクリプトで結合） */
extern _xtos_handler xtos_exc_handler_table[];

/*
 *  CPU例外ハンドラの静的登録（initialize_exceptionから各excinib分呼ばれる）
 *
 *  excnoは (prcid<<16)|EXCCAUSE で符号化されている。下位のEXCCAUSE部を
 *  取り出して対応表とROMテーブルへ登録する。
 */
void
define_exc(EXCNO excno, FP exc_entry)
{
	uint_t excause = (uint_t)(excno) & 0x3FU;

	_kernel_exc_handler_tbl[excause] = (void (*)(void *))(exc_entry);
	xtos_exc_handler_table[excause] = (_xtos_handler) core_exc_entry;
}

/*
 *  CPU例外ハンドラのディスパッチ（core_exc_entryからwindowed callで呼ばれる）
 */
void
_kernel_exc_dispatch(void *p_excinf)
{
	uint_t excause = ((const T_EXCINF *)p_excinf)->exccause & 0x3FU;
	void (*hdr)(void *) = _kernel_exc_handler_tbl[excause];

	if (hdr != NULL) {
		hdr(p_excinf);
	}
	else {
		default_exc_handler(p_excinf);
	}
}

/*
 *  割込みハンドラの静的登録（define_inh）
 *
 *  CRE_ISR/CFG_INTが生成した割込みハンドラ(_kernel_inthdr_N)をintno別の
 *  ハンドラ表へ登録する。initialize_interruptから各inhinib分呼ばれる。
 *  inhnoは素の割込み番号（target_kernel.trb参照）。
 */
void
define_inh(PCB *p_my_pcb, INHNO inhno, FP int_entry)
{
	_kernel_inh_handler_tbl[inhno] = (void (*)(void))(int_entry);
}

/*
 *  割込み要求ラインの属性設定（config_int）
 *
 *  initialize_interruptから各intinib分呼ばれる。TA_ENAINT指定時は割込みを
 *  許可する（enable_intが_kernel_intenable_maskとハードのINTENABLEを更新）。
 *  XtensaのCPU割込みレベルはintno毎にハードウェア固定のためintpriによる
 *  レベル設定は行わない。
 */
void
config_int(PCB *p_my_pcb, INTNO intno, ATR intatr, PRI intpri, uint_t affinity)
{
	_kernel_cfgint_mask[get_my_prcidx()] |= (1UL << intno);
	if ((intatr & TA_ENAINT) != 0U) {
		enable_int(intno);
	}
	else {
		disable_int(intno);
	}
}
