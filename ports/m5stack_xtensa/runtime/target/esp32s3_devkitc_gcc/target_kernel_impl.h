/*
 *  ターゲット依存部モジュール（ESP32-S3-DevKitC-1 / FMP3 用）
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
 *  test_dlynse.c は TOPPERS_MACRO_ONLY 付きで本ヘッダを直接インクルード
 *  するため、定義は target_test.h ではなく本ヘッダに置く必要がある
 *  （LX6版 target/esp32_devkitc_gcc/target_kernel_impl.h と同じ位置）。
 *
 *  本ポートの sil_dly_nse（arch/xtensa_gcc/common/core_kernel_impl.c、
 *  LX6と共通）は CCOUNT差分によるサイクル精度実装であり、古典的なループ
 *  回数校正方式（SIL_DLY_TIM1=最小遅延, SIL_DLY_TIM2=1ループ増分）とは
 *  異なる。よって以下は test_dlynse がプローブする遅延値の刻み（ns）を
 *  与えるためのシード値にすぎず、遅延精度そのものはCCOUNTで保証される。
 *
 *  値は LX6版（esp32_devkitc_gcc）およびESP32-P4版と同一の 14/8 を採用する。
 *  根拠：(a) sil_dly_nse の実装が arch/xtensa_gcc/common で LX6 と共通で、
 *  シード値の意味づけ（プローブ刻み）がLX6と全く同じ、(b) 本テストは
 *  CCOUNT実装では「校正定数」ではなくプローブ刻みでしかなく、値の大小は
 *  合否に影響しない（下記の実測参照）、(c) 最大プローブ
 *  SIL_DLY_TIM1+SIL_DLY_TIM2*50=414ns でも 1M回の実行時間が短く収まる。
 *
 *  ★**このテストの合格は校正の裏付けにならない。** 1回あたりの呼出し
 *  オーバヘッドが約2324ns（HRT換算）で最大プローブ414nsを一桁上回るため、
 *  全プローブがオーバヘッドだけで OK になる。
 *
 *  sil_dly_nse は cycles_per_usec=TCYC_PER_HRT で実クロックに追随する
 *  （core_kernel_impl.c:167）。かつ FMP3 が seam 起動で CPU を
 *  TOPPERS_S3_CPU_FREQ_MHZ へ設定する（target_kernel_impl.c）ため TCYC と
 *  実クロックは一致する。SIL_DLY_TIM のクロック別校正は下記 #if で実施する。
 */
/*
 *  ★2026-07-20 校正: SIL_DLY_TIM1/2 を CPU クロック毎に設定する。
 *  test_dlynse は sil_dly_nse を 1M 回呼んで実測遅延を測り delay_time>=dlytim を
 *  OK/NG 判定するが、旧値(14/8, 最大プローブ414ns)は 1呼出しオーバヘッド
 *  (≈92 CPUサイクル)より一桁小さく、全プローブがオーバヘッドだけで OK になり
 *  校正を一切検証できなかった。オーバヘッドは 92000/クロックMHz [ns]
 *  （実機実測 DevKitC-1 4a58: 80MHz→1162 / 160MHz→573 / 240MHz→391ns）。
 *  最大プローブ SIL_DLY_TIM1+SIL_DLY_TIM2*50 を約4×オーバヘッドに取り、
 *  *20/*50 のプローブで sil_dly_nse の busy-wait 遅延を実際に検証する
 *  （2×以上の校正誤差なら NG になる。sil_dly_nse 自体は CCOUNT サイクル精度で
 *  実クロック(=TCYC_PER_HRT)に追随済み。core_kernel_impl.c:167）。
 *  値は S3(LX7)/LX6 で共通（sil_dly_nse は arch 共有・同一オーバヘッドサイクル）。
 *  ※本テストは 1M回×多プローブで実機 15〜30s 要す（採取窓を長めに取ること）。
 *  詳細: 
 */
/*
 *  TOPPERS_S3_CPU_FREQ_MHZ の既定は target_timer.h（正本）が定義するが、その既定は
 *  #ifndef TOPPERS_MACRO_ONLY 内にあり、test_dlynse.c は本ヘッダを TOPPERS_MACRO_ONLY
 *  付きで読むため到達しない。コマンドライン -D が無い既定ビルドでも下記 #if が
 *  正しい分岐を選ぶよう既定を補完する（-D 指定時はそちらが優先。値は target_timer.h と
 *  一致させること）。
 */
#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#define TOPPERS_S3_CPU_FREQ_MHZ  160
#endif
#if TOPPERS_S3_CPU_FREQ_MHZ == 240
#define SIL_DLY_TIM1	400
#define SIL_DLY_TIM2	25
#elif TOPPERS_S3_CPU_FREQ_MHZ == 160
#define SIL_DLY_TIM1	600
#define SIL_DLY_TIM2	35
#elif TOPPERS_S3_CPU_FREQ_MHZ == 80
#define SIL_DLY_TIM1	1200
#define SIL_DLY_TIM2	70
#else /* 40 (Direct Boot / QEMU。dlynse は QEMU 非対象) */
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
