#ifndef TOPPERS_PHASE9_WIFI_CONNECT_APP_H
#define TOPPERS_PHASE9_WIFI_CONNECT_APP_H

/*
 *  ESP32-P4 (ports/m5stack_riscv, plan 7 stage B) version of
 *  ../wifi_connect_c5/phase9_wifi_connect_app.h. Same Arduino task as every
 *  other board, plus the second processor's liveness task, which this board
 *  carries in both profiles (the minimal one is ../phase3_p4).
 */

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#define CORE2_ALIVE_TASK_PRIORITY 11
#define CORE2_ALIVE_TASK_STACK_SIZE 2048

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
extern void toppers_p4_core2_alive_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE9_WIFI_CONNECT_APP_H */
