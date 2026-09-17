/*
 *  rgbLedWrite (ESP32 / LX6): one SK6812 / WS2812 pixel over RMT TX channel 0
 *
 *  The ESP32 (M5AtomLite: SK6812 3535 on G27) version of arduino_rgb_led.c,
 *  which is the ESP32-S3 one (M5AtomS3 Lite) and is left untouched so the S3
 *  stage's object stays byte for byte what it was (the stages are built with
 *  -g, so even a comment moved inside that file would change its DWARF). The
 *  CMakeLists links exactly one of the two per chip, into wifi-connect only.
 *  Same design as the S3 and C6 files: ESP-IDF v5.5's esp_driver_rmt
 *  register sequence with the driver objects, the interrupt and the encoder
 *  taken out, through the header-only hal/rmt_ll.h (esp32-libs 3.3.8) and
 *  direct RMTMEM writes. No IDF driver, no FreeRTOS, no interrupt line.
 *
 *  What differs from the ESP32-S3 file:
 *    - hal/rmt_ll.h (esp32) has no rmt_ll_tx_clear_sync_group(),
 *      rmt_ll_tx_reset_loop_count(), rmt_ll_tx_enable_loop_count() (no TX
 *      synchro, no loop count: soc_caps.h) and no rmt_ll_tx_stop() (no
 *      SOC_RMT_SUPPORT_ASYNC_STOP). The timeout path therefore stops the
 *      channel the way rmt_tx_disable() does on this chip: zero the channel
 *      memory (an all-zero word is the end marker) and wait for TX_DONE.
 *    - rmt_ll_set_group_clock_src() ignores the group divider: the source
 *      (RMT_CLK_SRC_APB = RMT_CLK_SRC_DEFAULT, 80 MHz) feeds the channel
 *      divider directly (conf1.ref_always_on = 1). rmt_sclk = 80 MHz, the
 *      same number the S3 reaches with its group divider of 1, so the
 *      channel divider and every tick count below are the S3's.
 *    - rmt_ll_enable_bus_clock() reads DPORT_PERIP_CLK_EN_REG through
 *      DPORT_READ_PERI_REG = esp_dport_access_reg_read(), which this
 *      runtime defines in wifi/shim/wifi_stubs.c (wifi-connect links it).
 *      Like the S3 header it is a macro that refuses to compile outside a
 *      PERIPH_RCC_ATOMIC() section (esp_private/periph_ctrl.h), whose
 *      periph_rcc_enter/exit are wifi/hal_src/periph_ctrl.c.
 *    - the RMT block: 8 channels, all TX-capable, 64 words each
 *      (soc_caps.h: SOC_RMT_CHANNELS_PER_GROUP 8,
 *      SOC_RMT_TX_CANDIDATES_PER_GROUP 8, SOC_RMT_MEM_WORDS_PER_CHANNEL 64).
 *    - RMT = 0x3ff56000, RMTMEM = 0x3ff56800 (esp32.peripherals.ld PROVIDEs
 *      both; only the symbols appear here), RMT_SIG_OUT0_IDX = 87
 *      (soc/gpio_sig_map.h).
 *    - the LED is an SK6812, not a WS2812B, so the bit timings are the
 *      intersection of the two data sheets (below); the S3 file's
 *      T1H = 0.90 us is outside the SK6812's window.
 *
 *  Clock: RMT_CLK_SRC_APB (80 MHz), channel divider 4: 1 tick = 50 ns.
 *  Every duration below is a whole number of ticks and the 15-bit duration
 *  field (max 32767 = 1.6 ms) holds the reset gap with room to spare.
 *
 *  Bit timing (800 kHz, 1.25 us per bit), chosen inside BOTH windows
 *  (WS2812B: T0H 0.40 T0L 0.85 T1H 0.80 T1L 0.45, SK6812: T0H 0.30 T0L 0.90
 *  T1H 0.60 T1L 0.60, each +-0.15 us):
 *    bit 0: high 0.35 us, low 0.90 us    bit 1: high 0.70 us, low 0.55 us
 *    reset: low >= 80 us (SK6812; the WS2812B wants >= 50 us); sent as one
 *    explicit symbol of 2 x 50 us = 100 us after the 24 data symbols, then
 *    the end marker (a word of 0: duration 0 stops the TX). The idle level
 *    is fixed low, so the line also stays low between calls.
 *    24 + 1 + 1 = 26 words fit the 64-word block of channel 0 without wrap.
 *
 *  Colour order GRB (the core's default), MSB first per byte. The SK6812
 *  3535 (RGB, no white channel) takes the same 24-bit GRB frame.
 *
 *  Completion: the raw TX_DONE(0) status is polled, at most
 *  ARD_RGB_POLL_LIMIT x esp_rom_delay_us(10) (20 ms; a frame is ~130 us).
 *  Each write logs `[LX6-RGB] tx_done=<count>` (count of completed
 *  transmissions so far) or `[LX6-RGB] tx timeout`.
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

#if !defined(TOPPERS_ESP32_LX6)
#error "arduino_rgb_led_lx6.c (ports/m5stack_xtensa) is the ESP32 (LX6) version; the ESP32-S3 one is arduino_rgb_led.c"
#endif
#if defined(SOC_RMT_SUPPORT_ASYNC_STOP)
#error "this chip has rmt_ll_tx_stop(); the timeout path below assumes it does not"
#endif

/*  RMTMEM is a linker symbol (esp32.peripherals.ld: 0x3ff56800); the
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
#define ARD_RMT_GROUP_DIV	1U				/* ignored on this chip; rmt_sclk = APB = 80 MHz */
#define ARD_RMT_CH_DIV		4U				/* 20 MHz: 1 tick = 50 ns */
#define ARD_RMT_TICK_NS		50U

/*  Durations in ticks (intersection of the SK6812 and WS2812B windows). */
#define ARD_WS_T0H		(350U / ARD_RMT_TICK_NS)	/* 7  */
#define ARD_WS_T0L		(900U / ARD_RMT_TICK_NS)	/* 18 */
#define ARD_WS_T1H		(700U / ARD_RMT_TICK_NS)	/* 14 */
#define ARD_WS_T1L		(550U / ARD_RMT_TICK_NS)	/* 11 */
#define ARD_WS_RESET	(50000U / ARD_RMT_TICK_NS)	/* 1000 = 50 us, twice = 100 us */

#define ARD_RGB_NBITS		24U
#define ARD_RGB_NWORDS		(ARD_RGB_NBITS + 2U)	/* + reset + end marker */
#define ARD_RGB_POLL_LIMIT	2000U
#define ARD_RGB_POLL_US		10U

/*  Largest value of a symbol's duration0/duration1 field (15 bits,
 *  rmt_symbol_word_t in hal/rmt_types.h). */
#define ARD_RMT_DURATION_MAX	((1U << 15) - 1U)

_Static_assert(350U % ARD_RMT_TICK_NS == 0 && 900U % ARD_RMT_TICK_NS == 0
			   && 700U % ARD_RMT_TICK_NS == 0 && 550U % ARD_RMT_TICK_NS == 0,
			   "bit durations are not whole ticks");
_Static_assert(ARD_WS_T0H + ARD_WS_T0L == ARD_WS_T1H + ARD_WS_T1L, "bit periods differ");
_Static_assert(ARD_WS_RESET <= ARD_RMT_DURATION_MAX, "reset gap exceeds the 15-bit duration field");
_Static_assert(ARD_RGB_NWORDS <= SOC_RMT_MEM_WORDS_PER_CHANNEL, "frame does not fit one RMT block");
_Static_assert(ARD_RMT_CH < SOC_RMT_TX_CANDIDATES_PER_GROUP, "channel 0 is not a TX channel");

static bool		ard_rgb_inited;				/* RMT clock/channel set up once */
static uint32_t	ard_rgb_tx_done;			/* completed transmissions */

static void
ard_rgb_init(void)
{
	rmt_dev_t	*hw = ARD_RMT_HW;

	/*  rmt_acquire_group_handle(): bus clock on, module reset. Both are
	 *  macro-wrapped to require a PERIPH_RCC_ATOMIC() section (they
	 *  touch DPORT_PERIP_CLK_EN_REG / DPORT_PERIP_RST_EN_REG, which other
	 *  peripherals share); periph_rcc_enter/exit are this runtime's own
	 *  (wifi/hal_src/periph_ctrl.c, linked into this profile). */
	PERIPH_RCC_ATOMIC() {
		rmt_ll_enable_bus_clock(ARD_RMT_GROUP, true);
		rmt_ll_reset_register(ARD_RMT_GROUP);
	}
	/*  rmt_hal_init() (plus the register/memory clock gate, which the
	 *  driver leaves enabled via rmt_ll_enable_periph_clock). This chip
	 *  has no TX sync group to clear. */
	rmt_ll_enable_periph_clock(hw, true);
	rmt_ll_mem_power_by_pmu(hw);
	rmt_ll_enable_mem_access_nonfifo(hw, true);
	rmt_ll_enable_interrupt(hw, UINT32_MAX, false);
	rmt_ll_clear_interrupt_status(hw, UINT32_MAX);
	/*  rmt_hal_tx_channel_reset() (no loop counter on this chip). */
	rmt_ll_tx_reset_channels_clock_div(hw, 1U << ARD_RMT_CH);
	rmt_ll_tx_reset_pointer(hw, ARD_RMT_CH);
	rmt_ll_enable_interrupt(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH), false);
	rmt_ll_clear_interrupt_status(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH));
	/*  rmt_select_periph_clock(): source (the divider arguments are
	 *  ignored on this chip), then the channel divider. Signature: (dev,
	 *  channel, src, integral, denominator, numerator). */
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

	/*  Same rule as pinMode(pin, OUTPUT) (arduino_gpio.c: on the ESP32 the
	 *  UART0 pads 1/3, the MSPI pads 6-11, everything outside
	 *  SOC_GPIO_VALID_GPIO_MASK, pin >= GPIO_NUM_MAX, and - the OUTPUT
	 *  part, which pinMode checks with a static helper of its own and is
	 *  repeated here so nothing is connected to a pad pinMode would have
	 *  refused - the input-only pads 34-39 outside
	 *  SOC_GPIO_VALID_OUTPUT_GPIO_MASK). G27 (the M5AtomLite's LED) is an
	 *  ordinary output-capable pad. */
	if (!ard_gpio_pin_ok(pin)
		|| ((SOC_GPIO_VALID_OUTPUT_GPIO_MASK >> pin) & 1ULL) == 0ULL) {
		syslog(LOG_WARNING, "[LX6-RGB] rgbLedWrite: refused pin %u", (uint_t) pin);
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
			syslog(LOG_NOTICE, "[LX6-RGB] tx_done=%u", (uint_t) ard_rgb_tx_done);
			return;
		}
		esp_rom_delay_us(ARD_RGB_POLL_US);
	}
	/*  Timed out: the channel may still be running and this chip cannot
	 *  stop it asynchronously. rmt_tx_disable()'s trick for this chip:
	 *  overwrite the channel memory with end markers so the transmitter
	 *  runs off the end, then (bounded here, unbounded in IDF) wait for
	 *  TX_DONE. Forget the init so the next call re-initialises
	 *  (reset_register + clock) before rewriting RMTMEM. */
	for (i = 0U; i < SOC_RMT_MEM_WORDS_PER_CHANNEL; i++) {
		mem[i] = 0U;
	}
	for (i = 0U; i < ARD_RGB_POLL_LIMIT; i++) {
		if ((rmt_ll_tx_get_interrupt_status_raw(hw, ARD_RMT_CH) & RMT_LL_EVENT_TX_DONE(ARD_RMT_CH)) != 0U) {
			break;
		}
		esp_rom_delay_us(ARD_RGB_POLL_US);
	}
	rmt_ll_clear_interrupt_status(hw, RMT_LL_EVENT_TX_MASK(ARD_RMT_CH) | RMT_LL_EVENT_TX_ERROR(ARD_RMT_CH));
	ard_rgb_inited = false;
	syslog(LOG_WARNING, "[LX6-RGB] tx timeout");
}
