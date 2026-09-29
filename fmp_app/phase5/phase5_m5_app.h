#ifndef TOPPERS_PHASE5_M5_APP_H
#define TOPPERS_PHASE5_M5_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

/*
 *  Worker tasks that esp_shim_task_create hands out for xTaskCreate*
 *  (m5_kernel_shim.c). Higher priority than the sketch so that a worker
 *  feeding a DMA ring is not starved by loop(); a worker spends almost all
 *  its time blocked. 4096 is what the shim checks requests against.
 */
#define M5_AUDIO_TASK_PRIORITY 5
#define M5_AUDIO_STACK_SIZE 4096

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
extern void m5_audio_task_entry(EXINF exinf);
extern void esp_shim_intr_isr(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE5_M5_APP_H */
