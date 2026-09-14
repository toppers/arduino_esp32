/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *    kernel.hのチップ依存部（ESP32-C6用）
 *
 *  pico2_riscv/rp2350版からの流用．割込みコントローラをXh3irqから
 *  INTMTX（Interrupt Matrix）に変更．C3はmeifa相当（割込み強制ビット）
 *  を持たないため，clr_int／ras_intは未サポートとした．
 *
 *  このヘッダファイルは，target_kernel.h（または，そこからインクルー
 *  ドされるファイル）のみからインクルードされる．他のファイルから直接
 *  インクルードしてはならない．
 */

#ifndef TOPPERS_CHIP_KERNEL_H
#define TOPPERS_CHIP_KERNEL_H

/*
 *  割込み優先度の範囲
 *
 *  割込みマトリクスの優先度フィールドは物理4ビットだが，有効範囲は
 *  1〜7の7段階（ESP-IDF riscv/interrupt.hの規定）．優先度0は割込み
 *  優先度マスク全解除状態（THRESH=1）でもマスクされる値として空け，
 *  -1〜-7を内部表現1〜7に対応付ける（intmtx_kernel_impl.h参照）．
 */
#define TMIN_INTPRI  (-7)    /* 割込み優先度の最小値（最高値）*/
#define TMAX_INTPRI  (-1)    /* 割込み優先度の最大値（最低値）*/

/*
 *  サポートできる機能の定義
 *
 *  ena_int／dis_int／clr_int／ras_int／prb_intをサポートする
 *  （clr_int／ras_intは，ソフトウェアでアサートできるFROM_CPUソースを
 *  割り当てた割込み線のみ．intmtx_kernel_impl.h参照）．
 */
#define TOPPERS_TARGET_SUPPORT_ENA_INT    /* ena_int */
#define TOPPERS_TARGET_SUPPORT_DIS_INT    /* dis_int */
#define TOPPERS_TARGET_SUPPORT_CLR_INT    /* clr_int */
#define TOPPERS_TARGET_SUPPORT_RAS_INT    /* ras_int */
#define TOPPERS_TARGET_SUPPORT_PRB_INT    /* prb_int */

/*
 *  コアで共通な定義
 */
#include "core_kernel.h"

#endif /* TOPPERS_CHIP_KERNEL_H */
