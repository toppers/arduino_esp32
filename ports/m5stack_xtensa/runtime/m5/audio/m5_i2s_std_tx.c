/*
 *  TOPPERS/FMP3 ESP32-S3 port - I2S standard-mode TX channel for M5Unified
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  What this is
 *  ------------
 *  The six functions of the ESP-IDF v5.5 I2S channel API that M5Unified's
 *  Speaker_Class calls, and nothing more:
 *
 *    i2s_new_channel / i2s_channel_init_std_mode / i2s_channel_enable /
 *    i2s_channel_disable / i2s_channel_write / i2s_del_channel
 *
 *  for one TX channel in standard (Philips / MSB) mode, master role, on the
 *  ESP32-S3. It replaces libesp_driver_i2s.a rather than linking it: that
 *  archive and the GDMA driver it needs would bring in seventeen FreeRTOS
 *  entry points, ISR yield hooks and the IDF clock tree, all as code nobody
 *  here can instrument. See m5_freertos_abi.c for why the ABI exists anyway.
 *
 *  What it is built from
 *  ---------------------
 *   - Slot format and the initial clock: i2s_hal_std_set_tx_slot /
 *     i2s_hal_set_tx_clock from the SDK's libhal.a. Those are Espressif's
 *     register sequences and call nothing but memset and a divider helper;
 *     re-deriving the TDM-based slot programming of the S3 by hand is where
 *     a silent format error would come from.
 *   - GDMA: the inline LL of hal/gdma_ll.h, directly. One OUT channel,
 *     a circular ring of descriptors, one EOF interrupt per buffer.
 *   - Interrupt: esp_intr_alloc_intrstatus (m5/shim/esp_shim_intr.c), which
 *     wires the source to a CPU line on PRC1 and runs the handler as an FMP3
 *     ISR. Allocation has to happen in a PRC1 task; Speaker_Class::begin runs
 *     in the sketch task, which is on PRC1.
 *   - Free-buffer queue: the FreeRTOS ABI (m5_freertos_abi.c), used from the
 *     ISR and from the writer exactly as libesp_driver_i2s uses it.
 *
 *  How data moves (the same design as the IDF driver)
 *  --------------------------------------------------
 *  dma_desc_num buffers of dma_frame_num frames each form a ring that the
 *  DMA plays forever. When a buffer finishes, the ISR clears it (auto_clear,
 *  so an idle channel plays silence) and posts it to a queue that holds at
 *  most dma_desc_num - 1 entries - the buffer being played is never offered.
 *  If nobody is writing and the queue is full, the ISR drops the oldest
 *  entry to make room: the queue always holds the most recently finished
 *  buffers. i2s_channel_write takes buffers from that queue in order and
 *  fills them; in steady state it blocks on the queue, and each EOF releases
 *  the buffer that is furthest from being played again.
 *
 *  What Speaker_Class does itself
 *  ------------------------------
 *  Its spk_task writes the real sample-rate dividers into the I2S registers
 *  and then waits, unbounded, for tx_update to clear. That only clears if
 *  the module clock is running, so init_std_mode must leave it running. The
 *  clock set here (the sample rate Speaker_Class passes, 48 kHz as a
 *  placeholder) is overwritten by spk_task before the first sample.
 *
 *  Not supported, and reported as such: RX, duplex, slave role, PDM, TDM,
 *  more than one channel at a time, and chips other than the ESP32-S3.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3

#include "esp_err.h"
#include "driver/i2s_std.h"
#include "hal/i2s_hal.h"
#include "hal/i2s_ll.h"
#include "hal/gdma_ll.h"
#include "hal/gpio_ll.h"
#include "hal/dma_types.h"
#include "hal/hal_utils.h"
#include "soc/gdma_channel.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

/*
 *  The FreeRTOS ABI, declared here rather than through <freertos/queue.h>:
 *  in the m5 runtime that name resolves to the compat header, whose static
 *  inline xQueueReceive would shadow the external function this driver must
 *  call. Types as the IDF Xtensa port defines them (see m5_freertos_abi.c).
 */
typedef int				abi_base_t;
typedef unsigned int	abi_ubase_t;
typedef void			*abi_queue_t;
typedef struct {
	volatile uint32_t	owner;
	volatile uint32_t	count;
} abi_mux_t;
#define ABI_FOREVER		0xFFFFFFFFU
#define ABI_BACK		0

extern abi_queue_t	xQueueGenericCreate(abi_ubase_t len, abi_ubase_t item, uint8_t type);
extern void			vQueueDelete(abi_queue_t q);
extern abi_base_t	xQueueGenericReset(abi_queue_t q, abi_base_t new_queue);
extern abi_base_t	xQueueReceive(abi_queue_t q, void *buf, uint32_t ticks);
extern abi_base_t	xQueueGenericSendFromISR(abi_queue_t q, const void *item,
											 abi_base_t *woken, abi_base_t pos);
extern abi_base_t	xQueueReceiveFromISR(abi_queue_t q, void *buf, abi_base_t *woken);
extern abi_base_t	xQueueIsQueueFullFromISR(abi_queue_t q);
extern abi_base_t	xPortEnterCriticalTimeout(abi_mux_t *mux, abi_base_t timeout);
extern void			vPortExitCritical(abi_mux_t *mux);

/*  m5/shim/esp_shim_intr.c and m5/shim/m5_kernel_shim.c. */
struct intr_handle_data_t;
extern esp_err_t	esp_intr_alloc_intrstatus(int source, int flags,
										  uint32_t intrstatusreg,
										  uint32_t intrstatusmask,
										  void (*handler)(void *), void *arg,
										  struct intr_handle_data_t **ret_handle);
extern esp_err_t	esp_intr_free(struct intr_handle_data_t *handle);
extern void			*heap_caps_aligned_calloc(size_t alignment, size_t n,
											  size_t size, uint32_t caps);
extern void			heap_caps_free(void *ptr);

/*  ROM (esp32s3.rom.api.ld: esp_rom_gpio_connect_out_signal = gpio_matrix_out). */
extern void	esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx,
											bool out_inv, bool oen_inv);

/*  The GDMA OUT channel this driver owns. Nothing else in the m5 runtime
 *  uses GDMA: the LCD's SPI DMA is closed at build time (m5_idf_prelude.h)
 *  and at run time (phase5DisableDisplayDma), and enabling the GDMA clock
 *  below does not reopen it - lgfx decides DMA by its own dma_channel. */
#define M5_I2S_GDMA_CH			0U
#define M5_I2S_GDMA_OUT_SOURCE	(71 + (int) M5_I2S_GDMA_CH)	/* ETS_DMA_OUT_CH0_INTR_SOURCE */

#define M5_I2S_SCLK_PLL_F160M	160000000U		/* I2S_CLK_SRC_PLL_160M */
#define M5_I2S_MAX_BUF_BYTES	4092U			/* dw0.size is 12 bits, keep 4-aligned */

struct i2s_channel_obj_t {
	bool				in_use;
	bool				initialized;	/* init_std_mode succeeded */
	bool				enabled;
	int					port;
	i2s_hal_context_t	hal;
	uint32_t			desc_num;
	uint32_t			frame_num;
	bool				auto_clear;
	uint32_t			buf_size;
	dma_descriptor_t	*desc;			/* desc_num descriptors, a ring */
	uint8_t				*pool;			/* desc_num * buf_size bytes */
	abi_queue_t			free_q;			/* finished buffers (uint8_t *) */
	uint8_t				*curr;			/* buffer being filled by write */
	uint32_t			rw_pos;
	struct intr_handle_data_t *intr;
	int					pins[4];		/* routed outputs, -1 if none */

	/*  Counters for m5_i2s_tx_stats(); read-only for everyone else. */
	volatile uint32_t	n_eof;			/* buffers the DMA finished */
	volatile uint32_t	n_dropped;		/* free buffers dropped: nobody wrote */
	volatile uint32_t	n_write_timeout;	/* a wait with timeout_ms > 0 ran out */
	volatile uint32_t	n_write_poll_empty;	/* timeout_ms == 0 and no free buffer */
	volatile uint64_t	bytes_written;
	volatile uint32_t	peak_abs;		/* largest |sample| written, 16-bit */
};

/*  One TX channel at a time: Speaker_Class opens exactly one. */
static struct i2s_channel_obj_t	m5_i2s_chan;
static bool						m5_gdma_clocked;
static abi_mux_t				m5_i2s_rcc_mux = { 0xB33FFFFFU, 0U };

static gdma_dev_t *
m5_gdma(void)
{
	return(&GDMA);
}

/* ---- interrupt ---- */

static void
m5_i2s_tx_isr(void *arg)
{
	struct i2s_channel_obj_t	*ch = (struct i2s_channel_obj_t *) arg;
	uint32_t					status;
	dma_descriptor_t			*done;
	uint8_t						*buf;
	uint8_t						*dropped;
	abi_base_t					woken = 0;

	status = gdma_ll_tx_get_interrupt_status(m5_gdma(), M5_I2S_GDMA_CH, false);
	gdma_ll_tx_clear_interrupt_status(m5_gdma(), M5_I2S_GDMA_CH, status);
	if ((status & GDMA_LL_EVENT_TX_EOF) == 0U) {
		return;
	}
	done = (dma_descriptor_t *)(uintptr_t)
		   gdma_ll_tx_get_eof_desc_addr(m5_gdma(), M5_I2S_GDMA_CH);
	if ((done < ch->desc) || (done >= ch->desc + ch->desc_num)) {
		return;		/* not one of ours: do not touch memory we do not own */
	}
	buf = (uint8_t *) done->buffer;
	ch->n_eof++;
	if (xQueueIsQueueFullFromISR(ch->free_q)) {
		/*  Nobody has written for a whole ring: drop the oldest free buffer
		 *  so the queue keeps the ones furthest from being played again. */
		(void) xQueueReceiveFromISR(ch->free_q, &dropped, &woken);
		ch->n_dropped++;
	}
	if (ch->auto_clear) {
		memset(buf, 0, ch->buf_size);
	}
	(void) xQueueGenericSendFromISR(ch->free_q, &buf, &woken, ABI_BACK);
}

/* ---- helpers ---- */

static uint32_t
m5_i2s_signal(int port, int which)
{
	/*  which: 0 = BCK, 1 = WS, 2 = SD out, 3 = MCLK */
	static const uint16_t	sig[2][4] = {
		{ I2S0O_BCK_OUT_IDX, I2S0O_WS_OUT_IDX, I2S0O_SD_OUT_IDX, I2S0_MCLK_OUT_IDX },
		{ I2S1O_BCK_OUT_IDX, I2S1O_WS_OUT_IDX, I2S1O_SD_OUT_IDX, I2S1_MCLK_OUT_IDX },
	};
	return(sig[port][which]);
}

static void
m5_route_output(int gpio, uint32_t signal)
{
	/*  The order arduino_gpio.c uses for an output: enable, select the GPIO
	 *  function on the pad, then connect the peripheral signal through the
	 *  matrix. */
	gpio_ll_output_enable(&GPIO, (uint32_t) gpio);
	gpio_ll_func_sel(&GPIO, (uint8_t) gpio, PIN_FUNC_GPIO);
	esp_rom_gpio_connect_out_signal((uint32_t) gpio, signal, false, false);
}

static void
m5_release_route(int gpio)
{
	gpio_ll_matrix_out_default(&GPIO, (uint32_t) gpio);
}

static void
m5_free_dma(struct i2s_channel_obj_t *ch)
{
	heap_caps_free(ch->pool);
	heap_caps_free(ch->desc);
	ch->pool = NULL;
	ch->desc = NULL;
}

/* ---- the API ---- */

esp_err_t
i2s_new_channel(const i2s_chan_config_t *chan_cfg,
				i2s_chan_handle_t *ret_tx_handle,
				i2s_chan_handle_t *ret_rx_handle)
{
	struct i2s_channel_obj_t	*ch = &m5_i2s_chan;
	abi_queue_t					q;

	if ((chan_cfg == NULL) || (ret_tx_handle == NULL)) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (ret_rx_handle != NULL) {
		return(ESP_ERR_NOT_SUPPORTED);		/* TX only */
	}
	if (chan_cfg->role != I2S_ROLE_MASTER) {
		return(ESP_ERR_NOT_SUPPORTED);
	}
	if (((int) chan_cfg->id != 0) && ((int) chan_cfg->id != 1)) {
		return(ESP_ERR_INVALID_ARG);		/* I2S_NUM_AUTO is not offered */
	}
	if ((chan_cfg->dma_desc_num < 2U) || (chan_cfg->dma_frame_num == 0U)) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (ch->in_use) {
		return(ESP_ERR_NOT_FOUND);			/* what IDF says for a busy port */
	}
	q = xQueueGenericCreate(chan_cfg->dma_desc_num - 1U, sizeof(uint8_t *), 0U);
	if (q == NULL) {
		return(ESP_ERR_NO_MEM);
	}

	memset(ch, 0, sizeof(*ch));
	ch->in_use = true;
	ch->port = (int) chan_cfg->id;
	ch->desc_num = chan_cfg->dma_desc_num;
	ch->frame_num = chan_cfg->dma_frame_num;
	ch->auto_clear = chan_cfg->auto_clear;
	ch->free_q = q;
	ch->pins[0] = ch->pins[1] = ch->pins[2] = ch->pins[3] = -1;

	/*  SYSTEM.perip_clk_en* are shared with every other peripheral: IDF
	 *  guards these read-modify-writes with its RCC spinlock; the ABI
	 *  critical section is that lock here. */
	(void) xPortEnterCriticalTimeout(&m5_i2s_rcc_mux, -1);
	/*  Parenthesised to call the functions, not the macros of the same name:
	 *  those expect IDF's PERIPH_RCC_ATOMIC() block around them, which is
	 *  the lock this critical section already provides. */
	(i2s_ll_enable_bus_clock)(ch->port, true);
	(i2s_ll_reset_register)(ch->port);
	if (!m5_gdma_clocked) {
		/*  The whole GDMA block is reset once, before anyone uses it. */
		_gdma_ll_enable_bus_clock(0, true);
		_gdma_ll_reset_register(0);
		m5_gdma_clocked = true;
	}
	vPortExitCritical(&m5_i2s_rcc_mux);
	gdma_ll_force_enable_reg_clock(m5_gdma(), true);

	i2s_hal_init(&ch->hal, ch->port);
	/*  The I2S module clock gate is the I2S's own register (tx_clkm_conf), not
	 *  a shared SYSTEM one, so the RCC wrapper macro is bypassed the same way. */
	(i2s_ll_enable_core_clock)(ch->hal.dev, true);

	*ret_tx_handle = ch;
	return(ESP_OK);
}

esp_err_t
i2s_channel_init_std_mode(i2s_chan_handle_t handle,
						  const i2s_std_config_t *std_cfg)
{
	struct i2s_channel_obj_t	*ch = handle;
	const i2s_std_slot_config_t	*s;
	i2s_hal_slot_config_t		slot;
	i2s_hal_clock_info_t		clk;
	hal_utils_clk_div_t			mclk_div;
	uint32_t					slot_bits, active_slots, bytes_per_sample;
	uint32_t					i;
	esp_err_t					err;

	if ((ch != &m5_i2s_chan) || !ch->in_use || (std_cfg == NULL)) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (ch->enabled) {
		return(ESP_ERR_INVALID_STATE);
	}
	s = &std_cfg->slot_cfg;
	if ((s->data_bit_width != I2S_DATA_BIT_WIDTH_16BIT)
			&& (s->data_bit_width != I2S_DATA_BIT_WIDTH_32BIT)) {
		return(ESP_ERR_NOT_SUPPORTED);		/* 8/24-bit: not needed, not tested */
	}
	if ((std_cfg->clk_cfg.clk_src != I2S_CLK_SRC_PLL_160M)
			&& (std_cfg->clk_cfg.clk_src != I2S_CLK_SRC_DEFAULT)) {
		return(ESP_ERR_NOT_SUPPORTED);
	}

	/* -- slot format: the same field-for-field copy IDF's i2s_std.c makes -- */
	memset(&slot, 0, sizeof(slot));
	slot.data_bit_width = s->data_bit_width;
	slot.slot_bit_width = s->slot_bit_width;
	slot.slot_mode = s->slot_mode;
	slot.std.slot_mask = s->slot_mask;
	slot.std.ws_width = s->ws_width;
	slot.std.ws_pol = s->ws_pol;
	slot.std.bit_shift = s->bit_shift;
	slot.std.left_align = s->left_align;
	slot.std.big_endian = s->big_endian;
	slot.std.bit_order_lsb = s->bit_order_lsb;
	i2s_hal_std_set_tx_slot(&ch->hal, false, &slot);

	/* -- clock: as IDF's i2s_std_calculate_clock for the master role -- */
	slot_bits = (s->slot_bit_width == I2S_SLOT_BIT_WIDTH_AUTO)
				? (uint32_t) s->data_bit_width : (uint32_t) s->slot_bit_width;
	memset(&clk, 0, sizeof(clk));
	clk.sclk = M5_I2S_SCLK_PLL_F160M;
	clk.bclk = std_cfg->clk_cfg.sample_rate_hz * 2U * slot_bits;	/* STD: 2 slots */
	clk.mclk = std_cfg->clk_cfg.sample_rate_hz
			   * (uint32_t) std_cfg->clk_cfg.mclk_multiple;
	if ((clk.bclk == 0U) || (clk.mclk < clk.bclk) || (clk.sclk < clk.mclk)) {
		return(ESP_ERR_INVALID_ARG);
	}
	clk.bclk_div = (uint16_t)(clk.mclk / clk.bclk);
	clk.mclk_div = (uint16_t)(clk.sclk / clk.mclk);
	i2s_hal_set_tx_clock(&ch->hal, &clk, I2S_CLK_SRC_PLL_160M, &mclk_div);
	/*  spk_task waits unbounded on tx_update, which clears only with the
	 *  module clock running (see the top of this file). */
	i2s_ll_tx_enable_clock(ch->hal.dev);

	/* -- DMA buffers: IDF's i2s_get_buf_size -- */
	active_slots = (s->slot_mode == I2S_SLOT_MODE_MONO) ? 1U : 2U;
	bytes_per_sample = (uint32_t) s->data_bit_width / 8U;
	ch->buf_size = ch->frame_num * active_slots * bytes_per_sample;
	if ((ch->buf_size == 0U) || (ch->buf_size > M5_I2S_MAX_BUF_BYTES)) {
		return(ESP_ERR_INVALID_ARG);
	}
	m5_free_dma(ch);			/* init_std_mode may be called again */
	ch->desc = (dma_descriptor_t *) heap_caps_aligned_calloc(
					4U, ch->desc_num, sizeof(dma_descriptor_t), 0U);
	ch->pool = (uint8_t *) heap_caps_aligned_calloc(
					4U, ch->desc_num, ch->buf_size, 0U);
	if ((ch->desc == NULL) || (ch->pool == NULL)) {
		m5_free_dma(ch);
		return(ESP_ERR_NO_MEM);
	}
	for (i = 0U; i < ch->desc_num; i++) {
		ch->desc[i].dw0.size = ch->buf_size;
		ch->desc[i].dw0.length = ch->buf_size;
		ch->desc[i].dw0.suc_eof = 1U;		/* one EOF interrupt per buffer */
		ch->desc[i].dw0.owner = 1U;		/* DMA; owner check stays off */
		ch->desc[i].buffer = &ch->pool[i * ch->buf_size];
		ch->desc[i].next = &ch->desc[(i + 1U) % ch->desc_num];
	}

	/* -- GDMA channel -- */
	gdma_ll_tx_reset_channel(m5_gdma(), M5_I2S_GDMA_CH);
	gdma_ll_tx_enable_owner_check(m5_gdma(), M5_I2S_GDMA_CH, false);
	gdma_ll_tx_enable_auto_write_back(m5_gdma(), M5_I2S_GDMA_CH, false);
	gdma_ll_tx_connect_to_periph(m5_gdma(), M5_I2S_GDMA_CH, GDMA_TRIG_PERIPH_I2S,
								 (ch->port == 0) ? SOC_GDMA_TRIG_PERIPH_I2S0
												 : SOC_GDMA_TRIG_PERIPH_I2S1);
	gdma_ll_tx_enable_interrupt(m5_gdma(), M5_I2S_GDMA_CH, GDMA_LL_TX_EVENT_MASK, false);
	gdma_ll_tx_clear_interrupt_status(m5_gdma(), M5_I2S_GDMA_CH, GDMA_LL_TX_EVENT_MASK);
	gdma_ll_tx_enable_interrupt(m5_gdma(), M5_I2S_GDMA_CH, GDMA_LL_EVENT_TX_EOF, true);
	if (ch->intr == NULL) {
		err = esp_intr_alloc_intrstatus(
				M5_I2S_GDMA_OUT_SOURCE, 0,
				(uint32_t)(uintptr_t) &m5_gdma()->channel[M5_I2S_GDMA_CH].out.int_st,
				GDMA_LL_EVENT_TX_EOF, m5_i2s_tx_isr, ch, &ch->intr);
		if (err != ESP_OK) {
			gdma_ll_tx_enable_interrupt(m5_gdma(), M5_I2S_GDMA_CH, GDMA_LL_EVENT_TX_EOF, false);
			m5_free_dma(ch);
			return(err);
		}
	}

	/* -- pins -- */
	ch->pins[0] = (int) std_cfg->gpio_cfg.bclk;
	ch->pins[1] = (int) std_cfg->gpio_cfg.ws;
	ch->pins[2] = (int) std_cfg->gpio_cfg.dout;
	ch->pins[3] = (int) std_cfg->gpio_cfg.mclk;
	for (i = 0U; i < 4U; i++) {
		if (ch->pins[i] >= 0) {
			m5_route_output(ch->pins[i], m5_i2s_signal(ch->port, (int) i));
		}
	}

	i2s_hal_std_enable_tx_channel(&ch->hal);
	ch->initialized = true;
	return(ESP_OK);
}

esp_err_t
i2s_channel_enable(i2s_chan_handle_t handle)
{
	struct i2s_channel_obj_t	*ch = handle;
	i2s_dev_t					*hw;

	if ((ch != &m5_i2s_chan) || !ch->in_use) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (!ch->initialized || ch->enabled) {
		return(ESP_ERR_INVALID_STATE);
	}
	hw = ch->hal.dev;
	(void) xQueueGenericReset(ch->free_q, 0);
	ch->curr = NULL;
	ch->rw_pos = 0U;
	memset(ch->pool, 0, ch->desc_num * ch->buf_size);

	/*  IDF's order for a GDMA target: reset TX and its FIFO, reset the DMA
	 *  channel, point it at the ring, start it, then start TX. */
	i2s_ll_tx_reset(hw);
	i2s_ll_tx_reset_fifo(hw);
	gdma_ll_tx_reset_channel(m5_gdma(), M5_I2S_GDMA_CH);
	gdma_ll_tx_set_desc_addr(m5_gdma(), M5_I2S_GDMA_CH,
							 (uint32_t)(uintptr_t) &ch->desc[0]);
	gdma_ll_tx_start(m5_gdma(), M5_I2S_GDMA_CH);
	i2s_ll_tx_start(hw);
	ch->enabled = true;
	return(ESP_OK);
}

esp_err_t
i2s_channel_disable(i2s_chan_handle_t handle)
{
	struct i2s_channel_obj_t	*ch = handle;

	if ((ch != &m5_i2s_chan) || !ch->in_use) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (!ch->enabled) {
		return(ESP_ERR_INVALID_STATE);
	}
	i2s_ll_tx_stop(ch->hal.dev);
	gdma_ll_tx_stop(m5_gdma(), M5_I2S_GDMA_CH);
	ch->enabled = false;
	return(ESP_OK);
}

/*
 *  IDF's contract: copy as much as fits, blocking for free buffers up to
 *  timeout_ms each; *bytes_written says how much went in; ESP_ERR_TIMEOUT
 *  if a buffer did not come free in time. Speaker_Class passes its FreeRTOS
 *  tick count here, and one tick is one millisecond in this SDK, so the two
 *  units agree; portMAX_DELAY waits forever.
 */
esp_err_t
i2s_channel_write(i2s_chan_handle_t handle, const void *src, size_t size,
				  size_t *bytes_written, uint32_t timeout_ms)
{
	struct i2s_channel_obj_t	*ch = handle;
	const uint8_t				*from = (const uint8_t *) src;
	size_t						done = 0U;
	size_t						n, k;
	uint32_t					peak;
	esp_err_t					err = ESP_OK;

	if (bytes_written != NULL) {
		*bytes_written = 0U;
	}
	if ((ch != &m5_i2s_chan) || !ch->in_use || ((src == NULL) && (size != 0U))) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (!ch->enabled) {
		return(ESP_ERR_INVALID_STATE);
	}
	peak = ch->peak_abs;
	while (done < size) {
		if ((ch->curr == NULL) || (ch->rw_pos >= ch->buf_size)) {
			if (!xQueueReceive(ch->free_q, &ch->curr,
							   (timeout_ms == ABI_FOREVER) ? ABI_FOREVER : timeout_ms)) {
				ch->curr = NULL;
				/*  Speaker_Class first writes with timeout 0 and then writes
				 *  the rest with portMAX_DELAY: a timeout-0 miss is a poll,
				 *  not a stall, so it is counted apart from real timeouts. */
				if (timeout_ms == 0U) {
					ch->n_write_poll_empty++;
				}
				else {
					ch->n_write_timeout++;
				}
				err = ESP_ERR_TIMEOUT;
				break;
			}
			ch->rw_pos = 0U;
		}
		n = ch->buf_size - ch->rw_pos;
		if (n > (size - done)) {
			n = size - done;
		}
		memcpy(&ch->curr[ch->rw_pos], &from[done], n);
		/*  The loudest sample written, for m5_i2s_tx_stats: the one number
		 *  that tells "the amplifier is fed silence" from "it is fed audio"
		 *  without a listener. 16-bit samples, the only width M5 writes. */
		for (k = 0U; k + 1U < n; k += 2U) {
			int16_t		v = (int16_t)(from[done + k] | (from[done + k + 1U] << 8));
			uint32_t	a = (v < 0) ? (uint32_t)(-(int32_t) v) : (uint32_t) v;

			if (a > peak) {
				peak = a;
			}
		}
		ch->rw_pos += (uint32_t) n;
		done += n;
	}
	ch->peak_abs = peak;
	ch->bytes_written += done;
	if (bytes_written != NULL) {
		*bytes_written = done;
	}
	return(err);
}

esp_err_t
i2s_del_channel(i2s_chan_handle_t handle)
{
	struct i2s_channel_obj_t	*ch = handle;
	uint32_t					i;

	if ((ch != &m5_i2s_chan) || !ch->in_use) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (ch->enabled) {
		return(ESP_ERR_INVALID_STATE);		/* Speaker_Class keeps the handle */
	}
	gdma_ll_tx_enable_interrupt(m5_gdma(), M5_I2S_GDMA_CH, GDMA_LL_EVENT_TX_EOF, false);
	if (ch->intr != NULL) {
		(void) esp_intr_free(ch->intr);
		ch->intr = NULL;
	}
	gdma_ll_tx_disconnect_from_periph(m5_gdma(), M5_I2S_GDMA_CH);
	for (i = 0U; i < 4U; i++) {
		if (ch->pins[i] >= 0) {
			m5_release_route(ch->pins[i]);
		}
	}
	m5_free_dma(ch);
	vQueueDelete(ch->free_q);
	ch->free_q = NULL;
	ch->in_use = false;
	ch->initialized = false;
	return(ESP_OK);
}

/*
 *  Diagnostics for a test or a bridge; any argument may be NULL.
 *  peak_abs is the largest |sample| written since the last
 *  m5_i2s_tx_reset_peak(). It is updated by the writer task without a lock,
 *  so a reset that races a write may keep or lose that one write's peak;
 *  it is a measurement aid, not an interface anything should depend on.
 */
void
m5_i2s_tx_stats(uint32_t *eof, uint32_t *dropped, uint32_t *write_timeouts,
				uint64_t *bytes, uint32_t *peak_abs, int32_t *enabled)
{
	struct i2s_channel_obj_t	*ch = &m5_i2s_chan;

	if (eof != NULL) { *eof = ch->n_eof; }
	if (dropped != NULL) { *dropped = ch->n_dropped; }
	if (write_timeouts != NULL) { *write_timeouts = ch->n_write_timeout; }
	if (bytes != NULL) { *bytes = ch->bytes_written; }
	if (peak_abs != NULL) { *peak_abs = ch->peak_abs; }
	if (enabled != NULL) { *enabled = (ch->in_use && ch->enabled) ? 1 : 0; }
}

uint32_t
m5_i2s_tx_poll_empty(void)
{
	return(m5_i2s_chan.n_write_poll_empty);
}

void
m5_i2s_tx_reset_peak(void)
{
	m5_i2s_chan.peak_abs = 0U;
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
