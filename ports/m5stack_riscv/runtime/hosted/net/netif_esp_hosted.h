/*
 *  ESP32-P4 + C6（ESP-Hosted）— lwIP netif グルー（ヘッダ）・段7d
 *
 *  取込み: `~/TOPPERS/ESP32/esp32_p4/wifi_p4_module/lwip_port/netif_esp_hosted.h`
 *  改変は `esp/p4hosted/IMPORT_PROVENANCE.md` §3-E が正本。
 *
 *  アプリは `netif_esp_hosted_start()` と `netif_esp_hosted_notify_link()` の
 *  2 本だけを呼ぶ。`netif_add` / `dhcp_start` 等の lwIP raw API は
 *  tcpip_thread 文脈でのみ実行される（本ファイル内で `tcpip_callback` 委譲）。
 */
#ifndef NETIF_ESP_HOSTED_H
#define NETIF_ESP_HOSTED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  起動（アプリから一度だけ）。内部で `tcpip_init()` を呼び tcpip_thread を
 *  起こし、その init_done コールバック内で netif_add / set_default /
 *  set_status_callback を行う。
 */
extern void netif_esp_hosted_start(void);

/*
 *  リンク状態通知（アプリタスク文脈から呼ぶ）。
 *    up   : STA_CONNECTED 後。rx コールバック登録 + netif up + DHCP 開始。
 *    down : STA_DISCONNECTED 後。DHCP 停止 + netif down + rx 解除。
 */
extern void netif_esp_hosted_notify_link(bool up);

/*  DHCP で取れた IPv4（ネットワークバイトオーダ u32・未取得 0）  */
extern uint32_t netif_esp_hosted_get_ipaddr(void);
extern uint32_t netif_esp_hosted_get_gw(void);
extern uint32_t netif_esp_hosted_get_netmask(void);

/*
 *  【改変 E-5（段7d）】DHCP の**リース時間**（秒。未取得 0）。
 *  AC R2-c が「取得アドレスとリース時間をログに出す」ことを求めている。
 *  出典・段E-seam のどちらにも無かった（`lease` の grep が 0 件）。
 */
extern uint32_t netif_esp_hosted_get_lease_sec(void);

/*  netif が up かつアドレスを持っているか  */
extern bool netif_esp_hosted_is_up(void);

/*  RX 経路の到達/取りこぼし（出典の 3 種）＋ TX（段E-seam の 1 種）  */
extern void netif_esp_hosted_get_rx_stats(uint32_t *total, uint32_t *pbuf_fail,
		uint32_t *input_fail);
extern uint32_t netif_esp_hosted_get_tx_count(void);

#ifdef __cplusplus
}
#endif

#endif /* NETIF_ESP_HOSTED_H */
