/*
 *  ターゲット依存部モジュール（無印ESP32 / Xtensa LX6 / FMP3 用）
 *
 *  カーネルのターゲット依存部のインクルードファイル．kernel_impl.h のター
 *  ゲット依存部の位置付けとなる．
 */

#ifndef TOPPERS_TARGET_KERNEL_IMPL_H
#define TOPPERS_TARGET_KERNEL_IMPL_H

/*
 *  TBITW_IPRI の定義のため読み込み
 */
#include <sil.h>

/*
 *  デフォルトの非タスクコンテキスト用のスタック領域の定義
 */
#define DEFAULT_ISTKSZ	(0x1000)	/* 4KByte */

/*
 *  デフォルトのアイドル処理用のスタック領域の定義
 */
#define DEFAULT_IDSTKSZ	(0x0100U)

/*
 *  sil_dly_nse の校正パラメータ（test/test_dlynse が参照）
 *
 *  本ポートの sil_dly_nse（arch/xtensa_gcc/common/core_kernel_impl.c）は
 *  CCOUNT差分によるサイクル精度実装（cycles_per_usec=TCYC_PER_HRT で実クロックに
 *  追随。40U 固定ではない）。
 *
 *  2026-07-20 校正: SIL_DLY_TIM1/2 を CPU クロック毎に設定する。旧値(14/8,
 *  最大プローブ414ns)は 1呼出しオーバヘッド(≈92 CPUサイクル)より一桁小さく、
 *  全プローブがオーバヘッドだけで OK になり校正を検証できなかった。オーバヘッドは
 *  92000/クロックMHz [ns]（S3実機実測 80→1162 / 160→573 / 240→391ns。sil_dly_nse は
 *  arch 共有なので LX6 も同一）。最大プローブ SIL_DLY_TIM1+SIL_DLY_TIM2*50 を
 *  約4×オーバヘッドに取り、*20 と *50 で sil_dly_nse を実際に検証（2×校正誤差で NG）。
 *  ※本テストは 1M回×多プローブで実機 15〜30s 要す。詳細:
 *  非公開作業記録/20260720-dlynse-calibration/
 */
/*
 *  TOPPERS_ESP32_CPU_FREQ_MHZ の既定は target_timer.h（正本）が定義するが、その既定は
 *  #ifndef TOPPERS_MACRO_ONLY 内にあり、test_dlynse.c は本ヘッダを TOPPERS_MACRO_ONLY
 *  付きで読むため到達しない。-D が無い既定ビルドでも下記 #if が正しい分岐を選ぶよう
 *  既定を補完する（-D 指定時はそちらが優先。値は target_timer.h と一致させること）。
 */
#ifndef TOPPERS_ESP32_CPU_FREQ_MHZ
#define TOPPERS_ESP32_CPU_FREQ_MHZ  160
#endif
#if TOPPERS_ESP32_CPU_FREQ_MHZ == 240
#define SIL_DLY_TIM1	400
#define SIL_DLY_TIM2	25
#elif TOPPERS_ESP32_CPU_FREQ_MHZ == 160
#define SIL_DLY_TIM1	600
#define SIL_DLY_TIM2	35
#elif TOPPERS_ESP32_CPU_FREQ_MHZ == 80
#define SIL_DLY_TIM1	1200
#define SIL_DLY_TIM2	70
#else /* 40 (Direct Boot only) */
#define SIL_DLY_TIM1	2400
#define SIL_DLY_TIM2	140
#endif

#ifndef TOPPERS_MACRO_ONLY

/*
 *  マスタプロセッサ依存の初期化
 */
extern void target_mprc_initialize(void);

/*
 *  ターゲットシステム依存の初期化
 */
extern void target_initialize(PCB *p_my_pcb);

/*
 *  ターゲットシステムの終了
 */
extern void target_exit(void) NoReturn;

/*
 *  エラー発生時の処理
 */
extern void Error_Handler(void);

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  チップ依存モジュール（ESP32-S3 用）
 */
#include <chip_kernel_impl.h>

#endif /* TOPPERS_TARGET_KERNEL_IMPL_H */
