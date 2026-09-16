/*
 *  rgbLedWrite (ESP32-S3): one WS2812 pixel over RMT TX channel 0
 *
 *  See arduino_rgb_led.h for what this replaces and why. The register
 *  sequence is ESP-IDF v5.5's esp_driver_rmt (rmt_common.c
 *  rmt_acquire_group_handle / rmt_select_periph_clock, rmt_tx.c
 *  rmt_new_tx_channel / rmt_tx_do_transaction, hal/rmt_hal.c rmt_hal_init /
 *  rmt_hal_tx_channel_reset) with the driver objects, the interrupt and the
 *  encoder taken out; the same hal/rmt_ll.h (esp32s3-libs 3.3.8) is used.
 *  Only inline hal functions and RMTMEM are touched: no IDF driver, no
 *  FreeRTOS, no interrupt allocation.
 *
 *  What differs from the ESP32-C6 file of the same name (the two are
 *  otherwise the same code):
 *    - clock source RMT_CLK_SRC_APB (= RMT_CLK_SRC_DEFAULT on this chip,
 *      soc/clk_tree_defs.h:221,224) instead of the C6's PLL_F80M branch.
 *      APB is 80 MHz whenever the CPU runs off the PLL, which is every
 *      configuration this runtime builds (TOPPERS_S3_CPU_FREQ_MHZ 80 / 160 /
 *      240). The C6's group-divider argument (1) means the same thing here:
 *      rmt_ll_set_group_clock_src() writes sclk_div_num = integral - 1, so
 *      rmt_sclk = 80 MHz.
 *    - the RMT block is bigger (SOC_RMT_CHANNELS_PER_GROUP 8, of which 0-3
 *      transmit; the C6 has 4, of which 0-1 transmit). The frame still fits
 *      one 48-word block, and channel 0 is a TX channel on both.
 *    - RMTMEM is at 0x60016800 and RMT at 0x60016000 (esp32s3.peripherals.ld
 *      PROVIDEs both); the addresses never appear here, only the symbols.
 * *    - RMT_SIG_OUT0_IDX is 81 on this chip (soc/gpio_sig_map.h:158).
 *    - hal/rmt_ll.h wraps rmt_ll_enable_bus_clock() and
 *      rmt_ll_reset_register() in macros that require a PERIPH_RCC_ATOMIC()
 *      section around the call (the C6 header of the same name does not),
 *      so ard_rgb_init() opens one.
 *  Everything below - the WS2812 timings, the GRB order, the polled
 *  completion, the timeout handling - is the C6 file's.
 *
 *  Clock:
 *    - source: RMT_CLK_SRC_APB, 80 MHz.
 *    - group divider 1 (rmt_sclk = 80 MHz), channel divider 4:
 *      1 tick = 50 ns. Every WS2812 duration below is a whole number of
 *      ticks, and the 15-bit duration field (max 32767 = 1.6 ms) holds the
 *      reset gap with room to spare.
 *
 *  WS2812 timing (800 kHz):
 *    bit 0: high 0.35 us, low 0.90 us    bit 1: high 0.90 us, low 0.35 us
 *    reset: low >= 50 us; sent as one explicit symbol after the 24 data
 *    symbols, then the end marker (a word of 0: duration 0 stops the TX).
 *    The idle level is fixed low, so the line also stays low between calls.
 *    24 + 1 + 1 = 26 words fit the 48-word block of channel 0 without wrap.
 *
 *  Colour order GRB (the core's default), MSB first per byte.
 *
 *  Completion: the raw TX_DONE(0) status is polled, at most
 *  ARD_RGB_POLL_LIMIT x esp_rom_delay_us(10) (20 ms; a frame is ~130 us).
 *  Each write logs `[S3-RGB] tx_done=<count>` (count of completed
 *  transmissions so far) or `[S3-RGB] tx timeout`.
 */
#include <stdint.h>
#include <stdbool.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/rmt_ll.h>
#include <hal/rmt_types.h>		/* rmt_symbol_word_t, rmt_clock_source_t */
#include <soc/rmt_struct.h>		/* RMT */
#include <soc/soc_caps.h>		/* SOC_RMT_MEM_WORDS_PER_CHANNEL etc. */
#include <soc/gpio_sig_map.h>	/* RMT_SIG_OUT0_IDX */
#include <esp_rom_gpio.h>		/* esp_rom_gpio_connect_out_signal (ROM) */
#include <esp_rom_sys.h>		/* esp_rom_delay_us (ROM) */
#include <esp_private/periph_ctrl.h>	/* PERIPH_RCC_ATOMIC (see ard_rgb_init) */

#include "arduino_gpio.h"
#include "arduino_rgb_led.h"

#if defined(TOPPERS_ESP32_LX6) || (!defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(TOPPERS_ESP32S3))
#error "arduino_rgb_led.c (ports/m5stack_xtensa) is the ESP32-S3 version"
#endif

/*  RMTMEM is a linker symbol (esp32s3.peripherals.ld: 0x60016800); the
 *  layout is the one esp_driver_rmt/src/rmt_private.h declares. */
typedef struct {
	struct {
		rmt_symbol_word_t	symbols[SOC_RMT_MEM_WORDS_PER_CHANNEL];
	} channels[SOC_RMT_CHANNELS_PER_GROUP];
} ard_rmt_block_mem_t;
extern ard_rmt_block_mem_t RMTMEM;

#define ARD_RMT_GROUP		0
#define ARD_RMT_CH			0U				/* TX channel 0 */
#define ARD_RMT_HW			(&RMT)
#define ARD_RMT_CLK_SRC		RMT_CLK_SRC_APB
#define ARD_RMT_GROUP_DIV	1U				/* rmt_sclk = 80 MHz */
#define ARD_RMT_CH_DIV		4U				/* 20 MHz: 1 tick = 50 ns */
#define ARD_RMT_TICK_NS		50U

/*  Durations in ticks. */
#define ARD_WS_T0H		(350U / ARD_RMT_TICK_NS)	/* 7  */
#define ARD_WS_T0L		(900U / ARD_RMT_TICK_NS)	/* 18 */
#define ARD_WS_T1H		(900U / ARD_RMT_TICK_NS)	/* 18 */
#define ARD_WS_T1L		(350U / ARD_RMT_TICK_NS)	/* 7  */
#define ARD_WS_RESET	(50000U / ARD_RMT_TICK_NS)	/* 1000 = 50 us, twice */

#define ARD_RGB_NBITS		24U
#define ARD_RGB_NWORDS		(ARD_RGB_NBITS + 2U)	/* + reset + end marker */
#define ARD_RGB_POLL_LIMIT	2000U
#define ARD_RGB_POLL_US		10U

/*  Largest value of a symbol's duration0/duration1 field (15 bits,
 *  rmt_symbol_word_t in hal/rmt_types.h). Not RMT_LL_MAX_IDLE_VALUE, which
 *  is the RX idle threshold limit and only happens to be the same number. */
#define ARD_RMT_DURATION_MAX	((1U << 15) - 1U)

_Static_assert(350U % ARD_RMT_TICK_NS == 0 && 900U % ARD_RMT_TICK_NS == 0,
			   "WS2812 durations are not whole ticks");
_Static_assert(ARD_WS_RESET <= ARD_RMT_DURATION_MAX, "reset gap exceeds the 15-bit duration field");
_Static_assert(ARD_RGB_NWORDS <= SOC_RMT_MEM_WORDS_PER_CHANNEL, "frame does not fit one RMT block");
_Static_assert(ARD_RMT_CH < SOC_RMT_TX_CANDIDATES_PER_GROUP, "channel 0 is not a TX channel");

static bool		ard_rgb_inited;				/* RMT clock/channel set up once */
static uint32_t	ard_rgb_tx_done;			/* completed transmissions */

static void
ard_rgb_init(void)
{
	rmt_dev_t	*hw = ARD_RMT_HW;

	/*  rmt_acquire_group_handle(): bus clock on, module reset.
	 *  On this chip (unlike the C6) hal/rmt_ll.h wraps these two in a
	 *  macro that refuses to compile outside a PERIPH_RCC_ATOMIC()
	 *  section - they touch SYSTEM.perip_clk_en0 / perip_rst_en0, which
	 *  other peripherals share. periph_rcc_enter/exit are this runtime's
	 *  own (wifi/hal_src/periph_ctrl.c, linked into this profile). */
	PERIPH_RCC_ATOMIC() {
		rmt_ll_enable_bus_clock(ARD_RMT_GROUP, true);
		rmt_ll_reset_register(ARD_RMT_GROUP);
	}
	/*  rmt_hal_init() (plus the register/memory clock gate, which the
	 *  driver leaves enabled via rmt_ll_enable_periph_clock). */
	rmt_ll_enable_periph_clock(hw, true);
	rmt_ll_mem_power_by_pmu(hw);
	rmt_ll_enable_mem_access_nonfifo(hw, true);
	rmt_ll_enable_interrupt(hw, UINT32_MAX, false);
	rmt_ll_clear_interrupt_status(hw, UINT32_MAX);
	rmt_ll_tx_clear_sync_group(hw);
	/*  rmt_hal_tx_channel_reset(). */
	rmt_ll_tx_reset_channels_clock_div(hw, 1U << ARD_RMT_CH);
	rmt_ll_tx_reset_pointer(hw, ARD_RMT_CH);
	rmt_ll_tx_reset_loop_count(hw, ARD_RMT_CH);
	rmt_ll_enable_interrupt(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH), false);
	rmt_ll_clear_interrupt_status(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH));
	/*  rmt_select_periph_clock(): group source + divider, then the
	 *  channel divider. Signature: (dev, channel, src, integral,
	 *  denominator, numerator). */
	rmt_ll_set_group_clock_src(hw, ARD_RMT_CH, ARD_RMT_CLK_SRC, ARD_RMT_GROUP_DIV, 1, 0);
	rmt_ll_enable_group_clock(hw, true);
	rmt_ll_tx_set_channel_clock_div(hw, ARD_RMT_CH, ARD_RMT_CH_DIV);
	/*  rmt_new_tx_channel(): one memory block, no carrier, idle low
	 *  (fixed), no wrap (the frame fits), no loop. */
	rmt_ll_tx_set_mem_blocks(hw, ARD_RMT_CH, 1);
	rmt_ll_tx_enable_carrier_modulation(hw, ARD_RMT_CH, false);
	rmt_ll_tx_fix_idle_level(hw, ARD_RMT_CH, 0, true);
	rmt_ll_tx_enable_wrap(hw, ARD_RMT_CH, false);
	rmt_ll_tx_enable_loop(hw, ARD_RMT_CH, false);
	rmt_ll_tx_enable_loop_count(hw, ARD_RMT_CH, false);
	ard_rgb_inited = true;
}

static void
ard_rgb_route(uint8_t pin)
{
	/*  Pad as a push-pull GPIO output (input buffer on too, harmless),
	 *  then its output source = RMT channel 0 instead of simple GPIO.
	 *  Done on every write, not cached: a pinMode(pin, OUTPUT) between
	 *  two writes puts out_sel back to simple GPIO, and a cached "already
	 *  routed" would leave the LED silently frozen. Both writes are
	 *  idempotent and cheap. The pin was checked by the caller. */
	pinMode(pin, OUTPUT);
	esp_rom_gpio_connect_out_signal((uint32_t) pin, RMT_SIG_OUT0_IDX, false, false);
}

static inline uint32_t
ard_rgb_symbol(uint32_t high_ticks, uint32_t low_ticks)
{
	rmt_symbol_word_t	w;

	w.val = 0U;
	w.level0 = 1U;
	w.duration0 = (uint16_t) high_ticks;
	w.level1 = 0U;
	w.duration1 = (uint16_t) low_ticks;
	return w.val;
}

void
rgbLedWrite(uint8_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
	rmt_dev_t			*hw = ARD_RMT_HW;
	volatile uint32_t	*mem = &RMTMEM.channels[ARD_RMT_CH].symbols[0].val;
	uint32_t			grb = ((uint32_t) green_val << 16) | ((uint32_t) red_val << 8) | blue_val;
	rmt_symbol_word_t	reset;
	uint32_t			i;

	/*  Same rule as pinMode (arduino_gpio.c: the USB Serial/JTAG and MSPI
	 *  pads, everything outside SOC_GPIO_VALID_GPIO_MASK, and
	 *  pin >= GPIO_NUM_MAX), checked before anything is routed or
	 *  connected on the pad. */
	if (!ard_gpio_pin_ok(pin)) {
		syslog(LOG_WARNING, "[S3-RGB] rgbLedWrite: refused pin %u", (uint_t) pin);
		return;
	}
	if (!ard_rgb_inited) {
		ard_rgb_init();
	}
	ard_rgb_route(pin);

	/*  rmt_tx_do_transaction(): pointer reset, then the symbols. */
	rmt_ll_tx_reset_pointer(hw, ARD_RMT_CH);
	for (i = 0U; i < ARD_RGB_NBITS; i++) {
		bool	one = ((grb >> (ARD_RGB_NBITS - 1U - i)) & 1U) != 0U;

		mem[i] = one ? ard_rgb_symbol(ARD_WS_T1H, ARD_WS_T1L)
					 : ard_rgb_symbol(ARD_WS_T0H, ARD_WS_T0L);
	}
	reset.val = 0U;
	reset.level0 = 0U;
	reset.duration0 = (uint16_t) ARD_WS_RESET;
	reset.level1 = 0U;
	reset.duration1 = (uint16_t) ARD_WS_RESET;
	mem[ARD_RGB_NBITS] = reset.val;
	mem[ARD_RGB_NBITS + 1U] = 0U;		/* end marker */

	rmt_ll_clear_interrupt_status(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH) | RMT_LL_EVENT_TX_ERROR(ARD_RMT_CH));
	rmt_ll_tx_start(hw, ARD_RMT_CH);

	for (i = 0U; i < ARD_RGB_POLL_LIMIT; i++) {
		if ((rmt_ll_tx_get_interrupt_status_raw(hw, ARD_RMT_CH) & RMT_LL_EVENT_TX_DONE(ARD_RMT_CH)) != 0U) {
			ard_rgb_tx_done++;
			syslog(LOG_NOTICE, "[S3-RGB] tx_done=%u", (uint_t) ard_rgb_tx_done);
			return;
		}
		esp_rom_delay_us(ARD_RGB_POLL_US);
	}
	/*  Timed out: the channel may still be running. Stop it and forget the
	 *  init so the next call re-initialises (reset_register + clock) before
	 *  rewriting RMTMEM under a possibly active transmitter. */
	rmt_ll_tx_stop(hw, ARD_RMT_CH);
	rmt_ll_clear_interrupt_status(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH) | RMT_LL_EVENT_TX_ERROR(ARD_RMT_CH));
	ard_rgb_inited = false;
	syslog(LOG_WARNING, "[S3-RGB] tx timeout");
}
