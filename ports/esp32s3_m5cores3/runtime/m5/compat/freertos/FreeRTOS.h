/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ用 FreeRTOS 互換ヘッダ
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  M5GFX/M5Unified が include する FreeRTOS API を，本ポート既存の esp_shim_*
 *  へ写像する互換ヘッダ（ヘッダオンリー・static inline）。
 *   §2。
 *
 *  ■なぜ esp/bt/stub/include/freertos/ を再利用しないか（★重複回避の判断）
 *    既に BT コントローラ（bt.c）用の FreeRTOS 互換ヘッダが
 *    esp/bt/stub/include/freertos/ に存在するが，そちらは
 *      - 型を <platform/os.h>（Wi-Fi/BT HAL スタブ）に依存し，
 *      - クリティカルセクションを esp_shim_bt_enter/exit_critical（実装は
 *        esp/bt/bt_shim.c＝BT ビルド専用）へ写像する
 *    ため，BT を含まない M5 単体ビルドに BT 依存を持ち込んでしまう。
 *    M5 経路（M0 棚卸し：M5GFX 単体は vTaskDelay のみ使用，mutex/queue は0件）
 *    には BT スピンロックは不要で，クリティカルセクションは design.md §2 の
 *    とおり esp_shim_int_disable/restore（コア0固定規約）へ写像するのが正しい。
 *    そこで M5 用は本ファイル群（自己完結・esp_shim 直写像）を別途用意する。
 *    両者は include パスが排他（BT ビルド or M5 ビルド）なので衝突しない。
 *
 *  ■写像先はすべて既存 esp_shim_*（esp/shim/esp_shim.c）。新プリミティブは
 *    実装しない（＝「重複実装しない」。宣言は esp_shim_public.h に集約）。
 */

#ifndef TOPPERS_M5_FREERTOS_H
#define TOPPERS_M5_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

#include "esp_shim_public.h"		/* esp_shim_* 宣言（single source of truth） */

/*  FreeRTOS 基本型（ESP-IDF/本ポート BT スタブと同一の実体幅に合わせる：
 *  esp/config/esp32/hal_stub_include/platform/os.h と一致）。 */
#ifndef TOPPERS_M5_FREERTOS_TYPES
#define TOPPERS_M5_FREERTOS_TYPES
typedef uint32_t	TickType_t;
typedef uint32_t	UBaseType_t;
typedef int32_t		BaseType_t;
typedef void (*TaskFunction_t)(void *);
#endif /* TOPPERS_M5_FREERTOS_TYPES */

#define pdFALSE		((BaseType_t) 0)
#define pdTRUE		((BaseType_t) 1)
#define pdPASS		((BaseType_t) 1)
#define pdFAIL		((BaseType_t) 0)

/*  tick=1ms（esp_shim 側も 1ms 前提。ESP_SHIM_BLOCK_FOREVER と同値） */
#define portMAX_DELAY		((TickType_t) 0xffffffffUL)
#define portTICK_PERIOD_MS	((TickType_t) 1)
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ	1000
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms)	((TickType_t)(ms))	/* tick=1ms */
#endif

/*  コア数（ESP32-S3 デュアルコア。M5 は原則コア0固定＝SHIM_LOCK 規約） */
#ifndef portNUM_PROCESSORS
#define portNUM_PROCESSORS	2
#endif
#ifndef configNUM_CORES
#define configNUM_CORES		portNUM_PROCESSORS
#endif

/*
 *  クリティカルセクション：
 *
 *  ★**退避状態を mux に持ってはならない。**
 *      enter: mux->saved = esp_shim_int_disable();
 *      exit : esp_shim_int_restore(mux->saved);
 *    この形は同一 mux でネストすると **内側 enter が saved を「割込み禁止状態」で
 *    上書き**するため，**外側 exit が禁止状態を復元し割込みが二度と開かない**。
 *    M5Unified は FreeRTOS のクリティカルセクションを使うので，実体を
 *    供給した瞬間にこの欠陥が有効化される。
 *
 *  ■本実装
 *    退避状態は **ネストカウンタ付きの大域**（m5/compat/esp_shim_m5_ext.c）で
 *    持ち，最外の enter でのみ退避・最外の exit でのみ復元する。
 *    mux は無視する（＝異なる mux をまたぐネストも正しく動く）。
 *    設計根拠・loc_cpu/unl_cpu を使わない理由は同ファイルの詳細コメント参照。
 *    ★コア間排他は「M5 タスクはコア0固定」規約で担保する（本写像は割込み
 *      禁止のみでコア間ロックはしない）。規約が破れた場合は
 *      esp_shim_m5_crit_viol_core に記録され，テストが検出する。
 *    ★区間内でブロッキング API を呼ばないこと（本ポートの E_CTX 特性）。
 *
 *  ■検証
 *    m5/compat/test/crit_nest_test.c が positive/negative control 込みで実証する。
 */
#ifdef __cplusplus
extern "C" {
#endif
extern void esp_shim_m5_enter_critical(void);
extern void esp_shim_m5_exit_critical(void);
extern uint32_t esp_shim_m5_read_ps(void);
extern volatile uint32_t esp_shim_m5_crit_nest;
extern volatile uint32_t esp_shim_m5_crit_viol_core;
extern volatile uint32_t esp_shim_m5_crit_viol_under;
#ifdef __cplusplus
}
#endif

/*  portMUX_TYPE は **ダミー**（実体はネストカウンタ側。mux は無視される）。 */
typedef struct {
	uint32_t	dummy;		/* 未使用（退避状態は大域ネストカウンタが持つ） */
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED	{ 0U }

static inline void
vPortEnterCritical(portMUX_TYPE *mux)
{
	(void) mux;
	esp_shim_m5_enter_critical();
}
static inline void
vPortExitCritical(portMUX_TYPE *mux)
{
	(void) mux;
	esp_shim_m5_exit_critical();
}

#define taskENTER_CRITICAL(mux)			vPortEnterCritical(mux)
#define taskEXIT_CRITICAL(mux)			vPortExitCritical(mux)
#define taskENTER_CRITICAL_ISR(mux)		vPortEnterCritical(mux)
#define taskEXIT_CRITICAL_ISR(mux)		vPortExitCritical(mux)
#define portENTER_CRITICAL(mux)			vPortEnterCritical(mux)
#define portEXIT_CRITICAL(mux)			vPortExitCritical(mux)

/*  ISR 文脈からの明示 yield は不要（ASP3 は割込み出口で自動再スケジュール）。 */
#define portYIELD_FROM_ISR(...)			((void) 0)


/*  ★`portENTER_CRITICAL_SAFE` / `portEXIT_CRITICAL_SAFE`
 *  ★**ここにも置く理由**: `portmacro.h` にも同じ定義があるが、
 *  **本ヘッダは `portmacro.h` を include していない**。ESP-IDF の `gdma.c` は
 *  `FreeRTOS.h` しか include しないので、そちらだけに置くと**届かない**
 *  。
 *  ★`#ifndef` で守ってあるので二重定義にはならない。
 *  ★m5 は単一コアなので CPU ロックで足りる。SMP 化したら見直すこと。 */
#ifndef portENTER_CRITICAL_SAFE
#define portENTER_CRITICAL_SAFE(mux)	do { (void)(mux); loc_cpu(); } while (0)
#define portEXIT_CRITICAL_SAFE(mux)		do { (void)(mux); unl_cpu(); } while (0)
#endif

/*
 *  ★`portENTER_CRITICAL_ISR` / `portEXIT_CRITICAL_ISR` も**ここに置く**。
 *  ★`spi_master.c` は `portmacro.h` を直接 include せず `FreeRTOS.h` だけを include する
 *  ——gdma.c で同じことが起きた（`portENTER_CRITICAL_SAFE` を両方に置いた前例）。
 *  ★**「片方に置けば足りる」は成り立たない。**
 */
#ifndef portENTER_CRITICAL_ISR
#define portENTER_CRITICAL_ISR(mux)   do { (void)(mux); loc_cpu(); } while (0)
#endif
#ifndef portEXIT_CRITICAL_ISR
#define portEXIT_CRITICAL_ISR(mux)    do { (void)(mux); unl_cpu(); } while (0)
#endif

#endif /* TOPPERS_M5_FREERTOS_H */
