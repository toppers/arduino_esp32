/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
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
 */

/*
 *  ESP32-C6 専用のスタートアップモジュール。
 *
 *  出典: fmp3_core（submodule・pristine）arch/riscv_gcc/common/start.S の
 *  複製。fmp3_core は改変しない方針のため、C6 固有の差分はこちら（repo 側の
 *  チップ層）に置く。
 *
 *  出典: fmp3_core 685b36a arch/riscv_gcc/common/start.S（差分は __riscv_flen
 *  ガードのみ。common/start.S が変わったら本ファイルへ写すこと）。
 *
 *  【差分】「FPUの初期化」ブロック（mstatus.FS 設定 + fscsr）を
 *  `#ifdef __riscv_flen` で括った。理由（実装計画 Task 5 の configure/build で発覚）:
 *  ESP32-C6 のコアは RV32IMAC で F 拡張を持たない
 *  （esp-idf/components/soc/esp32c6 に SOC_CPU_HAS_FPU の定義が無い。
 *   esp32p4 にはある = P4 は実 FPU を持つが C6 には無い）。
 *  fmp3_core 側の共通 core_support.S（コンテキストスイッチの FPU 退避/復元）
 *  は既に同じ `#ifdef __riscv_flen` で括られており（-march に f/d が無ければ
 *  コンパイラが __riscv_flen を定義しない）、F 拡張の無いチップでは
 *  FPU 関連コードが丸ごと消える設計になっている。ところが共通 start.S の
 *  FPU 初期化だけはこのガードが無く無条件（asm なので `fscsr` が
 *  `-march=rv32imac` の下でアセンブラに拒否される＝assembler error）。
 *  C6 では FPU 自体が存在しないため、mstatus.FS の初期化も fscsr による
 *  丸めモード設定も実行してはならない（実行しても意味が無いばかりか、
 *  実機では unimplemented CSR/命令として illegal instruction 例外になる
 *  おそれがある）。したがって「削除」ではなく「ガードして無効化」が
 *  正しい対処であり、core_support.S と同じ判定条件に揃えた。
 */

#define TOPPERS_MACRO_ONLY
#define TOPPERS_ASM_MACRO
#define UINT_C(val)     (val) /* uint_t型の定数を作るマクロ */
#define ULONG_C(val)    (val) /* ulong_t型の定数を作るマクロ */
#define CAST(type, val) (val) /* 型キャストを行うマクロ */

#include "kernel_impl.h"
#include "target_asm.inc"

/*
 *  同期用のマジックナンバー
 */
#define MAGIC_START 0x87654321

    ATEXT
    AALIGN(4)
    AGLOBAL(toppers_start)
ALABEL(toppers_start)
    /*
     *  Machine割込みの禁止
     */
    csrrc  t0, mstatus, MSTATUS_MIE
    csrw   mie, 0
    csrw   mip, 0

    /*
     *  スタックポインタ(sp)の初期化
     */
    my_istkpt sp, t0          /* スタックポインタ（sp）*/

    /*
     *  グローバルポインタ(gp)の初期化
     */
    AO_PUSH
    AO_NORELAX      /* gp相対を使用しない */
    la gp, __global_pointer$
    AO_POP

#ifdef __riscv_flen
    /*
     *  FPUの初期化（F/D 拡張を持つチップのみ。ESP32-C6 には無い）
     */
    li    t0, ~MSTATUS_FS          /* mstatus.FS を Initに */
    li    t1, MSTATUS_FS_INIT
    csrr  t2, mstatus
    and   t2, t2, t0
    or    t2, t2, t1
    csrw  mstatus, t2
    li    t0, FCSR_ROUNDING_MODE
    fscsr t0                 /* ラウンディングモード設定 */
#endif /* __riscv_flen */

    /*
     *  hardware_init_hookの呼出し
     *
     *  ターゲットハードウェアに依存して必要な初期化処理がある場合は，
     *  hardware_init_hookという関数を用意する．
     */
    jal  hardware_init_hook

    /*
     *  マスタプロセッサ以外は初期化待ち
     */
    my_pid  t0
    li      t1, TOPPERS_MASTER_PRCID
    bne     t0, t1, slave_wait

#ifndef TOPPERS_OMIT_BSS_INIT

    /*
     *  sbssセクションのクリア
     *
     *  SBSS_STARTからSBSS_ENDまでをゼロクリアする．
     */
    la    a0, __sbss_start
    la    a1, __sbss_end
    bgeu  a0, a1, start_3
ALABEL(start_2)
    sw    zero, (a0)
    addi  a0, a0, 4
    bltu  a0, a1, start_2
ALABEL(start_3)

    /*
     *  bssセクションのクリア
     *
     *  BSS_STARTからBSS_ENDまでをゼロクリアする．
     */
    la    a0, __bss_start
    la    a1, __bss_end
    bgeu  a0, a1, start_5
ALABEL(start_4)
    sw    zero, (a0)
    addi  a0, a0, 4
    bltu  a0, a1, start_4
ALABEL(start_5)
#endif /* TOPPERS_OMIT_BSS_INIT */

#ifndef TOPPERS_OMIT_DATA_INIT
    /*
     *  dataセクションの初期化（ROM化対応）
     *
     *  __idata_startから__idata_endまでを，__data_start以降にコピーす
     *  る．
     */
    la    a0, __idata_start
    la    a1, __data_start
    la    a2, __idata_end
    bgeu  a0, a2, start_7
ALABEL(start_6)
    lwu   t0, (a0)
    sw    t0, (a1)
    addi  a0, a0, 4
    addi  a1, a1, 4
    bltu  a0, a2, start_6
ALABEL(start_7)
#endif /* TOPPERS_OMIT_DATA_INIT */

    /*
     *  software_init_hookの呼出し
     *
     *  ソフトウェア環境（特にライブラリ）に依存して必要な初期化処理が
     *  ある場合は，software_init_hookという関数を用意する．
     */
    jal  software_init_hook

    /*
     *  target_mprc_initialize の呼び出し
     *
     *  sta_ker 以前にマスタプロセッサのみで行う必要のある初期化を実行
     */
ALABEL(start_8)
    jal  target_mprc_initialize

    /*
     *  カーネルを起動する
     */
ALABEL(start_9)
    la    t0, start_sync
    li    t1, MAGIC_START
    li    t2, (TNUM_PRCID * 4)
    add   t2, t0, t2
ALABEL(start_10)
    sw    t1, 0(t0)
    addi  t0, t0, 4
    bltu  t0, t2, start_10   /* if t0 < t2 goto start_10 */

    /* マスタプロセッサ用のstart_syncを初期化 */
    la       t0, start_sync
    my_pidx  t2
    slli     t2, t2, 2
    add      t0, t0, t2
    sw       zero, 0(t0)
    j        sta_ker

    /*
     *  マスタプロセッサ以外の初期化待ちルーチン
     */
ALABEL(slave_wait)
    la       t0, start_sync
    li       t1, MAGIC_START
    my_pidx  t2
    slli     t2, t2, 2
    add      t0, t0, t2
ALABEL(slave_wait_1)
    lwu      t2,   0(t0)
    bne      t1,   t2, slave_wait_1
    sw       zero, 0(t0)
    j        sta_ker

/*
 *  デフォルトのhardware_init_hook（weak定義）
 */
    ATEXT
    AALIGN(2)
    AWEAK(hardware_init_hook)
ALABEL(hardware_init_hook)
    jr  ra

/*
 *  デフォルトのsoftware_init_hook（weak定義）
 */
    ATEXT
    AALIGN(2)
    AWEAK(software_init_hook)
ALABEL(software_init_hook)
    jr  ra
