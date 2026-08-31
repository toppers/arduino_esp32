/*
 *  All-in-one profile (experimental)
 *
 *  M5Unified + SMP + Wi-Fi in one runtime. The reference port has the same
 *  combination (m5/app/m5_wifi_smoke) and measured 227,732 bytes of DRAM for
 *  it; this exists to reproduce that here and decide whether the profile set
 *  can collapse further.
 *
 *  ★Structure follows the reference: the Arduino task owns main_task, and
 *    anything Wi-Fi happens after the M5 work, because the two are separated
 *    IN TIME rather than by priority. Upstream measured ESP_TIMER_TSK unable to
 *    run for 528 ms during a Wi-Fi connect, and raising priorities starves the
 *    blob instead - so no priority is changed anywhere.
 */

#ifndef TOPPERS_ALLINONE_APP_H
#define TOPPERS_ALLINONE_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void toppers_arduino_task(EXINF exinf);
#endif

#endif  /* TOPPERS_ALLINONE_APP_H */
