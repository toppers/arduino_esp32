/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include "platform/os.h"
#include "esp_attr.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/critical_section.h"
#include "soc/soc_caps.h"
#ifdef __PERIPH_CTRL_ALLOW_LEGACY_API
#include "hal/clk_gate_ll.h"
#endif
#include "esp_log.h"

#if SOC_MODEM_CLOCK_IS_INDEPENDENT && SOC_MODEM_CLOCK_SUPPORTED
#include "esp_private/esp_modem_clock.h"
#endif
#include "soc/sens_reg.h"

/// @brief For simplicity and backward compatible, we are using the same spin lock for both bus clock on/off and reset
/// @note  We may want to split them into two spin locks in the future
DEFINE_CRIT_SECTION_LOCK_STATIC(periph_spinlock, __attribute__((unused)));

static uint8_t ref_counts[PERIPH_MODULE_MAX] = {0};

void periph_rcc_enter(void)
{
    esp_os_enter_critical_safe(&periph_spinlock);
}

void periph_rcc_exit(void)
{
    esp_os_exit_critical_safe(&periph_spinlock);
}

uint8_t periph_rcc_acquire_enter(periph_module_t periph)
{
    periph_rcc_enter();
    return ref_counts[periph];
}

void periph_rcc_acquire_exit(periph_module_t periph, uint8_t ref_count)
{
    ref_counts[periph] = ++ref_count;
    periph_rcc_exit();
}

uint8_t periph_rcc_release_enter(periph_module_t periph)
{
    periph_rcc_enter();
    return ref_counts[periph] - 1;
}

void periph_rcc_release_exit(periph_module_t periph, uint8_t ref_count)
{
    ref_counts[periph] = ref_count;
    periph_rcc_exit();
}

void periph_module_enable(periph_module_t periph)
{
#ifdef __PERIPH_CTRL_ALLOW_LEGACY_API
    assert(periph < PERIPH_MODULE_MAX);
    esp_os_enter_critical_safe(&periph_spinlock);
    if (ref_counts[periph] == 0) {
        periph_ll_enable_clk_clear_rst(periph);
    }
    ref_counts[periph]++;
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

void periph_module_disable(periph_module_t periph)
{
#ifdef __PERIPH_CTRL_ALLOW_LEGACY_API
    assert(periph < PERIPH_MODULE_MAX);
    esp_os_enter_critical_safe(&periph_spinlock);
    ref_counts[periph]--;
    if (ref_counts[periph] == 0) {
        periph_ll_disable_clk_set_rst(periph);
    }
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

void periph_module_reset(periph_module_t periph)
{
#ifdef __PERIPH_CTRL_ALLOW_LEGACY_API
    assert(periph < PERIPH_MODULE_MAX);
    esp_os_enter_critical_safe(&periph_spinlock);
    periph_ll_reset(periph);
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

#if !SOC_IEEE802154_BLE_ONLY
#if SOC_BT_SUPPORTED || SOC_WIFI_SUPPORTED || SOC_IEEE802154_SUPPORTED
IRAM_ATTR void wifi_bt_common_module_enable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_enable(PERIPH_PHY_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
    if (ref_counts[PERIPH_WIFI_BT_COMMON_MODULE] == 0) {
        periph_ll_wifi_bt_module_enable_clk();
    }
    ref_counts[PERIPH_WIFI_BT_COMMON_MODULE]++;
#if CONFIG_IDF_TARGET_ESP32C2
    if (ref_counts[PERIPH_COEX_MODULE] == 0) {
        periph_ll_coex_module_enable_clk_clear_rst();
    }
    ref_counts[PERIPH_COEX_MODULE]++;
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

IRAM_ATTR void wifi_bt_common_module_disable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_disable(PERIPH_PHY_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
    ref_counts[PERIPH_WIFI_BT_COMMON_MODULE]--;
    if (ref_counts[PERIPH_WIFI_BT_COMMON_MODULE] == 0) {
        periph_ll_wifi_bt_module_disable_clk();
    }
#if CONFIG_IDF_TARGET_ESP32C2
    ref_counts[PERIPH_COEX_MODULE]--;
    if (ref_counts[PERIPH_COEX_MODULE] == 0) {
        periph_ll_coex_module_disable_clk_set_rst();
    }
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}
#endif  //#if SOC_BT_SUPPORTED || SOC_WIFI_SUPPORTED
#endif  //#if !SOC_IEEE802154_BLE_ONLY

#if CONFIG_ESP_WIFI_ENABLED
void wifi_module_enable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_enable(PERIPH_WIFI_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
    if (ref_counts[PERIPH_WIFI_MODULE] == 0) {
        periph_ll_wifi_module_enable_clk_clear_rst();
    }
    ref_counts[PERIPH_WIFI_MODULE]++;
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

void wifi_module_disable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_disable(PERIPH_WIFI_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
    ref_counts[PERIPH_WIFI_MODULE]--;
    if (ref_counts[PERIPH_WIFI_MODULE] == 0) {
        periph_ll_wifi_module_disable_clk_set_rst();
    }
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}
#endif // CONFIG_ESP_WIFI_ENABLED

#if SOC_BT_SUPPORTED || SOC_WIFI_SUPPORTED || SOC_IEEE802154_SUPPORTED
// PERIPH_WIFI_BT_COMMON_MODULE is enabled outside
IRAM_ATTR void phy_module_enable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_enable(PERIPH_PHY_CALIBRATION_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
#if SOC_WIFI_SUPPORTED || SOC_BT_SUPPORTED
    periph_ll_phy_calibration_module_enable_clk_clear_rst();
    if (ref_counts[PERIPH_RNG_MODULE] == 0) {
        periph_ll_enable_clk_clear_rst(PERIPH_RNG_MODULE);
    }
    ref_counts[PERIPH_RNG_MODULE]++;
#endif
#if SOC_WIFI_SUPPORTED
    if (ref_counts[PERIPH_WIFI_MODULE] == 0) {
        periph_ll_wifi_module_enable_clk_clear_rst();
    }
    ref_counts[PERIPH_WIFI_MODULE]++;
#endif
#if SOC_BT_SUPPORTED
    if (ref_counts[PERIPH_BT_MODULE] == 0) {
        periph_ll_enable_clk_clear_rst(PERIPH_BT_MODULE);
    }
    ref_counts[PERIPH_BT_MODULE]++;
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

// PERIPH_WIFI_BT_COMMON_MODULE is disabled outside
IRAM_ATTR void phy_module_disable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_disable(PERIPH_PHY_CALIBRATION_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
#if SOC_BT_SUPPORTED
    ref_counts[PERIPH_BT_MODULE]--;
    if (ref_counts[PERIPH_BT_MODULE] == 0) {
        periph_ll_disable_clk_set_rst(PERIPH_BT_MODULE);
    }
#endif
#if SOC_WIFI_SUPPORTED
    ref_counts[PERIPH_WIFI_MODULE]--;
    if (ref_counts[PERIPH_WIFI_MODULE] == 0) {
        periph_ll_wifi_module_disable_clk_set_rst();
    }
#endif
#if SOC_WIFI_SUPPORTED || SOC_BT_SUPPORTED
    // Do not disable PHY clock and RNG clock
    ref_counts[PERIPH_RNG_MODULE]--;
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

IRAM_ATTR bool phy_module_has_clock_bits(uint32_t mask)
{
    uint32_t val = 0;
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    val = modem_clock_module_bits_get(PERIPH_PHY_MODULE);
#else
#if SOC_WIFI_SUPPORTED || SOC_BT_SUPPORTED
    val = DPORT_REG_READ(periph_ll_get_clk_en_reg(PERIPH_WIFI_BT_COMMON_MODULE));
#else
    return true;
#endif
#endif
    if ((val & mask) != mask) {
        ESP_LOGW("periph_ctrl", "phy module clock bits 0x%x, required 0x%x", val, mask);
        return false;
    }
    return true;
}

IRAM_ATTR void coex_module_enable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_enable(PERIPH_COEX_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
#if CONFIG_IDF_TARGET_ESP32C2
    if (ref_counts[PERIPH_COEX_MODULE] == 0) {
        periph_ll_coex_module_enable_clk_clear_rst();
    }
    ref_counts[PERIPH_COEX_MODULE]++;
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}

IRAM_ATTR void coex_module_disable(void)
{
#if SOC_MODEM_CLOCK_IS_INDEPENDENT
    modem_clock_module_disable(PERIPH_COEX_MODULE);
#else
    esp_os_enter_critical_safe(&periph_spinlock);
#if CONFIG_IDF_TARGET_ESP32C2
    ref_counts[PERIPH_COEX_MODULE]--;
    if (ref_counts[PERIPH_COEX_MODULE] == 0) {
        periph_ll_coex_module_disable_clk_set_rst();
    }
#endif
    esp_os_exit_critical_safe(&periph_spinlock);
#endif
}
#endif  //#if SOC_BT_SUPPORTED || SOC_WIFI_SUPPORTED || SOC_IEEE802154_SUPPORTED

/*
 * SAR系ペリフェラル（ADC/PWDET/温度センサ、PHY RXゲイン較正のアナログ測定回路
 * が読み出す共有レジスタ 0x6001c08c もここに含まれる）の電源初期化。
 * 実ESP-IDFではesp_system起動シーケンスの一部としてWiFi/BT利用に関係なく
 * 無条件に呼ばれる(esp_hw_support/port/esp32s3/sar_periph_ctrl.c の
 * sar_periph_ctrl_init())が、本ポートには起動シーケンス自体が無いため
 * ここに用意し、WiFi初期化前に呼び出す。
 *
 * 未呼出しだと SENS_SAR_PERI_CLK_GATE_CONF_REG の SARADC_CLK_EN が0のまま
 * （デフォルト無効）となり、PHY較正(bb_init内 ram_iq_est_enable等)が読む
 * アナログ測定レジスタが恒久的に0を返し、較正ループが無限に完了しない
 * （実機JTAG比較調査で確認）。
 */
#if defined(TOPPERS_ESP32_LX6)
/*
 * 無印ESP32(LX6)のSAR電源制御。実ESP-IDFの hal/esp32/sar_ctrl_ll.h
 * sar_ctrl_ll_set_power_mode() ＝ SENS.sar_meas_wait2.force_xpd_sar への書込み：
 *   FSM=0b00 / POWER_ON=0b11 / POWER_OFF=0b10。
 * force_xpd_sar は SENS_SAR_MEAS_WAIT2_REG[19:18]（S3の SENS_SAR_POWER_XPD_SAR_REG
 * とは別レジスタ）。無印には SARADCクロックゲート(SENS_SAR_PERI_CLK_GATE_CONF_REG)も
 * pwdet用regi2c(ANA_CONFIG)も無く、init/pwdet とも force_xpd_sar だけで足りる
 * （esp_hw_support/port/esp32/sar_periph_ctrl.c＋esp_phy/src/phy_override.c 準拠）。
 */
#define E32_SAR_MEAS_WAIT2_REG  0x3ff4880Cu   /* SENS_SAR_MEAS_WAIT2_REG(=DR_REG_SENS_BASE+0x0c) */
#define E32_FORCE_XPD_SAR_M     (0x3u << 18)  /* SENS_FORCE_XPD_SAR_M (bits[19:18]) */
#define E32_FORCE_XPD_SAR_ON    (0x3u << 18)  /* POWER_ON */
static inline void e32_sar_set_power_on(void)
{
    uint32_t v = *(volatile uint32_t *)E32_SAR_MEAS_WAIT2_REG;
    v = (v & ~E32_FORCE_XPD_SAR_M) | E32_FORCE_XPD_SAR_ON;
    *(volatile uint32_t *)E32_SAR_MEAS_WAIT2_REG = v;
}
static inline void e32_sar_set_power_fsm(void)
{
    *(volatile uint32_t *)E32_SAR_MEAS_WAIT2_REG &= ~E32_FORCE_XPD_SAR_M;
}
#endif /* TOPPERS_ESP32_LX6 */

void sar_periph_ctrl_init(void)
{
#if defined(TOPPERS_ESP32_LX6)
    /* esp32 sar_periph_ctrl_init(): set_power_mode(POWER_ON)。 */
    e32_sar_set_power_on();
#else
    /* SAR_CTRL_LL_POWER_FSM相当: SARADCクロック有効化＋force_xpd_sar解除。
     * リンカ提供の SENS 構造体シンボル（実ESP-IDFのリンカスクリプト依存）を
     * 本ポートは持たないため、hal/sar_ctrl_ll.hのstruct経由アクセスではなく
     * レジスタアドレスへの直接読み書きで同じビットを操作する。 */
    *(volatile uint32_t *)SENS_SAR_PERI_CLK_GATE_CONF_REG |= SENS_SARADC_CLK_EN_M;
    *(volatile uint32_t *)SENS_SAR_POWER_XPD_SAR_REG &= ~SENS_FORCE_XPD_SAR_M;
#endif
}

/*
 * PHY blob が PHY較正（pwdet_code_cal 等）の測定中に呼ぶ弱シンボルフック
 * set_xpd_sar / phy_set_pwdet_power の【強定義】。実ESP-IDFでは
 * components/esp_phy/src/phy_override.c が提供するが、本ポートは未提供だったため
 * blob 内の weak no-op が実行され、SARアナログ測定回路（TXパワーディテクタ等）が
 * 電源ON されないまま較正が測定完了フラグ 0x60006174 bit16 を永久に待ってハングして
 * いた（esp_wifi_init は通るが esp_wifi_start の較正でハング。
 * 追記10/11）。sar_periph_ctrl_init() が起動時に force_xpd_sar=0(FSM) にした後、
 * 較正時にこのフック経由で POWER_ON(force_xpd_sar=0b11) を取得する設計に合わせる。
 *
 * 実ESP-IDF sar_periph_ctrl_pwdet_power_acquire() → s_sar_power_acquire():
 *   regi2c_saradc_enable(): ANA_CONFIG(0x6000E044) I2C_SAR(bit18)=0,
 *                           ANA_CONFIG2(0x6000E048) ANA_SAR_CFG2(bit16)=1
 *   sar_ctrl_ll_set_power_mode(POWER_ON): force_xpd_sar(0x6000883C[30:29])=0b11
 * を参照カウント付きで再現（リンカ提供 struct 非依存の直接レジスタアクセス）。
 * set_xpd_sar と phy_set_pwdet_power はそれぞれ独立の状態フラグを持ち、
 * 共有の参照カウントで実電源を制御する（phy_override.c と同挙動）。
 */
#define ANA_CONFIG_REG_ADDR   0x6000E044u   /* I2C_SAR = bit18 */
#define ANA_CONFIG2_REG_ADDR  0x6000E048u   /* ANA_SAR_CFG2 = bit16 */

static int  s_sar_power_refcount;
static bool s_adc_xpd_flag;
static bool s_pwdet_xpd_flag;

static void s_sar_power_acquire(void)
{
    if (s_sar_power_refcount++ == 0) {
#if defined(TOPPERS_ESP32_LX6)
        /* 無印: pwdet電源ON＝force_xpd_sar=0b11（regi2cは不要）。 */
        e32_sar_set_power_on();
#else
        /* regi2c_saradc_enable(): SAR への内部I2C(regi2c)経路を有効化 */
        *(volatile uint32_t *)ANA_CONFIG_REG_ADDR  &= ~(1u << 18);
        *(volatile uint32_t *)ANA_CONFIG2_REG_ADDR |=  (1u << 16);
        /* POWER_ON: force_xpd_sar = 0b11 */
        *(volatile uint32_t *)SENS_SAR_POWER_XPD_SAR_REG |= SENS_FORCE_XPD_SAR_M;
#endif
    }
}

static void s_sar_power_release(void)
{
    if (s_sar_power_refcount > 0 && --s_sar_power_refcount == 0) {
#if defined(TOPPERS_ESP32_LX6)
        e32_sar_set_power_fsm();
#else
        /* FSM へ戻す（force_xpd_sar=0） */
        *(volatile uint32_t *)SENS_SAR_POWER_XPD_SAR_REG &= ~SENS_FORCE_XPD_SAR_M;
#endif
    }
}

void set_xpd_sar(bool en)
{
    if (s_adc_xpd_flag == en) {
        return;
    }
    s_adc_xpd_flag = en;
    if (en) {
        s_sar_power_acquire();
    } else {
        s_sar_power_release();
    }
}

void phy_set_pwdet_power(bool en)
{
    if (s_pwdet_xpd_flag == en) {
        return;
    }
    s_pwdet_xpd_flag = en;
    if (en) {
        s_sar_power_acquire();
    } else {
        s_sar_power_release();
    }
}

/*
 * BBPLL(480MHz)の電源投入・設定（CPUクロックソースはXTALのまま切り替えない）。
 *
 * 実ESP-IDFでは2nd-stage bootloader/esp_clk_initがCPUをPLLへ昇圧するため、
 * app_main到達時点でBBPLLは常に稼働している（実機で"cpu freq: 160MHz"を確認）。
 * 本ポートはbootloaderを持たずCPU=XTAL 40MHz直結で動作するため、BBPLLが
 * 一切設定されておらず、WiFi基底帯のアナログ測定エンジン（bb_init内
 * ram_iq_est_enable等が読むFE測定レジスタ0x6001c08c）のサンプリングクロック
 * が供給されない疑いが強い（レジスタI/F自体は読み書き可能だが測定値が
 * 恒久的に0＝クロック欠如の症状と整合。）。
 *
 * ESP-IDF v5.5.4のrtc_clk_bbpll_enable()+rtc_clk_bbpll_configure(40MHz,480)
 * のシーケンスを、CPUクロック切替（rtc_clk_cpu_freq_to_pll_mhz）を除いて
 * 再現する。REGI2C書込みはROM常駐のrom_i2c_writeReg（esp32s3.rom.ldで解決）
 * を使用。カーネルのタイミング前提（CCOUNT=40MHz）には影響しない。
 */
#define BBPLL_I2C_BLOCK   0x66  /* I2C_BBPLL (soc/regi2c_bbpll.h) */
#define BBPLL_I2C_HOSTID  1     /* I2C_BBPLL_HOSTID */
extern void rom_i2c_writeReg(uint8_t block, uint8_t host_id,
                             uint8_t reg_add, uint8_t data);
extern void rom_i2c_writeReg_Mask(uint8_t block, uint8_t host_id,
                                  uint8_t reg_add, uint8_t msb, uint8_t lsb,
                                  uint8_t data);
extern void esp_rom_delay_us(uint32_t us);

int esp_bbpll_enable_480m(void)
{
    volatile uint32_t *options0  = (volatile uint32_t *)0x60008000; /* RTC_CNTL_OPTIONS0_REG */
    volatile uint32_t *cpu_per   = (volatile uint32_t *)0x600C0010; /* SYSTEM_CPU_PER_CONF_REG */
    volatile uint32_t *ana_conf  = (volatile uint32_t *)0x6000E044; /* ANA_CONFIG_REG */
    volatile uint32_t *mst_conf0 = (volatile uint32_t *)0x6000E040; /* I2C_MST_ANA_CONF0_REG */
    int timeout;

    /* rtc_clk_bbpll_enable(): BB_I2C/BBPLL/BBPLL_I2CのFORCE_PD解除(bit6,10,8) */
    *options0 &= ~((1u << 6) | (1u << 10) | (1u << 8));

    /* clk_ll_bbpll_set_freq_mhz(480): PLL_FREQ_SEL(bit2)=1（480MHz選択） */
    *cpu_per |= (1u << 2);

    /* regi2cのI2C_BBPLLブロックを有効化（I2C_BBPLL_M=bit17をクリア） */
    *ana_conf &= ~(1u << 17);

    /* regi2c_ctrl_ll_bbpll_calibration_start() */
    *mst_conf0 &= ~(1u << 2);   /* I2C_MST_BBPLL_STOP_FORCE_HIGH クリア */
    *mst_conf0 |=  (1u << 3);   /* I2C_MST_BBPLL_STOP_FORCE_LOW セット */

    /* clk_ll_bbpll_set_config(480MHz, XTAL=40MHz):
     * div_ref=0, div7_0=8, dr1=0, dr3=0, dchgp=5, dcur=3, dbias=3 */
    rom_i2c_writeReg(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 4, 0x6B);          /* MODE_HF */
    rom_i2c_writeReg(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 2, (5u << 4) | 0); /* OC_REF_DIV: dchgp<<4|div_ref */
    rom_i2c_writeReg(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 3, 8);             /* OC_DIV_7_0 */
    rom_i2c_writeReg_Mask(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 5, 2, 0, 0);  /* OC_DR1=0 */
    rom_i2c_writeReg_Mask(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 5, 6, 4, 0);  /* OC_DR3=0 */
    rom_i2c_writeReg(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 6,
                     (1u << 6) | (3u << 4) | 3);                           /* OC_DCUR: DLREF_SEL|DHREF_SEL|dcur */
    rom_i2c_writeReg_Mask(BBPLL_I2C_BLOCK, BBPLL_I2C_HOSTID, 9, 1, 0, 3);  /* OC_VCO_DBIAS=3 */

    /* 較正完了待ち（I2C_MST_BBPLL_CAL_DONE=bit24）。無限ループを避け
     * 有限リトライとし、タイムアウト時は-1を返す（呼出し側でsyslog）。 */
    for (timeout = 100000; timeout > 0; timeout--) {
        if (*mst_conf0 & (1u << 24)) {
            break;
        }
    }
    esp_rom_delay_us(10);

    /* regi2c_ctrl_ll_bbpll_calibration_stop() */
    *mst_conf0 &= ~(1u << 3);
    *mst_conf0 |=  (1u << 2);

    return (timeout > 0) ? 0 : -1;
}

/*
 * ★システムクロック源をPLLへ切替える（WiFi BB較正に必須）。
 *
 * ESP32-S3はmodem独立クロックを持たず、WiFiベースバンド(BB/NRX, 0x6001c000の
 * AGC/IQ測定エンジン)のファンクショナルクロックはPLL由来のシステム/APB(80MHz)から
 * 分配される。本ポートは従来CPU/APB=XTAL 40MHz直結だったため、BBにクロックが供給
 * されず PHY較正(ram_iq_est_enable)が測定完了フラグ(0x60006174 bit16)を永久に待って
 * ハングしていた（FE=0x60006000は単なるAPBアクセス設定なのでXTALでも生存＝FE生存/
 * BB死亡の非対称。実機A/B＋SOC_CLK_SEL=PLLで0x6001c000復活を確認）。
 *
 * S3には「CPUをXTALに保ちmodemだけPLL駆動」する専用muxが無いため、system clockを
 * PLL(APB=80MHz)へ切替える。呼び出し後はCCOUNTがCPUクロックで進むため、
 * カーネルの時間換算(target_timer.h TCYC_PER_HRT)もCPU周波数に揃えること。
 * software_init_hook（BSS/DATA初期化後・カーネルtick開始前）から一度だけ呼ぶ。
 *
 * ★CPUクロック周波数のビルド時選択（TOPPERS_S3_CPU_FREQ_MHZ）：
 *  - 許容値：80／160／240（MHz）。それ以外は#errorで弾く。
 *  - 未定義時のデフォルト：160MHz（2026-07-10ユーザー決定で80から変更。
 *    stock ESP-IDFの既定と同じ。80が必要な場合は明示指定する）。
 *  - 使い方：EXTRA_OFLAGS="-DTOPPERS_S3_CPU_FREQ_MHZ=160" \
 *            bash esp/boot/build_ble_xip.sh
 *    （EXTRA_OFLAGSはbuild_bt_hal_lib.shへもexportされ全翻訳単位に効く。
 *    target_timer.hのTCYC_PER_HRT・ble_hs_smoke.cの周波数監視も同じ
 *    マクロに追随する）
 *  - 実装：SYSTEM_CPU_PER_CONFのPLL_FREQ_SEL(bit2)=1（480MHz PLL）固定、
 *    CPUPERIOD_SEL[1:0]=0/1/2 → 80/160/240MHz（vendored ESP-IDF
 *    hal/components/esp_hal_clock/esp32s3/include/hal/clk_tree_ll.h
 *    clk_ll_cpu_set_freq_mhz_from_pll()で裏取り）。APBは全選択で80MHz固定
 *    （rtc_clk.c rtc_clk_cpu_freq_to_pll_mhz()がrtc_clk_apb_freq_update
 *    (80*MHZ)を無条件に行う設計と同じ。UARTボーレート分周はAPB基準の
 *    ため影響無し）。
 *  - 240MHzのみ追加要件：切替え前にデジタル/RTCレギュレータ電圧(dbias)を
 *    pvt+3相当へ引き上げ、LDOスレーブを6個全開にする必要がある（vendored
 *    rtc_clk.c rtc_clk_cpu_freq_to_pll_mhz()の240MHz分岐を再現。本ポートは
 *    rtc_init()を実行せずeFuse較正値(rtc_set_stored_dbias)を持たないため、
 *    較正前デフォルト値 g_dig_dbias_pvt_240m=28／g_rtc_dbias_pvt_240m=28
 *    を使う）。
 *  - 検証状況（S3-A実機、2026-07-10）：80MHz=従来ベースライン、
 *    160MHz=BT-4調査(旧TOPPERS_S3_CPU160_EXP)でブート・タイマ精度・BLE接続
 *    を確認済み。240MHzは本一般化時に検証（結果はdocs/status.md等参照）。
 */
#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#define TOPPERS_S3_CPU_FREQ_MHZ  160
#endif
#if TOPPERS_S3_CPU_FREQ_MHZ != 80 && TOPPERS_S3_CPU_FREQ_MHZ != 160 \
    && TOPPERS_S3_CPU_FREQ_MHZ != 240
#error "TOPPERS_S3_CPU_FREQ_MHZ must be 80, 160, or 240"
#endif

#define DIG_REG_I2C_BLOCK   0x6D  /* I2C_DIG_REG (soc/regi2c_dig_reg.h) */
#define DIG_REG_I2C_HOSTID  1     /* I2C_DIG_REG_HOSTID */

extern void ets_update_cpu_frequency(uint32_t mhz);

/* リセット生存のステージマーカー（RTC_SLOW_MEM先頭）。クラッシュ後にJTAGで読む。 */
#define WIFI_DBG_MARK(n)  (*(volatile uint32_t *)0x50000000 = (0xC0DE0000u | (n)))

void esp_wifi_clock_init_pll(void)
{
    WIFI_DBG_MARK(0x01);
    /* PLLソース(BBPLL 480M)を有効化（CPU切替の前提。冪等） */
    (void)esp_bbpll_enable_480m();
    WIFI_DBG_MARK(0x02);

    /* stock rtc_init相当: アナログregi2c POR保護を解除 */
    *(volatile uint32_t *)0x60008034 &= ~(1u << 18); /* RTC_CNTL_I2C_RESET_POR_FORCE_PD */

    /*
     * ★割込み保護区間（BT-4緊急調査で追加）：CPUクロック源の実切替え
     * （SYSTEM_CPU_PER_CONF／SYSTEM_SYSCLK_CONFの2レジスタ書換え）は、
     * 本関数が「起動時に一度だけ・割込み未稼働のうちに」呼ばれる前提で
     * 元々無防備（他の全periph_ctrl.c関数はesp_os_enter_critical_safe/
     * exit_critical_safeでperiph_spinlockを取るのに、本関数だけ皆無）
     * だった。ble_hs_smoke.c（コミット5f680e4）でBT接続タイムアウト対策
     * として1秒周期で本関数を再アサートするようになり、BTコントローラ
     * のLevel-3割込み（線23/27）が稼働中の状態で実行されるようになった
     * ため、レジスタ書換えの最中にBT割込みが割り込む可能性が生じた。
     * クロック源切替えの瞬間にCPUが一時的にストール／グリッチし得る
     * ハードウェア特性上、割込み受理タイミングと重なると未定義動作の
     * リスクがある。切替え自体は数命令で完結するため、割込みマスク窓
     * は最小限（この2レジスタ書換えのみ）に留める（esp_bbpll_enable_480m
     * のI2C較正待ちループ等、時間のかかる処理はBT割込みの応答性を保つため
     * 意図的に対象外のまま）。
     */
#if TOPPERS_S3_CPU_FREQ_MHZ == 240
    /* ★240MHz時のみ：CPUクロック切替え前にレギュレータ電圧を引き上げる
     * （vendored rtc_clk.c rtc_clk_cpu_freq_to_pll_mhz()の240MHz分岐の再現。
     * dbias=pvt+3相当。値28はrtc_init.cの較正前デフォルト
     * g_rtc_dbias_pvt_240m/g_dig_dbias_pvt_240m。電圧安定待ち40usも同じ）。
     * 本関数は1秒周期で再アサートされ得るが（ble_hs_smoke.c）、regi2c書込みは
     * 冪等で、追加コストは~数十us/回・非クリティカル区間のため毎回行う。 */
    rom_i2c_writeReg_Mask(DIG_REG_I2C_BLOCK, DIG_REG_I2C_HOSTID,
                          4, 4, 0, 28);  /* I2C_DIG_REG_EXT_RTC_DREG[4:0]=28 */
    rom_i2c_writeReg_Mask(DIG_REG_I2C_BLOCK, DIG_REG_I2C_HOSTID,
                          6, 4, 0, 28);  /* I2C_DIG_REG_EXT_DIG_DREG[4:0]=28 */
    esp_rom_delay_us(40);
    /* RTC_CNTL_DATE_REG[18:13]=SLAVE_PD: 0でLDOスレーブ6個全開
     * （240MHzはDEFAULT_LDO_SLAVE(0x7)>>3=0。80/160MHzは従来どおり不変） */
    *(volatile uint32_t *)0x600081FCu &= ~(0x3Fu << 13);
#endif /* TOPPERS_S3_CPU_FREQ_MHZ == 240 */
    esp_os_enter_critical_safe(&periph_spinlock);
    /* SYSTEM_CPU_PER_CONF: PLL_FREQ_SEL(bit2)=1（480MHz PLL）、
     * CPUPERIOD_SEL[1:0]=0/1/2 → PLL選択時80/160/240MHz
     * （clk_tree_ll.h clk_ll_cpu_set_freq_mhz_from_pll()裏取り済み）。
     * 選択方法・許容値・デフォルトは本関数の上のコメント参照。 */
    {
        volatile uint32_t *cpu_per = (volatile uint32_t *)0x600C0010;
        *cpu_per = ((*cpu_per | (1u << 2)) & ~0x3u)
                   | (uint32_t)(TOPPERS_S3_CPU_FREQ_MHZ / 80 - 1);
    }
    /* ★SYSTEM_SYSCLK_CONF: PRE_DIV_CNT[9:0]=0 かつ SOC_CLK_SEL[11:10]=1(PLL)。
     * PRE_DIV_CNTを0にしないとPLL分周が狂い不安定クロックでクラッシュする
     * （stock値0x000a8400に一致させる）。この瞬間
     * CPU=TOPPERS_S3_CPU_FREQ_MHZ、APB=80MHz。 */
    {
        volatile uint32_t *sysclk = (volatile uint32_t *)0x600C0060;
        *sysclk = (*sysclk & ~0x3FFu & ~(0x3u << 10)) | (1u << 10);
    }
    esp_os_exit_critical_safe(&periph_spinlock);
    WIFI_DBG_MARK(0x03);
    /* ROM遅延(esp_rom_delay_us)・esp_clk_cpu_freq()等のCPU周波数基準を更新 */
    ets_update_cpu_frequency(TOPPERS_S3_CPU_FREQ_MHZ);

    /*
     * WIFI_CLK_EN(0x60026014)に「意味のある全WiFiクロック」SYSTEM_WIFI_CLK_EN
     * (0x00FB9FCF)をORする。本ポートは従来 default|WIFI_BT_COMMON(0x78078F)=
     * 0xfffce7bf しか設定せず、bit6/11/12/16/17（MAC等のクロック）が欠けていた。
     * このためMAC HAL初期化(hal_init)のMACレディ待ち(0x60033d14 bit0)が永久ハング
     * していた（stock機は0x60033d14=0x1でready）。hal_init
     * より前(ここ)で有効化する必要がある（有効化は順序依存）。 */
    *(volatile uint32_t *)0x60026014 |= 0x00FB9FCFu;

    WIFI_DBG_MARK(0x04);
}
