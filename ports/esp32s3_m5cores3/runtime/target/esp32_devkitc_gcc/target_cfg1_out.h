/*
 *  cfg1_out.c をリンクするために必要なスタブの定義（RP2350 用）
 *
 *  cfg1_out は start.o と cfg1_out.o のみをリンクして生成される（カーネル
 *  本体やターゲット依存部の .o はリンクしない）ため，start.S が参照するシン
 *  ボルのうち，それらの .o でしか定義されないものをスタブとして与える．
 *  なお _kernel_start は start.S が定義するため，ここでは定義しない．
 */

#include <kernel.h>

/*
 *  start.S が参照するシンボルのスタブ
 */
STK_T *const _kernel_istkpt_table[TNUM_PRCID];
void hardware_init_hook(void){}
void software_init_hook(void){}
void _kernel_target_mprc_initialize(void){}

/*
 *  RP2350 では自コア番号を SIO CPUID で得る（chip_asm.inc）ため，musca の
 *  ような _kernel_istk_table（MSP 範囲判定）や _kernel_mp_boot_flag
 *  （ブートハンドシェイクフラグ）は参照しない．
 */

/*
 *  チップ依存のスタブ定義（_kernel_istkpt 等）
 */
#include <chip_cfg1_out.h>
