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
 *		プロセッサ依存モジュール（Xtensa用）
 *
 *  このインクルードファイルは，target_config.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 *
 *  本ファイルはFMP3のarm_m_gcc（Cortex-M）移植を土台に、esp32_s3プロジェクト
 *  、
 *  コンテキストスイッチ・Window例外統合に関わる部分（do_dispatch等）は
 *  スタブ実装とし、次の作業単位で本実装する（詳細はdesign.md参照）。
 */

#ifndef TOPPERS_CORE_KERNEL_IMPL_H
#define TOPPERS_CORE_KERNEL_IMPL_H

/*
 *  型キャストを行うマクロの定義
 */
#ifndef CAST
#define CAST(type, val)     ((type)(val))
#endif /* CAST */

#ifndef TOPPERS_MACRO_ONLY

#include <core_pcb.h>

#ifdef TOPPERS_ESP32S3
/*
 *  chip_rom_libc.c。activate_context()（本ヘッダ末尾）から呼ぶ。
 *  本ヘッダの位置ではTCBがまだ未定義（task.hはkernel_impl.hのより後で
 *  読まれる）ため、struct task_control_blockの前方宣言で受ける。
 */
struct task_control_block;
extern void chip_rom_libc_reent_init(struct task_control_block *p_tcb);
#endif /* TOPPERS_ESP32S3 */

/*
 *  ターゲット依存のオブジェクト属性
 */
#define TA_DIRECT     UINT_C(0x01) /* CPU 例外ハンドラの直接呼び出し */
#define TARGET_EXCATR (TA_DIRECT)  /* ターゲット定義のCPU例外ハンドラ属性 */
#define TARGET_INHATR TA_NONKERNEL /* ターゲット定義の割込みハンドラ属性 */
#define TARGET_TSKATR (TA_FPU)     /* ターゲット定義のタスク属性
                                    * （TA_FPUはcore_kernel.hで定義。task.trbの
                                    *  tskatr検証でマスク許容される） */

/*
 *  エラーチェック方法の指定
 *
 *  Xtensa windowed ABIはスタックポインタを16byte境界に配置する必要が
 *  ある（ARM-Mの8byteより厳しい。core_stddef.h参照）。
 */
#define CHECK_STKSZ_ALIGN	16	/* スタックサイズのアライン単位 */
#define CHECK_FUNC_ALIGN	1	/* 関数のアライン単位 */
#define CHECK_FUNC_NONNULL		/* 関数の非NULLチェック */
#define CHECK_STACK_ALIGN	16	/* スタック領域のアライン単位 */
#define CHECK_STACK_NONNULL		/* スタック領域の非NULLチェック */
#define CHECK_MPF_ALIGN		4	/* 固定長メモリプール領域のアライン単位 */
#define CHECK_MPF_NONNULL		/* 固定長メモリプール領域の非NULLチェック */
#define CHECK_MB_ALIGN		4	/* 管理領域のアライン単位 */

/*
 *  非タスクコンテキスト用のスタック初期値
 */
#define TOPPERS_ISTKPT(istk, istksz) ((STK_T *)((char *)(istk) + (istksz)))

/*
 *  タスクコンテキストブロックの定義
 *
 *  windowed ABIでのタスクコンテキストは、レジスタウィンドウの内容を
 *  タスク自身のスタックへスピル済みの状態で保持する（スタックポインタ
 *  のみで復元可能）ため、ARM-M版と同様に sp のみで足りる。
 *
 *  FPU（Xtensa LX7コプロセッサCP0）コンテキストはeager保存方式：
 *  TA_FPU属性のタスク（fpu_flag=1、activate_contextで設定）は、
 *  ディスパッチャ（core_support.Sのdispatcher_1/dispatcher_2）が
 *  コンテキストスイッチ毎にf0〜f15＋FCR＋FSR（計72バイト＝
 *  XCHAL_CP0_SA_SIZE）をfpu_areaへ常時保存・復元する。レイアウトは
 *  ESP-IDF（tie.hのXCHAL_CP0_SA_LIST）と同じ [FCR, FSR, f0..f15]。
 *  fpu_area[]のオフセットはcore_offset.trb/core_sym.defでasmへ渡す。
 */
#define TSKCTXB_FPU_AREA_WORDS  18  /* FCR+FSR+f0..f15（72バイト） */

typedef struct task_context_block {
	uint32_t    *sp;      /* スタックポインタ */
	intptr_t    pc;       /* プログラムカウンタ（初期化用） */
	uint32_t    *stk_top; /* アクセス高速化のためのTINIBのコピー（cfgツールの
	                        * 汎用オフセット診断テーブル生成のため必要。現時点
	                        * では未使用） */
	uint32_t    fpu_flag; /* FPUコンテキスト保存・復元の要否（TA_FPU属性） */
	uint32_t    fpu_area[TSKCTXB_FPU_AREA_WORDS];
	                      /* FPU保存領域 [FCR, FSR, f0..f15] */
#ifdef TOPPERS_ESP32S3
	/*
	 *  ROM newlib用のタスク毎リエントラント構造体（chip_rom_libc.c）
	 *
	 *  ★ここより後ろに置くこと：fpu_flag/fpu_areaのオフセットは
	 *    core_sym.def/core_offset.trb経由でcore_support.Sのアセンブラが
	 *    参照する。末尾に追加する限りアセンブラ側のオフセットは不変で、
	 *    ディスパッチャの変更は一切不要（＝arch層アセンブラ非改変）。
	 *
	 *  ★★型を struct _reent / struct _rand48 で書かず、**不透明な領域**
	 *    として確保する理由：
	 *    <sys/reent.h> は newlib の <assert.h> を推移的にincludeし、
	 *    それが TOPPERS の assert()（t_stddef.h:242）を**newlib版で上書き
	 *    する**。本ヘッダは全カーネルソースから読まれるため、includeすると
	 *    カーネル全体のassert()がnewlib版に化け、__assert_func→abort→
	 *    _exit が未定義参照になってリンクできなくなる（実測：task.o/
	 *    startup.o/mutex.o等が一斉に__assert_funcを参照するようになる）。
	 *    → libcのヘッダはchip_rom_libc.cの中だけに閉じ込め、共通ヘッダ
	 *      には持ち込まない。実体の型付けはchip_rom_libc.c側で行い、
	 *      サイズ・アラインの一致は同ファイルのコンパイル時アサートで
	 *      担保する（ツールチェーン更新で_reentのサイズが変わったら
	 *      無音の破壊ではなくビルドエラーになる）。
	 *
	 *  uint64_tで確保するのはアラインのため（struct _rand48は
	 *  unsigned long long _rand_next を持つため8バイト境界が要る）。
	 *
	 *  rand48はreent._r48の実体。ROM rand()は _r48==NULL なら malloc(24)
	 *  するが、本ポートのカーネルテストビルドはヒープを持たない
	 *  （mallocを参照すると_sbrk_r等のnewlib syscallスタブ群が芋づるで
	 *   要求されリンクできない。test_ovr/rand_stub.cのコメント参照）。
	 *  そこで_r48を静的に与え、ROMがmallocを呼ぶ経路自体を発生させない。
	 *
	 *  無印ESP32(LX6)ではこの拡張を行わない：LX6側はesp/shim/
	 *  wifi_stubs.c が単一_reent共有の__getreent()を提供しており（W1〜W3で
	 *  動作実績あり）、TSKCTXBを膨らませる必要がない。共通ヘッダだが
	 *  チップで条件分岐させ、LX6のTCBレイアウトを一切変えない（非回帰）。
	 */
#define TSKCTXB_REENT_DWORDS   30  /* sizeof(struct _reent)  == 240 */
#define TSKCTXB_RAND48_DWORDS   3  /* sizeof(struct _rand48) ==  24 */
	uint64_t    reent[TSKCTXB_REENT_DWORDS];   /* per-task struct _reent */
	uint64_t    rand48[TSKCTXB_RAND48_DWORDS]; /* reent._r48 の実体 */
#endif /* TOPPERS_ESP32S3 */
} TSKCTXB;

/*
 *	タスク初期化コンテキストブロック
 */
#define USE_TSKINICTXB	/*  TSKINICTXBを使用する  */

typedef struct task_initialization_context_block {
	uint32_t    *stk_top;    /* スタック領域の末尾番地 */
	uint32_t	*stk_bottom; /* スタック領域の先頭番地 */
} TSKINICTXB;

#ifndef TOPPERS_MACRO_ONLY

/*
 *  タスク初期化コンテキストブロックの初期化（動的生成用）
 *
 *  動的オブジェクト生成（acre_tsk）がカーネル側（kernel/task_manage.c:197）
 *  から呼ぶ。arch 依存なので移植側が用意する
 *  （**fmp3_core には Xtensa を入れない**という契約による）。
 *
 *  規約は静的生成側と一致していなければならない:
 *    静的（cfg 生成の TINIB 初期化子）  stk_top = 配列の先頭
 *                                      stk_bottom = 先頭 + ROUND_STK_T(サイズ)
 *    本関数                            stk_top = stk
 *                                      stk_bottom = stk + stksz
 *  ⇒ 動的タスクと静的タスクで TSKINICTXB のレイアウトが同一になる。
 *  ここがずれると「動くが壊れている」型の欠陥になるので、
 *  TINIB の生成規約を変えたら本関数も併せて確認すること。
 *
 *  実装は arm_m_gcc/common/core_kernel_impl.h:127-134 と同一（構造体も同一）。
 */
Inline void
init_tskinictxb(TSKINICTXB *p_tskinictxb, size_t stksz, STK_T *stk)
{
	p_tskinictxb->stk_top = (uint32_t *)(stk);
	p_tskinictxb->stk_bottom = (uint32_t *)((char *)(stk) + (stksz));
}

/*
 *  TA_MEMALLOC で確保したスタック領域の先頭番地（free_mpk へ渡す値）
 *
 *  `stk_top` が「確保の先頭」であることに依存する（上記の規約どおり）。
 *  del_tsk（kernel/task_manage.c:274）が free_mpk へ渡す。
 */
Inline void *
tskinictxb_memalloc_ptr(TSKINICTXB *p_tskinictxb)
{
	return((void *)(p_tskinictxb->stk_top));
}

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  CPUロック状態の制御
 *
 *  PS.INTLEVELを最高レベル（15）に設定することで、NMI相当を除く
 *  すべての割込みを禁止する（core_sil.hのTOPPERS_disint/enaintと
 *  同じ考え方）。
 */
Inline bool_t
sense_lock(void)
{
	uint32_t ps;
	Asm("rsr.ps  %0" : "=a"(ps));
	return (bool_t)((ps & 0xFU) != 0U);   /* PS.INTLEVELが0でなければロック中 */
}

/*
 *  割込みネストカウンタ（非タスクコンテキスト判定用）
 *
 *  Xtensaには「割込みハンドラ実行中か」をハードウェアで直接判定する
 *  手段（ARM-MのIPSR等）が無いため、割込みハンドラの入場でインクリ
 *  メント・退場でデクリメントするソフトウェアカウンタで代替する
 *  （Zephyr/NuttXのnestedカウンタと同じ方式）。実体はcore_kernel_impl.c。
 *
 *  本作業単位は単一コアのみ対象のためグローバル変数とする。PCBの
 *  フィールドにしないのは、core_kernel_impl.hがPCB完全型が確定する
 *  pcb.hより前にincludeされ（kernel_impl.hのinclude順）、PCBメンバ
 *  参照ができないため。SMP対応時はプロセッサ毎の配列／PCBフィールド
 *  へ移す（要include順の再検討）。
 */
extern volatile uint_t _kernel_xtos_intnest[TNUM_PRCID];

/*
 *  自コアでCFG_INT設定済みの割込み番号マスク（定義はcore_kernel_impl.c、
 *  詳細コメントはcheck_intno_cfg参照）。check_intno_cfg等がE_OBJ判定に
 *  使うため、それらより前方（利用箇所より前）に宣言する。
 */
extern volatile uint32_t _kernel_cfgint_mask[TNUM_PRCID];

/*
 *  非タスクコンテキスト（割込みハンドラ内）かどうかの判定
 *
 *  ARM-M版はPSP/MSPどちらのスタックポインタが有効かで判定するが、
 *  Xtensaにはそれに相当するハードウェア機構が無い。割込みハンドラの
 *  入場でインクリメント・退場でデクリメントするソフトウェアカウンタ
 *  で非タスクコンテキストを判定する。割込みエントリで必ず
 *  i_begin_int()/i_end_int()を対で呼ぶこと（target_timer.cのtick
 *  割込みハンドラ参照）。
 */
Inline bool_t
sense_context(PCB *p_my_pcb)
{
	return (_kernel_xtos_intnest[get_my_prcidx()] > 0U);
}

/*
 *  割込みハンドラ入場・退場（非タスクコンテキストの設定／解除）
 *
 *  ROM常駐のXTOSディスパッチャ経由で呼ばれるC割込みハンドラの先頭・
 *  末尾で対にして呼び出し、sense_context()が割込み中にtrueを返すように
 *  する。
 */
Inline void
i_begin_int(void)
{
	_kernel_xtos_intnest[get_my_prcidx()] += 1U;
}

Inline void
i_end_int(void)
{
	_kernel_xtos_intnest[get_my_prcidx()] -= 1U;
}

#ifdef TOPPERS_S3_BT_L3LAT_DIAG
/*
 *  ★BT-4診断計装（既定OFF）：rsil>=3マスク窓（Level-3のBT割込みを
 *  遅延させる区間）の滞在時間計測フック。実体はesp/shim/esp_shim.c
 *  （同ファイル冒頭の設計メモ参照）。CPUロック区間はgiant lockのスピン
 *  待ちを含むため、コア1のカーネル活動による間接遅延もここで観測できる。
 */
extern void l3ld_lock_hook(uint32_t oldps, uint32_t ra);
extern void l3ld_unlock_hook(uint32_t newps);
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */

Inline void
lock_cpu(void)
{
	uint32_t dummy;
	Asm("rsil  %0, 15" : "=a"(dummy) :: "memory");
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_lock_hook(dummy, (uint32_t) __builtin_return_address(0));
#endif
}

Inline void
unlock_cpu(void)
{
	/*
	 * `wsr.ps 0`でPS全体を0クリアすると、PS.WOE（Window Overflow
	 * Enable，bit18）まで0になってしまう。Windowed ABIでは以後の
	 * 関数呼び出しでウィンドウが足りなくなった際にWindow Overflow
	 * 例外が発生しなくなり（WOE=0だと検出自体が無効化される）、
	 * レジスタウィンドウが黙って壊れる。`rsil`はPS.INTLEVELのみを書き換え
	 * WOE等の他ビットを保持するため、lock_cpu()と対称にrsilで
	 * INTLEVEL=0を設定する。
 */
	uint32_t dummy;
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	l3ld_unlock_hook(0U);	/* マスク解除の直前に窓を締める */
#endif
	Asm("rsil  %0, 0" : "=a"(dummy) :: "memory");
}

/*
 *  ディスパッチ要求（割込み処理からの復帰時のディスパッチ）
 *
 *  no-opのままでよい。自前の割込みエントリ_kernel_l1int_entry
 *  （core_support.S）のイグジット経路が、外側割込みからの復帰時に
 *  毎回p_runtsk/p_schedtskを直接比較して遅延ディスパッチを行うため、
 *  ここで別途「ディスパッチ保留」フラグを立てる必要はない
 *   §8）。
 */
Inline void
request_dispatch_retint(void)
{
}

/*
 *  割込み待ちのための短い遅延
 *
 *  本作業単位では未使用のためno-op（実際の割込み待ちループが必要に
 *  なった時点で実装する）。
 */
Inline void
delay_for_interrupt(void)
{
}

/*
 *  ディスパッチ保留状態でのロック制御
 *
 *  本作業単位ではディスパッチ本体を実装しないため，通常のlock_cpu/
 *  unlock_cpuと同じ実装とする（コンテキストスイッチ実装時に見直す）．
 */
#define lock_cpu_dsp()    lock_cpu()
#define unlock_cpu_dsp()  unlock_cpu()

/*
 *  メモリバリア
 */
Inline void
memory_barrier(void)
{
	Asm("memw" ::: "memory");
}

/*
 *  最大割込み番号／割込み優先度関連定数
 *
 *  Xtensaの割込み番号はINTENABLE/INTERRUPTレジスタのビット位置
 *  （0〜31）に対応する。IPMの段階制御は3段階（level 1..3）で実装済み
 *  （t_set_ipm/vpriマスク、2026-07-20）。TIPM_LOCK/TBITW_IPRIはcfgツールの
 *  汎用オフセット診断テーブル生成にも使われる。
 */
#define TMIN_INTNO    UINT_C(0)
#define TMAX_INTNO    31
#define TIPM_LOCK     TMIN_INTPRI
#define TBITW_IPRI    4

/*
 *  割込み優先度マスクの制御
 *
 *  IPMの段階制御（level 1..3）を vpri マスク方式で実装している（t_set_ipm。
 *  TIPM_ENAALL=全許可、-1/-2/-3 で level 1 / 1-2 / 1-3 を禁止。
 *  。
 *
 *  IPMはper-coreソフトウェア変数 _kernel_ipm[] で保持する（ARM-Mの
 *  BASEPRI相当）。CPUロック（lock_cpu/unlock_cpu、PS.INTLEVELを操作）
 *  とは独立に管理する必要がある：カーネルサービスは内部でlock_cpuして
 *  から t_get_ipm を呼ぶため、PS.INTLEVEL（sense_lock）でIPMを判定すると
 *  常にロック中に見えてしまう。よってIPMは専用変数で持ち、t_get_ipmは
 *  それを返す（タスクコンテキストの既定はTIPM_ENAALL）。
 */
extern volatile PRI _kernel_ipm[TNUM_PRCID];

/*
 *  IPMに対応するINTENABLE仮想優先度マスク（実体はcore_kernel_impl.c）と、
 *  自コア許可マスク（実体・詳説は後方の宣言参照。ここではt_set_ipmから
 *  参照するため前方宣言する。同一arrayの重複extern宣言はCで合法）。
 *  INTENABLEへ書く実効値は常に _kernel_intenable_mask[c] & _kernel_vpri_mask[c]。
 */
extern volatile uint32_t _kernel_vpri_mask[TNUM_PRCID];
extern volatile uint32_t _kernel_intenable_mask[TNUM_PRCID];

/*
 *  Xtensa割込みレベルマスク（XCHAL_INTLEVEL<n>_MASK）。S3/LX6とも
 *  core-isa.hで同値であることを確認済み:
 *    L1=0x000637FF L2=0x00380000 L3=0x28C08800、XCHAL_EXCM_LEVEL=3。
 *  トゥールチェーン付属の汎用core-isa.hは#include_nextでチップ固有ヘッダへ
 *  委譲するがそのinclude pathが常には無いため、ヘッダ依存を避けて自前定義
 *  する（target_timer.h・core_support.Sと同方針）。
 */
#define CORE_INTLEVEL1_MASK   UINT32_C(0x000637FF)
#define CORE_INTLEVEL2_MASK   UINT32_C(0x00380000)
#define CORE_INTLEVEL3_MASK   UINT32_C(0x28C08800)

/*
 *  IPM値（0..TMIN_INTPRI＝0..-3）から、許可すべき割込みのINTENABLEマスク
 *  （allowマスク）を導く。ipm=-N は level 1..N を禁止する（FMP3のIPM契約
 *  「閾値intpri以下の優先度を禁止」を Xtensa の intpri=-level 対応で写す。
 *   §5）。
 */
Inline uint32_t
_kernel_ipm_to_allow(PRI ipm)
{
	uint32_t masked = 0U;
	if (ipm <= -1) { masked |= CORE_INTLEVEL1_MASK; }
	if (ipm <= -2) { masked |= CORE_INTLEVEL2_MASK; }
	if (ipm <= -3) { masked |= CORE_INTLEVEL3_MASK; }
	return ~masked;		/* allow = 禁止レベル以外 */
}

/*
 *  ★**規約：IPMはCPUロックと独立のソフトウェア状態であり、t_set_ipmは
 *  状態の記録のみを行い、PS.INTLEVELには触れない。** 実際の割込み許可は
 *  呼出し元サービスコール末尾のunlock_cpu（rsil 0）が行う
 *  （ARM-MでBASEPRI書込みがPRIMASK=1中に行われるのと同じ構図）。
 *
 *  ★ここでINTLEVELを0へ落とすと、glock保持中に割込み窓が開き、tick割込みの
 *  signal_time()が自コアの保持するglockを取りに行って**自己デッドロック**する。
 *  SMPでのみ顕在化する（単一コアのglockはスピンしないフラグ実装のため無害）。
 *
 *  IPMの実マスクはvpri方式で行う。ESP-IDFの_xt_vpri_maskと同型に、
 *  IPMを INTENABLE の仮想優先度マスクとして適用する：INTENABLEへ
 *  書く実効値を常に _kernel_intenable_mask[c] & _kernel_vpri_mask[c] とする。
 *  有効なIPMは -3..0 の4値（TMIN_INTPRI=-3、level 1..3 に対応）。
 *
 *  安全性：t_set_ipm はタスクコンテキストからのみ呼ばれる（set_dspflg 経由の
 *  chg_ipm/ena_dsp/ext_tsk。いずれも lock_cpu() 下＝PS.INTLEVEL=15）。INTLEVEL=15
 *  中の INTENABLE 書込みは割込みを配送しないため glock 保持中でも安全で、
 *  PS.INTLEVEL には触れないので上記2026-07-10の自己デッドロックは再発しない。
 *  万一ISRコンテキスト（割込み入口が INTENABLE=0 で再入防止中）で呼ばれても
 *  再入を開かないよう、非ISR（_kernel_xtos_intnest==0）のときのみハードへ書く（保険）。
 *  ISR中に設定されたIPMは割込み出口の復元（core_support.S）が適用する。
 */
Inline void
t_set_ipm(PRI ipm)
{
	uint_t c = get_my_prcidx();
	uint32_t allow = _kernel_ipm_to_allow(ipm);

	_kernel_ipm[c] = ipm;
	_kernel_vpri_mask[c] = allow;
	/* ISRコンテキスト（割込み入口がINTENABLE=0で再入防止中）では書かない。
	   そこで書くと再入を開いてしまう。ISR中のIPMは割込み出口の復元が適用する。
	   非ISR（タスク文脈）でのみ即時適用する。 */
	if (_kernel_xtos_intnest[c] == 0U) {
		Asm("wsr.intenable  %0 ; rsync"
			:: "a"(_kernel_intenable_mask[c] & allow) : "memory");
	}
}

Inline PRI
t_get_ipm(void)
{
	return _kernel_ipm[get_my_prcidx()];
}

/*
 *  割込み属性の設定のチェック
 *
 *  当初はARM-M版のようなCFG_INTビットパターンテーブル
 *  を持たない設計のため「常にtrue」の簡略実装だった。しかしTTSP3の
 *  APIテストは「CFG_INT未設定の割込み番号を指定するとE_OBJが返る」こと
 *  を期待しており（dis_int/ena_int/clr_int/ras_int/prb_intのNGKI3098等）、
 *  常にtrueだとこの検証ができず多数のAPIテストが失敗していた。
 *  config_int（core_kernel_impl.c）が設定時にセットする
 *  _kernel_cfgint_maskを参照して正しく判定する。
 */
Inline bool_t
check_intno_cfg(INTNO intno)
{
	return (bool_t) ((_kernel_cfgint_mask[get_my_prcidx()] & (1UL << intno)) != 0U);
}

Inline bool_t
check_intno_clear(INTNO intno)
{
	return (bool_t) ((_kernel_cfgint_mask[get_my_prcidx()] & (1UL << intno)) != 0U);
}

Inline bool_t
check_intno_raise(INTNO intno)
{
	return (bool_t) ((_kernel_cfgint_mask[get_my_prcidx()] & (1UL << intno)) != 0U);
}

/*
 *  割込みの要求（ソフトウェアからの強制発生）
 *
 *  XtensaのINTSET特殊レジスタへ該当ビットを書くと割込み要求（INTERRUPT
 *  レジスタのビット）がセットされる。書込みで要求をセットできるのは
 *  ソフトウェア割込み・外部エッジ割込みのみ（ESP32-S3のINT7/INT29等の
 *  ソフトウェア割込み。XCHAL_INTTYPE_MASK_SOFTWARE）。ras_int/iras_intの実体。
 */
Inline void
raise_int(INTNO intno)
{
	uint32_t bit = (1UL << intno);
	Asm("wsr.intset  %0 ; rsync" :: "a"(bit) : "memory");
}

/*
 *  自コアで許可中のLevel-1割込みマスクの保持変数（実体はcore_kernel_impl.c）
 *
 *  _kernel_l1int_entry（core_support.S）は割込みディスパッチ中にINTENABLEを
 *  一旦0にし、復帰時にこのマスクへ戻す。tick/IPIに加えCFG_INT/ena_intで
 *  許可した割込み（例：test_int1のINT7）の許可状態をtickをまたいで保持する
 *  ため、ハードのINTENABLEと対で本ソフトウェアマスクを更新する。
 *  初期値はtick(INT6)＋（SMP時）IPI(INT13)。
 */
extern volatile uint32_t _kernel_intenable_mask[TNUM_PRCID];

/*
 *  LCOUNT暴走ガードの診断用per-core変数（実体はcore_kernel_impl.c）
 *
 *  実験W：core_support.Sの.Lret_do_restoreが
 *  異常なLCOUNT（閾値0x00100000以上）を検出して0へ置換した際に、ヒット
 *  回数・異常値・復帰先PC（中断PC）を記録する。診断recorder（diag_recorder.c）
 *  やアプリの周期出力から非ゼロ時のみ表示する用途を想定。
 */
extern volatile uint32_t _kernel_lcount_guard_cnt[TNUM_PRCID];
extern volatile uint32_t _kernel_lcount_guard_val[TNUM_PRCID];
extern volatile uint32_t _kernel_lcount_guard_pc[TNUM_PRCID];

/*
 *  割込み要求ライン単位の許可／禁止
 *
 *  XtensaのINTENABLE特殊レジスタ（1ビット/割込み番号）を操作する。合わせて
 *  自コアの許可マスク_kernel_intenable_maskも更新し、割込みディスパッチ復帰時
 *  の復元（core_support.S）で許可状態が失われないようにする。ROM提供のXTOS
 *  API（_xtos_ints_on等、tick/IPI初期化で使用）は内部的にINTENABLEを操作する。
 */
Inline void
disable_int(INTNO intno)
{
	uint_t c = get_my_prcidx();
	_kernel_intenable_mask[c] &= ~(1UL << intno);
	/* 実効値＝許可マスク & IPM許可マスク（vpri）。IPM保持中のマスクを崩さない。
	   dis_int/ena_intはCHECK_TSKCTXを持たずISR文脈からも呼べる。ISR文脈
	   （intnest>0、入口がINTENABLE=0で再入防止中）でハードへ書くと、unlock_cpu後に
	   全ソフトマスクが復活して再入（ネスト）を開いてしまうため書かない。ソフトマスクの
	   更新は反映され、割込み出口の復元がvpri込みで適用する（t_set_ipmと同じ規約）。 */
	if (_kernel_xtos_intnest[c] == 0U) {
		Asm("wsr.intenable  %0 ; rsync"
			:: "a"(_kernel_intenable_mask[c] & _kernel_vpri_mask[c]) : "memory");
	}
}

Inline void
enable_int(INTNO intno)
{
	uint_t c = get_my_prcidx();
	_kernel_intenable_mask[c] |= (1UL << intno);
	/* 実効値＝許可マスク & IPM許可マスク（vpri）。IPM保持中の ena_int が
	   マスク済み割込みを復活させないようANDする（ESP-IDFの and a5,a6 と同じ）。
	   ISR文脈では書かない（disable_int同様の理由。出口復元が適用）。 */
	if (_kernel_xtos_intnest[c] == 0U) {
		Asm("wsr.intenable  %0 ; rsync"
			:: "a"(_kernel_intenable_mask[c] & _kernel_vpri_mask[c]) : "memory");
	}
}

Inline void
clear_int(INTNO intno)
{
	/*
	 *  XtensaのINTCLEAR特殊レジスタへ該当ビットを書くと割込み要求
	 *  （INTERRUPTレジスタのビット）がクリアされる。クリアできるのは
	 *  ソフトウェア割込み・外部エッジ割込みのみ。レベル検出のペリフェラル
	 *  割込みは要因レジスタ側でクリアする必要がありチップ依存部で対応する
	 *  （本ポートで使うCCOMPARE0(tick)はWSR.CCOMPARE0が要因クリアを兼ねる）。
	 */
	uint32_t bit = (1UL << intno);
	Asm("wsr.intclear  %0 ; rsync" :: "a"(bit) : "memory");
}

Inline bool_t
probe_int(INTNO intno)
{
	uint32_t stat;
	Asm("rsr.interrupt  %0" : "=a"(stat));
	return (bool_t)((stat & (1UL << intno)) != 0U);
}

/*
 *  割込み番号の有効範囲チェック
 */
#define VALID_INTNO(prcid, intno) \
	((TMIN_INTNO <= INTNO_MASK(intno)) && (INTNO_MASK(intno) <= TMAX_INTNO))
#define VALID_INTNO_DISINT(prcid, intno)  VALID_INTNO(prcid, intno)
#define VALID_INTNO_CLRINT(prcid, intno)  VALID_INTNO(prcid, intno)
#define VALID_INTNO_RASINT(prcid, intno)  VALID_INTNO(prcid, intno)
#define VALID_INTNO_PRBINT(prcid, intno)  VALID_INTNO(prcid, intno)
#define VALID_INTNO_CFGINT(prcid, intno)  VALID_INTNO(prcid, intno)

/*
 *  割込み要求ラインの属性設定（config_int）（core_kernel_impl.c）
 *
 *  CFG_INTで設定した割込み要求ラインをinitialize_interruptから設定する。
 *  TA_ENAINT指定時は割込みを許可する（XtensaのCPU割込みレベルはintno毎に
 *  ハードウェア固定のためintpriによるレベル設定は不要）。
 */
extern void config_int(PCB *p_my_pcb, INTNO intno, ATR intatr,
											PRI intpri, uint_t affinity);

/*
 *  割込みハンドラの静的登録（define_inh）（core_kernel_impl.c）
 *
 *  VECBASEは変更せず、ROM汎用ベクタ→_kernel_l1int_entry→_kernel_l1int_dispatch
 *  の経路で割込みをディスパッチする。define_inhはCRE_ISR/CFG_INTで生成された
 *  割込みハンドラ(_kernel_inthdr_N)をintno別ハンドラ表へ登録し、demuxが
 *  ペンディング割込みに対応するハンドラを呼び出す。tick(INT6)/IPI(INT13)は
 *  CFG_INTを経ずtarget_timer.c/chip_ipi.cで個別処理する。
 */
extern void define_inh(PCB *p_my_pcb, INHNO inhno, FP int_entry);

/*
 *  割込みハンドラの出入口処理の生成マクロ
 *
 *  全割込みが_kernel_l1int_entry経由でディスパッチされるため、INHINIBの
 *  int_entryには生成された割込みハンドラ(_kernel_inthdr_N)をそのまま格納し、
 *  define_inhがintno→ハンドラ表へ登録する。
 */
#define INT_ENTRY(inhno, inthdr)                 inthdr
#define INTHDR_ENTRY(inhno, inhno_num, inthdr)   extern void inthdr(void);

/*
 *  intno別のLevel-1割込みハンドラ表（実体はcore_kernel_impl.c）。
 *  _kernel_l1int_dispatch（target_timer.c）がペンディング割込みに対して参照する。
 */
extern void (*_kernel_inh_handler_tbl[TMAX_INTNO + 1])(void);

/*
 *  CPU例外ハンドラ番号（EXCNO）の範囲
 *
 *  XtensaのEXCCAUSEレジスタ値（0〜63）をCPU例外ハンドラ番号として用いる。
 *  グローバルには (prcid<<16)|EXCCAUSE で符号化されるため、範囲チェックは
 *  下位（EXCCAUSE部）に対して行う。
 */
#define TMIN_EXCNO    UINT_C(0)
#define TMAX_EXCNO    UINT_C(63)
/* EXCNO_MASK(excno)（下位16bit=EXCCAUSE抽出）はkernel_impl.hで定義済み */

/*
 *  DEF_EXCで使用できるCPU例外ハンドラ番号かのチェック
 */
#define VALID_EXCNO_DEFEXC(excno) \
	((TMIN_EXCNO <= EXCNO_MASK(excno)) && (EXCNO_MASK(excno) <= TMAX_EXCNO))

/*
 *  CPU例外ハンドラの出入口処理の生成マクロ
 *
 *  Xtensaは全CPU例外がcore_exc_entry（core_support.S）経由でディスパッチ
 *  されるため、EXCINIBのexc_entryには登録するCハンドラ（exchdr）をそのまま
 *  格納し、define_excでEXCCAUSE→Cハンドラの対応表に登録する（arm_m方式）。
 */
#define EXC_ENTRY(excno, exchdr)              exchdr
#define EXCHDR_ENTRY(excno, excno_num, exchdr)    extern void exchdr(void *p_excinf);

/*
 *  CPU例外ハンドラの静的登録（core_kernel_impl.c）
 *
 *  EXCCAUSE→Cハンドラ対応表への登録と、ROMの例外ハンドラテーブルへの
 *  共通入口core_exc_entryの登録を行う。initialize_exception（kernel）から
 *  各DEF_EXC分呼ばれる。
 */
extern void define_exc(EXCNO excno, FP exc_entry);

/*
 *  CPU例外ハンドラのディスパッチ（core_exc_entryから呼ばれる）
 */
extern void _kernel_exc_dispatch(void *p_excinf);

/*
 *  CPU例外の発生した時のコンテキストの参照
 *
 *  発生時が非タスクコンテキスト（割込みハンドラ内等）ならtrue。
 *  core_exc_entryがフレームへ保存した発生時点の割込みネスト値を見る。
 */
Inline bool_t
exc_sense_context(void *p_excinf)
{
	return (((const T_EXCINF *)(p_excinf))->exncnt != 0U);
}

/*
 *  CPU例外の発生した時の割込み優先度マスクの参照
 *
 *  タスクコンテキストで発生した場合にのみ意味を持つ（非タスク時は
 *  正しい値を返す必要はない）。core_exc_entryが保存した発生時の
 *  _kernel_ipm値を返す。
 */
Inline PRI
exc_get_intpri(void *p_excinf)
{
	return (PRI)(((const T_EXCINF *)(p_excinf))->intpri);
}

/*
 *  CPU例外の発生した時にCPUロック状態または全割込みロック状態だったかの参照
 *
 *  Xtensaでは発生時PSのINTLEVELが0でなければCPUロック（またはカーネル内
 *  クリティカルセクション実行中）とみなす。
 */
Inline bool_t
exc_sense_lock(void *p_excinf)
{
	return ((((const T_EXCINF *)(p_excinf))->ps & 0xFU) != 0U);
}

/*
 *  CPU例外の発生した時のコンテキストと割込みのマスク状態の参照
 *
 *  発生時が、カーネル実行中でなく、タスクコンテキストであり、全割込み
 *  ロック状態でなく、CPUロック状態でなく、割込み優先度マスク全解除状態
 *  である時にtrue、そうでない時にfalseを返す（xsns_dpnから参照される）。
 */
Inline bool_t
exc_sense_intmask(void *p_excinf)
{
	return (!exc_sense_context(p_excinf)
				&& (exc_get_intpri(p_excinf) == TIPM_ENAALL)
				&& !exc_sense_lock(p_excinf));
}

/*
 *  ディスパッチャ・コンテキストスイッチ関連
 *
 *  実体はcore_support.S（アセンブリ）。dispatch_and_migrate/
 *  exit_and_migrate（自タスクのマイグレーション、2026-07-10実装）も
 *  同ファイル。do_dispatchはARM-M版内部名で本ポートでは実体を持たない。
 */
extern void   core_initialize(PCB *p_my_pcb);
extern void   core_terminate(void);
extern void   dispatch(void);
extern void   do_dispatch(void);
extern void   start_dispatch(void);
extern void   exit_and_dispatch(void);
extern void   dispatch_and_migrate(PCB *p_my_pcb, TCB *p_selftsk);
extern void   exit_and_migrate(PCB *p_my_pcb, TCB *p_selftsk) NoReturn;
extern void   call_exit_kernel(PCB *p_my_pcb) NoReturn;
extern void   default_exc_handler(void *p_excinf);
extern void   default_int_handler(void);

/*
 *  新規タスク起動トランポリン（core_support.S）
 */
extern void   start_r(void);

/*
 *  自前のLevel-1割込みエントリ（core_support.S）。
 *  target_hrt_initialize()で xtos_exc_handler_table[EXCCAUSE_LEVEL1_INTERRUPT]
 *  へ登録し、割込み復帰時プリエンプションを実現する
 *  。
 */
extern void   _kernel_l1int_entry(void);

/*
 *  タスク起動時のコンテキスト設定
 *
 *  新規タスクは、スタックポインタをスタック領域の末尾（高位番地。Xtensaの
 *  スタックは下方伸長）に、復帰PCをstart_rトランポリンに設定して起動する。
 *  TSKINICTXBのstk_bottomがスタック領域末尾（stk + stksz）を保持する
 *  （core_kernel.trbのGenerateTskinictxb参照）。SPは16バイト境界に整える。
 *
 *  FPU（eager保存）：TA_FPU属性ならfpu_flagを立て、fpu_areaをゼロ初期化
 *  する（初回ディスパッチでf0〜f15=0.0/FCR=0（丸め:最近接）/FSR=0が
 *  ロードされる。再起動時に前回起動時のFPU状態を引き継がないため）。
 *
 *  最外フレームのベース退避領域初期化（実機必須。USJ+BT/WiFi blobハング
 *  調査で確定、2026-07-12）：start_rはentryを実行せずjxでタスク本体へ
 *  直接渡る（core_support.S）ため、タスク本体の最初のentryが作る窓の
 *  「最外フレーム」はSP=stk_bottomそのものになる。start.Sの_start同様、
 *  Window Overflowハンドラ（ROM/自前とも標準コード）は退避対象フレームの
 *  [SP-12]をl32eで読み「呼出し元フレームのSP」として使うため、ここが
 *  未初期化のままだと、タスク実行が深くネストしてWindow Overflowがこの
 *  最外フレームまで達した時にゴミを辿ってスピル先が不正アドレスになり
 *  StoreProhibitedで破綻する（実機で確認：EXCCAUSE=29／EXCVADDR=0x60、
 *  ROM memcpy内のZOLループ中の窓スピルで発火。USJコンソール定義単体の
 *  コードサイズ/配置変化がBTコントローラタスクの呼出し深さをこの閾値の
 *  境界へ押し出したために顕在化した——USJの実行時挙動やクロック/割込みは
 *  無関係。_start.S:132-137と同じ理屈・同じ対処。[stk_bottom-12] =
 *  stk_bottom（自己参照）としてスピルがスタック内の有効域に収まるように
 *  する）。
 */
#define activate_context(p_tcb) do {                                       \
    uint_t fpu_i_;                                                         \
    (p_tcb)->tskctxb.sp = (uint32_t *)                                     \
        ((uint32_t)((p_tcb)->p_tinib->tskinictxb.stk_bottom) & ~0xFU);     \
    *(uint32_t *)((uint8_t *)(p_tcb)->tskctxb.sp - 12) =                   \
        (uint32_t)(p_tcb)->tskctxb.sp;                                     \
    (p_tcb)->tskctxb.pc = (intptr_t) start_r;                              \
    (p_tcb)->tskctxb.fpu_flag =                                            \
        (((p_tcb)->p_tinib->tskatr & TA_FPU) != 0U) ? 1U : 0U;             \
    for (fpu_i_ = 0U; fpu_i_ < TSKCTXB_FPU_AREA_WORDS; fpu_i_++) {         \
        (p_tcb)->tskctxb.fpu_area[fpu_i_] = 0U;                            \
    }                                                                      \
    ACTIVATE_CONTEXT_ROM_LIBC(p_tcb);                                      \
} while (0)

/*
 *  ROM newlib用per-task _reentの初期化（ESP32-S3のみ）
 *
 *  タスク起動（初回起動・再起動とも）の度にリエントラント構造体を
 *  初期化する。再起動時に前回起動時のerrno/rand状態を引き継がないため
 *  （fpu_areaのゼロ初期化と同じ方針）。_r48は静的実体（tskctxb.rand48）
 *  を指すため、再初期化してもリークしない（＝ROMのmalloc(24)経路に
 *  入らない。ヒープを持たないカーネルテストビルドで必須）。
 *
 *  activate_contextはsta_ker以降（initialize_task/act_tsk）にしか
 *  呼ばれず、software_init_hook()のchip_rom_libc_init()（_GLOBAL_REENT
 *  のSDIDINIT設定を含む）は必ずそれより前に完了している。
 */
#ifdef TOPPERS_ESP32S3
#define ACTIVATE_CONTEXT_ROM_LIBC(p_tcb)  chip_rom_libc_reent_init(p_tcb)
#else /* TOPPERS_ESP32S3 */
#define ACTIVATE_CONTEXT_ROM_LIBC(p_tcb)  ((void) 0)
#endif /* TOPPERS_ESP32S3 */

/*
 *  コプロセッサコンテキストの退避・復帰（本作業単位では未使用）
 */
#define save_context(p_tcb)
#define release_context(p_tcb)

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CORE_KERNEL_IMPL_H */
