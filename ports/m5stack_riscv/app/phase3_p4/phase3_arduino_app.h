#ifndef TOPPERS_PHASE3_ARDUINO_APP_H
#define TOPPERS_PHASE3_ARDUINO_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192
/*  The PRC2 heartbeat task (ESP32-P4 SMP only; see the .c file).  */
#define CORE2_ALIVE_TASK_PRIORITY 12
#define CORE2_ALIVE_TASK_STACK_SIZE 2048

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
extern void toppers_core2_alive_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE3_ARDUINO_APP_H */
