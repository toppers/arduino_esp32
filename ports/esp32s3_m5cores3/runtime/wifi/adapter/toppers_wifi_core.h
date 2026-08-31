/*
 * Shared Wi-Fi bring-up for the scan and connect adapters.
 *
 * Both adapters used to own a private `initialized` flag and call
 * esp_wifi_init()/esp_wifi_start() themselves, so linking them together gave
 * two independent initializations of one driver. They also carried two
 * byte-identical copies of the version-pinned WPA callback table. This module
 * owns all of that once.
 *
 * Ordering rule that the ESP32-S3 Arduino 3.3.8 Wi-Fi binary imposes:
 *
 *   esp_wifi_init() -> auth backend chosen -> esp_wifi_start()
 *
 * The auth backend cannot be swapped afterwards, because installing the full
 * supplicant over an already-installed table goes through a registration path
 * that frees the previous one - and ours is a static object. So the choice is
 * made once per boot, by whoever brings Wi-Fi up first, and a later caller
 * that needs the other backend is refused with ESP_ERR_INVALID_STATE rather
 * than being allowed to corrupt the heap.
 */

#ifndef TOPPERS_WIFI_CORE_H
#define TOPPERS_WIFI_CORE_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

/*
 * Bring the driver up to the point where esp_wifi_start() is legal, and pick
 * the auth backend. Idempotent: the second and later calls return ESP_OK
 * without touching anything, EXCEPT that a call asking for a different
 * `protected_auth` than the one already established returns
 * ESP_ERR_INVALID_STATE.
 *
 * protected_auth == true installs the real supplicant (needed for WPA2-PSK and
 * WPA3-SAE); false installs the minimal open table. Always initializing the
 * full supplicant makes an open AP fail with AUTH_EXPIRE, which is the whole
 * reason esp_supplicant_init() is wrapped - see toppers_wifi_connect.c.
 *
 * `tag` prefixes the stage markers, so each adapter keeps the log lines its
 * documentation promises (packaging/README.release.md lists the
 * "[WiFiConnect] init: ..." sequence for troubleshooting).
 *
 * `handler` is registered for WIFI_EVENT/ESP_EVENT_ANY_ID from inside this
 * function, between esp_shim_initialize() and esp_shim_coex_adapter_register().
 * That position is not cosmetic: registering it before esp_shim_initialize()
 * instead produced a WPA 4-way handshake timeout on association. Pass NULL if
 * the caller has no events to receive.
 */
int toppers_wifi_core_init(bool protected_auth, const char *tag,
                           esp_event_handler_t handler);

/* esp_wifi_start(). Idempotent. */
int toppers_wifi_core_start(const char *tag);

/*
 * esp_wifi_stop(), so that esp_wifi_set_config() can be issued again in the
 * order the driver wants. Idempotent when already stopped.
 *
 * Why this exists: this port deliberately sets the station config BEFORE
 * esp_wifi_start(), the same order the reference port uses. A scan has to
 * start the driver to do its work, so a WiFi.begin() after a scan would
 * otherwise configure an already-started driver - and that association fails
 * with AUTH_EXPIRE. Measured, not guessed.
 *
 * The auth backend is NOT re-selected: it stays as installed. Only the driver
 * is cycled.
 */
int toppers_wifi_core_stop(const char *tag);

/* True once toppers_wifi_core_start() has succeeded. */
bool toppers_wifi_core_started(void);

/*
 * Which backend is in force, for callers that must not guess. Returns false
 * when nothing has been initialized yet, so check toppers_wifi_core_ready()
 * first if the difference matters.
 */
bool toppers_wifi_core_ready(void);
bool toppers_wifi_core_protected_auth(void);

/*
 * True when this profile links a real supplicant, i.e. when protected auth is
 * possible at all. The scan-only profile does not.
 */
bool toppers_wifi_core_has_supplicant(void);

#endif /* TOPPERS_WIFI_CORE_H */
