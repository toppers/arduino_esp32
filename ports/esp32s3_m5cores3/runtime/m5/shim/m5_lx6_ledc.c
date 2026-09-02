/*
 *  無印ESP32(LX6) の LEDC（PWM）— M5Stack Basic のバックライト用
 *
 *  ★これが無いと「描画は成功しているのに画面が真っ暗」になる。
 *  M5GFX は M5Stack Basic のバックライトを `_set_pwm_backlight(GPIO32, ch7,
 *  44100)` として LEDC の PWM で駆動する（Light_PWM.cpp）。本ポートの
 *  ledc_* は何もせず ESP_OK を返すスタブだったため、パネルには正しく
 *  描かれているのにバックライトが点かなかった（2026-09-02、M5Stack Basic
 *  実機。GPIO32 を手で High にすると絵が出ることで確認）。
 *  CoreS3 はバックライトが AXP2101 経由なので表に出なかった。
 *
 *  実装は m5_gpio_stub.c と同じ流儀で、ESP-IDF の driver ではなく
 *  hal/ledc_ll.h のインライン（レジスタ直）を薄く包む。割込みも fade も
 *  使わないので、必要なのは「タイマを組んでチャネルを繋いで duty を書く」
 *  だけである。
 *
 *  M5GFX が使う範囲だけを持つ：
 *    speed_mode = LEDC_HIGH_SPEED_MODE（無印ESP32にはこれがある）
 *    duty_resolution = 9 bit、freq = 44100、channel = 7、timer = 3
 *  他の値でも動くように式で書いてあるが、確認したのはこの組合せである。
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include <driver/ledc.h>
#include <hal/ledc_ll.h>
#include <soc/dport_reg.h>
#include <soc/gpio_sig_map.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include "m5_stub_trace.h"

#define M5_LEDC_HW		LEDC_LL_GET_HW()

/*  ledc_ll_enable_bus_clock / _enable_reset_reg は ESP-IDF 側で
 *  「__DECLARE_RCC_ATOMIC_ENV を宣言した臨界区間の中で呼べ」というマクロに
 *  包まれている（ledc_ll.h:61,78）。その臨界区間は DPORT レジスタを他の
 *  ドライバと共有するためのもので、本ポートには他に触る者が居ない。
 *  中身は下の 2 ビットの set/clear そのものなので、直接書く。 */
#define M5_LEDC_BUS_CLK_ON()	REG_SET_BIT(DPORT_PERIP_CLK_EN_REG, DPORT_LEDC_CLK_EN)
#define M5_LEDC_RESET_RELEASE()	REG_CLR_BIT(DPORT_PERIP_RST_EN_REG, DPORT_LEDC_RST)
#define M5_LEDC_APB_HZ	80000000U	/* APB は 80MHz（PLL 由来。target 層が設定済み） */

/*  clock_divider は 10.8 の固定小数（整数部10bit・小数部8bit）。
 *  div = APB / (freq * 2^duty_resolution) を 1/256 単位で表す。 */
static uint32_t
m5_ledc_divider(uint32_t freq_hz, uint32_t duty_resolution)
{
	uint64_t	precise;

	if ((freq_hz == 0U) || (duty_resolution == 0U) || (duty_resolution > 20U)) {
		return(0U);
	}
	precise = ((uint64_t) M5_LEDC_APB_HZ << 8) /
			  ((uint64_t) freq_hz << duty_resolution);
	if ((precise < 256U) || (precise > 0x3FFFFFU)) {
		return(0U);	/* この周波数と分解能の組は APB からは作れない */
	}
	return((uint32_t) precise);
}

esp_err_t
ledc_timer_config(const ledc_timer_config_t *timer_conf)
{
	uint32_t	divider;

	M5_STUB_HIT("ledc_timer_config");
	if (timer_conf == 0) {
		return(ESP_ERR_INVALID_ARG);
	}
	divider = m5_ledc_divider((uint32_t) timer_conf->freq_hz,
							  (uint32_t) timer_conf->duty_resolution);
	if (divider == 0U) {
		return(ESP_ERR_INVALID_ARG);
	}

	M5_LEDC_BUS_CLK_ON();
	M5_LEDC_RESET_RELEASE();

	ledc_ll_set_clock_source(M5_LEDC_HW, timer_conf->speed_mode,
							 timer_conf->timer_num, LEDC_APB_CLK);
	ledc_ll_set_duty_resolution(M5_LEDC_HW, timer_conf->speed_mode,
								timer_conf->timer_num,
								(uint32_t) timer_conf->duty_resolution);
	ledc_ll_set_clock_divider(M5_LEDC_HW, timer_conf->speed_mode,
							  timer_conf->timer_num, divider);
	ledc_ll_timer_rst(M5_LEDC_HW, timer_conf->speed_mode,
					  timer_conf->timer_num);
	ledc_ll_timer_resume(M5_LEDC_HW, timer_conf->speed_mode,
						 timer_conf->timer_num);
	return(ESP_OK);
}

esp_err_t
ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{
	M5_STUB_HIT("ledc_channel_config");
	if (ledc_conf == 0) {
		return(ESP_ERR_INVALID_ARG);
	}

	M5_LEDC_BUS_CLK_ON();
	M5_LEDC_RESET_RELEASE();

	ledc_ll_bind_channel_timer(M5_LEDC_HW, ledc_conf->speed_mode,
							   ledc_conf->channel, ledc_conf->timer_sel);
	ledc_ll_set_hpoint(M5_LEDC_HW, ledc_conf->speed_mode, ledc_conf->channel,
					   (uint32_t) ledc_conf->hpoint);
	ledc_ll_set_duty_int_part(M5_LEDC_HW, ledc_conf->speed_mode,
							  ledc_conf->channel, (uint32_t) ledc_conf->duty);
	/*  fade は使わないので「今の duty へ 1 段で移る」設定にする。 */
	ledc_ll_set_fade_param(M5_LEDC_HW, ledc_conf->speed_mode,
						   ledc_conf->channel, 1U, 1U, 0U, 0U);
	ledc_ll_set_sig_out_en(M5_LEDC_HW, ledc_conf->speed_mode,
						   ledc_conf->channel, true);
	ledc_ll_set_duty_start(M5_LEDC_HW, ledc_conf->speed_mode,
						   ledc_conf->channel);

	/*  出力をピンへ。信号番号はチャネル順に並ぶ（gpio_sig_map.h）。 */
	if (ledc_conf->gpio_num >= 0) {
		uint32_t	sig;

		sig = (ledc_conf->speed_mode == LEDC_LOW_SPEED_MODE)
				  ? (uint32_t) LEDC_LS_SIG_OUT0_IDX
				  : (uint32_t) LEDC_HS_SIG_OUT0_IDX;
		sig += (uint32_t) ledc_conf->channel;
		gpio_set_direction((gpio_num_t) ledc_conf->gpio_num, GPIO_MODE_OUTPUT);
		esp_rom_gpio_connect_out_signal((uint32_t) ledc_conf->gpio_num, sig,
										false, false);
	}
	return(ESP_OK);
}

esp_err_t
ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{
	M5_STUB_HIT("ledc_set_duty");
	ledc_ll_set_duty_int_part(M5_LEDC_HW, speed_mode, channel, duty);
	ledc_ll_set_fade_param(M5_LEDC_HW, speed_mode, channel, 1U, 1U, 0U, 0U);
	return(ESP_OK);
}

esp_err_t
ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{
	M5_STUB_HIT("ledc_update_duty");
	ledc_ll_set_sig_out_en(M5_LEDC_HW, speed_mode, channel, true);
	ledc_ll_set_duty_start(M5_LEDC_HW, speed_mode, channel);
	if (speed_mode == LEDC_LOW_SPEED_MODE) {
		ledc_ll_ls_channel_update(M5_LEDC_HW, speed_mode, channel);
	}
	return(ESP_OK);
}
