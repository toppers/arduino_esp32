/*
 *  pinMode / digitalWrite / digitalRead (ESP32-P4)
 *
 *  ESP32-P4 (ports/m5stack_riscv, StampP4 plan stage B2c) version of the C6
 *  file arduino_gpio.c and the C5 file arduino_gpio_c5.c in this directory;
 *  those two are untouched (their stages are under the X-check) and
 *  arduino_gpio.h (the API, the mode constants and the ard_gpio_pin_ok()
 *  declaration) is shared. See arduino_gpio.h for why this file exists (the
 *  core's esp32-hal-gpio.c.o and ESP-IDF's driver/gpio are not linked) and
 *  for the R3 division of labour: attachInterrupt() does NOT call
 *  pinMode(); the sketch calls pinMode() first, then attachInterrupt().
 *  pinMode() here never touches int_type / int_ena.
 *
 *  The chip differences against the C5 file:
 *    - 55 GPIOs (0..54), all of them valid as input and as output
 *      (SOC_GPIO_VALID_GPIO_MASK = 0x007FFFFFFFFFFFFF, and
 *      SOC_GPIO_VALID_OUTPUT_GPIO_MASK is defined to be the same). There
 *      are no input-only pads.
 *    - No MSPI pads to refuse. The P4's flash and PSRAM are on dedicated
 *      pins, not on the GPIO matrix: soc/spi_pins.h defines every
 *      MSPI_IOMUX_PIN_NUM_* as GPIO_NUM_INVALID (pinned below).
 *    - The USB Serial/JTAG pads are 24 and 25 (C5: 13/14). They carry this
 *      port's console.
 *    - SEVEN pins are refused that no other chip in this port refuses:
 *      42..48, the SDIO link to the companion ESP32-C6 that IS the Wi-Fi
 *      of this board (reset 42, CLK 43, CMD 44, D0-D3 45..48;
 *      hosted/sdio/p4sdio_pins.h, and the M5Stack core's own
 *      variants/m5stack_stamp_p4/pins_arduino.h agrees pin for pin). A
 *      pinMode() on one of them takes the pad away from the SDMMC
 *      peripheral, and Wi-Fi stops - a failure whose cause is nowhere near
 *      the sketch line that caused it. This file is only ever compiled
 *      into the wifi-connect profile, where that link is always present.
 *
 *  Stage B2c delivers this file compiled into the wifi-connect stage and
 *  linked; the M5Stamp-P4 measurement (examples/GpioInterrupt on G16 = A0)
 *  is stage B4.
 *
 *  Everything is the header-only hal/gpio_ll.h (esp32p4_es-libs 3.3.8), in
 *  the order ESP-IDF's gpio_config() applies it (esp_driver_gpio/src/
 *  gpio.c: input -> open drain -> output -> pull-up -> pull-down), the same
 *  sequence as the C5 and C6 files; the ll functions have identical
 *  signatures on all three chips.
 *
 *  Locking: none, for the reason the C5 file gives - the registers written
 *  here are per-pin or write-1-to-set/clear, so two pins configured from
 *  two contexts do not clobber each other. On the P4 "two contexts" can
 *  mean two cores; the argument is unchanged, because it is about which
 *  words are written, not about who writes them.
 *
 *  Supported modes: INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT. Anything
 *  else writes nothing and logs one line. OUTPUT also enables the input
 *  buffer (IO_MUX FUN_IE), as the core's OUTPUT (0x03 = INPUT | OUTPUT
 *  bits) does: a digitalRead() on an OUTPUT pin returns the level the pin
 *  drives, and the GPIO interrupt logic sees the pin's own edges. The
 *  self-driven attachInterrupt test (examples/GpioInterrupt) depends on
 *  exactly this.
 */
#include <stdint.h>
#include <stdbool.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <soc/io_mux_reg.h>		/* PIN_FUNC_GPIO, USB_INT_PHY0_D{M,P}_GPIO_NUM */
#include <soc/soc_caps.h>		/* SOC_GPIO_VALID_GPIO_MASK, SOC_GPIO_VALID_OUTPUT_GPIO_MASK */
#include <soc/spi_pins.h>		/* MSPI_IOMUX_PIN_NUM_* (all INVALID on the P4) */

#include "p4sdio_pins.h"		/* P4SDIO_GPIO_* and P4SDIO_SLAVE_RST_GPIO */
#include "arduino_gpio.h"

#if !defined(TOPPERS_ESP32P4)
#error "arduino_gpio_p4.c (ports/m5stack_riscv) is the ESP32-P4 version"
#endif

/*  The constants must be the core's values (esp32-hal-gpio.h 3.3.8); a
 *  sketch compiles against that header and this file against its own. */
_Static_assert(INPUT == 0x01 && OUTPUT == 0x03 && INPUT_PULLUP == 0x05
			   && INPUT_PULLDOWN == 0x09 && LOW == 0 && HIGH == 1,
			   "arduino_gpio.h constants drifted from esp32-hal-gpio.h");
_Static_assert(USB_INT_PHY0_DM_GPIO_NUM == 24 && USB_INT_PHY0_DP_GPIO_NUM == 25,
			   "USB Serial/JTAG pads are not GPIO 24/25");
_Static_assert(SOC_GPIO_PIN_COUNT == 55 && GPIO_NUM_MAX == 55,
			   "ESP32-P4 GPIOs are not 0..54");
/*  The reason this file has no MSPI range: state it as a check, so that a
 *  chip variant that does put the flash on the matrix breaks the build
 *  instead of silently letting a sketch reconfigure the XIP pads. */
_Static_assert(MSPI_IOMUX_PIN_NUM_CLK == GPIO_NUM_INVALID
			   && MSPI_IOMUX_PIN_NUM_CS0 == GPIO_NUM_INVALID
			   && MSPI_IOMUX_PIN_NUM_MOSI == GPIO_NUM_INVALID,
			   "the P4's MSPI pads are GPIOs after all; add a refused range");

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)

/*  Mode bits of the core's encoding (esp32-hal-gpio.h). */
#define ARD_MODE_INPUT_BIT		0x01U
#define ARD_MODE_OUTPUT_BIT		0x02U
#define ARD_MODE_PULLUP_BIT		0x04U
#define ARD_MODE_PULLDOWN_BIT	0x08U

/*
 *  The SDIO link to the companion C6, as a contiguous range. The values
 *  come from p4sdio_pins.h - the driver's own header, so the refusal
 *  cannot drift away from the wiring - and the range is checked here to be
 *  exactly the seven pins that header names.
 */
#define ARD_C6_PIN_MIN	P4SDIO_SLAVE_RST_GPIO
#define ARD_C6_PIN_MAX	P4SDIO_GPIO_D3
_Static_assert(ARD_C6_PIN_MIN == 42 && ARD_C6_PIN_MAX == 48
			   && P4SDIO_GPIO_CLK == 43 && P4SDIO_GPIO_CMD == 44
			   && P4SDIO_GPIO_D0 == 45 && P4SDIO_GPIO_D1 == 46
			   && P4SDIO_GPIO_D2 == 47,
			   "the companion C6's SDIO pins are not the contiguous range 42..48");

bool
ard_gpio_pin_ok(uint8_t pin)
{
	return pin < (uint8_t) GPIO_NUM_MAX
		&& ((SOC_GPIO_VALID_GPIO_MASK >> pin) & 1U) != 0U
		&& pin != (uint8_t) USB_INT_PHY0_DM_GPIO_NUM
		&& pin != (uint8_t) USB_INT_PHY0_DP_GPIO_NUM
		&& !(pin >= (uint8_t) ARD_C6_PIN_MIN && pin <= (uint8_t) ARD_C6_PIN_MAX);
}

/*  Output driver allowed on this pad (input-only pads exist on the ESP32,
 *  not on the P4; kept for symmetry with the Xtensa and C5 files). */
static inline bool
ard_gpio_output_ok(uint8_t pin)
{
	return ((SOC_GPIO_VALID_OUTPUT_GPIO_MASK >> pin) & 1U) != 0U;
}

void
pinMode(uint8_t pin, uint8_t mode)
{
	gpio_dev_t	*hw = ARD_GPIO_HW;
	uint32_t	n = (uint32_t) pin;

	if (!ard_gpio_pin_ok(pin)) {
		syslog(LOG_WARNING, "[P4-GPIO] pinMode: refused pin %u", (uint_t) pin);
		return;
	}
	if (mode != INPUT && mode != INPUT_PULLUP && mode != INPUT_PULLDOWN
		&& mode != OUTPUT) {
		syslog(LOG_WARNING, "[P4-GPIO] pinMode: unsupported mode 0x%02x on pin %u",
			   (uint_t) mode, (uint_t) pin);
		return;
	}
	if ((mode & ARD_MODE_OUTPUT_BIT) != 0U && !ard_gpio_output_ok(pin)) {
		syslog(LOG_WARNING, "[P4-GPIO] pinMode: pin %u is input-only", (uint_t) pin);
		return;
	}

	/*  Input buffer: every accepted mode has the INPUT bit, including
	 *  OUTPUT (see the file comment). */
	if ((mode & ARD_MODE_INPUT_BIT) != 0U) {
		gpio_ll_input_enable(hw, n);
	}
	else {
		gpio_ll_input_disable(hw, n);
	}
	/*  Push-pull (OPEN_DRAIN is refused above). */
	gpio_ll_od_disable(hw, n);
	/*  Output driver, and the pad's output source = the GPIO matrix's
	 *  simple-GPIO signal (SIG_GPIO_OUT_IDX), output enable from
	 *  GPIO_ENABLE_REG bit n, not from a peripheral. */
	if ((mode & ARD_MODE_OUTPUT_BIT) != 0U) {
		gpio_ll_matrix_out_default(hw, n);
		gpio_ll_output_enable(hw, n);
	}
	else {
		gpio_ll_output_disable(hw, n);
		gpio_ll_set_output_enable_ctrl(hw, pin, false, false);
	}
	/*  IO_MUX function = GPIO (PIN_FUNC_GPIO); after this the output
	 *  enable above is the one that counts. */
	gpio_ll_func_sel(hw, pin, PIN_FUNC_GPIO);
	/*  Pulls: only the one the mode asks for, the other cleared. */
	if ((mode & ARD_MODE_PULLUP_BIT) != 0U) {
		gpio_ll_pullup_en(hw, n);
	}
	else {
		gpio_ll_pullup_dis(hw, n);
	}
	if ((mode & ARD_MODE_PULLDOWN_BIT) != 0U) {
		gpio_ll_pulldown_en(hw, n);
	}
	else {
		gpio_ll_pulldown_dis(hw, n);
	}
}

void
digitalWrite(uint8_t pin, uint8_t val)
{
	if (!ard_gpio_pin_ok(pin)) {
		return;
	}
	gpio_ll_set_level(ARD_GPIO_HW, (uint32_t) pin, (val != 0U) ? 1U : 0U);
}

int
digitalRead(uint8_t pin)
{
	if (!ard_gpio_pin_ok(pin)) {
		return LOW;
	}
	/*  in_data_next; 0 unless the pad's input buffer is enabled. */
	return gpio_ll_get_level(ARD_GPIO_HW, (uint32_t) pin);
}
