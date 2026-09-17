/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス（polarfire_soc_kit.h と同一）。無保証。
 */

/*
 *    M5Stamp ESP32P4 ボード固有の定義
 */
#ifndef M5STAMP_ESP32P4_KIT_H
#define M5STAMP_ESP32P4_KIT_H

#ifdef M5STAMP_ESP32P4
#define TARGET_NAME   "M5Stamp ESP32P4 <HP RV32IMAFC, RISC-V>"
/*  HP コア周波数（Step1 実機測定）。mtime も同クロックで歩進する。  */
#define CORE_CLK_MHZ  360
/*  コンソールは UART0 を使用（U0TXD=GPIO37/U0RXD=GPIO38）  */
#define USE_UART0
/*
 *  sil_dly_nse 用のチューニング値。arch/riscv_gcc/common/core_support.S の
 *  sil_dly_nse は古典的な回数校正方式（SIL_DLY_TIM1=最小遅延[ns]、
 *  SIL_DLY_TIM2=1ループ増分[ns]）で、xtensa版（CCOUNTサイクル精度）とは
 *  異なりこの2値自体が実際の遅延精度を左右する。
 *
 *  実機較正（2026-08-15、M5Stamp ESP32P4 30:ed:a0:ea:98:86、seam・360MHz、
 *  旧値14/8のまま test_dlynse を実行して測定。詳細
 *  .steering/20260815-c3-smallitems/item1-p4-dlynse-calibration.md）:
 *    実測オーバヘッド a_real = 13ns（k=0、dlytim=14 のとき delay_time=13）
 *    実測ループ単価   b_real ≈ 8.1〜8.5ns/loop（(94-13)/10=8.1、(436-13)/50=8.46）
 *  旧値 TIM1=14 は a_real(13) を上回っており境界〜小プローブで NG
 *  （delay_time < dlytim）になっていた。新値は両方に安全側マージンを持たせ
 *  a_real/b_real を下回るよう選ぶ（sil_dly_nse は "要求以上待つ" 側が安全、
 *  "要求より短く待つ" 側が壊れている）。
 */
#define SIL_DLY_TIM1  8
#define SIL_DLY_TIM2  6
#endif /* M5STAMP_ESP32P4 */

#endif /* M5STAMP_ESP32P4_KIT_H */
