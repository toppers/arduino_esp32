/*
 *  pinMode / digitalWrite / digitalRead (ESP32-C5)
 *
 *  ESP32-C5 (ports/m5stack_riscv, C5 plan A3) version of the C6 file
 *  arduino_gpio.c in this directory; the C6 file is untouched (its stages
 *  are under the X-check) and arduino_gpio.h (the API, the mode constants
 *  and the ard_gpio_pin_ok() declaration) is shared. The chip differences
 *  are the refused pins: USB Serial/JTAG on 13/14 (C6: 12/13), the
 *  in-package flash's MSPI pads on 15-22 (C6: 24-30), GPIOs 0..28 (C6:
 *  0..30) - all taken from the esp32c5-libs headers and pinned with
 *  _Static_assert below. See arduino_gpio.h for why this file exists (the
 *  core's esp32-hal-gpio.c.o and ESP-IDF's driver/gpio are not linked)
 *  and for the R3 division of labour: attachInterrupt() does NOT call
 *  pinMode(); the sketch calls pinMode() first, then attachInterrupt().
 *  pinMode() here never touches int_type / int_ena.
 *
 *  C5 plan stage 1 delivers this file written and syntax-checked only; it
 *  is compiled into the wifi-connect stage at stage 3 and measured on the
 *  M5Stamp-C5 at stage 4 (examples/GpioInterrupt on G1).
 *
 *  Everything is the header-only hal/gpio_ll.h (esp32c5-libs 3.3.8), in
 *  the order ESP-IDF's gpio_config() applies it (esp_driver_gpio/src/
 *  gpio.c: input -> open drain -> output -> pull-up -> pull-down).
 *  gpio_output_enable() there is matrix_out_default -> output_enable ->
 *  func_sel(PIN_FUNC_GPIO); gpio_output_disable() is output_disable ->
 *  set_output_enable_ctrl(false, false) -> func_sel(PIN_FUNC_GPIO). Both
 *  are reproduced below rather than called: the driver is not linked.
 *
 *  Locking: none. arduino_interrupt.c does not lock GPIO writes either.
 *  The registers written here are per-pin (IO_MUX_GPIOn_REG, pin[n],
 *  func_out_sel_cfg[n]) or write-1-to-set/clear (enable_w1ts/w1tc,
 *  out_w1ts/w1tc), so two pins configured from two contexts do not
 *  clobber each other; the same pin is the sketch's to serialise. The
 *  dispatch ISR of arduino_interrupt.c only touches status / status_w1tc.
 *
 *  Supported modes: INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT. Anything
 *  else (OPEN_DRAIN variants, ANALOG, PULLUP alone, ...) writes nothing
 *  and logs one line. OUTPUT also enables the input buffer (IO_MUX
 *  FUN_IE), as the core's OUTPUT (0x03 = INPUT | OUTPUT bits) does: a
 *  digitalRead() on an OUTPUT pin returns the level the pin drives, and
 *  the GPIO interrupt logic sees the pin's own edges. The self-driven
 *  attachInterrupt test (examples/GpioInterrupt) depends on exactly this.
 *
 *  Refused pins: 13 and 14 are the USB Serial/JTAG pads (the console of
 *  this port; gpio_ll_func_sel() would drop the USB pad enable), 15-22 are
 *  the MSPI pads of the in-package flash (C5FH4; func_sel on one of them
 *  kills XIP), and anything outside SOC_GPIO_VALID_GPIO_MASK (0-28 on the
 *  C5) does not exist. OUTPUT is refused on pads outside
 *  SOC_GPIO_VALID_OUTPUT_GPIO_MASK (none on the C5; the rule is shared with
 *  the Xtensa file, where the ESP32 has input-only pads). G28 (the
 *  M5Stamp-C5's BOOT button / blue LED, C5 plan A10) is an ordinary GPIO
 *  here and is not refused.
 */
#include <stdint.h>
#include <stdbool.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <soc/io_mux_reg.h>		/* PIN_FUNC_GPIO, USB_INT_PHY0_D{M,P}_GPIO_NUM */
#include <soc/soc_caps.h>		/* SOC_GPIO_VALID_GPIO_MASK, SOC_GPIO_VALID_OUTPUT_GPIO_MASK */
#include <soc/spi_pins.h>		/* MSPI_IOMUX_PIN_NUM_* (the in-package flash) */

#include "arduino_gpio.h"

#if !defined(TOPPERS_ESP32C5)
#error "arduino_gpio_c5.c (ports/m5stack_riscv) is the ESP32-C5 version"
#endif

/*  The constants must be the core's values (esp32-hal-gpio.h 3.3.8); a
 *  sketch compiles against that header and this file against its own. */
_Static_assert(INPUT == 0x01 && OUTPUT == 0x03 && INPUT_PULLUP == 0x05
			   && INPUT_PULLDOWN == 0x09 && LOW == 0 && HIGH == 1,
			   "arduino_gpio.h constants drifted from esp32-hal-gpio.h");
_Static_assert(USB_INT_PHY0_DM_GPIO_NUM == 13 && USB_INT_PHY0_DP_GPIO_NUM == 14,
			   "USB Serial/JTAG pads are not GPIO 13/14");
_Static_assert(SOC_GPIO_PIN_COUNT == 29 && GPIO_NUM_MAX == 29,
			   "ESP32-C5 GPIOs are not 0..28");

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)

/*  Mode bits of the core's encoding (esp32-hal-gpio.h). */
#define ARD_MODE_INPUT_BIT		0x01U
#define ARD_MODE_OUTPUT_BIT		0x02U
#define ARD_MODE_PULLUP_BIT		0x04U
#define ARD_MODE_PULLDOWN_BIT	0x08U

/*  The MSPI pads are 15..22 (CS1 15, CS0 16, MISO 17, WP 18, HD 20, CLK 21,
 *  MOSI 22; 19 lies inside the range and is refused with them. soc/spi_pins.h
 *  of esp32c5-libs 3.3.8). */
#define ARD_MSPI_PIN_MIN	MSPI_IOMUX_PIN_NUM_CS1
#define ARD_MSPI_PIN_MAX	MSPI_IOMUX_PIN_NUM_MOSI
_Static_assert(ARD_MSPI_PIN_MIN == 15 && ARD_MSPI_PIN_MAX == 22
			   && MSPI_IOMUX_PIN_NUM_CS0 > ARD_MSPI_PIN_MIN && MSPI_IOMUX_PIN_NUM_CS0 < ARD_MSPI_PIN_MAX
			   && MSPI_IOMUX_PIN_NUM_MISO > ARD_MSPI_PIN_MIN && MSPI_IOMUX_PIN_NUM_MISO < ARD_MSPI_PIN_MAX
			   && MSPI_IOMUX_PIN_NUM_WP > ARD_MSPI_PIN_MIN && MSPI_IOMUX_PIN_NUM_WP < ARD_MSPI_PIN_MAX
			   && MSPI_IOMUX_PIN_NUM_HD > ARD_MSPI_PIN_MIN && MSPI_IOMUX_PIN_NUM_HD < ARD_MSPI_PIN_MAX
			   && MSPI_IOMUX_PIN_NUM_CLK > ARD_MSPI_PIN_MIN && MSPI_IOMUX_PIN_NUM_CLK < ARD_MSPI_PIN_MAX,
			   "C5 MSPI flash pads are not 15-22");

bool
ard_gpio_pin_ok(uint8_t pin)
{
	return pin < (uint8_t) GPIO_NUM_MAX
		&& ((SOC_GPIO_VALID_GPIO_MASK >> pin) & 1U) != 0U
		&& pin != (uint8_t) USB_INT_PHY0_DM_GPIO_NUM
		&& pin != (uint8_t) USB_INT_PHY0_DP_GPIO_NUM
		&& !(pin >= (uint8_t) ARD_MSPI_PIN_MIN && pin <= (uint8_t) ARD_MSPI_PIN_MAX);
}

/*  Output driver allowed on this pad (input-only pads exist on the ESP32,
 *  not on the C5; kept for symmetry with the Xtensa file). */
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
		syslog(LOG_WARNING, "[C5-GPIO] pinMode: refused pin %u", (uint_t) pin);
		return;
	}
	if (mode != INPUT && mode != INPUT_PULLUP && mode != INPUT_PULLDOWN
		&& mode != OUTPUT) {
		syslog(LOG_WARNING, "[C5-GPIO] pinMode: unsupported mode 0x%02x on pin %u",
			   (uint_t) mode, (uint_t) pin);
		return;
	}
	if ((mode & ARD_MODE_OUTPUT_BIT) != 0U && !ard_gpio_output_ok(pin)) {
		syslog(LOG_WARNING, "[C5-GPIO] pinMode: pin %u is input-only", (uint_t) pin);
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
