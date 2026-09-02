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
 *  クラッシュ/ハング診断recorder のスタブ実装
 *  （無印ESP32(Xtensa LX6) QEMU PoC用、diag_recorder.h宣言のAPI群の
 *  最小no-opスタブ）
 *
 *  [2026-07-13新規] 本PoCの範囲（vector table初期化＋UARTポーリング
 *  出力のみ、tasklist.md T1）は診断recorder機能自体を対象としない。
 *  ただしarch/xtensa_gcc/common/core_kernel_impl.c（無改造で共有）が
 *  diag_recorder.hを無条件includeしdiag_panic_snapshot()を呼ぶため、
 *  リンクを通すために最小no-opスタブを提供する（esp32s3_devkitc_gcc/
 *  diag_recorder.cの実装ポートは本PoCでは行わない。design.md §6参照）。
 *  本PoCはhardware_init_hookがsta_ker到達前に停止する設計のため、
 *  これらの関数は実行時に到達しない。
 */

#include <stdint.h>
#include "diag_recorder.h"

void
diag_event(uint8_t id, uint32_t arg1, uint32_t arg2)
{
    /* no-op スタブ（本PoCでは未使用） */
}

void
diag_heartbeat(void)
{
    /* no-op スタブ（本PoCでは未使用） */
}

void
diag_panic_snapshot(const void *p_excinf)
{
    /* no-op スタブ（本PoCでは未使用） */
}

void
diag_boot_check_and_dump(intptr_t exinf)
{
    /* no-op スタブ（本PoCでは未使用） */
}
