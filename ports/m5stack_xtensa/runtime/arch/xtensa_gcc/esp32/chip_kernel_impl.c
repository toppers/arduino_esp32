/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．（条件略）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 * ターゲット依存部モジュール（無印ESP32 / Xtensa LX6用）
 *
 * ジャイアントロックの実体。TNUM_PRCID>=2ではXtensa S32C1Iソフトウェア
 * スピンロックのLOCK変数（chip_kernel_impl.hのacquire_lock等が操作）。
 * 必ず内蔵SRAM(.bss)に置く（コア間コヒーレンシ、design.md §0）。
 * SILスピンロック(SIL_LOC_SPN)はchip_sil.hでSIL_LOC_INT（割込みロック）へ
 * 割り付けており、ARM固有のPRIMASKベースSILスピンロック実装は不要のため削除。
 */

#include "kernel_impl.h"

LOCK giant_lock;

#if TNUM_PRCID >= 2
/*
 * SILスピンロックの実体（chip_sil.hのSIL_LOC_SPN/SIL_UNL_SPNが使用）。
 * syslog等のSIL臨界区間をコア間で排他する（低レベルUART出力の
 * 文字化け防止・テスト出力の整合）。内蔵SRAM(.bss)配置で0=空き初期化。
 */
LOCK sil_spn_lock;
#endif /* TNUM_PRCID >= 2 */
