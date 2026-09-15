/*
 *  pinMode / digitalWrite / digitalRead (ESP32-S3 / ESP32)
 *
 *  Xtensa (ports/m5stack_xtensa) counterpart of the ESP32-C6 file of the
 *  same name (ports/m5stack_riscv/runtime/arduino/arduino_gpio.c). See
 *  arduino_gpio.h for why this file exists and for the R3 division of
 *  labour: attachInterrupt() does NOT call pinMode(); the sketch calls
 *  pinMode() first, then attachInterrupt(). pinMode() here never touches
 *  int_type / int_ena.
 *
 *  Register sequence: the same as the C6 file, which follows ESP-IDF's
 *  gpio_config() (input -> open drain -> output -> pull-up -> pull-down)
 *  with gpio_output_enable()/gpio_output_disable() reproduced inline
 *  (matrix_out_default -> output_enable -> func_sel(PIN_FUNC_GPIO), and
 *  output_disable -> set_output_enable_ctrl(false, false) -> func_sel).
 *
 *  ESP32 (LX6) difference: its IO_MUX registers are not at a fixed stride,
 *  so hal/gpio_ll.h's IO_MUX helpers (pull-up/down, input enable, func_sel)
 *  index GPIO_PIN_MUX_REG_OFFSET[], a table that lives in libsoc
 *  (soc/esp32/gpio_periph.c) which this port does not link. The table is
 *  rebuilt here, privately, from the PERIPHS_IO_MUX_*_U macros of
 *  soc/io_mux_reg.h (no numeric literals; a few entries are pinned to
 *  IDF's values by _Static_assert), and the IO_MUX bits are written with
 *  the register macros. The ESP32-S3's IO_MUX is at a fixed stride
 *  (IO_MUX_GPIO0_REG + 4 * n); its gpio_ll helpers need no table except
 *  gpio_ll_pullup_en()/gpio_ll_pulldown_en(), which are avoided (below).
 *
 *  Locking: none (same reasoning as the C6 file: per-pin registers and
 *  w1ts/w1tc registers; the dispatch ISR only touches status/status_w1tc).
 *
 *  Supported modes: INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT. OUTPUT
 *  also enables the input buffer (FUN_IE), as the core's OUTPUT (0x03 =
 *  INPUT | OUTPUT bits) does: digitalRead() on an OUTPUT pin returns the
 *  level the pin drives and the interrupt logic sees the pin's own edges
 *  (the self-driven attachInterrupt test of examples/GpioInterrupt).
 *
 *  Refused pins (ard_gpio_pin_ok): the console pads (ESP32-S3: USB
 *  Serial/JTAG GPIO 19/20; ESP32: UART0 GPIO 1/3), the SPI flash pads
 *  (ESP32-S3: MSPI 26-32; ESP32: MSPI 6-11), pads that are not GPIOs on
 *  the ESP32 (20, 24, 28-31: no IO_MUX register) and pin >= GPIO_NUM_MAX.
 *  The ESP32-S3's octal-PSRAM pads 33-37 are NOT refused (boards without
 *  octal PSRAM use them as GPIOs); a sketch that touches them on a CoreS3
 *  gets what it asked for.
 */
#include <stdint.h>
#include <stdbool.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <soc/io_mux_reg.h>
#include <soc/spi_pins.h>		/* MSPI_IOMUX_PIN_NUM_* */
#include <soc/uart_pins.h>		/* U0TXD_GPIO_NUM / U0RXD_GPIO_NUM (ESP32) */

#include "arduino_gpio.h"

/*  Chip selection as in arduino_interrupt.c: TOPPERS_ESP32_LX6 is the
 *  ESP32, everything else in this port is the ESP32-S3. */
#if !defined(TOPPERS_ESP32_LX6) && !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TOPPERS_ESP32S3)
#error "arduino_gpio.c (ports/m5stack_xtensa): neither the ESP32 (TOPPERS_ESP32_LX6) nor the ESP32-S3"
#endif

/*  The constants must be the core's values (esp32-hal-gpio.h 3.3.8); a
 *  sketch compiles against that header and this file against its own. */
_Static_assert(INPUT == 0x01 && OUTPUT == 0x03 && INPUT_PULLUP == 0x05
			   && INPUT_PULLDOWN == 0x09 && LOW == 0 && HIGH == 1,
			   "arduino_gpio.h constants drifted from esp32-hal-gpio.h");

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)

/*  Mode bits of the core's encoding (esp32-hal-gpio.h). */
#define ARD_MODE_INPUT_BIT		0x01U
#define ARD_MODE_OUTPUT_BIT		0x02U
#define ARD_MODE_PULLUP_BIT		0x04U
#define ARD_MODE_PULLDOWN_BIT	0x08U

#if defined(TOPPERS_ESP32_LX6)
/*
 *  IO_MUX register of each ESP32 pad, as an offset from DR_REG_IO_MUX_BASE
 *  (0 = the pad is not a GPIO). Same content as libsoc's
 *  GPIO_PIN_MUX_REG_OFFSET[] but derived from the macros, and private.
 */
#define ARD_OFF(u)	((uint8_t) ((u) - DR_REG_IO_MUX_BASE))
static const uint8_t ard_iomux_off[GPIO_NUM_MAX] = {
	ARD_OFF(PERIPHS_IO_MUX_GPIO0_U),	ARD_OFF(PERIPHS_IO_MUX_U0TXD_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO2_U),	ARD_OFF(PERIPHS_IO_MUX_U0RXD_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO4_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO5_U),
	ARD_OFF(PERIPHS_IO_MUX_SD_CLK_U),	ARD_OFF(PERIPHS_IO_MUX_SD_DATA0_U),
	ARD_OFF(PERIPHS_IO_MUX_SD_DATA1_U),	ARD_OFF(PERIPHS_IO_MUX_SD_DATA2_U),
	ARD_OFF(PERIPHS_IO_MUX_SD_DATA3_U),	ARD_OFF(PERIPHS_IO_MUX_SD_CMD_U),
	ARD_OFF(PERIPHS_IO_MUX_MTDI_U),		ARD_OFF(PERIPHS_IO_MUX_MTCK_U),
	ARD_OFF(PERIPHS_IO_MUX_MTMS_U),		ARD_OFF(PERIPHS_IO_MUX_MTDO_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO16_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO17_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO18_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO19_U),
	0 /* 20: not bonded */,				ARD_OFF(PERIPHS_IO_MUX_GPIO21_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO22_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO23_U),
	0 /* 24 */,							ARD_OFF(PERIPHS_IO_MUX_GPIO25_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO26_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO27_U),
	0 /* 28 */, 0 /* 29 */, 0 /* 30 */, 0 /* 31 */,
	ARD_OFF(PERIPHS_IO_MUX_GPIO32_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO33_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO34_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO35_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO36_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO37_U),
	ARD_OFF(PERIPHS_IO_MUX_GPIO38_U),	ARD_OFF(PERIPHS_IO_MUX_GPIO39_U),
};
/*  Pin the derivation to IDF's soc/esp32/gpio_periph.c values. */
_Static_assert(GPIO_NUM_MAX == 40, "ESP32 GPIO count is not 40");
_Static_assert(ARD_OFF(PERIPHS_IO_MUX_GPIO0_U) == 0x44
			   && ARD_OFF(PERIPHS_IO_MUX_U0TXD_U) == 0x88
			   && ARD_OFF(PERIPHS_IO_MUX_GPIO16_U) == 0x4c
			   && ARD_OFF(PERIPHS_IO_MUX_GPIO25_U) == 0x24
			   && ARD_OFF(PERIPHS_IO_MUX_GPIO39_U) == 0x10,
			   "ESP32 IO_MUX offsets differ from IDF's GPIO_PIN_MUX_REG_OFFSET");

static inline uint32_t
ard_iomux_reg(uint32_t n)
{
	return DR_REG_IO_MUX_BASE + ard_iomux_off[n];
}

bool
ard_gpio_pin_ok(uint8_t pin)
{
	return pin < (uint8_t) GPIO_NUM_MAX
		&& ard_iomux_off[pin] != 0U
		&& pin != (uint8_t) U0TXD_GPIO_NUM && pin != (uint8_t) U0RXD_GPIO_NUM
		&& !(pin >= (uint8_t) MSPI_IOMUX_PIN_NUM_CLK
			 && pin <= (uint8_t) MSPI_IOMUX_PIN_NUM_CS0);
}
_Static_assert(MSPI_IOMUX_PIN_NUM_CLK == 6 && MSPI_IOMUX_PIN_NUM_CS0 == 11,
			   "ESP32 flash pads are not 6-11");

#else	/* ESP32-S3 */

bool
ard_gpio_pin_ok(uint8_t pin)
{
	return pin < (uint8_t) GPIO_NUM_MAX
		&& pin != (uint8_t) USB_INT_PHY0_DM_GPIO_NUM
		&& pin != (uint8_t) USB_INT_PHY0_DP_GPIO_NUM
		&& !(pin >= (uint8_t) MSPI_IOMUX_PIN_NUM_CS1
			 && pin <= (uint8_t) MSPI_IOMUX_PIN_NUM_MOSI);
}
_Static_assert(USB_INT_PHY0_DM_GPIO_NUM == 19 && USB_INT_PHY0_DP_GPIO_NUM == 20,
			   "USB Serial/JTAG pads are not GPIO 19/20");
_Static_assert(MSPI_IOMUX_PIN_NUM_CS1 == 26 && MSPI_IOMUX_PIN_NUM_MOSI == 32,
			   "ESP32-S3 flash pads are not 26-32");
#endif

/*  IO_MUX bits (input enable, function select, pulls): gpio_ll on the S3,
 *  register macros on the private table for the ESP32. */
static void
ard_iomux_input(uint32_t n, bool enable)
{
#if defined(TOPPERS_ESP32_LX6)
	if (enable) {
		PIN_INPUT_ENABLE(ard_iomux_reg(n));
	}
	else {
		PIN_INPUT_DISABLE(ard_iomux_reg(n));
	}
#else
	if (enable) {
		gpio_ll_input_enable(ARD_GPIO_HW, n);
	}
	else {
		gpio_ll_input_disable(ARD_GPIO_HW, n);
	}
#endif
}

static void
ard_iomux_func_gpio(uint32_t n)
{
#if defined(TOPPERS_ESP32_LX6)
	PIN_FUNC_SELECT(ard_iomux_reg(n), PIN_FUNC_GPIO);
#else
	gpio_ll_func_sel(ARD_GPIO_HW, (uint8_t) n, PIN_FUNC_GPIO);
#endif
}

static void
ard_iomux_pulls(uint32_t n, bool pullup, bool pulldown)
{
#if defined(TOPPERS_ESP32_LX6)
	if (pullup) {
		SET_PERI_REG_MASK(ard_iomux_reg(n), FUN_PU);
	}
	else {
		CLEAR_PERI_REG_MASK(ard_iomux_reg(n), FUN_PU);
	}
	if (pulldown) {
		SET_PERI_REG_MASK(ard_iomux_reg(n), FUN_PD);
	}
	else {
		CLEAR_PERI_REG_MASK(ard_iomux_reg(n), FUN_PD);
	}
#else
	/*  Not gpio_ll_pullup_en()/gpio_ll_pulldown_en(): on the S3 those two
	 *  (unlike the *_dis pair) index libsoc's GPIO_PIN_MUX_REG[], which is
	 *  not linked. The S3's IO_MUX is at a fixed stride, so write the
	 *  register the *_dis helpers compute (IO_MUX_GPIO0_REG + 4 * n). */
	if (pullup) {
		REG_SET_BIT(IO_MUX_GPIO0_REG + (n * 4U), FUN_PU);
	}
	else {
		REG_CLR_BIT(IO_MUX_GPIO0_REG + (n * 4U), FUN_PU);
	}
	if (pulldown) {
		REG_SET_BIT(IO_MUX_GPIO0_REG + (n * 4U), FUN_PD);
	}
	else {
		REG_CLR_BIT(IO_MUX_GPIO0_REG + (n * 4U), FUN_PD);
	}
#endif
}

void
pinMode(uint8_t pin, uint8_t mode)
{
	gpio_dev_t	*hw = ARD_GPIO_HW;
	uint32_t	n = (uint32_t) pin;

	if (!ard_gpio_pin_ok(pin)) {
		syslog(LOG_WARNING, "[XT-GPIO] pinMode: refused pin %u", (uint_t) pin);
		return;
	}
	if (mode != INPUT && mode != INPUT_PULLUP && mode != INPUT_PULLDOWN
		&& mode != OUTPUT) {
		syslog(LOG_WARNING, "[XT-GPIO] pinMode: unsupported mode 0x%02x on pin %u",
			   (uint_t) mode, (uint_t) pin);
		return;
	}

	/*  Input buffer: every accepted mode has the INPUT bit, including
	 *  OUTPUT (see the file comment). */
	ard_iomux_input(n, (mode & ARD_MODE_INPUT_BIT) != 0U);
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
	ard_iomux_func_gpio(n);
	/*  Pulls: only the one the mode asks for, the other cleared. */
	ard_iomux_pulls(n, (mode & ARD_MODE_PULLUP_BIT) != 0U,
					(mode & ARD_MODE_PULLDOWN_BIT) != 0U);
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
	/*  0 unless the pad's input buffer is enabled. */
	return gpio_ll_get_level(ARD_GPIO_HW, (uint32_t) pin);
}
