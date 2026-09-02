/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ用 FreeRTOS task.h 互換ヘッダ
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  写像先は既存 esp_shim_task_*（esp/shim/esp_shim.c）。design.md §2。
 *  ★M5GFX 単体経路が実際に使うのは vTaskDelay のみ（M0 棚卸し）。他は
 *    M5Unified 統合（段階3）に備えた design.md 想定範囲の最小写像。
 */

#ifndef TOPPERS_M5_FREERTOS_TASK_H
#define TOPPERS_M5_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

/*  コア親和性指定なし（xTaskCreatePinnedToCore の coreID は無視する規約） */
#define tskNO_AFFINITY	((BaseType_t) 0x7fffffff)

#define taskSCHEDULER_NOT_STARTED	0
#define taskSCHEDULER_SUSPENDED		1
#define taskSCHEDULER_RUNNING		2

/*
 *  遅延：vTaskDelay(n) → esp_shim_task_delay(n)（tick=1ms → dly_tsk μs）。
 *  ★M5GFX がパネル検出のポーリング遅延で使う唯一の FreeRTOS API。
 */
static inline void
vTaskDelay(TickType_t ticks)
{
	esp_shim_task_delay((uint32_t) ticks);
}

/*  同一優先度の他タスクへ譲る（esp_shim_task_yield＝dly_tsk(0)相当）。 */
static inline void
taskYIELD(void)
{
	esp_shim_task_yield();
}
#define portYIELD()	taskYIELD()

/*
 *  タスク生成（PinnedToCore の coreID は無視＝コア0固定。SHIM_LOCK 規約）。
 */
static inline BaseType_t
xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
						 uint32_t usStackDepth, void *pvParameters,
						 UBaseType_t uxPriority, TaskHandle_t *pvCreatedTask,
						 BaseType_t xCoreID)
{
	(void) xCoreID;
	return (BaseType_t) esp_shim_task_create(pvTaskCode, pcName, usStackDepth,
											  pvParameters, (uint32_t) uxPriority,
											  pvCreatedTask);
}

static inline BaseType_t
xTaskCreate(TaskFunction_t pvTaskCode, const char *pcName,
			uint32_t usStackDepth, void *pvParameters,
			UBaseType_t uxPriority, TaskHandle_t *pvCreatedTask)
{
	return (BaseType_t) esp_shim_task_create(pvTaskCode, pcName, usStackDepth,
											  pvParameters, (uint32_t) uxPriority,
											  pvCreatedTask);
}

static inline void
vTaskDelete(TaskHandle_t task)
{
	esp_shim_task_delete(task);		/* NULL=自タスク */
}

/*
 *  ★task notification（M5Unified の Speaker/Mic が使う唯一の同期方式）。
 *  実測: 呼び出し 9 箇所（`ulTaskNotifyTake` ×5 / `xTaskNotifyGive` ×4）で、
 *  **すべてタスク文脈**・`*FromISR` は 0 件。⇒ タスク文脈 API へ写して安全。
 *  ★`portMAX_DELAY`（無限待ち）は 0xFFFFFFFF としてシムへ渡す。
 */
static inline uint32_t
ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
	return esp_shim_task_notify_take((int) xClearCountOnExit,
									 (uint32_t) xTicksToWait);
}

static inline BaseType_t
xTaskNotifyGive(TaskHandle_t xTaskToNotify)
{
	esp_shim_task_notify_give((void *) xTaskToNotify);
	return (BaseType_t) 1;		/* pdPASS */
}

static inline TaskHandle_t
xTaskGetCurrentTaskHandle(void)
{
	return (TaskHandle_t) esp_shim_task_get_current();
}

static inline BaseType_t
xTaskGetSchedulerState(void)
{
	return (BaseType_t) taskSCHEDULER_RUNNING;	/* sta_ker 後は常時 RUNNING */
}

/*  現在の tick（1ms 単位）。SYSTIMER（μs）を ms 換算（レジスタ読取のみ）。 */
static inline TickType_t
xTaskGetTickCount(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}
static inline TickType_t
xTaskGetTickCountFromISR(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}

#endif /* TOPPERS_M5_FREERTOS_TASK_H */
