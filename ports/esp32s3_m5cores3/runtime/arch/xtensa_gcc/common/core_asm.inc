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
 *  @(#) $Id: core_asm.inc 289 2021-08-05 14:44:10Z ertl-komori $
 */

#ifndef TOPPERS_CORE_ASM_INC
#define TOPPERS_CORE_ASM_INC

/*
 *  セクション情報
 *
 *  ARM-M版にあった ATEXT/AGLOBAL/ALABEL 等のディレクティブ間接マクロ
 *  （Thumbモード切替等ARM固有の事情のためのラッパー）は、Xtensaでは
 *  素のGASディレクティブ（.text/.global/ラベル:）をstart.S/core_support.S
 *  で直接使うため削除した。
 */

#define DATA_START  __data_start
#define IDATA_START __idata_start
#define IDATA_END   __idata_end
#define BSS_START   __bss_start
#define BSS_END     __bss_end

/*
 *  PS（プロセッサステータス）レジスタのビット定義
 *  （Xtensa ISA。ESP-IDFのxtensa/corebits.hと同値。ヘッダ依存を避けるため
 *   コンテキストスイッチで使う分のみ自前定義する）
 */
#define PS_INTLEVEL_MASK  0x0000000F  /* 割込みレベル（CPUロックはrsilで操作） */
#define PS_EXCM           0x00000010  /* 例外モード */
#define PS_UM             0x00000020  /* ユーザ（vカーネル）モード */
#define PS_WOE            0x00040000  /* ウィンドウオーバフロー有効 */
#define PS_CALLINC_SHIFT  16          /* CALLINCフィールド（bits[17:16]） */
#define PS_CALLINC_CALL4  0x00010000  /* CALLINC=1（call4相当のウィンドウ回転） */

/*
 *  新規タスク起動時のPS値
 *
 *  ユーザモード＋ウィンドウ有効＋割込みレベル0（ロック解除）。
 *  加えてCALLINC=1を立て、start_rからjxで飛んだ先のタスクの`entry`が
 *  ウィンドウを1（=4レジスタ）回転させ、start_rのa6に置いたexinfが
 *  タスク側のa2（第1引数）に入るようにする（design.md「引数レジスタ規約」）。
 */
#define PS_START_TASK  (PS_WOE | PS_UM | PS_CALLINC_CALL4)

/*
 *  Level-3割込みエントリ（_kernel_l3int_entry）用の擬似call4 PS値。
 *
 *  ★実機検証（l3_uart_probe）でINTLEVEL=3のまま維持する初期設計が
 *  不具合を起こすことを確認したため修正。当初はsignal_time()専用の
 *  assert(!sense_lock())さえ気にすれば良いと考えINTLEVEL=3を維持した
 *  （sig_sem()等の一般サービスコールはsense_context()で分岐しsense_lock()
 *  を見ないため一見安全に見えた）が、見落としがあった：unlock_cpu()
 *  （core_kernel_impl.h）は"rsil ,0"でPS.INTLEVELを無条件に0へ落とす
 *  実装（WOEビット保護のため、save/restoreではなく絶対値設定）。
 *  sig_sem()等カーネルサービスの内部クリティカルセクション
 *  （lock_cpu()/unlock_cpu()ペア、semaphore.c等で多用）がLevel-3
 *  ディスパッチ中（INTLEVEL=3前提）に呼ばれると、unlock_cpu()が
 *  INTLEVELを3から0へ強制的に落としてしまい、まだ処理中の割込みが
 *  再入・破壊される（実機でUART割込み1回成功後に完全停止する不具合の
 *  根本原因と特定）。
 *
 *  Level-1と同じくPS_START_TASK相当（INTLEVEL=0）でCディスパッチを
 *  呼ぶことで修正する。Level-3の意義（Level-1側のマスク・処理待ちに
 *  邪魔されず即座にエントリできること）はハードウェアが実際に
 *  Level-3例外として受理した時点で既に達成されており、C側ディスパッチ
 *  自体をLevel-3のまま維持する必要は無い（Level-1も同じ理由でC側は
 *  INTLEVEL=0）。戻りは引き続きEPC3/EPS3/rfi 3を使う（RFI nはPS/PCを
 *  EPS_n/EPC_nから無条件に上書きするため、C側ディスパッチ中に
 *  INTLEVELが変化していても正しく復帰できる）。
 */
#define PS_L3INT_PSEUDO_CALL4  (PS_WOE | PS_UM | PS_CALLINC_CALL4)

/*
 *  割込みエントリ（_kernel_l1int_entry）用の定義
 */
#define PS_EXCM_BIT       0x00000010  /* PS.EXCM（例外モード） */

/*
 *  割込みフレームのオフセット（ROMの_UserExceptionVectorが確保する
 *  256バイトフレームを再利用。ROMは[16/20/24/80/84]を既に使用済み。
 *  追加分を空き位置に配置する。ウィンドウスピル時に中断窓の直上の親窓は
 *  [中断SP-16..]=[FB+0xF0..]へ書かれるので、下記(最大〜0x64)とは衝突しない）。
 */
#define F_pc         0    /* EPC1（復帰PC） */
#define F_ps         4    /* PS */
#define F_sar        8    /* SAR */
#define F_a2        16    /* ROMが退避済み */
#define F_a3        20    /* ROMが退避済み */
#define F_a4        24    /* ROMが退避済み */
#define F_a5        28
#define F_a6        32
#define F_a7        36
#define F_a8        40
#define F_a9        44
#define F_a10       48
#define F_a11       52
#define F_a12       56
#define F_a13       60
#define F_a14       64
#define F_a15       68
#define F_a0        80    /* ROMが退避済み */
#define F_oldsp     84    /* ROMが退避済み（中断時の実SP） */
#define F_exccause  88
#define F_lbeg      92
#define F_lend      96
#define F_lcount   100
/*
 *  CPU例外エントリ(core_exc_entry)がxsns_dpn判定用に保存する、CPU例外
 *  発生時点の元の状態。exncntは元の割込みネスト値（=非タスクコンテキスト
 *  判定）、intpriは元の割込み優先度マスク(_kernel_ipm)。T_EXCINF(C側,
 *  core_kernel_impl.h)のexncnt/intpriフィールドとオフセットを一致させる。
 *  0x100フレーム内でROM・l1intが未使用の領域(104〜)を用いる。
 */
#define F_exncnt   104
#define F_intpri   108

#define EXC_FRAME_SIZE  0x100  /* ROMが確保するフレームサイズ（中断SP - 0x100 = FB） */

/*
 *  割込みスタックのルートフレーム（擬似call4でCハンドラを呼ぶ際の
 *  ルート窓＋中断タスクフレーム番地(FB)の退避スロット）。16バイト整列。
 */
#define ROOT_FRAME_SIZE     32
#define ISTK_SAVED_FB       16   /* ルートフレーム内のFB退避スロット（基底退避域16Bの上） */

/*
 *  tick割込み(CCOMPARE0=int6)のINTENABLEマスク。
 *  Cハンドラ(signal_time)はINTLEVEL=0で動く必要がある（FMP3の
 *  assert(!sense_lock())）ため、割込みの再入防止はINTLEVELではなく
 *  INTENABLEでint6を落とすことで行う（int6が唯一のLevel-1割込み源）。
 */
#define INT_TIMER_MASK      0x40  /* 1 << 6 */

/*
 *  カーネル管理Level-1割込みのINTENABLEマスク。
 *  SMP（TNUM_PRCID>=2）ではtick(INT6)に加えIPI(INT13=0x2000)も許可する。
 *  割込み復帰時にこのマスクでINTENABLEを復元する（単一コアではtickのみ）。
 */
#if TNUM_PRCID >= 2
#define INT_IPI_MASK        0x2000  /* 1 << 13（IPI、chip_ipi.hと一致） */
#define INT_KERNEL_MASK     (INT_TIMER_MASK | INT_IPI_MASK)
#else
#define INT_KERNEL_MASK     INT_TIMER_MASK
#endif

/*
 *  自コアのPCB／割込みスタックをロードするマクロ（SMP対応）
 *
 *  単一コアは常にindex 0だが、SMP（TNUM_PRCID>=2）では各コアが自分の
 *  PCB/割込みスタックを使う必要があるため、get_my_prcidx()（PRID）で
 *  インデックスする。単一コアではmy_prcidxが0を返すので従来と等価。
 *  dstに結果、tmpは破壊する。
 */
.macro load_my_pcb dst tmp
    my_prcidx \tmp
    movi      \dst, _kernel_p_pcb_table
    addx4     \dst, \tmp, \dst
    l32i      \dst, \dst, 0
.endm

.macro load_my_istkpt dst tmp
    my_prcidx \tmp
    movi      \dst, _kernel_istkpt_table
    addx4     \dst, \tmp, \dst
    l32i      \dst, \dst, 0
.endm

/*
 *  自コアの割込みネストカウンタ _kernel_xtos_intnest[prcidx] のアドレスを
 *  dstに求める（値ではなくアドレス。呼び出し側でl32i/s32i offset 0）。
 *  SMPで各コアが独立にネストを数えるための per-core 配列。tmpは破壊する。
 */
.macro load_my_intnest_addr dst tmp
    my_prcidx \tmp
    movi      \dst, _kernel_xtos_intnest
    addx4     \dst, \tmp, \dst
.endm

/*
 * dstに自プロセッサのインデックス（0オリジン）を代入する起動時専用マクロ．
 * dst = get_my_prcidx() 相当
 *
 * _kernel_start の冒頭，スタックポインタを設定する前に呼び出される．自コア
 * 番号を確定できるハードウェア機構（CPUID 等）を持つチップでは my_prcidx と
 * 同一でよいため，デフォルトでは my_prcidx に委譲する．
 */
#ifndef MY_PRCIDX_BOOT_DEFINED
.macro my_prcidx_boot reg
    my_prcidx \reg
.endm
#endif /* MY_PRCIDX_BOOT_DEFINED */

/*
 * 起動時のウォッチドッグ早期無効化マクロのデフォルト（何もしない）。
 * WDTを持つ／ROMが有効化するチップ（ESP32-S3等）はchip_asm.incで
 * chip_wdt_early_disableを定義し CHIP_WDT_EARLY_DISABLE_DEFINED を宣言する。
 */
#ifndef CHIP_WDT_EARLY_DISABLE_DEFINED
.macro chip_wdt_early_disable
.endm
#endif /* CHIP_WDT_EARLY_DISABLE_DEFINED */

/*
 * CPU例外エントリ(core_exc_entry)冒頭のチップ上書きフック（デフォルトは空）。
 *
 * core_exc_entryは、CPU例外フレームの F_a0（中断時a0）と F_oldsp（中断時の
 * 実SP）がROMの_UserExceptionVectorによって既に退避済みである、という
 * S3(LX7)ROMの挙動を前提にしている（出口 .Lret_int_r がこの2ワードを
 * 読んでa0/a1を復元する）。
 *
 * しかし無印ESP32(LX6)のROM _UserExceptionVectorはa0・中断時SPを一切
 * 退避しない（a2/a3/a4のみ退避。tick経路で実証済み）。そのため未対策の
 * LX6では、実行中タスクコンテキストで発生したCPU例外の自己復帰時に
 * F_a0/F_oldspからゴミが読まれ a1(SP)が無効値になりハングする。
 *
 * このフックは、そのROM退避挙動の差をチップ層で吸収するための空マクロ。
 * S3(esp32s3)はROMが既に退避しているのでデフォルトの空定義=no-opでよく、
 * 既存挙動は完全に不変。LX6(esp32)チップ層(chip_asm.inc)だけが本マクロを
 * 「a0→F_a0、a1(FB)+EXC_FRAME_SIZE→F_oldsp を自前で退避し直す」実装で
 * 上書きする（Level-1割込みで導入した chip_l1int_entry_trampoline と同一の
 * 肩代わりロジックのCPU例外版）。
 *
 * このフックが呼ばれる時点：a0=中断時a0（LX6/S3ともROMは最終的にa0を
 * 元値のままjxする＝常にライブ）、a1=FB（例外フレーム基底＝中断SP -
 * EXC_FRAME_SIZE）、a2/a3/a4はROMがフレームへ退避済み（スクラッチ可）。
 * core_exc_entryの最初の命令は "rsr a2, PS" なのでa2はここでも破壊してよい。
 */
#ifndef CHIP_EXC_ENTRY_FIXUP_DEFINED
.macro chip_exc_entry_fixup
.endm
#endif /* CHIP_EXC_ENTRY_FIXUP_DEFINED */

#endif /* TOPPERS_CORE_ASM_INC */
