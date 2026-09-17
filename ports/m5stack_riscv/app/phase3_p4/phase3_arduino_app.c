/*
 * The executable task body is compiled by Arduino builder and linked as an
 * external object. This translation unit keeps the FMP3 application source
 * contract explicit and gives the configuration a stable application name.
 *
 * ESP32-P4 (ports/m5stack_riscv, StampP4 plan stage A1) copy of
 * ../phase3/phase3_arduino_app.c, plus the second core's task. The P4
 * minimal stage is SMP (TNUM_PRCID 2, plan P4): the Arduino task is bound to
 * PRC1 by the cfg, and PRC2 runs toppers_core2_alive_task below, whose only
 * job is to say, once a second on the console, that core1 is scheduling
 * tasks - the evidence stage A4 (hardware, Blink) counts, next to the seam's
 * raw 'C' marker that core1 executed instructions at all. It sleeps with
 * dly_tsk between lines, so it costs the sketch nothing.
 */
#include <kernel.h>
#include <t_syslog.h>

#include "phase3_arduino_app.h"

const char toppers_phase3_application[] = "Arduino sketch bridge (ESP32-P4, SMP)";

void
toppers_core2_alive_task(EXINF exinf)
{
	uint32_t	n = 0;

	(void) exinf;
	for (;;) {
		n++;
		syslog(LOG_NOTICE, "[P4-CORE2] alive %u", (uint_t) n);
		dly_tsk(1000000U);		/* RELTIM is microseconds in this port */
	}
}
