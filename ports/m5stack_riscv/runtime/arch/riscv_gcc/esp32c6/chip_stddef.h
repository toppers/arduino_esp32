/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *    t_stddef.hのチップ依存部（ESP32-C6用）
 *
 *  pico2_riscv/rp2350版からの流用．
 */

#ifndef TOPPERS_CHIP_STDDEF_H
#define TOPPERS_CHIP_STDDEF_H

/*
 *  ターゲットを識別するためのマクロの定義
 */
#define TOPPERS_ESP32C6    /* チップ略称 */

/*
 *  開発環境で共通な定義
 */
#ifndef TOPPERS_MACRO_ONLY
#include <stdint.h>
#endif /* TOPPERS_MACRO_ONLY */

#define TOPPERS_STDFLOAT_TYPE1
#include "tool_stddef.h"

/*
 *  コアで共通な定義
 */
#include "core_stddef.h"

#endif /* TOPPERS_CHIP_STDDEF_H */
