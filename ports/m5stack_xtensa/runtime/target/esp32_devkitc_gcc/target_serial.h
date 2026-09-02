/*
 *  シリアルインタフェースドライバのターゲット依存部（無印ESP32 / Xtensa LX6）
 *  （非TECS版専用）
 *
 *  対話的シリアルコンソール（sample1のserial_rea_dat/serial_wri_dat）用の
 *  SIOドライバ定義。既定はUART0（DR_REG_UART_BASE=0x60000000）を割込み
 *  駆動で使う。UART0のレベル割込みは割込みマトリクスでCPU割込みINT5
 *  （EXTERN_LEVEL、レベル1）へ配線し、CFG_INT/CRE_ISR（chip_serial.cfg）で
 *  sio_isrを繋ぐ。低レベルsyslog出力（target_fput_log）は従来どおり
 *  ポーリング送信で行う。
 *
 *  [改変] 2026-07-10: ビルド時オプション`TOPPERS_S3_CONSOLE_USJ`
 *  （`-o "-DTOPPERS_S3_CONSOLE_USJ"`、`TOPPERS_S3_CPU_FREQ_MHZ`と同じ
 *  EXTRA_OFLAGS経由の切替）を指定すると、コンソールの実体がUSB-Serial-
 *  JTAG（USJ）に切り替わる。1ポートしか持たない実機（M5Stack系ボード等）
 *  向け。未定義時は本ファイルのマクロ展開結果は変更前と完全に同一
 *  （既定ビルドの非回帰、詳細設計 非公開作業記録/20260710-m5-arduino-idf-mode/
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
 *
 *  2026-08-03: `#undef` を前置した。chip_syssvc.h:76 が既定 2 を定義しており、
 *    本ターゲットはそれを 1 へ**上書きする**のが従来からの意図だが、
 *    `#undef` が無いために全 TU で "TNUM_PORT" redefined が出ていた。
 *    値も上書きの向きも変えていない（警告だけを消す）。
 */
#undef TNUM_PORT
#define TNUM_PORT		1

/*
 *  UART0/USJを配線するCPU割込み番号と割込み属性
 *
 *  USART_INTNOは素のCPU割込み番号。UART0/USJのマトリクスsourceをこの
 *  番号へマップする（chip_serial_sio.cのsio_initialize）。割込み管理機構は
 *  _kernel_l1int_entry→demux→CRE_ISRのsio_isr（core_kernel_impl.c）で
 *  ディスパッチする。
 *
 *  実験記録（l3_uart_probe、step1/l3_uart_probe/、2026-07-09実施）：
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
 *
 *  [改変] 2026-07-15: 無印ESP32（ESP32-D0WDQ6/PICO-D4、Xtensa LX6）の
 *  非USJ分岐（本#else節）を、従来のINT5からINT18へ変更した。実機
 *  （M5Stick、W3 BT Classic調査）でBTコントローラblob（classic ESP32用
 *  libbtdm_app.a）が`xt_set_interrupt_handler()`経由でベースバンド
 *  割込みハンドラをCPU割込み線5・7・8へ動的に確保することを実機syslog
 *  トレースで確認した。UART0コンソールが同じ線5を静的に予約していたため、
 *  esp_bt_controller_enable()実行後にBT側の割込み要因が誰にもクリア
 *  されずpendingし続け、sio_isrへの再突入が止まらなくなりタスクレベル
 *  実行（LOGTASKのwai_sem復帰含む）が飢餓状態になっていた
 *  （詳細：非公開作業記録/20260714-esp32-classic-wifi-bt/steering.md）。
 *  esp_shim_cfg.h既定の設計原則（「blobは1〜15の範囲を動的に使うため
 *  ターゲットのペリフェラルは16以降に退避する」）にUART0だけ追従できて
 *  いなかったのが真因。INT18はcore-isa.h（LX6, xtensa-esp32-elf付属）で
 *  XCHAL_INT18_LEVEL=1・XCHAL_INT18_TYPE=EXTERN_LEVELを確認済みで、
 *  USJのINT17と同型の性質を持つ。かつ上記の棚卸し済み使用線（0-3,5,6,7,
 *  8,13,17,23,27）のいずれとも衝突しない。実機（M5Stick）で
 *  bt_classic_ctrl_smokeを再検証し、LOGTASK起動メッセージ完全出力・
 *  esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)到達を確認済み。
 */
#ifdef TOPPERS_S3_CONSOLE_USJ
#define USART_INTNO		17U				/* INT17（EXTERN_LEVEL, レベル1） */
#else /* TOPPERS_S3_CONSOLE_USJ */
#define USART_INTNO		18U				/* INT18（EXTERN_LEVEL, レベル1）。
										   BTコントローラblobが動的に使う
										   INT5/7/8との衝突回避のため2026-07-15
										   にINT5から変更（上記コメント参照） */
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
