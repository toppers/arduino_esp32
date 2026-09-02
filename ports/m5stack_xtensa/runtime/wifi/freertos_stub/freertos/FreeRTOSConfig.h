/*
 *  BlueDroidホストコンパイル
 *  専用のFreeRTOSConfig.hスタブ。
 *
 *  osi/thread.h(bt/common/osi/include)が"freertos/FreeRTOSConfig.h"を
 *  #includeする（実行時にはFreeRTOS本体は使わずesp_shim.c/bt_shim.cへ
 *  委譲するため、ここではコンパイルが通る程度の定数値を供給すれば足りる。
 *  実際のタスク優先度・スタックサイズはFMP3側cfgファイル（bt.cfg等）で
 *  決まり、本ファイルの値は実行時には使用されない）。
 *
 *  値はESP-IDF既定（sdkconfig.hのCONFIG_FREERTOS_*系）を踏襲。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configMAX_PRIORITIES            25
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ              100
#endif
#define configMINIMAL_STACK_SIZE        768
#define configMAX_TASK_NAME_LEN         16
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_COUNTING_SEMAPHORES   1
#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       1
#define configTIMER_QUEUE_LENGTH        10
#define configTIMER_TASK_STACK_DEPTH    2048
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configNUM_CORES                 1

#define INCLUDE_vTaskDelay              1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_xTaskGetSchedulerState  1

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms)  ((ms) / (1000 / configTICK_RATE_HZ))
#endif

#endif /* FREERTOS_CONFIG_H */
