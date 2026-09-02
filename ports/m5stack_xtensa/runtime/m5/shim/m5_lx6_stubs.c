/*
 *  無印ESP32(LX6) の m5 構成でだけ要るスタブ
 *
 *  M5GFX の platforms/esp32 は CONFIG_IDF_TARGET_ESP32 のとき、S3 では通らない
 *  ESP32 classic 固有の API を呼ぶ。本ポートに実体が無いものをここで受ける。
 *  S3 ではこの TU 自体をビルドしない（CMakeLists.txt が A1_CHIP で分ける）ので、
 *  S3 の成果物は 1 バイトも変わらない。
 *
 *  ★スタブである以上「呼ばれても何もしない」。M5Stack Basic の LCD 経路で
 *  実害が無いことは実機で確かめること。害が出たら実装を入れる。
 *
 *  __getreent は m5_lx6_reent.c へ分けてある（wifi 構成の wifi_stubs.c が
 *  同じものを持ち、all-in-one では両方がリンクされるため）。
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "m5_stub_trace.h"

/*
 *  ---- GPIO プル制御 ----
 *  m5_gpio_stub.c が持っていない 2 つ。M5GFX の pinMode から呼ばれる。
 */
esp_err_t
gpio_pullup_en(int gpio_num)
{
	M5_STUB_HIT("gpio_pullup_en");
	(void) gpio_num;
	return(ESP_OK);
}

esp_err_t
gpio_pulldown_dis(int gpio_num)
{
	M5_STUB_HIT("gpio_pulldown_dis");
	(void) gpio_num;
	return(ESP_OK);
}

/*
 *  ---- RTC GPIO ----
 *
 *  ESP32 classic の pinMode は、RTC ドメインにも出ているピンを RTC GPIO 側で
 *  切り離してから通常の GPIO として使う。本ポートは RTC GPIO ドライバを
 *  持たないので「RTC GPIO ではない」と答える。rtc_gpio_is_valid_gpio() が
 *  false を返せば M5GFX は残りの rtc_gpio_* を呼ばない経路を通る。
 */
bool
rtc_gpio_is_valid_gpio(int gpio_num)
{
	M5_STUB_HIT("rtc_gpio_is_valid_gpio");
	(void) gpio_num;
	return(false);
}

esp_err_t rtc_gpio_deinit(int n)        { M5_STUB_HIT("rtc_gpio_deinit");        (void) n; return(ESP_OK); }
esp_err_t rtc_gpio_pullup_en(int n)     { M5_STUB_HIT("rtc_gpio_pullup_en");     (void) n; return(ESP_OK); }
esp_err_t rtc_gpio_pullup_dis(int n)    { M5_STUB_HIT("rtc_gpio_pullup_dis");    (void) n; return(ESP_OK); }
esp_err_t rtc_gpio_pulldown_en(int n)   { M5_STUB_HIT("rtc_gpio_pulldown_en");   (void) n; return(ESP_OK); }
esp_err_t rtc_gpio_pulldown_dis(int n)  { M5_STUB_HIT("rtc_gpio_pulldown_dis");  (void) n; return(ESP_OK); }

/*
 *  ---- SPI DMA ワークアラウンド ----
 *
 *  ESP32 classic のエラッタ（DMA とフラッシュキャッシュの同時アクセス）に
 *  対する ESP-IDF 側の細工。本ポートの SPI 経路は DMA を使わない
 *  （m5_spi_bus_stub.c の dma_chan は無視している）ので、呼ばれても何もしない。
 */
void
spicommon_dmaworkaround_idle(int dmachan)
{
	M5_STUB_HIT("spicommon_dmaworkaround_idle");
	(void) dmachan;
}

void
spicommon_dmaworkaround_transfer_active(int dmachan)
{
	M5_STUB_HIT("spicommon_dmaworkaround_transfer_active");
	(void) dmachan;
}
