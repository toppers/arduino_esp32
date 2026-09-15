/*
 * Shared Wi-Fi bring-up for the scan and connect adapters (ESP32-C6 /
 * ESP32-C5).
 *
 * ESP32-C6 (ports/m5stack_riscv) version of
 * ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_core.h. Both
 * adapters used to own a private `initialized` flag and call
 * esp_wifi_init()/esp_wifi_start() themselves; this module owns the
 * bring-up once, so linking scan and connect together gives one driver
 * initialization.
 *
 * What differs from the Xtensa version (decision D6 / S3-3, docs/c6-port.md):
 *
 *   The Xtensa adapter chooses an "auth backend" after the password is
 *   known: it wraps esp_supplicant_init() to a no-op, installs a minimal
 *   open-association callback table into the driver's private g_ic for an
 *   open AP, and calls the real supplicant only for a protected one. That
 *   table is an offset-and-index description of a private ABI pinned to the
 *   ESP32-S3/ESP32 3.3.8 blobs. The C6 blob is a different binary with an
 *   unknown layout, so this port does NOT inject any table and does NOT wrap
 *   esp_supplicant_init(): the supplicant is initialized inside
 *   esp_wifi_init(), exactly as the development repository's C6 station
 *   demo (esp/app/wifi_sta.c) does, which reached STA/DHCP/ping on the
 *   M5NanoC6 that way. Consequently there is no backend to pick and no
 *   ESP_ERR_INVALID_STATE for "the other backend"; the `protected_auth`
 *   flag is still accepted so the two adapters read the same on both
 *   chips, but on the C6 it only says what the caller intends.
 *
 *   Open AP on the C6 is UNVERIFIED until stage 4 measures it. On the
 *   Xtensa blobs an always-initialized supplicant made an open association
 *   fail with AUTH_EXPIRE; whether the C6 blob behaves the same is not
 *   known. This port does not claim open-AP support; an open request is
 *   passed to the driver with a NOTICE saying so, and the driver's own
 *   disconnect reason is the answer.
 *
 * Ordering (the development repository's C6 path, esp/app/wifi_sta.c):
 *
 *   esp_shim_initialize() -> event handler -> esp_shim_coex_adapter_register()
 *   -> wifi_module_enable() -> esp_wifi_init() -> set_mode/storage/ps
 *   -> [set_config] -> esp_wifi_start()
 *
 * sar_periph_ctrl_init(), esp_bbpll_enable_480m() and
 * esp_wifi_clock_init_pll() of the Xtensa sequence are S3/LX6-specific
 * (register addresses of those chips) and do not exist for the C6; the
 * modem clock and the APM access-path filters are handled inside the shim's
 * _wifi_clock_enable callback (esp_wifi_adapter.c, c6_apm_unblock; S3-6).
 *
 * ESP32-C5 (C5 plan stage 3, A11): the same ordering and the same D6
 * position. The dev C5 station path (esp/app/wifi_sta.c + wifi_sta_c5.inc
 * at 1d96bcba) calls the same functions in the same order; the C5's APM
 * unblock (four controllers' FUNC_CTRL + 32 TEE masters, dev
 * c5_apm_unblock in esp_wifi_adapter_c5.inc) and its CLIC interrupt lines
 * are inside the shim as well, so the adapter has no C5 register code.
 * Open AP is UNVERIFIED on the C5 as on the C6.
 */

#ifndef TOPPERS_WIFI_CORE_H
#define TOPPERS_WIFI_CORE_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

/*
 * Bring the driver up to the point where esp_wifi_start() is legal.
 * Idempotent: the second and later calls return ESP_OK without touching the
 * driver (a late joiner's handler is still registered).
 *
 * `protected_auth` is what the caller intends (true: WPA2-PSK/WPA3-SAE,
 * false: open). On the C6 it does not select anything - see the file
 * comment - and a later call with the other value is accepted, not refused.
 *
 * `tag` prefixes the stage markers ("[WiFiConnect] init: ..."), so each
 * adapter keeps the log lines its documentation promises.
 *
 * `handler` is registered for WIFI_EVENT/ESP_EVENT_ANY_ID between
 * esp_shim_initialize() and esp_shim_coex_adapter_register(), the position
 * the development repository's C6 demo uses. Pass NULL if the caller has no
 * events to receive.
 */
int toppers_wifi_core_init(bool protected_auth, const char *tag,
                           esp_event_handler_t handler);

/* esp_wifi_start(). Idempotent. */
int toppers_wifi_core_start(const char *tag);

/*
 * esp_wifi_stop(), so that esp_wifi_set_config() can be issued again in the
 * order the driver wants (config before start). A scan starts the driver to
 * do its work, so a WiFi.begin() after a scan cycles it here. Idempotent
 * when already stopped.
 */
int toppers_wifi_core_stop(const char *tag);

/* True once toppers_wifi_core_start() has succeeded. */
bool toppers_wifi_core_started(void);

/* True once toppers_wifi_core_init() has succeeded. */
bool toppers_wifi_core_ready(void);

/*
 * What the first successful init asked for. Informational on the C6 (no
 * backend is selected by it); kept so the scan adapter reads the same as
 * on the Xtensa chips.
 */
bool toppers_wifi_core_protected_auth(void);

/*
 * True when this profile links a real supplicant. The C6 wifi-connect
 * profile always does (libsupplicant.a is in the stage's lib/), and
 * esp_wifi_init() initializes it, so this is a constant here.
 */
bool toppers_wifi_core_has_supplicant(void);

#endif /* TOPPERS_WIFI_CORE_H */
