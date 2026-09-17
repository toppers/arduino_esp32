/*
 * Stable FMP3 application identity for the Arduino Wi-Fi connect profile.
 *
 * ESP32-P4 (ports/m5stack_riscv, plan 7 stage B) version of
 * ../wifi_connect_c5/phase9_wifi_connect_app.c. Two differences from that
 * file, both because this chip runs two processors:
 *   - the second processor's liveness task lives here too (the minimal
 *     profile's copy is ../phase3_p4/phase3_arduino_app.c), so that a
 *     wifi-connect capture shows the same evidence that PRC2 schedules;
 *   - the cfg beside this file places it with CLASS(CLS_PRC2).
 * The executable task body of the sketch is compiled by the Arduino builder
 * and linked as an external object (toppers_arduino_task,
 * src/bridge/ArduinoSketchBridge.cpp).
 */
#include <kernel.h>
#include <t_syslog.h>
#include "phase9_wifi_connect_app.h"

const char toppers_phase9_wifi_connect_application[] =
    "Arduino WiFi connect bridge";

/*
 *  One second. RELTIM is MICROSECONDS in this port, not milliseconds - the
 *  minimal profile's copy of this task (../phase3_p4/phase3_arduino_app.c)
 *  says so and passes 1000000U. This file was written with 1000U and a
 *  comment that called it one second; on hardware the task ran at ~1 kHz
 *  and printed 24,589 "alive" lines in a 25 s capture (measured
 *  2026-09-18), which floods the console the Wi-Fi evidence has to come
 *  out of.
 */
#define CORE2_ALIVE_PERIOD_US 1000000U

void
toppers_p4_core2_alive_task(EXINF exinf)
{
    uint_t count = 0U;
    ID     prcid = 0;

    (void) exinf;
    if (get_pid(&prcid) != E_OK) {
        prcid = -1;
    }
    syslog(LOG_NOTICE, "[P4-CORE2] task start on processor %d", (int_t) prcid);
    for (;;) {
        count++;
        syslog(LOG_NOTICE, "[P4-CORE2] alive %u", count);
        (void) dly_tsk(CORE2_ALIVE_PERIOD_US);
    }
}
