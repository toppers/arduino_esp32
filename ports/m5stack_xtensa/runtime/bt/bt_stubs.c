/*
 *  BT(BLE)ブリングアップ用スタブ（ESP32-S3 / FMP3）
 *
 *  bt.c/rtc_clk.c 等が参照するが、本ブリングアップ（sleep_mode=0固定・
 *  メモリ解放未使用）では実行時に到達しない/no-opで足りるシンボルをまとめる。
 *  WiFiビルドの wifi_stubs.c / esp_shim_blobglue.c に相当物が無い分を補う。
 */
#include <stdint.h>
#include <stddef.h>

typedef int esp_err_t;
#define ESP_OK 0

/*  電源管理サブモード（BTモデムスリープ。sleep_mode=0では未使用）。 */
esp_err_t
esp_sleep_sub_mode_config(int mode, int activate)
{
	(void) mode; (void) activate;
	return(ESP_OK);
}

int
esp_sleep_sub_mode_dump_config(void *file)
{
	(void) file;
	return(0);
}

esp_err_t
esp_sleep_sub_mode_force_disable(int mode)
{
	(void) mode;
	return(ESP_OK);
}

/*  heap_caps 追加分（esp_shim_libc.c 未提供分）。BT較正の実体アロケーションは
 *  esp_shim のヒープを使うため、これらは統計/領域追加の no-op で足りる。 */
esp_err_t
heap_caps_add_region(intptr_t start, intptr_t end)
{
	(void) start; (void) end;
	return(ESP_OK);
}

size_t
heap_caps_get_free_size(uint32_t caps)
{
	(void) caps;
	return(0x8000U);	/* ダミー：十分な空きがあると返す */
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/allocator.cのログ出力
 *  （heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)）が要求する。
 *  上のheap_caps_get_free_sizeと異なり，こちらはesp_shimヒープの実測値
 *  （esp_shim_heap_largest_free_block，esp_shim.c）へ委譲する
 *  （W3③のRAM実測に使う値そのものであり，ダミー固定値では意味がないため）。
 */
extern size_t esp_shim_heap_largest_free_block(void);

size_t
heap_caps_get_largest_free_block(uint32_t caps)
{
	(void) caps;
	return(esp_shim_heap_largest_free_block());
}
