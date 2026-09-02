#ifndef TOPPERS_LWIP_ARCH_SYS_ARCH_H
#define TOPPERS_LWIP_ARCH_SYS_ARCH_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef TaskHandle_t sys_thread_t;

typedef struct sys_mbox_s {
    QueueHandle_t os_mbox;
} *sys_mbox_t;

#define ERR_NEED_SCHED 123
#define sys_msleep(ms) esp_shim_task_delay(ms)
#define LWIP_COMPAT_MUTEX 0

#define sys_mutex_valid_val(mutex) ((mutex) != NULL)
#define sys_mutex_valid(mutex) ((mutex) != NULL && sys_mutex_valid_val(*(mutex)))
#define sys_mutex_set_invalid(mutex) (*(mutex) = NULL)
#define sys_mbox_valid(mbox) (*(mbox) != NULL)
#define sys_mbox_set_invalid(mbox) (*(mbox) = NULL)
#define sys_sem_valid_val(sem) ((sem) != NULL)
#define sys_sem_valid(sem) ((sem) != NULL && sys_sem_valid_val(*(sem)))
#define sys_sem_set_invalid(sem) (*(sem) = NULL)

#endif
