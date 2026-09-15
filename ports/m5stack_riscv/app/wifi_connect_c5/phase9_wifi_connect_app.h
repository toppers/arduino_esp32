#ifndef TOPPERS_PHASE9_WIFI_CONNECT_APP_H
#define TOPPERS_PHASE9_WIFI_CONNECT_APP_H

/*
 *  ESP32-C5 (ports/m5stack_riscv, C5 plan stage 3) copy of the C6 file
 *  ../wifi_connect/phase9_wifi_connect_app.h (same task priority and stack
 *  size as the C6 and Xtensa profiles and as the minimal app).
 */

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE9_WIFI_CONNECT_APP_H */
