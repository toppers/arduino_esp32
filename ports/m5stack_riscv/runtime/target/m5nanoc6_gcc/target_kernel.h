/*
 *  ターゲット依存モジュール（M5NanoC6 / ESP32-C6 用・FMP3）
 *  fmp3/target/m5stamp_esp32p4_gcc/target_kernel.h を単一コア向けに縮めたもの。
 */
#ifndef TOPPERS_TARGET_KERNEL_H
#define TOPPERS_TARGET_KERNEL_H

/*
 *  プロセッサ数（ESP32-C6 の HP コアは 1 つ。LP コアは対象外）
 */
#ifndef TNUM_PRCID
#define TNUM_PRCID  1
#endif /* TNUM_PRCID */
#if TNUM_PRCID != 1
#error ESP32-C6 has a single HP core: TNUM_PRCID must be 1.
#endif

#define PRC1  1
#define TOPPERS_MASTER_PRCID   PRC1
#define TOPPERS_TMASTER_PRCID  PRC1
#define TOPPERS_TEPP_PRC       0x1  /* PRC1 */

/*
 *  クラスID（target_class.py と一致させる）
 */
#define CLS_PRC1      1    /* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_ALL_PRC1  2    /* 割付け可能：すべて，初期割付け：PRC1 */

/*
 *  高分解能タイマのタイマ周期（systimer は 16MHz、HRTCNT=1us 刻み）
 */
#define TSTEP_HRTCNT  1U

#include "chip_kernel.h"

#endif /* TOPPERS_TARGET_KERNEL_H */
