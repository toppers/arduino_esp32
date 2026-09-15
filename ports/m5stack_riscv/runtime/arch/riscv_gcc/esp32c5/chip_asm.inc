/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
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
 * 
 *  $Id: core_asm.inc 64 2018-08-20 06:57:46Z ertl-honda $
 */

/*
 *    アセンブリ言語用マクロ定義（ESP32-C5 GNU開発環境用）
 *
 *  出典: fmp3/arch/riscv_gcc/esp32p4/chip_asm.inc（P4 CLIC 層）。
 *  差分: HWLP / PIE コプロセッサのコンテキスト退避マクロ群を削除（C5 に
 *  コプロセッサは無い。追加コンテキストのマクロは空）。my_pid / my_pidx /
 *  my_cidx（mhartid ベース）、TLS の init_additional_regs_start_r、CLIC 用の
 *  core_get_exccode_asm（下位 12bit マスク）は P4 のまま。
 */

#ifndef TOPPERS_CHIP_ASM_INC
#define TOPPERS_CHIP_ASM_INC

/*
 *  自プロセッサのプロセッサIDの取得
 *    プロセッサID(prcid, 1オリジン)を返す．ESP32-C5 は mhartid=0 の単一コア
 *    -> prcid = mhartid + 1 (hart0=PRC1=マスタ)．chip_kernel_impl.h の
 *    get_my_prcidx / chip_sil.h の sil_get_pid と一致させる．
 */
.macro my_pid reg
    csrr \reg, mhartid
    addi \reg, \reg, 1
.endm

/*
 *  自プロセッサのプロセッサINDEXの取得（0オリジン）
 */
.macro my_pidx reg
    csrr \reg, mhartid
.endm

/*
 *  自プロセッサのコンテキストINDEXの取得
 *    CLIC の割込み優先度マスクは自コアの閾値のためコンテキストINDEXは
 *    実質未使用．I/F互換のため mhartid を返す．
 */
.macro my_cidx reg
    csrr \reg, mhartid
.endm

/*
 *  ディスパッチ時の追加コンテキストの保存（コプロセッサ無し）
 */
.macro save_additional_regs_disp
.endm

/*
 *  ディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_disp
.endm

/*
 *  タスク実行開始時(start_r)のTLS(スレッドローカルストレージ)初期化．
 *  P4 / C6 と同じ（ESP-IDF FreeRTOS ポートの uxInitialiseStackTLS と同方式:
 *  リンカ提供の TLS 初期化イメージ(.tdata の値 + .tbss のゼロ)を，タスク
 *  スタック最下位(p_tinib->stk)へコピーし，その先頭を tp とする．新規タスク
 *  開始時点(activate_context: sp=stk+stksz)ではスタックは丸ごと未使用のため
 *  安全．.tbss は NOLOAD のため _thread_local_bss_start からは読まずサイズ
 *  計算のみに用いる）．
 *
 *  レジスタ: \tcb_reg(a0=p_runtsk)は読むだけで不変．t0-t3/tp を破壊．
 */
#ifdef TOPPERS_SUPPORT_TLS
.macro init_additional_regs_start_r tcb_reg
    lw    t0, TCB_p_tinib(\tcb_reg)        /* t0 = p_tinib(RV32固定のため lw．lx は
                                             * core_asm.inc が本ファイル末尾の
                                             * #include より後で #define するため
                                             * ここではまだ未定義) */
    lw    tp, TINIB_stk(t0)                /* tp = p_tinib->stk(基準点，以後不変) */
    mv    t0, tp                           /* コピー先カーソル */
    la    t1, _thread_local_data_start     /* .tdata コピー元 */
    la    t2, _thread_local_data_end
.Ltls_data_loop\@:
    beq   t1, t2, .Ltls_bss_start\@
    lb    t3, 0(t1)
    sb    t3, 0(t0)
    addi  t1, t1, 1
    addi  t0, t0, 1
    j     .Ltls_data_loop\@
.Ltls_bss_start\@:
    la    t1, _thread_local_bss_start      /* .tbss はサイズ計算のみ(読まない) */
    la    t2, _thread_local_bss_end
.Ltls_bss_loop\@:
    beq   t1, t2, .Ltls_done\@
    sb    zero, 0(t0)
    addi  t0, t0, 1
    addi  t1, t1, 1
    j     .Ltls_bss_loop\@
.Ltls_done\@:
.endm
#else /* TOPPERS_SUPPORT_TLS */
.macro init_additional_regs_start_r tcb_reg
.endm
#endif /* TOPPERS_SUPPORT_TLS */

/*
 *  割込み/例外ハンドラ入口処理での追加コンテキストの保存
 */
.macro save_additional_regs_int
.endm

/*
 *  割込み/例外ハンドラ入口処理で保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int
.endm

/*
 *  割込み/例外ハンドラからのディスパッチ時の追加コンテキストの保存
 */
.macro save_additional_regs_int_disp
.endm

/*
 *  割込み/例外ハンドラからのディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int_disp
.endm

/*
 *  CPU例外番号(exccode)を mcause から reg に取り出す
 *    CLIC モードでは mcause 上位に mpp/mpie/mpil 等が同時にセットされるため，
 *    下位12bit(exccode)のみ残す(slli/srli 20 は XLEN=32 前提)．reg のみ破壊．
 *    core_support.S の CPU例外入口で用いる．
 */
.macro core_get_exccode_asm reg
    csrr  \reg, mcause
    slli  \reg, \reg, 20
    srli  \reg, \reg, 20
.endm

/*
 *  コア依存部で共通な定義
 */
#include "core_asm.inc"

#endif /* TOPPERS_CHIP_ASM_INC */
