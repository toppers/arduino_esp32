#ifndef TOPPERS_PHASE9_WIFI_CONNECT_APP_H
#define TOPPERS_PHASE9_WIFI_CONNECT_APP_H

/*
 *  ESP32-C6 (ports/m5stack_riscv) copy of ports/m5stack_xtensa/app/
 *  wifi_connect/phase9_wifi_connect_app.h (same task priority and stack
 *  size as the Xtensa profile and as the C6 minimal app).
 */

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE9_WIFI_CONNECT_APP_H */
