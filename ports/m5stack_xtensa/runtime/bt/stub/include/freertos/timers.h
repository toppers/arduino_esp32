/*
 *  NimBLE NPL（npl_os_freertos.c）用のFreeRTOS timers.h互換スタブ．
 *
 *  本ビルドはCONFIG_BT_NIMBLE_USE_ESP_TIMER=1で構成するため，NPLの
 *  calloutはesp_timer_*（bt/bt_shim.c実装）を使い，FreeRTOSソフト
 *  タイマ（xTimer*）経路は#else側でコンパイルされない．ただし
 *  nimble_npl_os.hがfreertos/timers.hを無条件includeするため，型と
 *  プロトタイプだけを提供する（xTimer*は本ビルドでは呼ばれない）．
 */
#ifndef TOPPERS_BT_FREERTOS_TIMERS_H
#define TOPPERS_BT_FREERTOS_TIMERS_H

#include "freertos/FreeRTOS.h"

typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

#ifndef TOPPERS_MACRO_ONLY
/*
 *  以下はCONFIG_BT_NIMBLE_USE_ESP_TIMER=1の本ビルドでは参照されない
 *  （プロトタイプのみ．呼ばれないため実体不要）．
 */
extern TimerHandle_t xTimerCreate(const char *pcTimerName,
								  TickType_t xTimerPeriod, UBaseType_t uxAutoReload,
								  void *pvTimerID, TimerCallbackFunction_t pxCallback);
extern BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait);
extern BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);
extern BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);
extern BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait);
extern BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
									 TickType_t xTicksToWait);
extern BaseType_t xTimerStopFromISR(TimerHandle_t xTimer, BaseType_t *pxWoken);
extern BaseType_t xTimerResetFromISR(TimerHandle_t xTimer, BaseType_t *pxWoken);
extern BaseType_t xTimerChangePeriodFromISR(TimerHandle_t xTimer,
											TickType_t xNewPeriod, BaseType_t *pxWoken);
extern BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer);
extern TickType_t xTimerGetExpiryTime(TimerHandle_t xTimer);
extern void *pvTimerGetTimerID(TimerHandle_t xTimer);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_TIMERS_H */
