/*
 * This application exercises the public M5 FreeRTOS compatibility headers. The API
 * bodies remain in the existing esp32_s3 m5 shim; this repository supplies
 * only the application-side probe.
 */
#include "phase4_freertos_app.h"

#include <stdint.h>
#include <target_syssvc.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

volatile uint32_t phase4_freertos_probe_failures;
volatile uint32_t phase4_freertos_probe_checks;
volatile TickType_t phase4_freertos_tick_delta;
volatile int64_t phase4_freertos_sem_wait_us;
volatile int64_t phase4_freertos_delay_us;
volatile uint32_t phase8_queue_checks;
volatile uint32_t phase8_fromisr_wrapper_checks;
volatile uint32_t phase8_pool_checks;

static void
phase4_log(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

static void
phase4_check(int condition, const char *name)
{
    phase4_freertos_probe_checks++;
    if (!condition) {
        phase4_freertos_probe_failures++;
        phase4_log("[FreeRTOS] FAIL: ");
        phase4_log(name);
        phase4_log("\n");
    }
}

static void
phase8_queue_probe(void)
{
    QueueHandle_t queue;
    QueueHandle_t pool[5];
    uint32_t sent;
    uint32_t received;
    BaseType_t woken;
    uint_t i;

    queue = xQueueCreate(2U, sizeof(uint32_t));
    phase4_check(queue != NULL, "queue creation");
    if (queue != NULL) {
        phase4_check(uxQueueMessagesWaiting(queue) == 0U,
                     "new queue is empty");
        phase4_check(uxQueueSpacesAvailable(queue) == 2U,
                     "new queue exposes all spaces");
        phase4_check(xQueueReceive(queue, &received, 0U) == pdFALSE,
                     "empty queue receive fails without waiting");

        sent = 0x12345678U;
        phase4_check(xQueueSend(queue, &sent, 0U) == pdTRUE,
                     "first queue send succeeds");
        sent = 0x89abcdefU;
        phase4_check(xQueueSend(queue, &sent, 0U) == pdTRUE,
                     "second queue send succeeds");
        phase4_check(xQueueIsQueueFullFromISR(queue) == pdTRUE,
                     "full queue is reported from ISR wrapper");
        phase4_check(xQueueSend(queue, &sent, 0U) == pdFALSE,
                     "full queue send fails without waiting");

        received = 0U;
        phase4_check(xQueueReceive(queue, &received, 0U) == pdTRUE
                        && received == 0x12345678U,
                     "queue preserves FIFO order");
        received = 0U;
        woken = pdTRUE;
        phase4_check(xQueueReceiveFromISR(queue, &received, &woken) == pdTRUE
                        && received == 0x89abcdefU,
                     "receive FromISR wrapper returns queued data");
        phase4_check(woken == pdTRUE,
                     "receive FromISR leaves wake flag unchanged");

        sent = 0x55aa55aaU;
        woken = pdTRUE;
        phase4_check(xQueueSendFromISR(queue, &sent, &woken) == pdTRUE,
                     "send FromISR wrapper succeeds");
        phase4_check(woken == pdFALSE,
                     "send FromISR clears wake flag by port contract");
        phase4_check(xQueueReceive(queue, &received, 0U) == pdTRUE
                        && received == sent,
                     "task receive observes FromISR wrapper data");

        sent = 1U;
        (void) xQueueSend(queue, &sent, 0U);
        xQueueReset(queue);
        phase4_check(uxQueueMessagesWaiting(queue) == 0U,
                     "queue reset removes pending data");
        vQueueDelete(queue);
    }
    phase8_queue_checks++;
    phase8_fromisr_wrapper_checks++;

    for (i = 0U; i < 5U; i++) {
        pool[i] = xQueueCreate(1U, sizeof(uint32_t));
    }
    phase4_check(pool[0] != NULL && pool[1] != NULL
                    && pool[2] != NULL && pool[3] != NULL,
                 "all four queue pool slots allocate");
    phase4_check(pool[4] == NULL,
                 "fifth queue allocation reports pool exhaustion");
    for (i = 0U; i < 4U; i++) {
        vQueueDelete(pool[i]);
    }
    queue = xQueueCreate(1U, sizeof(uint32_t));
    phase4_check(queue != NULL, "deleted queue slot can be reused");
    vQueueDelete(queue);
    phase8_pool_checks++;
}

static void
phase8_semaphore_probe(void)
{
    SemaphoreHandle_t sem;
    SemaphoreHandle_t pool[5];
    BaseType_t woken;
    uint_t i;

    sem = xSemaphoreCreateBinary();
    phase4_check(sem != NULL, "FromISR semaphore creation");
    if (sem != NULL) {
        woken = pdTRUE;
        phase4_check(xSemaphoreGiveFromISR(sem, &woken) == pdTRUE,
                     "give FromISR wrapper succeeds");
        phase4_check(woken == pdFALSE,
                     "give FromISR clears wake flag by port contract");
        woken = pdTRUE;
        phase4_check(xSemaphoreTakeFromISR(sem, &woken) == pdTRUE,
                     "take FromISR wrapper succeeds");
        phase4_check(woken == pdFALSE,
                     "take FromISR clears wake flag by port contract");
        vSemaphoreDelete(sem);
    }
    phase8_fromisr_wrapper_checks++;

    for (i = 0U; i < 5U; i++) {
        pool[i] = xSemaphoreCreateBinary();
    }
    phase4_check(pool[0] != NULL && pool[1] != NULL
                    && pool[2] != NULL && pool[3] != NULL,
                 "all four semaphore pool slots allocate");
    phase4_check(pool[4] == NULL,
                 "fifth semaphore allocation reports pool exhaustion");
    for (i = 0U; i < 4U; i++) {
        vSemaphoreDelete(pool[i]);
    }
    sem = xSemaphoreCreateBinary();
    phase4_check(sem != NULL, "deleted semaphore slot can be reused");
    vSemaphoreDelete(sem);
    phase8_pool_checks++;
}

void
phase4_freertos_probe_task(EXINF exinf)
{
    SemaphoreHandle_t sem;
    TickType_t tick_started;
    int64_t started;

    (void) exinf;
    phase4_log("[FreeRTOS] FreeRTOS compatibility probe start\n");

    phase4_check(portTICK_PERIOD_MS == 1U, "tick period is 1 ms");
    phase4_check(pdMS_TO_TICKS(2U) == 2U, "millisecond conversion");

    sem = xSemaphoreCreateBinary();
    phase4_check(sem != NULL, "binary semaphore creation");
    if (sem != NULL) {
        phase4_check(xSemaphoreTake(sem, 0U) == pdFALSE,
                     "zero timeout returns pdFALSE");
        phase4_check(xSemaphoreGive(sem) == pdTRUE,
                     "give returns pdTRUE");
        phase4_check(xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE,
                     "portMAX_DELAY maps to an indefinite wait");

        tick_started = xTaskGetTickCount();
        started = esp_shim_time_us();
        phase4_check(xSemaphoreTake(sem, pdMS_TO_TICKS(2U)) == pdFALSE,
                     "finite timeout returns pdFALSE");
        phase4_freertos_sem_wait_us = esp_shim_time_us() - started;
        phase4_freertos_tick_delta = xTaskGetTickCount() - tick_started;
        phase4_check(phase4_freertos_sem_wait_us >= 1500,
                     "two ticks wait for approximately 2 ms");
        phase4_check(phase4_freertos_sem_wait_us < 250000,
                     "finite timeout remains bounded");
        phase4_check(phase4_freertos_tick_delta >= 1U,
                     "xTaskGetTickCount advances during a wait");
        vSemaphoreDelete(sem);
    }

    phase4_check(xSemaphoreTake(NULL, 0U) == pdFALSE,
                 "invalid take maps to pdFALSE");
    phase4_check(xSemaphoreGive(NULL) == pdFALSE,
                 "invalid give maps to pdFALSE");

    started = esp_shim_time_us();
    vTaskDelay(pdMS_TO_TICKS(1U));
    phase4_freertos_delay_us = esp_shim_time_us() - started;
    phase4_check(phase4_freertos_delay_us >= 700,
                 "vTaskDelay converts one tick to 1 ms");
    phase4_check(phase4_freertos_delay_us < 250000,
                 "vTaskDelay remains bounded");

    phase8_queue_probe();
    phase8_semaphore_probe();

    /*
     *  Let the log task drain before the verdict.
     *
     *  phase4_log writes to the port directly, one character at a time,
     *  while esp_shim reports through syslog and the log task. Nothing
     *  serialises the two, so a line written while the log task is draining
     *  comes out shredded - and phase8_semaphore_probe() has just provoked a
     *  burst of "esp_shim: acre_sem failed" on purpose, which is what makes
     *  the collision likely rather than incidental. The verdict is printed
     *  exactly once, so losing that one line loses the answer:
     *
     *      [APIProbe] FreeRTOS API boundary probe Psp_shim: acre_sem fail...
     *      obe] FreeRTOS API boundary probe PASS
     *
     *  Both of those are this line, from two different runs, and
     *  Test-Hardware.ps1 misread it two runs in six. The other hardware
     *  tests do not suffer this: they count repeated markers rather than
     *  relying on a single line surviving.
     */
    vTaskDelay(pdMS_TO_TICKS(300U));

    if (phase4_freertos_probe_failures == 0U) {
        phase4_log("[APIProbe] FreeRTOS API boundary probe PASS\n");
    }
    else {
        phase4_log("[APIProbe] FreeRTOS API boundary probe FAILED\n");
    }
    ext_tsk();
}
