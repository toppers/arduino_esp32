/*
 *  シリアルインタフェースドライバのターゲット依存部（ESP32-S3-DevKitC-1）
 *  （非TECS版専用）
 *
 *  対話的シリアルコンソール（sample1のserial_rea_dat/serial_wri_dat）用の
 *  SIOドライバ定義。既定はUART0（DR_REG_UART_BASE=0x60000000）を割込み
 *  駆動で使う。UART0のレベル割込みは割込みマトリクスでCPU割込みINT5
 *  （EXTERN_LEVEL、レベル1）へ配線し、CFG_INT/CRE_ISR（chip_serial.cfg）で
 *  sio_isrを繋ぐ。低レベルsyslog出力（target_fput_log）は従来どおり
 *  ポーリング送信で行う。
 *
 *  ビルド時オプション`TOPPERS_S3_CONSOLE_USJ`
 *  （CMake preset ビルドでは `cmake --preset <seam-s3-*> -DA1_CONSOLE_USJ=ON`。
 *  CMakeLists が全 .o＝libfmp3/cfg＋消費側 seam_common に `-DTOPPERS_S3_CONSOLE_USJ`
 *  を注入する。classic 時代の `EXTRA_OFLAGS="-o -DTOPPERS_S3_CONSOLE_USJ"` は
 *  classic 全廃で廃止）を指定すると、コンソールの実体がUSB-Serial-
 *  JTAG（USJ）に切り替わる。1ポートしか持たない実機（M5Stack系ボード等）
 *  向け。未定義時は本ファイルのマクロ展開結果は変更前と完全に同一
 *  （既定ビルドの非回帰、詳細設計 
 *  design-usj-console.md）。
 *
 *  QEMU（TOPPERS_QEMU）のUSB_SERIAL_JTAGは読み出し常時0のスタブでしか
 *  ないため、USJコンソールとQEMUの同時指定はビルド時エラーにする。
 */

#ifndef TOPPERS_TARGET_SERIAL_H
#define TOPPERS_TARGET_SERIAL_H

#if defined(TOPPERS_S3_CONSOLE_USJ) && defined(TOPPERS_QEMU)
#error "QEMUのUSB_SERIAL_JTAGはスタブ（読み出し常時0）のためUSJコンソール不可。QEMUはUART0を使うこと"
#endif

/*
 *  サポートするシリアルポートの数（UART0/USJいずれか1ポート）
 */
#define TNUM_PORT		1

/*
 *  UART0/USJを配線するCPU割込み番号と割込み属性
 *
 *  USART_INTNOは素のCPU割込み番号。UART0/USJのマトリクスsourceをこの
 *  番号へマップする（chip_serial_sio.cのsio_initialize）。割込み管理機構は
 *  _kernel_l1int_entry→demux→CRE_ISRのsio_isr（core_kernel_impl.c）で
 *  ディスパッチする。
 *
 *  ★実験記録（l3_uart_probe、step1/l3_uart_probe/、2026-07-09実施）：
 *  一時的にINT23（Level-3、_kernel_l3int_entry/_kernel_l3int_dispatch経由）へ
 *  配線し、Level-3割込み機構の実機検証に使用した。3つのバグ（INTLEVEL=3維持と
 *  unlock_cpu()の衝突、INTENABLEマスク移植漏れ、EXCSAVE3再アーム漏れ）を修正後、
 *  30回の割込み駆動出力完走を確認（詳細はcore_support.S/core_asm.incの
 *  各コメント参照）。検証完了につきINT5（Level-1）へ復帰済み。INT23/27は
 *  BTコントローラのLevel-3割込み専用予約（esp_shim_cfg.h参照）のため、
 *  BT系ビルドではUARTに使用しないこと。
 *
 *  USJ用のINT17（EXTERN_LEVEL、レベル1、core-isa.hで確認済み）は、割込み線
 *  棚卸し結果（INT0-3=Wi-Fi blob、INT5=UART0、INT6=tick、INT7=テスト用、
 *  INT13=IPI、INT23/27=BT(L3)）でも完全未使用、かつblobの動的選択範囲
 *  （1〜15、esp_shim_cfg.h参照）の外側のため採用した
 *  （design-usj-console.md §5）。
 */
#ifdef TOPPERS_S3_CONSOLE_USJ
#define USART_INTNO		17U				/* INT17（EXTERN_LEVEL, レベル1） */
#else /* TOPPERS_S3_CONSOLE_USJ */
#define USART_INTNO		5U				/* INT5（EXTERN_LEVEL, レベル1） */
#endif /* TOPPERS_S3_CONSOLE_USJ */
#define USART_INTPRI	(TMAX_INTPRI)	/* レベル1 */
#define USART_ISRPRI	1

/*
 *  ボーレート（UART0実機で必要。QEMU/ROM既定・USJでは未使用）
 */
#define BPS_SETTING		(115200)

/*
 * チップで共通なSIOドライバ定義（sio_opn_por等のプロトタイプ）
 */
#include "chip_serial.h"

#endif /* TOPPERS_TARGET_SERIAL_H */
