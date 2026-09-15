/*
 *  Arduino-facing credential-free Wi-Fi scan adapter for ESP32-C6/FMP3.
 *
 *  ESP32-C6 (ports/m5stack_riscv) version of
 *  ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_scan.c. The scan
 *  itself is the same sequence the development repository's C6 demo ran on
 *  the M5NanoC6 (esp/app/wifi_sta.c, wifi_sta_c6_scan_run:
 *  esp_wifi_scan_start(NULL, false) -> wait for WIFI_EVENT_SCAN_DONE ->
 *  esp_wifi_scan_get_ap_num/records). What differs from the Xtensa file is
 *  only the bring-up comment: on the C6 there is no auth backend to guess
 *  (D6, toppers_wifi_core.h), so a scan-first boot does not constrain a
 *  later WiFi.begin().
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

#if !defined(TOPPERS_ESP32C6)
#error "toppers_wifi_scan.c (ports/m5stack_riscv) is the ESP32-C6 version"
#endif

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
 * Bring-up is shared with the connect adapter (toppers_wifi_core.c).
 *
 * The `protected_auth` argument is informational on the C6: the supplicant
 * is initialized by esp_wifi_init() whatever is passed (D6). A scan asks
 * for what this profile can do (a real supplicant), which also keeps the
 * value the connect adapter reads back consistent with the Xtensa port.
 */
static int initialize_wifi(void)
{
    int error;

    if (toppers_wifi_core_ready()) {
        error = toppers_wifi_core_init(toppers_wifi_core_protected_auth(),
                                       "[WiFiScan]", wifi_event_handler);
    }
    else {
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
