/*
 * Shared Wi-Fi bring-up for the scan and connect adapters.
 * See toppers_wifi_core.h for the ordering rule this enforces.
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
 * The version-pinned WPA callback table.
 *
 * ★Do not copy this table into another file. Two copies of an
 * offset-and-index dependent ABI description stay correct only by luck.
 *
 * The offset and the entry count are pinned to the M5Stack Arduino core 3.3.8
 * Wi-Fi binary. Both static assertions below are load-bearing: the driver
 * calls index 22 with no NULL check when wifi_set_config_process() finishes,
 * even for WIFI_AUTH_OPEN, and a NULL there kills the Wi-Fi task and leaves
 * esp_wifi_set_config() waiting on a semaphore that never comes.
 */
#define WIFI_3_3_8_WPA_CALLBACK_OFFSET 0x1b4U
#define WIFI_3_3_8_WPA_CALLBACK_COUNT  27U

extern uint8_t g_ic[];

typedef struct {
    bool (*wpa_sta_init)(void);
    bool (*wpa_sta_deinit)(void);
    int (*wpa_sta_connect)(uint8_t *bssid);
    void (*wpa_sta_connected_cb)(uint8_t *bssid);
    void (*wpa_sta_disconnected_cb)(uint8_t reason_code);
    int (*wpa_sta_rx_eapol)(uint8_t *src_addr, uint8_t *buf,
                            uint32_t length);
    bool (*wpa_sta_in_4way_handshake)(void);
    /* Indices 7..21 are unused by open association. */
    void *unused_before_config_done[15U];
    void (*wpa_config_done)(void);
    void *unused_owe_callbacks[2U];
    void (*wpa_sta_clear_curr_pmksa)(void);
    void (*wpa_config_reload)(void);
} open_wpa_callbacks_t;

_Static_assert(sizeof(open_wpa_callbacks_t) ==
               WIFI_3_3_8_WPA_CALLBACK_COUNT * sizeof(void *),
               "Arduino 3.3.8 WPA callback table ABI mismatch");
_Static_assert(offsetof(open_wpa_callbacks_t, wpa_config_done) ==
               22U * sizeof(void *),
               "wpa_config_done must remain at callback index 22");

extern esp_err_t esp_wifi_sta_connect_internal(const uint8_t *bssid);

static bool open_wpa_sta_init(void) { return true; }
static bool open_wpa_sta_deinit(void) { return true; }

static int open_wpa_sta_connect(uint8_t *bssid)
{
    /* Open authentication needs no supplicant profile or association IE. */
    return (int)esp_wifi_sta_connect_internal(bssid);
}

static void open_wpa_sta_connected(uint8_t *bssid) { (void)bssid; }
static void open_wpa_sta_disconnected(uint8_t reason) { (void)reason; }

static int open_wpa_sta_rx_eapol(uint8_t *src, uint8_t *buf, uint32_t length)
{
    (void)src; (void)buf; (void)length;
    return -1;
}

static bool open_wpa_sta_in_4way_handshake(void) { return false; }
static void open_wpa_noop(void) {}

static open_wpa_callbacks_t open_wpa_callbacks = {
    .wpa_sta_init = open_wpa_sta_init,
    .wpa_sta_deinit = open_wpa_sta_deinit,
    .wpa_sta_connect = open_wpa_sta_connect,
    .wpa_sta_connected_cb = open_wpa_sta_connected,
    .wpa_sta_disconnected_cb = open_wpa_sta_disconnected,
    .wpa_sta_rx_eapol = open_wpa_sta_rx_eapol,
    .wpa_sta_in_4way_handshake = open_wpa_sta_in_4way_handshake,
    .wpa_config_done = open_wpa_noop,
    .wpa_sta_clear_curr_pmksa = open_wpa_noop,
    .wpa_config_reload = open_wpa_noop,
};

static void install_open_wpa_callbacks(void)
{
    void **slot = (void **)(g_ic + WIFI_3_3_8_WPA_CALLBACK_OFFSET);

    *slot = (void *)&open_wpa_callbacks;
}

/*
 * The real supplicant, reached past the --wrap that keeps esp_wifi_init() from
 * initializing it. Declared weak on purpose: the scan-only profile links no
 * supplicant at all and therefore has no --wrap and no __real_ symbol. There
 * it resolves to NULL and a request for protected auth is refused instead of
 * failing to link.
 */
extern int __real_esp_supplicant_init(void) __attribute__((weak));

static bool initialized;
static bool started;
static bool auth_backend_protected;

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
bool toppers_wifi_core_protected_auth(void) { return auth_backend_protected; }

bool toppers_wifi_core_has_supplicant(void)
{
    return __real_esp_supplicant_init != NULL;
}

static int prepare_auth_backend(bool protected_auth, const char *tag)
{
    int error;

    if (protected_auth) {
        if (__real_esp_supplicant_init == NULL) {
            /*
             * Loud, not silent: a profile without the supplicant cannot do
             * WPA at all, and returning success here would look like a
             * connection that simply never authenticates.
             */
            stage_log(tag, "auth backend: this profile has no supplicant");
            return ESP_ERR_NOT_SUPPORTED;
        }
        stage_log(tag, "auth backend: full supplicant");
        error = __real_esp_supplicant_init();
        if (error != ESP_OK) {
            return error;
        }
    }
    else {
        stage_log(tag, "auth backend: open minimal");
        install_open_wpa_callbacks();
    }
    auth_backend_protected = protected_auth;
    return ESP_OK;
}

int toppers_wifi_core_init(bool protected_auth, const char *tag,
                           esp_event_handler_t handler)
{
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t error;

    if (initialized) {
        if (auth_backend_protected != protected_auth) {
            /*
             * The backend is already chosen and cannot be swapped - see the
             * header. Say which way round it is; "scan first, then join a
             * protected network" and "join a protected network, then join an
             * open one" both land here and are easy to misread as a driver
             * fault.
             */
            syslog(LOG_ERROR,
                   "%s auth backend is already %s and cannot be changed",
                   tag, auth_backend_protected ? "full supplicant"
                                               : "open minimal");
            (void)logtask_flush(0U);
            return ESP_ERR_INVALID_STATE;
        }
        /*
         * The shim is already up, so a late joiner's handler can go in right
         * here; only the very first registration has to sit in the position
         * below.
         */
        register_handler_once(handler);
        return 0;
    }

    stage_log(tag, "init: shim begin");
#if defined(TOPPERS_ESP32_LX6)
    /*  ★無印ESP32(LX6) は ROM の newlib を通る経路があり、その入口の
     *  ROM __getreent は syscall_table_ptr_pro/app が指す stub table を辿る。
     *  bootloader 経由の起動ではこのポインタが 0 のままなので、先に張らないと
     *  最初に errno や malloc へ触れた時点で LoadProhibited になる
     *  （EXCCAUSE=28, pc=ROM __getreent 0x4000be94。2026-09-02、
     *  M5Stack Basic 実機で esp_shim_initialize の中で発生）。
     *  wifi_stubs.c が表を用意しているが、呼ぶ側がどこにも無かった。
     *  S3 は arch 側の chip_rom_libc.c が software_init_hook で同じことをする。 */
    {
        extern void esp32_rom_libc_init(void);

        esp32_rom_libc_init();
    }
#endif
    esp_shim_initialize();
    register_handler_once(handler);
    esp_shim_coex_adapter_register();
    {
        extern void wifi_module_enable(void);
        extern void sar_periph_ctrl_init(void);
        extern void esp_wifi_clock_init_pll(void);

        wifi_module_enable();
        sar_periph_ctrl_init();
        esp_wifi_clock_init_pll();
    }

    stage_log(tag, "init: esp_wifi_init begin");
    config.nvs_enable = 0;
    config.tx_buf_type = 0;
    config.static_tx_buf_num = 16;
    if (!protected_auth) {
        /*
         * An open association must not carry crypto function pointers; the
         * driver otherwise takes the WPA path far enough to time out.
         */
        memset(&config.wpa_crypto_funcs, 0, sizeof(config.wpa_crypto_funcs));
        config.wpa_crypto_funcs.size = sizeof(config.wpa_crypto_funcs);
        config.wpa_crypto_funcs.version = ESP_WIFI_CRYPTO_VERSION;
    }
    error = esp_wifi_init(&config);
    stage_log(tag, error == ESP_OK ? "init: esp_wifi_init OK"
                                   : "init: esp_wifi_init FAILED");
    syslog(LOG_NOTICE, "%s esp_wifi_init=%d", tag, (int_t)error);
    if (error != ESP_OK) {
        return (int)error;
    }

    error = (esp_err_t)prepare_auth_backend(protected_auth, tag);
    if (error != ESP_OK) {
        syslog(LOG_NOTICE, "%s auth backend initialization=%d",
               tag, (int_t)error);
        return (int)error;
    }

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
