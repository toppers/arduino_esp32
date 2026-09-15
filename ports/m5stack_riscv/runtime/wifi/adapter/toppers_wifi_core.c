/*
 * Shared Wi-Fi bring-up for the scan and connect adapters (ESP32-C6 /
 * ESP32-C5). See toppers_wifi_core.h for how this differs from the Xtensa
 * version (D6: no WPA callback table, no --wrap=esp_supplicant_init) and
 * for what the C5 shares with the C6 (everything in this file: the dev
 * C5 station path, esp/app/wifi_sta.c with wifi_sta_c5.inc at 1d96bcba, is
 * the C6 sequence; the chip-specific work - APM/TEE unblock, modem clock,
 * CLIC interrupt lines - is inside the vendored shim, C5 plan stage 3).
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "syssvc/logtask.h"

#include "esp_event.h"
#include "esp_shim.h"
#include "esp_wifi.h"
#include "toppers_wifi_core.h"

/*
 * ESP32-C6 / ESP32-C5 only. The Xtensa adapter lives in ports/m5stack_xtensa;
 * this file must never be compiled for a chip whose blob needs the private
 * callback table that this version deliberately does not carry.
 */
#if !defined(TOPPERS_ESP32C6) && !defined(TOPPERS_ESP32C5)
#error "toppers_wifi_core.c (ports/m5stack_riscv) is the ESP32-C6 / ESP32-C5 version"
#endif

/*
 * NOTE (D6): the version-pinned WPA callback table of the Xtensa adapter is
 * intentionally absent here. Do not add it back for the C6 without a
 * measured offset for the C6 blob; do not copy it from the Xtensa file
 * (BUILDING.md: one copy of that ABI description, in the Xtensa
 * toppers_wifi_core.c).
 */

/* wifi/idf_src/periph_ctrl.c (esp-idf v5.5.4 original). */
extern void wifi_module_enable(void);

static bool initialized;
static bool started;
static bool protected_auth_requested;

/*
 * Handlers already registered, so that a second adapter joining later does not
 * register the same one twice. Two adapters listen for different events, and
 * both need to be registered, but esp_event does not promise to deduplicate.
 */
#define WIFI_CORE_MAX_HANDLERS 4
static esp_event_handler_t registered_handlers[WIFI_CORE_MAX_HANDLERS];
static uint8_t registered_handler_count;

static void register_handler_once(esp_event_handler_t handler)
{
    uint8_t index;

    if (handler == NULL) {
        return;
    }
    for (index = 0; index < registered_handler_count; ++index) {
        if (registered_handlers[index] == handler) {
            return;
        }
    }
    (void)esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     handler, NULL);
    if (registered_handler_count < WIFI_CORE_MAX_HANDLERS) {
        registered_handlers[registered_handler_count++] = handler;
    }
}

static void stage_log(const char *tag, const char *message)
{
    syslog(LOG_NOTICE, "%s %s", tag, message);
    (void)logtask_flush(0U);
}

bool toppers_wifi_core_started(void) { return started; }
bool toppers_wifi_core_ready(void) { return initialized; }
bool toppers_wifi_core_protected_auth(void) { return protected_auth_requested; }
bool toppers_wifi_core_has_supplicant(void) { return true; }

int toppers_wifi_core_init(bool protected_auth, const char *tag,
                           esp_event_handler_t handler)
{
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t error;

    if (initialized) {
        /*
         * No backend to reconcile on the C6 (see the header); a late
         * joiner only adds its handler. The shim is already up, so the
         * handler can go in right here.
         */
        register_handler_once(handler);
        return 0;
    }

    stage_log(tag, "init: shim begin");
    /*
     * Development repository C6 order (esp/app/wifi_sta.c): the shim
     * (heap, pools, and - with TOPPERS_ESP_WIFI_WPA2 - psa_crypto_init()
     * for the supplicant's PTK/MIC derivation), then the event handler,
     * then the coex adapter, then the Wi-Fi module clock/reset. No ROM libc
     * table to set up first: the C6 has no chip_rom_libc.c equivalent and
     * the dev demo reached scan/DHCP/ping without one (the ROM rand()
     * winner is recorded in docs/c6-port.md, stage 3).
     */
    esp_shim_initialize();
    register_handler_once(handler);
    esp_shim_coex_adapter_register();
    wifi_module_enable();
    /*
     * Not called on the C6 (S3/LX6-only functions, absent from this port):
     * sar_periph_ctrl_init(), esp_bbpll_enable_480m(),
     * esp_wifi_clock_init_pll(). The CPU runs at the PLL already (the
     * bootloader set it, seam_c6_clk.c raised it to 160 MHz), and the modem
     * clock is enabled by the shim's _wifi_clock_enable callback when the
     * driver asks for it.
     */

    stage_log(tag, "init: esp_wifi_init begin");
    config.nvs_enable = 0;
    /*
     * Static TX buffers, as in the dev demo: the default per-packet malloc
     * from the shim heap fragmented under sustained traffic on the Xtensa
     * chips (dev esp/app/wifi_sta.c comment); 16 * ~1.6 KB allocated once.
     */
    config.tx_buf_type = 0;
    config.static_tx_buf_num = 16;
    /*
     * D6: config.wpa_crypto_funcs is left as WIFI_INIT_CONFIG_DEFAULT()
     * sets it for BOTH open and protected requests. The Xtensa adapter
     * zeroes it for an open AP because there it also skips the supplicant;
     * here the supplicant is initialized by esp_wifi_init() regardless, and
     * a crypto table that does not match the initialized supplicant would
     * be a new, unmeasured state. Open AP on the C6 is unverified (stage 4);
     * the NOTICE saying so is issued by toppers_fmp3_wifi_begin() on every
     * open request, not here (this runs once per boot, and possibly from
     * the scan adapter).
     */
    error = esp_wifi_init(&config);
    stage_log(tag, error == ESP_OK ? "init: esp_wifi_init OK"
                                   : "init: esp_wifi_init FAILED");
    syslog(LOG_NOTICE, "%s esp_wifi_init=%d", tag, (int_t)error);
    if (error != ESP_OK) {
        return (int)error;
    }
    stage_log(tag, "auth backend: supplicant (initialized by esp_wifi_init)");
    protected_auth_requested = protected_auth;

    (void)esp_wifi_set_mode(WIFI_MODE_STA);
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    initialized = true;
    return 0;
}

int toppers_wifi_core_start(const char *tag)
{
    esp_err_t error;

    if (started) {
        return 0;
    }
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    stage_log(tag, "init: esp_wifi_start begin");
    error = esp_wifi_start();
    syslog(LOG_NOTICE, "%s esp_wifi_start=%d", tag, (int_t)error);
    if (error != ESP_OK) {
        stage_log(tag, "init: esp_wifi_start FAILED");
        return (int)error;
    }
    stage_log(tag, "init: esp_wifi_start OK");
    started = true;
    return 0;
}

int toppers_wifi_core_stop(const char *tag)
{
    esp_err_t error;

    if (!started) {
        return 0;
    }
    stage_log(tag, "init: esp_wifi_stop begin");
    error = esp_wifi_stop();
    syslog(LOG_NOTICE, "%s esp_wifi_stop=%d", tag, (int_t)error);
    if (error != ESP_OK) {
        return (int)error;
    }
    started = false;
    return 0;
}
