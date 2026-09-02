/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ用 FreeRTOS semphr.h 互換ヘッダ
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  写像先は既存 esp_shim_sem_* / esp_shim_mutex_*（esp/shim/esp_shim.c）。
 *  BT スタブ（esp/bt/stub/include/freertos/semphr.h）と同じ写像規約に合わせる。
 *  ★M5GFX 単体経路では未使用（M0 棚卸し：mutex/semaphore は0件）。段階3の
 *    M5Unified（Speaker/Mic 等）に備えた最小写像。
 */

#ifndef TOPPERS_M5_FREERTOS_SEMPHR_H
#define TOPPERS_M5_FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

static inline SemaphoreHandle_t
xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
	return (SemaphoreHandle_t) esp_shim_sem_create((uint32_t) uxMaxCount,
												   (uint32_t) uxInitialCount);
}

/*  非再帰ミューテックスは max=1/init=1 のセマフォで代用（BT スタブと同一）。 */
static inline SemaphoreHandle_t
xSemaphoreCreateMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 1U);
}

/*  バイナリセマフォは生成直後は空（取得前に give が必要）＝max=1/init=0。 */
static inline SemaphoreHandle_t
xSemaphoreCreateBinary(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 0U);
}

static inline void
vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	esp_shim_sem_delete(xSemaphore);
}

static inline BaseType_t
xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_sem_take(xSemaphore, (uint32_t) xTicksToWait);
}

static inline BaseType_t
xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	return (BaseType_t) esp_shim_sem_give(xSemaphore);
}

/*  ISR 版：本ポートでは同一経路（psnd 直送の実績経路）。 */
static inline BaseType_t
xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *woken)
{
	if (woken != NULL) {
		*woken = pdFALSE;
	}
	return (BaseType_t) esp_shim_sem_give(xSemaphore);
}

static inline BaseType_t
xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore, BaseType_t *woken)
{
	if (woken != NULL) {
		*woken = pdFALSE;
	}
	return (BaseType_t) esp_shim_sem_take(xSemaphore, 0U);
}

/*
 *  再帰ミューテックス（esp_shim_mutex_* は owner/count を追跡）。
 */
static inline SemaphoreHandle_t
xSemaphoreCreateRecursiveMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_mutex_create(true);
}
static inline BaseType_t
xSemaphoreTakeRecursive(SemaphoreHandle_t mtx, TickType_t xTicksToWait)
{
	(void) xTicksToWait;
	return (BaseType_t) esp_shim_mutex_lock(mtx);
}
static inline BaseType_t
xSemaphoreGiveRecursive(SemaphoreHandle_t mtx)
{
	return (BaseType_t) esp_shim_mutex_unlock(mtx);
}


/*  ★ESP-IDF の "WithCaps" 拡張（理由は queue.h の同名コメント参照）。
 *  ★`uxMemoryCaps` は無視する（シムのプールから確保するので選択肢が無い）。 */
static inline SemaphoreHandle_t
xSemaphoreCreateBinaryWithCaps(UBaseType_t uxMemoryCaps)
{
	(void) uxMemoryCaps;
	return xSemaphoreCreateBinary();
}

static inline SemaphoreHandle_t
xSemaphoreCreateMutexWithCaps(UBaseType_t uxMemoryCaps)
{
	(void) uxMemoryCaps;
	return xSemaphoreCreateMutex();
}

static inline void
vSemaphoreDeleteWithCaps(SemaphoreHandle_t xSemaphore)
{
	vSemaphoreDelete(xSemaphore);
}

#endif /* TOPPERS_M5_FREERTOS_SEMPHR_H */
