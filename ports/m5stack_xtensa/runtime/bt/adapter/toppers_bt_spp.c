/*
 * Bluetooth Classic SPP server for Arduino sketches.
 *
 * See docs/superpowers/specs/2026-09-02-bt-classic-spp-design.md.
 *
 * The stack itself is not built here: the M5Stack Arduino core ships BlueDroid
 * compiled, as libbt.a beside the libbtdm_app.a controller, and this runtime
 * supplies the FreeRTOS/ESP-IDF shims those two archives call into. This file
 * is only the bring-up sequence, the callbacks, and the receive ring that gets
 * bytes from the callback context over to the Arduino task.
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*  ESP-IDF headers do not pull sdkconfig.h in themselves - the IDF build
 *  system force-includes it. Without it esp_bt.h takes its "bluetooth is not
 *  enabled" branch and BT_CONTROLLER_INIT_CONFIG_DEFAULT() becomes a static
 *  assertion failure. */
#include "sdkconfig.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_shim.h"
#include "syssvc/logtask.h"

extern void	esp_shim_coex_adapter_register(void);
extern void	esp_shim_bt_clock_init(void);
#include "toppers_bt_spp.h"

/*
 * The receive ring.
 *
 * Written by the SPP callback, read by the sketch. Both sides hold it for a
 * few instructions only, so the guard is the same interrupt lock the Wi-Fi
 * adapter uses rather than a mutex the callback context may not take.
 *
 * A full ring drops the newest bytes and counts them. Dropping silently would
 * make a too-slow sketch look like a flaky link.
 */
#define BT_SPP_RING_SIZE 1024U

static uint8_t		spp_ring[BT_SPP_RING_SIZE];
static volatile uint32_t spp_ring_head;		/* write index */
static volatile uint32_t spp_ring_tail;		/* read index */
static volatile uint32_t spp_ring_dropped;

static volatile uint32_t spp_handle;		/* 0 when not connected */
static volatile bool	spp_congested;
static volatile bool	spp_server_started;
static bool		spp_stack_up;

static void
ring_reset(void)
{
	uint32_t	state = esp_shim_int_disable();

	spp_ring_head = 0U;
	spp_ring_tail = 0U;
	esp_shim_int_restore(state);
}

static void
ring_put(const uint8_t *data, size_t len)
{
	uint32_t	state = esp_shim_int_disable();
	size_t		i;

	for (i = 0U; i < len; i++) {
		uint32_t	next = (spp_ring_head + 1U) % BT_SPP_RING_SIZE;

		if (next == spp_ring_tail) {
			/*  Full: drop the rest and account for it. */
			spp_ring_dropped += (uint32_t)(len - i);
			break;
		}
		spp_ring[spp_ring_head] = data[i];
		spp_ring_head = next;
	}
	esp_shim_int_restore(state);
}

static size_t
ring_take(uint8_t *buf, size_t len)
{
	uint32_t	state = esp_shim_int_disable();
	size_t		taken = 0U;

	while (taken < len && spp_ring_tail != spp_ring_head) {
		buf[taken] = spp_ring[spp_ring_tail];
		spp_ring_tail = (spp_ring_tail + 1U) % BT_SPP_RING_SIZE;
		taken++;
	}
	esp_shim_int_restore(state);
	return(taken);
}

/*
 * GAP callback.
 *
 * The pairing policy is the upstream one, unchanged: Secure Simple Pairing
 * confirmations are accepted automatically and legacy pairing replies with the
 * fixed PIN. It is NOT secure - any device in range can pair and connect - and
 * the README, the release README and the example all say so.
 */
static void
bt_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
	switch (event) {
	case ESP_BT_GAP_AUTH_CMPL_EVT:
		if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
			syslog_1(LOG_NOTICE, "bt: paired with %s",
					 (intptr_t) param->auth_cmpl.device_name);
		}
		else {
			syslog_1(LOG_WARNING, "bt: pairing failed, status %d",
					 (intptr_t) param->auth_cmpl.stat);
		}
		break;

	case ESP_BT_GAP_CFM_REQ_EVT:
		/*  Accept the numeric comparison without showing it. */
		esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
		break;

	case ESP_BT_GAP_KEY_NOTIF_EVT:
		syslog_1(LOG_NOTICE, "bt: passkey %d",
				 (intptr_t) param->key_notif.passkey);
		break;

	case ESP_BT_GAP_PIN_REQ_EVT: {
		esp_bt_pin_code_t	pin = { '1', '2', '3', '4' };

		esp_bt_gap_pin_reply(param->pin_req.bda, true, 4U, pin);
		break;
	}

	default:
		break;
	}
}

static void
bt_spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
	switch (event) {
	case ESP_SPP_INIT_EVT:
		if (param->init.status == ESP_SPP_SUCCESS) {
			esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE,
							  0U, "SPP_SERVER");
		}
		break;

	case ESP_SPP_START_EVT:
		spp_server_started = (param->start.status == ESP_SPP_SUCCESS);
		if (spp_server_started) {
			esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
									 ESP_BT_GENERAL_DISCOVERABLE);
		}
		break;

	case ESP_SPP_SRV_OPEN_EVT:
		ring_reset();
		spp_congested = false;
		spp_handle = param->srv_open.handle;
		syslog_0(LOG_NOTICE, "bt: SPP connection open");
		break;

	case ESP_SPP_CLOSE_EVT:
		spp_handle = 0U;
		spp_congested = false;
		syslog_0(LOG_NOTICE, "bt: SPP connection closed");
		break;

	case ESP_SPP_DATA_IND_EVT:
		ring_put(param->data_ind.data, (size_t) param->data_ind.len);
		break;

	case ESP_SPP_CONG_EVT:
		spp_congested = (param->cong.cong != 0);
		break;

	default:
		break;
	}
}

/*
 * Bring-up.
 *
 * Every stage is checked and reported, because a half-initialised BlueDroid
 * fails later in a place that says nothing about which stage went wrong.
 */
bool
toppers_bt_spp_begin(const char *device_name)
{
	esp_bt_controller_config_t	cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	esp_spp_cfg_t			spp_cfg = {
		.mode = ESP_SPP_MODE_CB,
		.enable_l2cap_ertm = true,
		.tx_buffer_size = 0U,
	};
	esp_err_t			err;

	if (spp_stack_up) {
		return(true);
	}
	if (device_name == NULL) {
		device_name = "M5Stack-SPP";
	}

	ring_reset();
	spp_ring_dropped = 0U;
	spp_handle = 0U;
	spp_congested = false;
	spp_server_started = false;

	cfg.mode = ESP_BT_MODE_CLASSIC_BT;

	/*
	 *  ★Register the coexistence adapter first.
	 *
	 *  On the LX6 the coex function table is not supplied by ROM or by any
	 *  blob; esp_coex_adapter.c has to hand libcoexist.a its callbacks and
	 *  then call coex_pre_init(). The Wi-Fi profile does this from
	 *  toppers_wifi_core.c, which this profile does not link, and the
	 *  controller calls coex_init()/coex_enable() regardless of whether
	 *  Wi-Fi exists. Without it coex_enable() dereferences a null coex
	 *  environment - seen on 2026-09-02 on a real M5Stack Basic as
	 *  EXCCAUSE=28 at coex_core_enable+0x8, vaddr=0x30.
	 */
	esp_shim_coex_adapter_register();

	/*
	 *  ★Ungate the BT clock and release the BT reset first.
	 *
	 *  ESP-IDF's bt.c expects periph_module_enable(PERIPH_BT_MODULE) to do
	 *  this, but this port's periph_ctrl.c has that entire function body
	 *  behind __PERIPH_CTRL_ALLOW_LEGACY_API, which nothing defines - it is a
	 *  no-op, and silently so. bt_shim.c carries esp_shim_bt_clock_init() for
	 *  exactly this, writing the DPORT clock-enable and reset registers, and
	 *  says it must be called immediately before controller init.
	 *
	 *  Without it the baseband stays in reset: on 2026-09-02 on a real
	 *  M5Stack Basic the routing was programmed (src 4 -> line 8, src 6 and 7
	 *  -> line 5), the CPU lines were enabled (INTENABLE=0x000601e2) and the
	 *  handlers were installed, yet DPORT_PRO_INTR_STATUS never showed a
	 *  single BT source pending and esp_bt_controller_enable() waited forever.
	 */
	esp_shim_bt_clock_init();


	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: controller init failed (%d)", (intptr_t) err);
		return(false);
	}
	err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: controller enable failed (%d)",
				 (intptr_t) err);
		return(false);
	}
	syslog_0(LOG_NOTICE, "bt: controller enabled");
	err = esp_bluedroid_init();
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: bluedroid init failed (%d)", (intptr_t) err);
		return(false);
	}
	syslog_0(LOG_NOTICE, "bt: bluedroid initialised");
	err = esp_bluedroid_enable();
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: bluedroid enable failed (%d)",
				 (intptr_t) err);
		return(false);
	}
	syslog_0(LOG_NOTICE, "bt: bluedroid enabled");
	err = esp_bt_gap_register_callback(&bt_gap_callback);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: gap callback failed (%d)", (intptr_t) err);
		return(false);
	}
	err = esp_spp_register_callback(&bt_spp_callback);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: spp callback failed (%d)", (intptr_t) err);
		return(false);
	}
	err = esp_bt_gap_set_device_name(device_name);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: device name failed (%d)", (intptr_t) err);
		return(false);
	}
	/*
	 *  The pairing policy. Fixed PIN 1234 for legacy pairing; the SSP path is
	 *  accepted in the GAP callback. Neither authenticates the peer.
	 */
	{
		esp_bt_pin_code_t	pin = { '1', '2', '3', '4' };

		(void) esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4U, pin);
	}

	/*  esp_spp_start_srv() runs from ESP_SPP_INIT_EVT, not from here. */
	err = esp_spp_enhanced_init(&spp_cfg);
	if (err != ESP_OK) {
		syslog_1(LOG_ERROR, "bt: spp init failed (%d)", (intptr_t) err);
		return(false);
	}

	spp_stack_up = true;
	syslog_1(LOG_NOTICE, "bt: SPP server up as %s", (intptr_t) device_name);
	return(true);
}

void
toppers_bt_spp_end(void)
{
	if (!spp_stack_up) {
		return;
	}
	(void) esp_spp_deinit();
	(void) esp_bluedroid_disable();
	(void) esp_bluedroid_deinit();
	(void) esp_bt_controller_disable();
	(void) esp_bt_controller_deinit();

	spp_handle = 0U;
	spp_congested = false;
	spp_server_started = false;
	spp_stack_up = false;
	ring_reset();
}

bool
toppers_bt_spp_connected(void)
{
	return(spp_handle != 0U);
}

size_t
toppers_bt_spp_available(void)
{
	uint32_t	state = esp_shim_int_disable();
	uint32_t	head = spp_ring_head;
	uint32_t	tail = spp_ring_tail;

	esp_shim_int_restore(state);
	return((size_t)((head + BT_SPP_RING_SIZE - tail) % BT_SPP_RING_SIZE));
}

int
toppers_bt_spp_read(void)
{
	uint8_t	byte;

	if (ring_take(&byte, 1U) == 0U) {
		return(-1);
	}
	return((int) byte);
}

size_t
toppers_bt_spp_read_bytes(uint8_t *buf, size_t len)
{
	if (buf == NULL || len == 0U) {
		return(0U);
	}
	return(ring_take(buf, len));
}

size_t
toppers_bt_spp_write(const uint8_t *buf, size_t len)
{
	uint32_t	handle = spp_handle;

	if (buf == NULL || len == 0U || handle == 0U || spp_congested) {
		return(0U);
	}
	if (esp_spp_write(handle, (int) len, (uint8_t *) buf) != ESP_OK) {
		return(0U);
	}
	return(len);
}

void
toppers_bt_spp_log_line(const char *message)
{
	if (message != NULL) {
		syslog(LOG_NOTICE, "%s", message);
		(void) logtask_flush(0U);
	}
}

uint32_t
toppers_bt_spp_dropped(void)
{
	return(spp_ring_dropped);
}
