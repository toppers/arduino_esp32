/*
 *  hosted Wi-Fi（ESP32-P4 + Stamp AddOn C6）の Arduino 向けアダプタ — 内部契約
 *
 *  `src/ToppersFMP3_WiFi.cpp` が呼ぶ `toppers_fmp3_wifi_*` を、SDIO の RPC
 *  （hosted/rpc/p4hosted_rpc.h）と 802.3 データパス（hosted/net/）の上に
 *  載せるための、3 つの .c が共有する小さな内部ヘッダ。公開 API そのものは
 *  ToppersFMP3_WiFi.cpp 側の extern "C" 宣言が正本で、ここには書かない。
 *
 *  C6 / C5 の native アダプタ（toppers_wifi_core.{c,h} ほか）とは別物である:
 *  あちらは esp_wifi blob を直接叩き、こちらは相手のチップに RPC で頼む。
 */
#ifndef TOPPERS_WIFI_HOSTED_H
#define TOPPERS_WIFI_HOSTED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  スキャン 1 件分。SSID は NUL 終端してコピーして持つ（フックが渡す領域は
 *  戻ると無効になるため）。 */
#define TOPPERS_HOSTED_MAX_RECORDS	20
#define TOPPERS_HOSTED_SSID_MAX		33

struct toppers_hosted_ap {
	char		ssid[TOPPERS_HOSTED_SSID_MAX];
	int32_t		rssi;
	uint32_t	chan;
	uint32_t	authmode;
};

/*  SDIO と相手チップを一度だけ立ち上げる（chip_id の読み出し、slave_config、
 *  wifi_init / set_mode(STA) / wifi_start）。2 度目以降は何もせず true。
 *  どの入口（scan でも begin でも）から呼ばれても同じ順序を通る。 */
bool	toppers_hosted_core_ready(void);

/*  受信を回す（RPC の応答とイベントを取り込む）。tries は rp_pump のそれ。 */
void	toppers_hosted_core_pump(uint32_t tries);

/*  スキャンの結果表（core と connect が読む）。 */
const struct toppers_hosted_ap	*toppers_hosted_scan_record(uint8_t index);
uint8_t							 toppers_hosted_scan_count(void);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_WIFI_HOSTED_H */
