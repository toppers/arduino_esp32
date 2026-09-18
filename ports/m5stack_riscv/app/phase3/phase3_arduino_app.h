#ifndef TOPPERS_PHASE3_ARDUINO_APP_H
#define TOPPERS_PHASE3_ARDUINO_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);

/*
 *  cfg の CRE_CYC / CRE_ALM が通知先に指すハンドラ（実体は
 *  runtime/arduino/arduino_kernel_api.c）。宣言をここに直接書くのは、
 *  cfg が読む -I に runtime/ が入っていないためである。
 */
extern void toppers_fmp3_sample_cyclic(intptr_t exinf);
extern void toppers_fmp3_sample_alarm(intptr_t exinf);
#endif

#endif  /* TOPPERS_PHASE3_ARDUINO_APP_H */
