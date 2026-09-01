/*
 *  kernel.h のターゲット依存部（RP2350 / RaspberryPi Pico 2 / FMP3 用）
 *
 *  このインクルードファイルは，kernel.h でインクルードされる．このファ
 *  イルをインクルードする前に，t_stddef.h がインクルードされる．
 */

#ifndef TOPPERS_TARGET_KERNEL_H
#define TOPPERS_TARGET_KERNEL_H

/*
 *  プロセッサ数
 *
 *  RP2350 は dual Cortex-M33（最大 2 コア）構成．
 *  TNUM_PRCID が未指定なら単一プロセッサ（1）とする．
 */
#ifndef TNUM_PRCID
#define TNUM_PRCID	1
#endif /* TNUM_PRCID */

#if TNUM_PRCID < 1 || TNUM_PRCID > 2
#error TNUM_PRCID is out of range (RP2350 supports 1 or 2 processors).
#endif

/*
 *  プロセッサID
 */
#define PRC1	1
#if TNUM_PRCID >= 2
#define PRC2	2
#endif /* TNUM_PRCID >= 2 */

/*
 *  マスタプロセッサのID
 */
#define TOPPERS_MASTER_PRCID	PRC1
#define TOPPERS_TMASTER_PRCID	PRC1

/*
 *  クラスID
 */
#if TNUM_PRCID == 1
#define CLS_PRC1		1	/* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_ALL_PRC1	2	/* 割付け可能：すべて，初期割付け：PRC1 */
#elif TNUM_PRCID == 2
#define CLS_PRC1		1	/* 割付け可能：PRC1，初期割付け：PRC1 */
#define CLS_PRC2		2	/* 割付け可能：PRC2，初期割付け：PRC2 */
#define CLS_ALL_PRC1	3	/* 割付け可能：すべて，初期割付け：PRC1 */
#define CLS_ALL_PRC2	4	/* 割付け可能：すべて，初期割付け：PRC2 */
#endif /* TNUM_PRCID */

/*
 *  通信パスが存在するプロセッサの集合
 */
#ifndef TOPPERS_TEPP_PRC
#if TNUM_PRCID == 1
#define TOPPERS_TEPP_PRC	0x1		/* PRC1 のみ */
#else /* TNUM_PRCID == 1 */
#define TOPPERS_TEPP_PRC	0x3		/* PRC1 と PRC2 */
#endif /* TNUM_PRCID == 1 */
#endif /* TOPPERS_TEPP_PRC */

/*
 *  カーネル管理の割込み優先度の範囲
 */
#define TMIN_INTPRI	(-3)	/* 割込み優先度の最小値（最高値）*/

/*
 *  高分解能タイマの設定
 *
 *  [2026-07-09 訂正] 本コメントは元々RP2350（Raspberry Pi Pico 2）移植の
 *  ものがコピペで残存しており，ESP32-S3の実装（Xtensaコア内蔵CCOUNT/
 *  CCOMPARE0，target_timer.c/target_timer.h）には文字通りには当てはまら
 *  なかった。RP2350はTIMER0自体がハードウェアで自然に2^32[us]周期で
 *  ラップする独立カウンタだが，ESP32-S3のCCOUNTは80MHz(WiFiビルド)/
 *  40MHzのCPUサイクルカウンタで，これをTCYC_PER_HRTで割った値（us単位）
 *  は本来2^32/80,000,000≈53.687秒ごとに割った後の値がラップしてしまう
 *  （実機で53〜56秒ごとにタイマ駆動タスクが永眠するフリーズとして顕在化
 *  した不具合。詳細はesp/debug/JTAG_DEBUG.md 追記51/52）。
 *
 *  対策として，target_hrt_get_current()（target_timer.h）側でCCOUNTの
 *  生サイクル差分（32bit符号なし減算で自然にラップ対応）を64bitへ
 *  ソフトウェア積算してからus換算するように変更した。この結果，
 *  target_hrt_get_current()が返すHRTCNT（us単位）は改めて自然な
 *  2^32[us]周期（約71.6分）でラップするようになり，本来の「TIMER0が
 *  2^32[us]周期でラップする」というRP2350のコメントの**意図**（HRTCNT
 *  自体がフルサイクルでラップする＝TCYC_HRTCNTによる追加補正が不要）
 *  にESP32-S3でも整合するようになった。TCYC_HRTCNT は今後も未定義の
 *  ままで正しい（kernel/time_event.c update_current_evttim()のラップ
 *  補正コードはコンパイル時に無効のままでよい）。
 */
#ifndef USE_TIM_AS_HRT
#define USE_TIM_AS_HRT
#endif /* USE_TIM_AS_HRT */
#undef TCYC_HRTCNT
#define TSTEP_HRTCNT	1U

#ifndef TOPPERS_MACRO_ONLY
extern void	sta_ker(void);
#endif /* TOPPERS_MACRO_ONLY */

/*
 *  チップ依存で共通な定義
 */
#include "chip_kernel.h"

#endif /* TOPPERS_TARGET_KERNEL_H */
