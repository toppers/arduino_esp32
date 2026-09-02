/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ用 FreeRTOS queue.h 互換ヘッダ
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  写像先は既存 esp_shim_queue_*（esp/shim/esp_shim.c）。design.md §2。
 *  ★M5GFX 単体経路では未使用（M0 棚卸し：queue は0件）。段階3の M5Unified
 *    に備えた最小写像。
 */

#ifndef TOPPERS_M5_FREERTOS_QUEUE_H
#define TOPPERS_M5_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

static inline QueueHandle_t
xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
	return (QueueHandle_t) esp_shim_queue_create((uint32_t) uxQueueLength,
												 (uint32_t) uxItemSize);
}

static inline void
vQueueDelete(QueueHandle_t xQueue)
{
	esp_shim_queue_delete(xQueue);
}

static inline BaseType_t
xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_send(xQueue, (void *) pvItemToQueue,
											(uint32_t) xTicksToWait, false);
}
#define xQueueSendToBack	xQueueSend

static inline BaseType_t
xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
				  TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_send(xQueue, (void *) pvItemToQueue,
											(uint32_t) xTicksToWait, true);
}

static inline BaseType_t
xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue, BaseType_t *woken)
{
	if (woken != NULL) {
		*woken = pdFALSE;
	}
	return (BaseType_t) esp_shim_queue_send_from_isr(xQueue, (void *) pvItemToQueue);
}

static inline BaseType_t
xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_recv(xQueue, pvBuffer, (uint32_t) xTicksToWait);
}

static inline UBaseType_t
uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_queue_msg_waiting(xQueue);
}

static inline UBaseType_t
uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_queue_spaces_available(xQueue);
}

static inline void
xQueueReset(QueueHandle_t xQueue)
{
	esp_shim_queue_reset(xQueue);
}


/*
 *  ★ESP-IDF の "WithCaps" 拡張（`freertos/idf_additions.h` 相当）。
 *  ★**IDF の `idf_additions.h` を include すると `freertos/stream_buffer.h` まで
 *  要求され、IDF の FreeRTOS カーネル本体を引き込むことになる**（我々の互換層と衝突する）。
 *  ⇒ ESP-IDF の i2s ドライバが実際に使う 5 本だけをここで供給する:
 *    xQueueCreateWithCaps / vQueueDeleteWithCaps /
 *    xSemaphoreCreateBinaryWithCaps / xSemaphoreCreateMutexWithCaps / vSemaphoreDeleteWithCaps
 *  ★`uxMemoryCaps`（配置メモリの指定）は**無視する**。本ポートは
 *  シム側のプールから確保するので、caps を選ぶ余地が無い。
 *  ★これは「機能を削った」のではなく「選択肢が 1 つしかない」という意味である。
 */
static inline QueueHandle_t
xQueueCreateWithCaps(UBaseType_t uxQueueLength, UBaseType_t uxItemSize,
					 UBaseType_t uxMemoryCaps)
{
	(void) uxMemoryCaps;
	return xQueueCreate(uxQueueLength, uxItemSize);
}

static inline void
vQueueDeleteWithCaps(QueueHandle_t xQueue)
{
	vQueueDelete(xQueue);
}


/*  ★ISR 文脈からの受信／満杯判定（ESP-IDF の I2S ドライバが使う。実測で判明）。
 *  ★`pxHigherPriorityTaskWoken` は本ポートでは**使わない**（`iwup_tsk` が
 *  起床要求を出すので、ISR 出口での明示的な yield は不要）。
 *  呼び出し側が参照するので**触らずに残す**（勝手に pdTRUE にしない）。 */
static inline BaseType_t
xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
					 BaseType_t *pxHigherPriorityTaskWoken)
{
	(void) pxHigherPriorityTaskWoken;
	/*  ★タイムアウト 0 ＝ 待たない。ISR から待つことはできない。 */
	return (BaseType_t) esp_shim_queue_recv(xQueue, pvBuffer, 0U);
}

static inline BaseType_t
xQueueIsQueueFullFromISR(QueueHandle_t xQueue)
{
	return (BaseType_t)((esp_shim_queue_spaces_available(xQueue) == 0U) ? 1 : 0);
}

#endif /* TOPPERS_M5_FREERTOS_QUEUE_H */
