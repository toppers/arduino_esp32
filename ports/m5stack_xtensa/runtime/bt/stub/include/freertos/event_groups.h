/*
 *  BlueDroid SPPプロファイル（btc_spp.c）用のFreeRTOS event_groups.h
 *  互換ヘッダ（コンパイル専用スタブ、W3④）。
 *
 *  btc_spp.cはtx_event_group（SLOT_WRITE_BIT/SLOT_CLOSE_BIT）1個のみを
 *  xEventGroupCreate()で生成し、SetBits/ClearBits/WaitBits/Deleteの
 *  基本5関数だけを使う（grep確認済み）。実体は
 *  esp/shim/esp_shim.cのイベントフラグプール（CRE_FLGプール、
 *  TOPPERS_ESP32_BT_BLUEDROID_CLASSIC限定）へ委譲する。
 */
#ifndef TOPPERS_BT_FREERTOS_EVENT_GROUPS_H
#define TOPPERS_BT_FREERTOS_EVENT_GROUPS_H

#include "freertos/FreeRTOS.h"

typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;

#ifndef TOPPERS_MACRO_ONLY
extern void *esp_shim_flag_create(void);
extern void esp_shim_flag_delete(void *flg);
extern uint32_t esp_shim_flag_set_bits(void *flg, uint32_t bits_to_set);
extern uint32_t esp_shim_flag_clear_bits(void *flg, uint32_t bits_to_clear);
extern uint32_t esp_shim_flag_wait_bits(void *flg, uint32_t bits_to_wait_for,
										 bool_t clear_on_exit, bool_t wait_for_all,
										 uint32_t block_time_tick);

static inline EventGroupHandle_t
xEventGroupCreate(void)
{
	return (EventGroupHandle_t) esp_shim_flag_create();
}

static inline void
vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
	esp_shim_flag_delete(xEventGroup);
}

static inline EventBits_t
xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
	return (EventBits_t) esp_shim_flag_set_bits(xEventGroup, (uint32_t) uxBitsToSet);
}

static inline EventBits_t
xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
	return (EventBits_t) esp_shim_flag_clear_bits(xEventGroup, (uint32_t) uxBitsToClear);
}

static inline EventBits_t
xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor,
					 const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits,
					 TickType_t xTicksToWait)
{
	return (EventBits_t) esp_shim_flag_wait_bits(xEventGroup, (uint32_t) uxBitsToWaitFor,
												  xClearOnExit ? true : false,
												  xWaitForAllBits ? true : false,
												  (uint32_t) xTicksToWait);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_EVENT_GROUPS_H */
