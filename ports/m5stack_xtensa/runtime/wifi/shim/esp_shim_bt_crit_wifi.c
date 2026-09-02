/*
 *  esp-idf submodule(v5.5.4)供給の Wi-Fi ビルド用 critical section スタブ。
 *
 *  $ESPIDF の esp_wifi_private.h が freertos/FreeRTOS.h を include するため、
 *  Wi-Fi ビルドでも repo の freertos shim(esp/bt/stub/include)を -I に足す。
 *  その shim は portENTER_CRITICAL(mux) を esp_shim_bt_enter_critical(mux) に
 *  マップするが、その実体(BT専用mux実装)は bt_shim.c 側で Wi-Fi ビルドには
 *  リンクされない。periph_ctrl.c / phy_init.c 等が portENTER_CRITICAL を使う
 *  ため、Wi-Fi ビルド専用の最小実装をここで供給する。
 *
 *  Wi-Fi は PRC_NUM=1(単一コア)構成なので、割込み禁止＋ネストカウンタで
 *  排他が成立する(BT のような per-core mux は不要)。TOPPERS_ESPIDF_SUPPLY 時
 *  のみ有効(baseline/BTビルドとは二重定義しない)。
 */
#if defined(TOPPERS_ESPIDF_SUPPLY) && !defined(TOPPERS_BT_CLASSIC)
#include <stdint.h>
#include "esp_shim.h"

static uint32_t	wc_saved_state;
static int32_t	wc_nest;

void
esp_shim_bt_enter_critical(void *mux)
{
	uint32_t	state = esp_shim_int_disable();

	(void) mux;	/* 単一コアのため mux は使わない */
	if (wc_nest++ == 0) {
		wc_saved_state = state;	/* 最外だけ退避 */
	}
}

void
esp_shim_bt_exit_critical(void *mux)
{
	(void) mux;
	if (--wc_nest == 0) {
		esp_shim_int_restore(wc_saved_state);
	}
}
#endif /* TOPPERS_ESPIDF_SUPPLY && !TOPPERS_BT_CLASSIC */
