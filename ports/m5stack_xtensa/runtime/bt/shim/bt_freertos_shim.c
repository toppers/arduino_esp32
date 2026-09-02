/*
 * Real FreeRTOS symbols for the prebuilt BlueDroid archives.
 *
 * ★Why this file exists at all.
 *
 * Upstream built BlueDroid from source, so its FreeRTOS "shim" could live
 * entirely in headers: bt/stub/include/freertos/*.h are static inline wrappers
 * that rewrite xQueueSend() into esp_shim_ring_send() at every call site. That
 * works only while you own the call sites.
 *
 * This port links the M5Stack core's *compiled* libbt.a and libbtdm_app.a
 * instead. Their call sites were compiled by Espressif against the real
 * FreeRTOS ABI, so they reference xQueueGenericSend and friends by name, and
 * no header can reach them. This file supplies those names, mapped onto the
 * same esp_shim_* primitives the headers use, so both paths end up in one
 * implementation rather than two that drift.
 *
 * The signatures here are the FreeRTOS ones the archives were compiled
 * against, not the convenience macros: FreeRTOS's xQueueSend/xQueueReceive
 * are macros over xQueueGenericSend, so that is what the object code calls.
 *
 * The stub headers are deliberately NOT included: they define the same names
 * as static inline functions, which would collide with the external
 * definitions below. The primitives are declared here instead.
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*  FreeRTOS ABI types, as the archives were compiled with them. */
typedef int32_t		BaseType_t;
typedef uint32_t	UBaseType_t;
typedef uint32_t	TickType_t;
typedef void		*QueueHandle_t;
typedef void		*SemaphoreHandle_t;
typedef void		*TaskHandle_t;
typedef void		*EventGroupHandle_t;
typedef uint32_t	EventBits_t;
typedef void		(*TaskFunction_t)(void *);

#define pdTRUE		((BaseType_t) 1)
#define pdFALSE		((BaseType_t) 0)
#define BT_PORT_MAX_DELAY	0xffffffffUL

/*  The shim primitives. Same set the stub headers map onto. */
extern void	*esp_shim_ring_create(uint32_t len, uint32_t item_size);
extern void	esp_shim_ring_delete(void *que);
extern int32_t	esp_shim_ring_send(void *que, void *item,
								   uint32_t block_time_tick);
extern int32_t	esp_shim_ring_send_from_isr(void *que, void *item);
extern int32_t	esp_shim_ring_recv(void *que, void *item,
								   uint32_t block_time_tick);
extern uint32_t	esp_shim_ring_msg_waiting(void *que);
extern uint32_t	esp_shim_ring_spaces_available(void *que);

extern void	*esp_shim_sem_create(uint32_t max, uint32_t init);
extern void	esp_shim_sem_delete(void *sem);
extern int32_t	esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t	esp_shim_sem_give(void *sem);

extern void	*esp_shim_mutex_create(bool_t recursive);
extern void	esp_shim_mutex_delete(void *mtx);
extern int32_t	esp_shim_mutex_lock(void *mtx);
extern int32_t	esp_shim_mutex_unlock(void *mtx);

extern void	*esp_shim_flag_create(void);
extern void	esp_shim_flag_delete(void *flg);
extern uint32_t	esp_shim_flag_set_bits(void *flg, uint32_t bits_to_set);
extern uint32_t	esp_shim_flag_clear_bits(void *flg, uint32_t bits_to_clear);
extern uint32_t	esp_shim_flag_wait_bits(void *flg, uint32_t bits_to_wait_for,
										bool_t clear_on_exit,
										bool_t wait_for_all,
										uint32_t block_time_tick);

extern int32_t	esp_shim_task_create_pinned(void (*entry)(void *),
											const char *name,
											uint32_t stack_size, void *param,
											uint32_t freertos_prio,
											void **task_handle,
											uint32_t core_id);
extern void	esp_shim_task_delete(void *task_handle);
extern void	esp_shim_task_delay(uint32_t tick);
extern int64_t	esp_shim_time_us(void);

extern void	esp_shim_bt_enter_critical(void *mux);
extern void	esp_shim_bt_exit_critical(void *mux);
extern uint32_t	esp_shim_int_disable(void);
extern void	esp_shim_int_restore(uint32_t state);

/*
 *  Queues.
 *
 *  ucQueueType and xCopyPosition are FreeRTOS's own discriminators. The ring
 *  has no notion of either: queueSEND_TO_FRONT is used by FreeRTOS only for
 *  priority insertion, which BlueDroid does not depend on, and the type byte
 *  only separates queues from the semaphores that FreeRTOS implements as
 *  queues. Both are ignored on purpose, not by oversight.
 */
QueueHandle_t
xQueueGenericCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize,
					uint8_t ucQueueType)
{
	(void) ucQueueType;
	return((QueueHandle_t) esp_shim_ring_create(uxQueueLength, uxItemSize));
}

void
vQueueDelete(QueueHandle_t xQueue)
{
	esp_shim_ring_delete(xQueue);
}

BaseType_t
xQueueGenericSend(QueueHandle_t xQueue, const void *pvItemToQueue,
				  TickType_t xTicksToWait, BaseType_t xCopyPosition)
{
	(void) xCopyPosition;
	return((BaseType_t) esp_shim_ring_send(xQueue, (void *) pvItemToQueue,
										   (uint32_t) xTicksToWait));
}

BaseType_t
xQueueGenericSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
						 BaseType_t *pxHigherPriorityTaskWoken,
						 BaseType_t xCopyPosition)
{
	(void) xCopyPosition;
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return((BaseType_t) esp_shim_ring_send_from_isr(xQueue,
													(void *) pvItemToQueue));
}

BaseType_t
xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
	return((BaseType_t) esp_shim_ring_recv(xQueue, pvBuffer,
										   (uint32_t) xTicksToWait));
}

UBaseType_t
uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
	return((UBaseType_t) esp_shim_ring_spaces_available((void *) xQueue));
}

UBaseType_t
uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
	return((UBaseType_t) esp_shim_ring_msg_waiting((void *) xQueue));
}

/*
 *  Semaphores and mutexes.
 *
 *  FreeRTOS implements both as queues, so the archives call the queue entry
 *  points for them. They are routed to the semaphore and mutex primitives
 *  rather than to the ring, which is what the stub headers do too.
 */
SemaphoreHandle_t
xQueueCreateMutex(uint8_t ucQueueType)
{
	/*  queueQUEUE_TYPE_RECURSIVE_MUTEX == 4 in FreeRTOS. */
	return((SemaphoreHandle_t) esp_shim_mutex_create(ucQueueType == 4U));
}

SemaphoreHandle_t
xQueueCreateCountingSemaphore(UBaseType_t uxMaxCount,
							  UBaseType_t uxInitialCount)
{
	return((SemaphoreHandle_t) esp_shim_sem_create(uxMaxCount, uxInitialCount));
}

BaseType_t
xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
	return((BaseType_t) esp_shim_sem_take(xQueue, (uint32_t) xTicksToWait));
}

BaseType_t
xQueueGiveFromISR(QueueHandle_t xQueue,
				  BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return((BaseType_t) esp_shim_sem_give(xQueue));
}

BaseType_t
xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
	(void) xTicksToWait;
	return((BaseType_t) esp_shim_mutex_lock(xMutex));
}

BaseType_t
xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
	return((BaseType_t) esp_shim_mutex_unlock(xMutex));
}

/*
 *  Event groups.
 */
EventGroupHandle_t
xEventGroupCreate(void)
{
	return((EventGroupHandle_t) esp_shim_flag_create());
}

void
vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
	esp_shim_flag_delete(xEventGroup);
}

EventBits_t
xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
	return((EventBits_t) esp_shim_flag_set_bits(xEventGroup,
												(uint32_t) uxBitsToSet));
}

EventBits_t
xEventGroupClearBits(EventGroupHandle_t xEventGroup,
					 const EventBits_t uxBitsToClear)
{
	return((EventBits_t) esp_shim_flag_clear_bits(xEventGroup,
												  (uint32_t) uxBitsToClear));
}

EventBits_t
xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
					const EventBits_t uxBitsToWaitFor,
					const BaseType_t xClearOnExit,
					const BaseType_t xWaitForAllBits,
					TickType_t xTicksToWait)
{
	return((EventBits_t) esp_shim_flag_wait_bits(xEventGroup,
												 (uint32_t) uxBitsToWaitFor,
												 xClearOnExit != pdFALSE,
												 xWaitForAllBits != pdFALSE,
												 (uint32_t) xTicksToWait));
}

/*
 *  Tasks.
 */
BaseType_t
xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
						const uint32_t usStackDepth, void *pvParameters,
						UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask,
						const BaseType_t xCoreID)
{
	return((BaseType_t) esp_shim_task_create_pinned(pvTaskCode, pcName,
													usStackDepth, pvParameters,
													uxPriority,
													(void **) pxCreatedTask,
													(uint32_t) xCoreID));
}

void
vTaskDelete(TaskHandle_t xTaskToDelete)
{
	esp_shim_task_delete(xTaskToDelete);
}

void
vTaskDelay(const TickType_t xTicksToDelay)
{
	esp_shim_task_delay((uint32_t) xTicksToDelay);
}

/*  BlueDroid only logs this; FMP3 tasks have no runtime name to hand back. */
char *
pcTaskGetName(TaskHandle_t xTaskToQuery)
{
	static char	unnamed[] = "fmp3";

	(void) xTaskToQuery;
	return(unnamed);
}

/*  Priority changes are not modelled: FMP3 task priorities are static. */
void
vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
	(void) xTask;
	(void) uxNewPriority;
}

/*
 *  Port layer.
 *
 *  The critical sections are the BT ones bt_shim.c owns (per-core mux), not
 *  the single-core Wi-Fi variant.
 */
BaseType_t
xPortEnterCriticalTimeout(void *mux, int timeout)
{
	(void) timeout;
	esp_shim_bt_enter_critical(mux);
	return(pdTRUE);
}

void
vPortExitCritical(void *mux)
{
	esp_shim_bt_exit_critical(mux);
}

/*  FMP3 dispatches on its own; there is no cooperative yield to request. */
void
vPortYield(void)
{
}

BaseType_t
xPortInIsrContext(void)
{
	return(sns_ctx() ? pdTRUE : pdFALSE);
}
