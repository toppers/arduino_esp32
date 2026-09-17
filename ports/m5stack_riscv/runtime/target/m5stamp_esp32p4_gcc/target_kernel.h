/*
 *    kernel.hのターゲット依存部（Plarfire SoC Kit用）
 *
 *  このヘッダファイルは，kernel.hからインクルードされる．他のファイル
 *  から直接インクルードすることはない．このファイルをインクルードする
 *  前に，t_stddef.hがインクルードされるので，それに依存してもよい．
 * 
 *  $Id: target_kernel.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

#ifndef TOPPERS_TARGET_KERNEL_H
#define TOPPERS_TARGET_KERNEL_H

/*
 *  プロセッサ数
 */
#ifndef TNUM_PRCID
#define TNUM_PRCID  4
#endif /* TNUM_PRCID */

/*
 *  プロセッサIDの定義
 */
#define PRC1  1
#define PRC2  2
#define PRC3  3
#define PRC4  4

/*
 *  マスタプロセッサ
 */
#define TOPPERS_MASTER_PRCID  PRC1

/*
 *  タイムマスタプロセッサ
 */
#define TOPPERS_TMASTER_PRCID  PRC1

/*
 *  タイムイベント処理プロセッサとするプロセッサ
 */
#define TOPPERS_TEPP_PRC  0xf  /* PRC1,PRC2,PRC3,PRC4 */

/*
 *  クラスIDの定義
 */
#if TNUM_PRCID == 1
#define CLS_PRC1      1    /* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_ALL_PRC1  2    /* 割付け可能：すべて，初期割付け：PRC1 */
#elif TNUM_PRCID == 2
#define CLS_PRC1      1    /* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_PRC2      2    /* 割付け可能：PRC2，初期割付け：PRC2 */
#define CLS_ALL_PRC1  3    /* 割付け可能：すべて，初期割付け：PRC1 */
#define CLS_ALL_PRC2  4    /* 割付け可能：すべて，初期割付け：PRC2 */
#elif TNUM_PRCID == 3
#define CLS_PRC1      1    /* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_PRC2      2    /* 割付け可能：PRC2，初期割付け：PRC2 */
#define CLS_PRC3      3    /* 割付け可能：PRC3，初期割付け：PRC3 */
#define CLS_ALL_PRC1  4    /* 割付け可能：すべて，初期割付け：PRC1 */
#define CLS_ALL_PRC2  5    /* 割付け可能：すべて，初期割付け：PRC2 */
#define CLS_ALL_PRC3  6    /* 割付け可能：すべて，初期割付け：PRC3 */
#elif TNUM_PRCID == 4
#define CLS_PRC1      1    /* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_PRC2      2    /* 割付け可能：PRC2，初期割付け：PRC2 */
#define CLS_PRC3      3    /* 割付け可能：PRC3，初期割付け：PRC3 */
#define CLS_PRC4      4    /* 割付け可能：PRC4，初期割付け：PRC4 */
#define CLS_ALL_PRC1  5    /* 割付け可能：すべて，初期割付け：PRC1 */
#define CLS_ALL_PRC2  6    /* 割付け可能：すべて，初期割付け：PRC2 */
#define CLS_ALL_PRC3  7    /* 割付け可能：すべて，初期割付け：PRC3 */
#define CLS_ALL_PRC4  8    /* 割付け可能：すべて，初期割付け：PRC4 */
#else
#error TNUM_PRCID is out of range.
#endif

/*
 *  高分解能タイマのタイマ周期
 */
#ifndef USE_64BIT_HRTCNT
/* TCYC_HRTCNTは定義しない．*/
#endif /* USE_64BIT_HRTCNT */

/*
 *  高分解能タイマのカウント値の進み幅
 */
#define TSTEP_HRTCNT  1U

/*
 *  FPSRの設定値
 */
#define FCSR_ROUNDING_MODE  FCSR_RNE_MODE
/*
 *  チップで共通な定義
 */
#include "chip_kernel.h"

#endif /* TOPPERS_TARGET_KERNEL_H */
