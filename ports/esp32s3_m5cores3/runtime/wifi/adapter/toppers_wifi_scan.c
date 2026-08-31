/*
 *  Arduino-facing credential-free Wi-Fi scan adapter for ESP32-S3/FMP3.
 *
 *  The ESP-IDF Wi-Fi binary uses the FMP3 OS adapter imported beside this
 *  file.  No NVS credentials or access-point connection are used here.
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_shim.h"
#include "esp_wifi.h"
#include "toppers_wifi_core.h"

#define TOPPERS_WIFI_MAX_RECORDS 20
#define TOPPERS_WIFI_SCAN_WAIT_US 50000U
#define TOPPERS_WIFI_SCAN_TIMEOUT_LOOPS 300U

static volatile bool scan_done;
static uint16_t record_count;
static wifi_ap_record_t records[TOPPERS_WIFI_MAX_RECORDS];

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_SCAN_DONE) {
        scan_done = true;
    }
}

/*
 * Bring-up is shared with the connect adapter. This used to be a
 * private copy that called esp_wifi_init()/esp_wifi_start() itself, which is
 * exactly why the two adapters could not be linked into one profile.
 *
 * A scan needs no credentials, so it asks for the open-minimal backend. That
 * is what the scan-only profile has always installed, and scanning works with
 * it. The consequence is an ordering constraint the caller can hit: if a scan
 * brings Wi-Fi up first, a later WiFi.begin() with a password is refused,
 * because the backend cannot be swapped once installed. Conversely, connecting
 * first and scanning afterwards is fine - the scan finds Wi-Fi already up and
 * reuses whichever backend is in force.
 */
static int initialize_wifi(void)
{
    int error;

    if (toppers_wifi_core_ready()) {
        /*
         * Somebody already chose a backend. Ask for that one so this call is
         * a no-op rather than an ESP_ERR_INVALID_STATE refusal.
         */
        error = toppers_wifi_core_init(toppers_wifi_core_protected_auth(),
                                       "[WiFiScan]", wifi_event_handler);
    }
    else {
        /*
         * Nothing has chosen a backend yet, and the choice cannot be changed
         * afterwards, so a scan has to guess which association the sketch will
         * ask for next. Guess the protected one when this profile can do it.
         *
         * Rationale: "scan, show a list, join the network you picked" almost
         * always means a protected network, and joining an open AP rarely
         * needs a scan first. The scan-only profile links no supplicant, so
         * there it falls back to the open-minimal table, which is what that
         * profile has always installed.
         *
         * A sketch that really wants scan-then-open can call
         * WiFi.begin(ssid, "") before scanning; that fixes the backend to the
         * open one and the scan will then reuse it.
         */
        error = toppers_wifi_core_init(toppers_wifi_core_has_supplicant(),
                                       "[WiFiScan]", wifi_event_handler);
    }
    if (error != 0) {
        return error;
    }
    return toppers_wifi_core_start("[WiFiScan]");
}

int16_t toppers_fmp3_wifi_scan_networks(void)
{
    esp_err_t error;
    uint16_t found = 0;
    uint16_t wanted;
    uint32_t wait_loop;
    uint16_t index;

    if (initialize_wifi() != 0) {
        return -2;
    }

    record_count = 0;
    memset(records, 0, sizeof(records));
    scan_done = false;
    error = esp_wifi_scan_start(NULL, false);
    syslog(LOG_NOTICE, "[WiFiScan] esp_wifi_scan_start=%d", (int_t)error);
    if (error != ESP_OK) {
        return -2;
    }

    for (wait_loop = 0; wait_loop < TOPPERS_WIFI_SCAN_TIMEOUT_LOOPS;
         ++wait_loop) {
        if (scan_done) {
            break;
        }
        (void)dly_tsk(TOPPERS_WIFI_SCAN_WAIT_US);
    }
    if (!scan_done) {
        (void)esp_wifi_scan_stop();
        syslog(LOG_NOTICE, "[WiFiScan] timeout");
        return -3;
    }

    error = esp_wifi_scan_get_ap_num(&found);
    if (error != ESP_OK) {
        return -2;
    }
    wanted = (found > TOPPERS_WIFI_MAX_RECORDS)
                 ? TOPPERS_WIFI_MAX_RECORDS
                 : found;
    if (wanted != 0U) {
        error = esp_wifi_scan_get_ap_records(&wanted, records);
        if (error != ESP_OK) {
            return -2;
        }
    }
    record_count = wanted;

    syslog(LOG_NOTICE, "[WiFiScan] found %d APs", (int_t)record_count);
    for (index = 0; index < record_count; ++index) {
        syslog(LOG_NOTICE, "[WiFiScan] AP[%d] rssi=%d ch=%d SSID=%s",
               (int_t)index, (int_t)records[index].rssi,
               (int_t)records[index].primary,
               (const char *)records[index].ssid);
    }
    syslog(LOG_NOTICE, "[WiFiScan] done");
    return (int16_t)record_count;
}

const char *toppers_fmp3_wifi_ssid(uint8_t index)
{
    return (index < record_count)
               ? (const char *)records[index].ssid
               : "";
}

int32_t toppers_fmp3_wifi_rssi(uint8_t index)
{
    return (index < record_count) ? records[index].rssi : 0;
}

int32_t toppers_fmp3_wifi_channel(uint8_t index)
{
    return (index < record_count) ? records[index].primary : 0;
}

uint8_t toppers_fmp3_wifi_auth_mode(uint8_t index)
{
    return (index < record_count) ? (uint8_t)records[index].authmode : 0;
}

void toppers_fmp3_wifi_scan_delete(void)
{
    record_count = 0;
    memset(records, 0, sizeof(records));
    (void)esp_wifi_clear_ap_list();
}
