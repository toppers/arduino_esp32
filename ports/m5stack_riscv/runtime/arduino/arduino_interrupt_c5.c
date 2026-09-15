/*
 *  attachInterrupt / detachInterrupt (ESP32-C5)
 *
 *  ESP32-C5 (ports/m5stack_riscv, C5 plan A3) version of the C6 file
 *  arduino_interrupt.c in this directory, which is itself the RISC-V
 *  version of ports/m5stack_xtensa/runtime/arduino/arduino_interrupt.c.
 *  The C6 file is untouched (its stages are under the X-check); this is a
 *  copy with the chip differences below. The structure is the upstream
 *  one: one shared dispatch ISR plus a per-pin callback table, created
 *  with acre_isr() the first time attachInterrupt() is called.
 *
 *  What the C5 changes against the C6 file:
 *    - The interrupt matrix is programmed through the kernel's
 *      esp32c5_intmtx_route() (arch/riscv_gcc/esp32c5/chip_kernel_impl.c,
 *      exported under its kernel_rename name _kernel_esp32c5_intmtx_route,
 *      the way the dev esp/shim/esp_shim_intr_c5.c calls it). On the C5
 *      the MAP value is the CLIC line number itself (16..47), and the
 *      routine asserts the line is an external one.
 *    - The GPIO source is ETS_GPIO_INTR_SOURCE (soc/interrupts.h, 31 on
 *      the C5 - the C6 has 30; checked below), routed to line
 *      ARD_GPIO_INTNO (23, arduino_interrupt_c5.h).
 *    - The line-collision checks read the C5 shim's line table
 *      (wifi/shim/esp_shim_intr_c5_lines.h: ESP_SHIM_C5_WIFI_LINE_MIN/MAX,
 *      25..39) and the C5's reserved lines (18..22 tests / IDF disabled
 *      slot, 40..44 IDF vectors_clic.S).
 *    - The kernel keeps no per-line source mask on the C5 (CLIC, one
 *      source per line by construction); the route call is still the
 *      kernel's so that this file and the shim program the matrix the
 *      same way.
 *    - GPIO register programming is the ESP-IDF hal for the esp32c5
 *      (hal/gpio_ll.h, header-only inline), the same calls as the C6. The
 *      C5 has 29 GPIOs (0..28), so the "high" status word is always 0.
 *    - Single core: ARD_CORE_ID is 0 and gpio_ll_intr_enable_on_core()
 *      asserts that.
 *
 *  C5 plan stage 1 delivers this file written and syntax-checked only; it
 *  is compiled into the wifi-connect stage at stage 3 and measured on the
 *  M5Stamp-C5 at stage 4 (examples/GpioInterrupt on G1, C5 plan A10).
 *  attachInterrupt() does NOT configure the pad: the sketch calls
 *  pinMode() first (arduino_gpio.h, ruling R3).
 *
 *  Constraints for the user:
 *    - attachInterrupt() from task context only (acre_isr is a service call).
 *    - Mode constants equal ESP-IDF's gpio_int_type_t (Arduino.h): identity.
 *    - Unsupported modes (0 = disabled, values >= GPIO_INTR_MAX, including
 *      ONLOW_WE/ONHIGH_WE) are refused: the attach does not happen and a
 *      WARNING is logged. Silently accepting them would register a
 *      callback that never fires, or write an undefined trigger type.
 *
 *  Dispatch shape (level line: chip_initialize() writes ATTR = machine/level
 *  to every CLIC line and the cfg has no TA_EDGE): read status ->
 *  clear -> deliver -> loop until empty. A missed clear re-enters on a
 *  level line, so nothing is lost.
 */
#include <stdint.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <soc/interrupts.h>

#include "target_timer.h"		/* INTNO_TIMER */
#include "target_syssvc.h"		/* INTNO_SIO */
#include "esp_shim_intr_c5_lines.h"	/* ESP_SHIM_C5_WIFI_LINE_MIN/MAX */
#include "arduino_interrupt_c5.h"	/* ARD_GPIO_INTNO / ARD_CORE_ID */

#if !defined(TOPPERS_ESP32C5)
#error "arduino_interrupt_c5.c (ports/m5stack_riscv) is the ESP32-C5 version"
#endif

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)
#define ARD_GPIO_INTSRC	((uint_t) ETS_GPIO_INTR_SOURCE)

/*  The kernel's routing entry point (chip_kernel_impl.c, renamed by
 *  chip_rename.h). Same declaration as the dev esp/shim/esp_shim_intr_c5.c. */
extern void _kernel_esp32c5_intmtx_route(uint_t intsrc, INTNO intno);

/*
 *  ------------------------------------------------------------------
 *  Build-time checks
 *  ------------------------------------------------------------------
 *  A mismatch between the cfg's CFG_INT/ENA_DYNISR and this value makes
 *  acre_isr() return E_OBJ, which attachInterrupt() reports loudly below.
 *  A collision with another user of the line, on the other hand, breaks
 *  silently at run time (the tick is stolen, the console stops), so it is
 *  caught here.
 *
 *  (0) The referenced definitions must be visible: an invisible macro
 *      turns `#if X == Y` into 0 == 0 (true) or 0 == 19 (false) and the
 *      check passes vacuously.
 */
#if !defined(INTNO_TIMER)
#error "INTNO_TIMER is not visible (target_timer.h); the line collision check would pass vacuously"
#endif
#if !defined(INTNO_SIO)
#error "INTNO_SIO is not visible (target_syssvc.h); the line collision check would pass vacuously"
#endif
#if !defined(ESP_SHIM_C5_WIFI_LINE_MIN) || !defined(ESP_SHIM_C5_WIFI_LINE_MAX)
#error "ESP_SHIM_C5_WIFI_LINE_MIN/MAX are not visible (esp_shim_intr_c5_lines.h); the line collision check would pass vacuously"
#endif
#if !defined(CLIC_EXT_OFFSET) || !defined(CLIC_TNUM_INTNO)
#error "CLIC_EXT_OFFSET / CLIC_TNUM_INTNO are not visible (esp32c5.h); the line range check would pass vacuously"
#endif
#if (ARD_GPIO_INTNO == INTNO_TIMER)
#error "arduino_interrupt_c5: the CPU interrupt line collides with the tick (INTNO_TIMER)"
#endif
#if (ARD_GPIO_INTNO == INTNO_SIO)
#error "arduino_interrupt_c5: the CPU interrupt line collides with the console (INTNO_SIO)"
#endif
#if (ESP_SHIM_C5_WIFI_LINE_MIN <= ARD_GPIO_INTNO) && (ARD_GPIO_INTNO <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "arduino_interrupt_c5: the CPU interrupt line is inside the Wi-Fi shim's reserved range (esp_shim_intr_c5.cfg)"
#endif
/*  18/20/21 are the kernel test suite's software-raised lines, 19 its
 *  unregistered line, 22 ESP-IDF's disconnected-source slot
 *  (arduino_interrupt_c5.h). Values, not names: INTNO1/2/3 live in
 *  target_test.h and are not visible in this build. */
#if (ARD_GPIO_INTNO >= 18) && (ARD_GPIO_INTNO <= 22)
#error "arduino_interrupt_c5: the CPU interrupt line is one of the test / IDF-reserved lines 18..22"
#endif
/*  40..44 are reserved by ESP-IDF's vectors_clic.S (esp_shim_intr_c5_lines.h). */
#if (ARD_GPIO_INTNO >= 40) && (ARD_GPIO_INTNO <= 44)
#error "arduino_interrupt_c5: the CPU interrupt line is one ESP-IDF's vectors_clic.S reserves (40..44)"
#endif
/*  External CLIC lines only (16..47): esp32c5_intmtx_route asserts the same. */
#if (ARD_GPIO_INTNO < CLIC_EXT_OFFSET) || (ARD_GPIO_INTNO > (CLIC_TNUM_INTNO - 1))
#error "arduino_interrupt_c5: the CPU interrupt line is outside the external CLIC lines (CLIC_EXT_OFFSET .. CLIC_TNUM_INTNO-1)"
#endif
/*  The source number is taken from the SDK header; pin it to the TRM value
 *  so a header drift is a build error, not a silent misroute. */
_Static_assert(ETS_GPIO_INTR_SOURCE == 31, "ETS_GPIO_INTR_SOURCE is not 31 (ESP32-C5 interrupt matrix)");
/*  The dispatch loop below handles GPIOs 0..31 only (one status word). */
_Static_assert(GPIO_NUM_MAX <= 32, "arduino_interrupt: more than 32 GPIOs; add the high status word");

static void	(*ard_isr_tbl[GPIO_NUM_MAX])(void);
static ID	ard_isrid;			/* 0 = not created */

/*
 *  Diagnostic counters (never silent; probes and post-mortems read them).
 */
volatile uint32_t	ard_intr_n_dispatch;	/* dispatch ISR entries          */
volatile uint32_t	ard_intr_n_call;		/* user handler calls            */
volatile uint32_t	ard_intr_n_orphan;		/* pins that fired with no handler */
volatile int32_t	ard_intr_acre_ercd;		/* raw acre_isr result (0 = not run) */

static void
ard_gpio_dispatch(EXINF exinf)
{
	uint32_t	status;

	(void) exinf;
	ard_intr_n_dispatch++;
	for (;;) {
		gpio_ll_get_intr_status(ARD_GPIO_HW, ARD_CORE_ID, &status);
		if (status == 0U) {
			break;
		}
		/*  Clear first, then deliver: a new event during delivery sets
		 *  status again and is picked up by the next iteration (or by the
		 *  level line re-entering).  */
		gpio_ll_clear_intr_status(ARD_GPIO_HW, status);
		while (status != 0U) {
			uint32_t	pin = (uint32_t) __builtin_ctz(status);

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
}

void
attachInterrupt(uint8_t pin, void (*fn)(void), int mode)
{
	if (pin >= (uint8_t) GPIO_NUM_MAX || fn == NULL) {
		syslog(LOG_WARNING, "arduino_interrupt_c5: attach refused pin=%u fn=%s",
			   (uint_t) pin, fn == NULL ? "NULL" : "set");
		return;
	}
	/*
	 *  Mode range (upstream: `mode < RISING || mode > ONHIGH`). Written
	 *  with the IDF names because this TU does not read Arduino.h
	 *  (RISING = GPIO_INTR_POSEDGE = 1 ... ONHIGH = GPIO_INTR_HIGH_LEVEL = 5).
	 *  Not refusing them breaks silently:
	 *    - 0 (GPIO_INTR_DISABLE) registers a callback that never fires
	 *    - ONLOW_WE (0x0C) / ONHIGH_WE (0x0D) and anything >= GPIO_INTR_MAX
	 *      make gpio_ll_set_intr_type() write an undefined trigger type
	 */
	if (mode <= (int) GPIO_INTR_DISABLE || mode >= (int) GPIO_INTR_MAX) {
		syslog(LOG_WARNING,
			   "arduino_interrupt_c5: attach refused pin=%u mode=%d (unsupported on this port)",
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
			 *  Usual causes: arduino_interrupt_c5.cfg not in the cfg (E_OBJ),
			 *  AID_ISR exhausted (E_NOID).  */
			syslog(LOG_ERROR, "arduino_interrupt_c5: acre_isr failed ercd=%d",
				   (int_t) erid);
			return;
		}
		ard_isrid = (ID) erid;
		/*  Route the GPIO source to the line through the kernel (writes the
		 *  MAP register with the CLIC line number).  */
		_kernel_esp32c5_intmtx_route(ARD_GPIO_INTSRC, (INTNO) ARD_GPIO_INTNO);
		syslog(LOG_NOTICE, "arduino_interrupt_c5: dispatch isr id=%d on intno %u (src %u)",
			   (int_t) ard_isrid, (uint_t) ARD_GPIO_INTNO, (uint_t) ARD_GPIO_INTSRC);
	}
	ard_isr_tbl[pin] = fn;
	gpio_ll_set_intr_type(ARD_GPIO_HW, (uint32_t) pin, (gpio_int_type_t) mode);
	gpio_ll_clear_intr_status(ARD_GPIO_HW, 1UL << pin);
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
