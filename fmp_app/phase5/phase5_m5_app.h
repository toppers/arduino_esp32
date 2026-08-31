#ifndef TOPPERS_PHASE5_M5_APP_H
#define TOPPERS_PHASE5_M5_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE5_M5_APP_H */
