/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: GPIO ドライバ面（`esp_driver_gpio` の必要面だけ）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜ `gpio.c` を引かないのか（実測）
 *  ============================================================================
 *  `esp-idf/components/esp_driver_gpio/src/gpio.c` の未定義参照は
 *  `rtc_gpio_*`（10 本）・`gpio_hal_*`・`esp_intr_*`・`heap_caps_calloc`・
 *  `vPortEnterCritical`・`fprintf` 等で、`rtc_io.c` → `rtc_module.c` →
 *  ADC/touch/sar/LDO/sleep へ連鎖する（AC §1-4 の閉包計測）。
 *  Ethernet が使うのは **8 本だけ**なので、`hal/gpio_ll.h`（全部 static inline）
 *  の上に**その 8 本だけ**を書く。
 *
 *  ============================================================================
 *  実装の出典（推測で書いていない）
 *  ============================================================================
 *  各関数の本体は `esp_driver_gpio/src/gpio.c` の同名関数を読んで、
 *  **ESP32-P4 の平の（RTC/LP でない）GPIO に効く部分だけ**を写したものである。
 *  行番号は IDF v5.5.4（本 repo の submodule）のもの。
 *
 *  P4 固有の単純化（一次資料つき）:
 *   - `SOC_GPIO_SUPPORT_RTC_INDEPENDENT = 1`（`soc/esp32p4/include/soc/soc_caps.h`）
 *     ⇒ プルアップ/ダウンは GPIO0-15 でも **IO_MUX 側だけ**で効く。
 *      `gpio.c` の RTC ドメイン分岐は P4 では死んでいる。
 *   - `GPIO_LL_GET_HW(0) == &GPIO`（P4 の GPIO は 1 インスタンス）。
 *   - `PIN_FUNC_GPIO == 1`（全 pin 一律）。
 *
 *  **触る pin は Ethernet の RMII/SMI/PHY-reset だけ**である。Unit PoE-P4 の
 *  実配線は `esp/eth/app/fmp_eth_sta/` と `unit_poe_p4_kit.h` を参照。
 *  それらはすべて GPIO16 以上（＝`SOC_RTCIO_PIN_COUNT=16` の外）なので、
 *  RTC 側の始末は要らない——**この前提が破れたら数える**（`p4shim_n_gpio_rtcio`）。
 */

#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4shim.h"

#include "soc/soc_caps.h"
#include "soc/gpio_num.h"
#include "hal/gpio_ll.h"
/*
 *  **IDF の公開ヘッダを必ず include する**（プロトタイプの照合を
 *  コンパイラにさせるため）。これを怠って実機で 1 度踏んだ:
 *  `gpio_func_sel()` を戻り値 void で書いたが、実際の宣言は
 *      esp_err_t gpio_func_sel(gpio_num_t gpio_num, uint32_t func);
 *  で、呼び手 `esp_eth_mac_esp_gpio.c:67` は
 *  `ESP_RETURN_ON_ERROR(gpio_func_sel(...))` と戻り値を見る。
 *  ⇒ a0 に残っていたゴミが非 0 と読まれ、実機で
 *     `failed to set GPIO function at GPIO #31` → `SMI GPIO init failed`
 *     → `esp_eth_mac_new_esp32` 失敗、となった（run1 の実測）。
 *  **リンクは通り、ビルドも通り、実機でだけ壊れる**型である。
 *  ヘッダを include しておけば conflicting types でコンパイル時に止まる。
 */
#include "driver/gpio.h"
#include "esp_private/gpio.h"
#include "esp_private/esp_gpio_reserve.h"

#define P4SHIM_GPIO_HW			(&GPIO)

/*  P4: GPIO0..GPIO15 は RTC/LP 側も持つ（`SOC_RTCIO_PIN_COUNT`）。  */
#ifndef SOC_RTCIO_PIN_COUNT
#define SOC_RTCIO_PIN_COUNT		16
#endif

static bool
p4shim_gpio_ok(int gpio_num)
{
	if (gpio_num < 0 || gpio_num >= SOC_GPIO_PIN_COUNT) {
		p4shim_n_gpio_badarg++;
		return false;
	}
	if (gpio_num < SOC_RTCIO_PIN_COUNT) {
		/*
		 *  RTC/LP も持つ pin。**拒否はしない**——P4 は
		 *  `SOC_GPIO_SUPPORT_RTC_INDEPENDENT=1` なので平の GPIO として
		 *  扱って正しい。ただし「前提の外の pin を触った」ことは数える
		 *  （`gpio_reset_pin` の RTC deinit を省いている根拠が
		 *   この前提だからである）。
		 */
		p4shim_n_gpio_rtcio++;
	}
	return true;
}

/*
 *  `gpio.c:1151`。単一レジスタ書込み（IO_MUX の mcu_sel）なので排他は要らない。
 *  IDF 本体は gpio_num が USB の pad（24..27）のとき USB PHY の pad enable を
 *  落とすが、Ethernet の pin はそこに当たらない（当たったら数える対象＝
 *  上の `p4shim_gpio_ok` ではなく、当たらないことを配線表で保証している）。
 */
esp_err_t
gpio_func_sel(gpio_num_t gpio_num, uint32_t func)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	gpio_ll_func_sel(P4SHIM_GPIO_HW, (uint8_t) gpio_num, func);
	return ESP_OK;
}

/*  `gpio.c:206`。単一ビット（IO_MUX fun_ie）。  */
esp_err_t
gpio_input_enable(gpio_num_t gpio_num)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	gpio_ll_input_enable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	return ESP_OK;
}

/*
 *  `gpio.c:222`。**3 段の合成で、順序に意味がある**。
 *  IDF 本体のコメント（"otherwise oe can only be controlled by peripheral"）の
 *  とおり、`matrix_out_default`（出力経路を `SIG_GPIO_OUT_IDX` へ戻す）を
 *  先にやらないと `output_enable` が効かない。**1 段でも落とすと
 *  「書けたのに出力が出ない」**という、実機でしか見えない形で壊れる。
 */
esp_err_t
gpio_output_enable(gpio_num_t gpio_num)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	(void) loc_cpu();
	gpio_ll_matrix_out_default(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_output_enable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_func_sel(P4SHIM_GPIO_HW, (uint8_t) gpio_num, PIN_FUNC_GPIO);
	(void) unl_cpu();
	return ESP_OK;
}

/*  `gpio.c:245`。out_w1ts/out_w1tc なのでハード側で atomic。  */
esp_err_t
gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	gpio_ll_set_level(P4SHIM_GPIO_HW, (uint32_t) gpio_num, level);
	return ESP_OK;
}

/*
 *  `gpio.c:279`。`gpio_pull_mode_t` の値は `hal/gpio_types.h`:
 *    GPIO_PULLUP_ONLY=0 / GPIO_PULLDOWN_ONLY=1 / GPIO_PULLUP_PULLDOWN=2 /
 *    GPIO_FLOATING=3
 *  Ethernet が使うのは `GPIO_FLOATING` だけ（`esp_eth_mac_esp_gpio.c:66,95`）
 *  だが、4 通りとも実装する（片方だけ在ると「効いたつもりで効かない」を作る）。
 */
esp_err_t
gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	(void) loc_cpu();
	switch ((int) pull) {
	case 0:		/* GPIO_PULLUP_ONLY */
		gpio_ll_pullup_en(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		gpio_ll_pulldown_dis(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		break;
	case 1:		/* GPIO_PULLDOWN_ONLY */
		gpio_ll_pullup_dis(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		gpio_ll_pulldown_en(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		break;
	case 2:		/* GPIO_PULLUP_PULLDOWN */
		gpio_ll_pullup_en(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		gpio_ll_pulldown_en(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		break;
	case 3:		/* GPIO_FLOATING */
		gpio_ll_pullup_dis(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		gpio_ll_pulldown_dis(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
		break;
	default:
		(void) unl_cpu();
		p4shim_n_gpio_badarg++;
		return ESP_ERR_INVALID_ARG;
	}
	(void) unl_cpu();
	return ESP_OK;
}

/*
 *  `gpio.c:842` → `gpio_hal_iomux_in`（`hal/gpio_hal.c:34`）。
 *  「この信号は IOMUX から取る（GPIO マトリクス経由ではない）」と宣言してから
 *  入力を開き、pin の機能を IOMUX の func へ切り替える。
 */
esp_err_t
gpio_iomux_input(gpio_num_t gpio_num, int func, uint32_t signal_idx)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	(void) loc_cpu();
	gpio_ll_set_input_signal_from(P4SHIM_GPIO_HW, signal_idx, false);
	gpio_ll_input_enable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_func_sel(P4SHIM_GPIO_HW, (uint8_t) gpio_num, (uint32_t) func);
	(void) unl_cpu();
	return ESP_OK;
}

/*
 *  `gpio.c:855` → `gpio_hal_iomux_out`。**`func_sel` だけ**である。
 *  IDF 本体のコメント（"as long as func_sel is not GPIO, oe is controlled by
 *  peripheral"）のとおり、出力有効はペリフェラルが持つので**触らない**。
 *  ここで親切に `output_enable` を足すと、RMII の出力を CPU 側が奪う。
 */
esp_err_t
gpio_iomux_output(gpio_num_t gpio_num, int func)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	gpio_ll_func_sel(P4SHIM_GPIO_HW, (uint8_t) gpio_num, (uint32_t) func);
	return ESP_OK;
}

/*
 *  `gpio.c:465`。Ethernet では PHY リセット pin の下ごしらえ
 *  （`esp_eth_phy_802_3.c:443` の直前経路）と `emac_esp_gpio_deinit_all` で使う。
 *  IDF 本体は RTC 側の deinit も行うが、本シムは**平の GPIO 分だけ**を行う
 *  （P4 は `SOC_GPIO_SUPPORT_RTC_INDEPENDENT=1`。RTC 側を持つ pin を
 *   渡されたことは `p4shim_gpio_ok` が数えている）。
 *  最後の `esp_gpio_revoke` は IDF 本体と同じく行う。
 */
extern uint64_t esp_gpio_revoke(uint64_t gpio_mask);

esp_err_t
gpio_reset_pin(gpio_num_t gpio_num)
{
	if (!p4shim_gpio_ok(gpio_num)) {
		return ESP_ERR_INVALID_ARG;
	}
	(void) loc_cpu();
	gpio_ll_intr_disable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_input_disable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_output_disable(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	/*  浮かせない（IDF 本体と同じ。出力可能な pin は弱プルアップを入れる）  */
	gpio_ll_pullup_en(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_pulldown_dis(P4SHIM_GPIO_HW, (uint32_t) gpio_num);
	gpio_ll_func_sel(P4SHIM_GPIO_HW, (uint8_t) gpio_num, PIN_FUNC_GPIO);
	(void) unl_cpu();
	(void) esp_gpio_revoke(1ULL << gpio_num);
	return ESP_OK;
}

/*
 *  ============================================================================
 *  GPIO の予約表（`esp_hw_support/esp_gpio_reserve.c` の等価実装）
 *  ============================================================================
 *  本体は 64bit の `_Atomic` 1 個に対する `atomic_fetch_or` / `atomic_fetch_and`
 *  / `atomic_load` の 3 本である。RV32 に 64bit のロックフリー atomic は無く、
 *  そのままだと `__atomic_fetch_or_8` 等（libatomic）を引く。
 *  ⇒ **`loc_cpu`/`unl_cpu` で置き換える**（本シムの利用者は 1 コア上の
 *    タスク文脈だけである。C-1 と同じ制約で、そう明記しておく）。
 *
 *  **戻り値は「呼ぶ前の予約マスク」である**（bool ではない）。
 *  `esp_eth_mac_esp_gpio.c:47,57,87` は `(esp_gpio_reserve(m) & m) == 0` で
 *  衝突を見るので、ここを取り違えると**衝突を常に見逃す**。
 *
 *  初期値は IDF 本体と同じ「有効な GPIO 以外は最初から予約済み」
 *  （`~SOC_GPIO_VALID_GPIO_MASK`）にする。
 */
static uint64_t	p4shim_gpio_reserved = ~((uint64_t) SOC_GPIO_VALID_GPIO_MASK);

uint64_t
esp_gpio_reserve(uint64_t gpio_mask)
{
	uint64_t prev;

	(void) loc_cpu();
	prev = p4shim_gpio_reserved;
	p4shim_gpio_reserved |= gpio_mask;
	(void) unl_cpu();
	return prev;
}

uint64_t
esp_gpio_revoke(uint64_t gpio_mask)
{
	uint64_t prev;

	(void) loc_cpu();
	prev = p4shim_gpio_reserved;
	p4shim_gpio_reserved &= ~gpio_mask;
	(void) unl_cpu();
	return prev;
}

bool
esp_gpio_is_reserved(uint64_t gpio_mask)
{
	uint64_t cur;

	(void) loc_cpu();
	cur = p4shim_gpio_reserved;
	(void) unl_cpu();
	return (cur & gpio_mask) != 0ULL;
}
