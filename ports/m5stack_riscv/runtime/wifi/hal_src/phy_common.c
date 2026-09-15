#include "esp_heap_caps.h"
#include <sys/lock.h>
/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_private/esp_gpio_reserve.h"
#include "soc/gpio_sig_map.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_phy_init.h"
#include "esp_private/phy.h"
#include "esp_phy.h"
#include "esp_attr.h"
#include "platform/os.h"

#if SOC_PM_SUPPORT_PMU_MODEM_STATE && CONFIG_ESP_WIFI_ENHANCED_LIGHT_SLEEP
#include "hal/temperature_sensor_ll.h"
#endif

static const char* TAG = "phy_comm";

static volatile uint16_t s_phy_modem_flag = 0;

#if !CONFIG_ESP_PHY_DISABLE_PLL_TRACK
extern void phy_param_track_tot(bool en_wifi, bool en_ble_154);
static esp_timer_handle_t phy_track_pll_timer;
#if CONFIG_ESP_WIFI_ENABLED
static volatile int64_t s_wifi_prev_timestamp;
#endif
#if CONFIG_IEEE802154_ENABLED || CONFIG_BT_ENABLED
static volatile int64_t s_bt_154_prev_timestamp;
#endif
#define PHY_TRACK_PLL_PERIOD_IN_US (CONFIG_ESP_PHY_PLL_TRACK_PERIOD_MS * 1000)
static void phy_track_pll_internal(void);
#endif

static esp_phy_ant_gpio_config_t s_phy_ant_gpio_config = { 0 };
static esp_phy_ant_config_t s_phy_ant_config = { 0 };

#if CONFIG_IEEE802154_ENABLED || CONFIG_BT_ENABLED || CONFIG_ESP_WIFI_ENABLED
bool phy_enabled_modem_contains(esp_phy_modem_t modem)
{
    return (s_phy_modem_flag & modem) != 0;
}
#endif

#if !CONFIG_ESP_PHY_DISABLE_PLL_TRACK
void phy_track_pll(void)
{
    // Light sleep scenario: enabling and disabling PHY frequently, the timer will not get triggered.
    // Using a variable to record the previously tracked time when PLL was last called.
    // If the duration is larger than PHY_TRACK_PLL_PERIOD_IN_US, then track PLL.
    bool need_track_pll = false;
#if CONFIG_ESP_WIFI_ENABLED
    if (phy_enabled_modem_contains(PHY_MODEM_WIFI)) {
        need_track_pll = need_track_pll || ((esp_timer_get_time() - s_wifi_prev_timestamp) > PHY_TRACK_PLL_PERIOD_IN_US);
    }
#endif
#if CONFIG_IEEE802154_ENABLED || CONFIG_BT_ENABLED
    if (phy_enabled_modem_contains(PHY_MODEM_BT | PHY_MODEM_IEEE802154)) {
        need_track_pll = need_track_pll || ((esp_timer_get_time() - s_bt_154_prev_timestamp) > PHY_TRACK_PLL_PERIOD_IN_US);
    }
#endif
    if (need_track_pll) {
        phy_track_pll_internal();
    }
}

static void phy_track_pll_internal(void)
{
    bool wifi_track_pll = false;
    bool ble_154_track_pll = false;
#if CONFIG_ESP_WIFI_ENABLED
    if (phy_enabled_modem_contains(PHY_MODEM_WIFI)) {
        wifi_track_pll = true;
        s_wifi_prev_timestamp = esp_timer_get_time();
    }
#endif

#if CONFIG_IEEE802154_ENABLED || CONFIG_BT_ENABLED
    if (phy_enabled_modem_contains(PHY_MODEM_BT | PHY_MODEM_IEEE802154)) {
        ble_154_track_pll = true;
        s_bt_154_prev_timestamp = esp_timer_get_time();
    }
#endif
    if (wifi_track_pll || ble_154_track_pll) {
#if CONFIG_ESP_PHY_PLL_TRACK_DEBUG
#if CONFIG_IEEE802154_ENABLED || CONFIG_BT_ENABLED
        ESP_LOGI("PLL_TRACK", "BT or IEEE802154 tracks PLL: %s", ble_154_track_pll ? "True" : "False");
#endif
#if CONFIG_ESP_WIFI_ENABLED
        ESP_LOGI("PLL_TRACK", "Wi-Fi tracks PLL: %s", wifi_track_pll ? "True" : "False");
#endif
#endif
        phy_param_track_tot(wifi_track_pll, ble_154_track_pll);
    }
}

/*
 *  ============================================================================
 *  esp-2 検証計装（positive control）— 既定ビルドには 1 バイトも入らない
 *  ----------------------------------------------------------------------------
 *  -DESP2_PLLTRACK_PROBE が付いたときだけ有効（CMake は
 *  -DA1_ESP2_PLLTRACK_PROBE=ON。golden 5 構成の 1 つ（s3-wifi）なので
 *  -DA1_ALLOW_NONGOLDEN=ON も要る＝「golden とは別物」がコマンド行に必ず現れる）。
 *  検証手順・判定表は .steering/20260727-fable-fixes-batch1/part1-6-esp2-ac.md。
 *
 *  なぜ「行番号中立」にしてあるか（ここを崩すと既定ビルドのバイトが動く）
 *    本ファイルの ESP_ERROR_CHECK() は __FILE__/__LINE__ を
 *    _esp_error_check_failed() へ渡す。実測（objdump -dr phy_common.o）:
 *        f9: movi a12, 121   ← phy_track_pll_init の 1 本目
 *       11b: movi a12, 122   ← 同 2 本目
 *       141: movi a12, 127   ← phy_track_pll_deinit
 *    ＝行番号が**即値としてコードに焼かれている**。⇒ 本ブロックを素朴に挿すと
 *    ESP2_PLLTRACK_PROBE が**未定義でも** app_xip.bin が変わってしまう
 *    （スキップされる #if ブロックも物理行は消費するため）。
 *    そこで:
 *      (1) 本ブロックの直後に #line で元の行番号（107）へ戻す
 *          — #line はフラグの有無に関わらず必ず通る位置に置くこと。
 *      (2) 呼出しは**既存行への追記**だけで行い、行を 1 行も増やさない。
 *    これで「フラグ OFF ⇒ app_xip.bin が既定 golden から 1 バイトも動かない」
 *    ことを sha256 で実証できる（AC §1）。
 *  ============================================================================
 */
#ifdef ESP2_PLLTRACK_PROBE
#include <kernel.h>
#include <t_syslog.h>

/*
 *  出力の間引き: 先頭 5 回は毎回／以降 30 回に 1 回／通算 600 回で打切り。
 *  n（通算発火回数）と t_us（発火時刻）を毎行出すので、間引いても
 *    「行間の Δt_us ÷ Δn」で**発火間隔が復元できる**（1 Hz なら 1,000,000）。
 *  t_us は esp_timer_get_time()（= esp_shim_time_us()）を uint32 へ切った値。
 *    レビュー esp-1 の未修正バグと同じく 2^32 μs ＝ 71.6 分でラップするので、
 *    観測は短時間（10 分以内）に限ること。dt_us は unsigned 減算なので
 *    ラップを 1 回跨いでも値は正しい。
 *  1 Hz × 上限 600 回 ＝ 実質 10 分。以降は無音になる（ログ溢れ防止）。
 */
#define ESP2_PLLTRACK_HEAD_N    5U
#define ESP2_PLLTRACK_EVERY     30U
#define ESP2_PLLTRACK_MAX_LOG   600U

static uint32_t esp2_plltrack_n;
static uint32_t esp2_plltrack_prev_us;

static void esp2_plltrack_probe_fire(void)
{
    uint32_t now = (uint32_t) esp_timer_get_time();
    uint32_t n;

    esp2_plltrack_n++;
    n = esp2_plltrack_n;
    if ((n <= ESP2_PLLTRACK_HEAD_N || (n % ESP2_PLLTRACK_EVERY) == 0U)
                                        && n <= ESP2_PLLTRACK_MAX_LOG) {
        syslog(LOG_NOTICE, "[PLLTRACK] fired n=%u t_us=%u dt_us=%u",
               (uint32_t) n, now, (uint32_t)(now - esp2_plltrack_prev_us));
    }
    esp2_plltrack_prev_us = now;
}

static void esp2_plltrack_probe_init(void)
{
    esp2_plltrack_prev_us = (uint32_t) esp_timer_get_time();
    syslog(LOG_NOTICE, "[PLLTRACK] init done period_us=%u",
           (uint32_t) PHY_TRACK_PLL_PERIOD_IN_US);
}
#define ESP2_PLLTRACK_FIRE()    esp2_plltrack_probe_fire()
#define ESP2_PLLTRACK_INIT()    esp2_plltrack_probe_init()
#else  /* !ESP2_PLLTRACK_PROBE（＝既定・本番構成） */
#define ESP2_PLLTRACK_FIRE()    ((void) 0)
#define ESP2_PLLTRACK_INIT()    ((void) 0)
#endif /* ESP2_PLLTRACK_PROBE */
#line 107
static void phy_track_pll_timer_callback(void* arg)
{ ESP2_PLLTRACK_FIRE();  /* esp-2 計装。既定は空マクロ。行を増やさないこと */
    _lock_t phy_lock = phy_get_lock();
    _lock_acquire(&phy_lock);
    phy_track_pll_internal();
    _lock_release(&phy_lock);
}

void phy_track_pll_init(void)
{
    const esp_timer_create_args_t phy_track_pll_timer_args = {
            .callback = &phy_track_pll_timer_callback,
            .name = "phy-track-pll-timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&phy_track_pll_timer_args, &phy_track_pll_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(phy_track_pll_timer, PHY_TRACK_PLL_PERIOD_IN_US)); ESP2_PLLTRACK_INIT();  /* 計装は同一行（行番号中立） */
}

void phy_track_pll_deinit(void)
{
    ESP_ERROR_CHECK(esp_timer_stop(phy_track_pll_timer));
    ESP_ERROR_CHECK(esp_timer_delete(phy_track_pll_timer));
}
#endif

static const char *phy_get_modem_str(esp_phy_modem_t modem)
{
    switch (modem) {
        case PHY_MODEM_WIFI: return "Wi-Fi";
        case PHY_MODEM_BT: return "Bluetooth";
        case PHY_MODEM_IEEE802154: return "IEEE 802.15.4";
        default: return "";
    }
}

void phy_set_modem_flag(esp_phy_modem_t modem)
{
    s_phy_modem_flag |= modem;
}

void phy_clr_modem_flag(esp_phy_modem_t modem)
{
    if ((s_phy_modem_flag & modem) == 0) {
        ESP_LOGW(TAG, "Clear the flag of %s before it's set", phy_get_modem_str(modem));
    }
    s_phy_modem_flag &= ~modem;
}

esp_phy_modem_t phy_get_modem_flag(void)
{
    return s_phy_modem_flag;
}

static DRAM_ATTR bool s_phy_ant_need_update_flag = false;

IRAM_ATTR bool phy_ant_need_update(void)
{
    return s_phy_ant_need_update_flag;
}

void phy_ant_clr_update_flag(void)
{
    s_phy_ant_need_update_flag = false;
}

static void phy_ant_set_gpio_output(uint32_t io_num)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << io_num);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

esp_err_t esp_phy_set_ant_gpio(esp_phy_ant_gpio_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 4; i++) {
        if (config->gpio_cfg[i].gpio_select == 1) {
            if(esp_gpio_is_reserved(config->gpio_cfg[i].gpio_num)) {
                ESP_LOGE(TAG, "gpio[%d] number: %d is reserved\n", i, config->gpio_cfg[i].gpio_num);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        if (config->gpio_cfg[i].gpio_select == 1) {
            phy_ant_set_gpio_output(config->gpio_cfg[i].gpio_num);
            esp_rom_gpio_connect_out_signal(config->gpio_cfg[i].gpio_num, ANT_SEL0_IDX + i, 0, 0);
        }
    }

    memcpy(&s_phy_ant_gpio_config, config, sizeof(esp_phy_ant_gpio_config_t));

    return ESP_OK;
}

esp_err_t esp_phy_get_ant_gpio(esp_phy_ant_gpio_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &s_phy_ant_gpio_config, sizeof(esp_phy_ant_gpio_config_t));

    return ESP_OK;
}

static bool phy_ant_config_check(esp_phy_ant_config_t *config)
{
    if ((config->rx_ant_mode >= ESP_PHY_ANT_MODE_MAX)
        ||(config->tx_ant_mode >= ESP_PHY_ANT_MODE_MAX)
        ||(config->rx_ant_default >= ESP_PHY_ANT_MAX)) {
        ESP_LOGE(TAG, "Invalid antenna: rx=%d, tx=%d, default=%d",
            config->rx_ant_mode, config->tx_ant_mode, config->rx_ant_default);
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->tx_ant_mode == ESP_PHY_ANT_MODE_AUTO) && (config->rx_ant_mode != ESP_PHY_ANT_MODE_AUTO)) {
        ESP_LOGE(TAG, "If tx ant is AUTO, also need to set rx ant to AUTO");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void phy_ant_update(void)
{
    uint8_t rx_ant0 = 0, rx_ant1 = 0, tx_ant0 = 0;
    esp_phy_ant_config_t *config = &s_phy_ant_config;
    uint8_t ant0 = config->enabled_ant0;
    uint8_t ant1 = config->enabled_ant1;
    bool rx_auto = false;
    uint8_t def_ant = 0;
    switch (config->rx_ant_mode) {
        case ESP_PHY_ANT_MODE_ANT0:
            rx_ant0 = ant0;
            rx_ant1 = ant0;
            break;
        case ESP_PHY_ANT_MODE_ANT1:
            rx_ant0 = ant1;
            rx_ant1 = ant1;
            break;
        case ESP_PHY_ANT_MODE_AUTO:
            rx_ant0 = ant0;
            rx_ant1 = ant1;
            rx_auto = true;
            break;
        default:
            rx_ant0 = ant0;
            rx_ant1 = ant0;
    }

    switch (config->tx_ant_mode) {
        case ESP_PHY_ANT_MODE_ANT0:
            tx_ant0 = ant0;
            break;
        case ESP_PHY_ANT_MODE_ANT1:
            tx_ant0 = ant1;
            break;
        default:
            tx_ant0 = ant0;
    }

    switch (config->rx_ant_default) {
        case ESP_PHY_ANT_ANT0:
            def_ant = 0;
            break;
        case ESP_PHY_ANT_ANT1:
            def_ant = 1;
            break;
        default:
            def_ant = 0;
    }

    ant_dft_cfg(def_ant);
    ant_tx_cfg(tx_ant0);
    ant_rx_cfg(rx_auto, rx_ant0, rx_ant1);
}

esp_err_t esp_phy_set_ant(esp_phy_ant_config_t *config)
{
    if (!config || (phy_ant_config_check(config) != ESP_OK)) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_phy_ant_config, config, sizeof(esp_phy_ant_config_t));
    if ( phy_get_modem_flag() == 0 ) {
        // Set flag and will be updated when PHY enable
        s_phy_ant_need_update_flag = true;
    } else {
        // Update immediately when PHY is enabled
        phy_ant_update();
    }

    return ESP_OK;
}

esp_err_t esp_phy_get_ant(esp_phy_ant_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid args");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config, &s_phy_ant_config, sizeof(esp_phy_ant_config_t));
    return ESP_OK;
}

#if SOC_PM_SUPPORT_PMU_MODEM_STATE
typedef enum {
    PHY_I2C_MST_CMD_TYPE_RF_OFF = 0,
    PHY_I2C_MST_CMD_TYPE_RF_ON,
    PHY_I2C_MST_CMD_TYPE_BBPLL_CFG,
    PHY_I2C_MST_CMD_TYPE_MAX
} phy_i2c_master_command_type_t;

static uint32_t phy_ana_i2c_master_burst_config(phy_i2c_master_command_attribute_t *attr, int size, phy_i2c_master_command_type_t type)
{
    #define I2C1_BURST_VAL(en, start, end) (((en) << 31) | ((end) << 22) | ((start) << 16))
    #define I2C0_BURST_VAL(en, start, end) (((en) << 15) | ((end) <<  6) | ((start) <<  0))

    uint32_t brust = 0;
    for (int i = 0; i < size; i++) {
        if (attr[i].config.start == 0xff || attr[i].config.end == 0xff) /* ignore invalid configure */
            continue;

        if (attr[i].cmd_type == type) {
            if (attr[i].config.host_id) {
                brust |= I2C1_BURST_VAL(1, attr[i].config.start, attr[i].config.end);
            } else {
                brust |= I2C0_BURST_VAL(1, attr[i].config.start, attr[i].config.end);
            }
        }
    }
    return brust;
}

uint32_t phy_ana_i2c_master_burst_bbpll_config(void)
{
    /* PHY supports 2 I2C masters, and the maximum number of configurations
     * supported by the I2C master command memory is the command type
     * (PHY_I2C_MST_CMD_TYPE_MAX) multiplied by 2 */
    phy_i2c_master_command_attribute_t cmd[2 * PHY_I2C_MST_CMD_TYPE_MAX];
    int size = sizeof(cmd) / sizeof(cmd[0]);
    phy_i2c_master_command_mem_cfg(cmd, &size);

    return phy_ana_i2c_master_burst_config(cmd, size, PHY_I2C_MST_CMD_TYPE_BBPLL_CFG);
}

uint32_t phy_ana_i2c_master_burst_rf_onoff(bool on)
{
    /* PHY supports 2 I2C masters, and the maximum number of configurations
     * supported by the I2C master command memory is the command type
     * (PHY_I2C_MST_CMD_TYPE_MAX) multiplied by 2 */
    phy_i2c_master_command_attribute_t cmd[2 * PHY_I2C_MST_CMD_TYPE_MAX];
    int size = sizeof(cmd) / sizeof(cmd[0]);
    phy_i2c_master_command_mem_cfg(cmd, &size);

    return phy_ana_i2c_master_burst_config(cmd, size, on ? PHY_I2C_MST_CMD_TYPE_RF_ON : PHY_I2C_MST_CMD_TYPE_RF_OFF);
}

#if CONFIG_ESP_WIFI_ENHANCED_LIGHT_SLEEP
void phy_wakeup_from_modem_state_extra_init(void)
{
    temperature_sensor_ll_enable(true);
}
#endif
#endif
