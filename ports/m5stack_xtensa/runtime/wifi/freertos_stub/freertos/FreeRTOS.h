/*
 *  BTコントローラ（bt.c）用のFreeRTOS互換ヘッダ（コンパイル専用スタブ）
 *
 *  bt.cはosi関数テーブル越しではなく，FreeRTOS APIをインラインで直接
 *  呼ぶ（Wi-Fi blobとの本質的な違い．docs/dev/esp-idf-integration.md
 *  Phase D参照）．実体はesp/esp_shim.cの既存プリミティブへ委譲する
 *  （新しいプリミティブの発明はしない．ヘッダ単位でのシムという
 *  差し替え粒度が新規なだけ）．
 */
#ifndef TOPPERS_BT_FREERTOS_H
#define TOPPERS_BT_FREERTOS_H

#include <stdint.h>			/* uint8_t 等（実FreeRTOS環境も供給。wpa includes.h が
							   -D__ets__ でシステムヘッダをskipし本ヘッダ経由の型供給に依存） */
#include <stddef.h>			/* size_t/NULL */
#include <platform/os.h>	/* TickType_t/UBaseType_t/BaseType_t */

#define pdFALSE		0
#define pdTRUE		1
#define pdPASS		1
#define pdFAIL		0

/*  FreeRTOSキュー系の戻り値（NimBLE NPLがxQueueReceive失敗判定に使用） */
#ifndef errQUEUE_EMPTY
#define errQUEUE_EMPTY	0
#endif
#ifndef errQUEUE_FULL
#define errQUEUE_FULL	0
#endif

#define portMAX_DELAY		0xffffffffUL	/* ESP_SHIM_BLOCK_FOREVERと同値 */
#define portTICK_PERIOD_MS	1		/* esp_shim側もtick=1msの前提 */
/*  レビュー指摘#9：FreeRTOSConfig.h（BlueDroidホスト向けosi/thread.h専用
 *  スタブ、値は同ファイルのコメントどおり実行時未使用のコンパイル専用値）
 *  もconfigTICK_RATE_HZを定義するため、同一TUで両ヘッダがincludeされた
 *  場合の無guard多重定義（include順で値が100/1000の間でズレる）を防ぐ。
 *  現状は両ヘッダを同時にincludeするソースが無く実害は無いが（osi/thread.h
 *  はESP-IDF側の実ソースであり本ポートではコンパイルしていない＝latent）、
 *  #ifndefで先着優先にして将来の回帰を防ぐ。 */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ	1000		/* tick=1ms（NimBLE NPLの時間換算用） */
#endif

/*
 *  NimBLEのホストタスク生成・コア親和で参照するFreeRTOS構成マクロ．
 *  ESP-IDF既定に倣う（configMAX_PRIORITIES=25）．esp_shim_task_createは
 *  優先度を一律（ESP_SHIM_WIFI_TASK_PRI）に写像するため，実値は
 *  ランタイム動作に影響しない（コンパイル用）．portNUM_PROCESSORSは
 *  ESP32-S3の物理コア数（2）．
 */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES	25
#endif
#ifndef portNUM_PROCESSORS
#define portNUM_PROCESSORS		2
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms)	((TickType_t)(ms))	/* tick=1ms */
#endif

typedef void (*TaskFunction_t)(void *);

/*
 *  クリティカルセクション（SMP対応版．ESP32-S3はPRC_NUM=2のデュアルコア
 *  のため，ESP32-C3向けの「mux変数自体に退避値を格納する」実装は使えない
 *  （同一muxの入れ子取得で退避値を上書きし，割込み禁止が残置される既知の
 *  バグ．姉妹プロジェクトC3のPhase D-1で確認・修正済み．docs/bt-shim.md
 *  1039-1087行のS3引き継ぎメモ参照）。
 *
 *  実体はesp/bt/bt_shim.cのesp_shim_bt_enter/exit_critical()に委譲する：
 *  コア単位の割込みネストカウンタ＋saved状態，かつmuxは「所有コア＋
 *  再入カウント」を持つ実スピンロックとして扱う（ESP-IDF本家spinlock_t
 *  同型）。portMUX_TYPEの2フィールド構造はbt_shim.c側の
 *  struct esp_shim_bt_muxと完全に一致させること。
 */
typedef struct {
	uint32_t	owner;	/* 0=未取得，取得中は「コアindex+1」 */
	uint32_t	count;	/* 再入回数 */
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED	{ 0, 0 }

#ifndef TOPPERS_MACRO_ONLY
extern void esp_shim_bt_enter_critical(void *mux);
extern void esp_shim_bt_exit_critical(void *mux);
#endif

#define portENTER_CRITICAL(mux)		esp_shim_bt_enter_critical(mux)
#define portEXIT_CRITICAL(mux)			esp_shim_bt_exit_critical(mux)
#define portENTER_CRITICAL_ISR(mux)	esp_shim_bt_enter_critical(mux)
#define portEXIT_CRITICAL_ISR(mux)		esp_shim_bt_exit_critical(mux)
#define portENTER_CRITICAL_SAFE(mux)	esp_shim_bt_enter_critical(mux)
#define portEXIT_CRITICAL_SAFE(mux)	esp_shim_bt_exit_critical(mux)

/*
 *  ASP3は割込み出口で自動的に再スケジューリングするため，ISR文脈からの
 *  明示的なyield要求は不要（no-op）．bt.cは引数無し／有り両方の
 *  呼び方をするため可変長引数マクロにする．
 */
#define portYIELD_FROM_ISR(...)	((void) 0)

#endif /* TOPPERS_BT_FREERTOS_H */
