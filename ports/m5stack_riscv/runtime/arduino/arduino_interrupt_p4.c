/*
 *  attachInterrupt / detachInterrupt (ESP32-P4)
 *
 *  ESP32-P4 (ports/m5stack_riscv, StampP4 plan stage B2c) version of the C6
 *  file arduino_interrupt.c and the C5 file arduino_interrupt_c5.c in this
 *  directory. Those two are untouched (their stages are under the X-check);
 *  this is a copy with the chip differences below. The structure is the
 *  upstream one: one shared dispatch ISR plus a per-pin callback table,
 *  created with acre_isr() the first time attachInterrupt() is called.
 *
 *  What the P4 changes against the C5 file:
 *    - The interrupt matrix is written here, not through the kernel. The
 *      C6 and C5 chip layers export esp32c<N>_intmtx_route(); the P4 chip
 *      layer does not, and the port's own CLIC shim
 *      (wifi/shim/esp_shim_intr_clic.c, esp_shim_clic_intr_route()) writes
 *      INTMTX_MAP(0, source) = line directly. This file does the same
 *      thing, through the same macro, so that the two agree by
 *      construction rather than by comment.
 *    - The GPIO source is ETS_GPIO_INTR0_SOURCE (74 on the P4). The chip
 *      has four GPIO interrupt sources, but the SDK's hal only ever
 *      enables the first: gpio_ll_intr_enable_on_core() ignores core_id
 *      and writes GPIO_LL_INTR0_ENA ("TODO: IDF-7995"), and
 *      gpio_ll_get_intr_status() reads intr_0. Choosing another source
 *      would produce a line that never fires.
 *    - 55 GPIOs (0..54), so the dispatch loop reads BOTH status words -
 *      the C6 and C5 files assert GPIO_NUM_MAX <= 32 and read one. Pins
 *      32..54 arrive in the high word.
 *    - Two cores. ARD_CORE_ID is 0 (hart 0 = PRC1, where the Arduino task
 *      and the matrix entry are); the hal ignores it, see above.
 *    - The line-collision checks read the port's CLIC line table
 *      (wifi/shim/esp_shim_intr_clic_lines.h) rather than a copy of it.
 *
 *  Stage B2c delivers this file compiled into the wifi-connect stage and
 *  linked; the M5Stamp-P4 measurement (examples/GpioInterrupt on G16 = A0,
 *  the variant's first analog pin, free on the side header) is stage B4.
 *  attachInterrupt() does NOT configure the pad: the sketch calls
 *  pinMode() first (arduino_gpio.h, ruling R3).
 *
 *  Constraints for the user:
 *    - attachInterrupt() from task context only (acre_isr is a service call).
 *    - Mode constants equal ESP-IDF's gpio_int_type_t (Arduino.h): identity.
 *    - Unsupported modes (0 = disabled, values >= GPIO_INTR_MAX, including
 *      ONLOW_WE/ONHIGH_WE) are refused: the attach does not happen and a
 *      WARNING is logged.
 *
 *  Dispatch shape (level line): read status -> clear -> deliver -> loop
 *  until empty. A missed clear re-enters on a level line, so nothing is
 *  lost.
 */
#include <stdint.h>
#include <kernel.h>
#include <sil.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <soc/interrupts.h>

#include "esp32p4.h"				/* INTMTX_MAP, CLIC_* */
#include "target_syssvc.h"			/* INTNO_SIO */
#include "esp_shim_intr_clic_lines.h"	/* ESP_SHIM_CLIC_INTR_LINE0..5 */
#include "arduino_interrupt_p4.h"	/* ARD_GPIO_INTNO / ARD_CORE_ID */

#if !defined(TOPPERS_ESP32P4)
#error "arduino_interrupt_p4.c (ports/m5stack_riscv) is the ESP32-P4 version"
#endif

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)
#define ARD_GPIO_INTSRC	((uint32_t) ETS_GPIO_INTR0_SOURCE)

/*
 *  ------------------------------------------------------------------
 *  Build-time checks
 *  ------------------------------------------------------------------
 *  A mismatch between the cfg's CFG_INT/ENA_DYNISR and this value makes
 *  acre_isr() return E_OBJ, which attachInterrupt() reports loudly below.
 *  A collision with another user of the line, on the other hand, breaks
 *  silently at run time (the console stops, or the SDIO link to the
 *  companion C6 loses its interrupt), so it is caught here.
 *
 *  (0) The referenced definitions must be visible: an invisible macro
 *      turns `#if X == Y` into 0 == 0 (true) or 0 == 23 (false) and the
 *      check passes vacuously.
 */
#if !defined(INTNO_SIO)
#error "INTNO_SIO is not visible (target_syssvc.h); the line collision check would pass vacuously"
#endif
#if !defined(ESP_SHIM_CLIC_INTR_LINE0) || !defined(ESP_SHIM_CLIC_INTR_LINE5)
#error "ESP_SHIM_CLIC_INTR_LINE0..5 are not visible (esp_shim_intr_clic_lines.h); the line collision check would pass vacuously"
#endif
#if !defined(CLIC_EXT_OFFSET) || !defined(CLIC_TNUM_INTNO)
#error "CLIC_EXT_OFFSET / CLIC_TNUM_INTNO are not visible (esp32p4.h); the line range check would pass vacuously"
#endif
#if !defined(CLIC_INTNO_MSIP) || !defined(CLIC_INTNO_MTIMER)
#error "CLIC_INTNO_MSIP / CLIC_INTNO_MTIMER are not visible (esp32p4.h); the tick/IPI check would pass vacuously"
#endif
#if (ARD_GPIO_INTNO == CLIC_INTNO_MTIMER) || (ARD_GPIO_INTNO == CLIC_INTNO_MSIP)
#error "arduino_interrupt_p4: the CPU interrupt line collides with the tick or the IPI (CLINT internal lines)"
#endif
#if (ARD_GPIO_INTNO == INTNO_SIO)
#error "arduino_interrupt_p4: the CPU interrupt line collides with the console (INTNO_SIO)"
#endif
#if (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE0) \
 || (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE1) \
 || (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE2) \
 || (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE3) \
 || (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE4) \
 || (ARD_GPIO_INTNO == ESP_SHIM_CLIC_INTR_LINE5)
#error "arduino_interrupt_p4: the CPU interrupt line is one of the CLIC shim's slots (esp_shim_intr_clic.cfg); SDMMC lives there"
#endif
/*  17..19 and 22 are the test suite's lines and ESP-IDF's disconnected-source
 *  slot (arduino_interrupt_p4.h). Values, not names: INTNO1/2/3 live in
 *  target_test.h and are not visible in this build. */
#if ((ARD_GPIO_INTNO >= 17) && (ARD_GPIO_INTNO <= 19)) || (ARD_GPIO_INTNO == 22) \
 || (ARD_GPIO_INTNO == 45)
#error "arduino_interrupt_p4: the CPU interrupt line is one of the test / IDF-reserved lines (17..19, 22, 45)"
#endif
/*  40..44 vectors_clic.S, 46 Ethernet, 47 SDMMC by convention. */
#if ((ARD_GPIO_INTNO >= 40) && (ARD_GPIO_INTNO <= 44)) \
 || (ARD_GPIO_INTNO == 46) || (ARD_GPIO_INTNO == 47)
#error "arduino_interrupt_p4: the CPU interrupt line is one of the reserved high lines (40..44 IDF, 46 EMAC, 47 SDMMC)"
#endif
/*  External CLIC lines only (16..47): the matrix cannot drive an internal one. */
#if (ARD_GPIO_INTNO < CLIC_EXT_OFFSET) || (ARD_GPIO_INTNO > (CLIC_TNUM_INTNO - 1))
#error "arduino_interrupt_p4: the CPU interrupt line is outside the external CLIC lines (CLIC_EXT_OFFSET .. CLIC_TNUM_INTNO-1)"
#endif
/*  The source number is taken from the SDK header; pin it to the TRM value
 *  so a header drift is a build error, not a silent misroute. */
_Static_assert(ETS_GPIO_INTR0_SOURCE == 74,
			   "ETS_GPIO_INTR0_SOURCE is not 74 (ESP32-P4 interrupt matrix)");
/*  Two status words cover 0..63; the P4 has 55 GPIOs. */
_Static_assert(GPIO_NUM_MAX == 55,
			   "ESP32-P4 GPIOs are not 0..54; revisit the dispatch loop's word count");

static void	(*ard_isr_tbl[GPIO_NUM_MAX])(void);
static ID	ard_isrid;			/* 0 = not created */

/*
 *  Diagnostic counters (never silent; probes and post-mortems read them).
 */
volatile uint32_t	ard_intr_n_dispatch;	/* dispatch ISR entries          */
volatile uint32_t	ard_intr_n_call;		/* user handler calls            */
volatile uint32_t	ard_intr_n_orphan;		/* pins that fired with no handler */
volatile int32_t	ard_intr_acre_ercd;		/* raw acre_isr result (0 = not run) */

/*
 *  Deliver one status word. `base` is the pin number of bit 0 (0 for the
 *  low word, 32 for the high one).
 */
static void
ard_gpio_deliver(uint32_t status, uint32_t base)
{
	while (status != 0U) {
		uint32_t	pin = base + (uint32_t) __builtin_ctz(status);

		status &= status - 1U;
		if (pin < (uint32_t) GPIO_NUM_MAX && ard_isr_tbl[pin] != NULL) {
			ard_intr_n_call++;
			(*ard_isr_tbl[pin])();
		}
		else {
			ard_intr_n_orphan++;
		}
	}
}

static void
ard_gpio_dispatch(EXINF exinf)
{
	uint32_t	status, status_high;

	(void) exinf;
	ard_intr_n_dispatch++;
	for (;;) {
		gpio_ll_get_intr_status(ARD_GPIO_HW, ARD_CORE_ID, &status);
		gpio_ll_get_intr_status_high(ARD_GPIO_HW, ARD_CORE_ID, &status_high);
		if ((status == 0U) && (status_high == 0U)) {
			break;
		}
		/*  Clear first, then deliver: a new event during delivery sets
		 *  status again and is picked up by the next iteration (or by the
		 *  level line re-entering).  */
		if (status != 0U) {
			gpio_ll_clear_intr_status(ARD_GPIO_HW, status);
		}
		if (status_high != 0U) {
			gpio_ll_clear_intr_status_high(ARD_GPIO_HW, status_high);
		}
		ard_gpio_deliver(status, 0U);
		ard_gpio_deliver(status_high, 32U);
	}
}

void
attachInterrupt(uint8_t pin, void (*fn)(void), int mode)
{
	if (pin >= (uint8_t) GPIO_NUM_MAX || fn == NULL) {
		syslog(LOG_WARNING, "arduino_interrupt_p4: attach refused pin=%u fn=%s",
			   (uint_t) pin, fn == NULL ? "NULL" : "set");
		return;
	}
	/*
	 *  Mode range (upstream: `mode < RISING || mode > ONHIGH`). Written
	 *  with the IDF names because this TU does not read Arduino.h
	 *  (RISING = GPIO_INTR_POSEDGE = 1 ... ONHIGH = GPIO_INTR_HIGH_LEVEL = 5).
	 */
	if (mode <= (int) GPIO_INTR_DISABLE || mode >= (int) GPIO_INTR_MAX) {
		syslog(LOG_WARNING,
			   "arduino_interrupt_p4: attach refused pin=%u mode=%d (unsupported on this port)",
			   (uint_t) pin, (int_t) mode);
		return;
	}
	if (ard_isrid == 0) {
		T_CISR	cisr;
		ER_ID	erid;

		cisr.isratr = TA_NULL;
		cisr.exinf  = (EXINF) 0;
		cisr.intno  = (INTNO) ARD_GPIO_INTNO;
		cisr.isr    = ard_gpio_dispatch;
		cisr.isrpri = 1;
		erid = acre_isr(&cisr);
		ard_intr_acre_ercd = (int32_t) erid;
		if (erid < 0) {
			/*  Never silent: after this the attach can never deliver.
			 *  Usual causes: arduino_interrupt_p4.cfg not in the cfg (E_OBJ),
			 *  AID_ISR exhausted (E_NOID).  */
			syslog(LOG_ERROR, "arduino_interrupt_p4: acre_isr failed ercd=%d",
				   (int_t) erid);
			return;
		}
		ard_isrid = (ID) erid;
		/*  Route the GPIO source to the line: INTMTX_MAP's first argument
		 *  is the 0-based hart id (not the 1-based FMP3 prcid), the same
		 *  call the CLIC shim makes.  */
		sil_wrw_mem(INTMTX_MAP(0, ARD_GPIO_INTSRC), (uint32_t) ARD_GPIO_INTNO);
		syslog(LOG_NOTICE, "arduino_interrupt_p4: dispatch isr id=%d on intno %u (src %u)",
			   (int_t) ard_isrid, (uint_t) ARD_GPIO_INTNO, (uint_t) ARD_GPIO_INTSRC);
	}
	ard_isr_tbl[pin] = fn;
	gpio_ll_set_intr_type(ARD_GPIO_HW, (uint32_t) pin, (gpio_int_type_t) mode);
	if (pin < 32U) {
		gpio_ll_clear_intr_status(ARD_GPIO_HW, 1UL << pin);
	}
	else {
		gpio_ll_clear_intr_status_high(ARD_GPIO_HW, 1UL << (pin - 32U));
	}
	gpio_ll_intr_enable_on_core(ARD_GPIO_HW, ARD_CORE_ID, (uint32_t) pin);
}

void
detachInterrupt(uint8_t pin)
{
	if (pin >= (uint8_t) GPIO_NUM_MAX) {
		return;
	}
	gpio_ll_intr_disable(ARD_GPIO_HW, (uint32_t) pin);
	gpio_ll_set_intr_type(ARD_GPIO_HW, (uint32_t) pin, GPIO_INTR_DISABLE);
	ard_isr_tbl[pin] = NULL;
}
