#ifndef TOPPERS_PHASE4_FREERTOS_APP_H
#define TOPPERS_PHASE4_FREERTOS_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192
#define PHASE4_PROBE_TASK_PRIORITY 9
#define PHASE4_PROBE_TASK_STACK_SIZE 4096

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
extern void phase4_freertos_probe_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE4_FREERTOS_APP_H */
