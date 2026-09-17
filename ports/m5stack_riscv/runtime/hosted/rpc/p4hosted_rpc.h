/*
 *  esp_hosted の RPC とトランスポート（ESP32-P4、hosted Wi-Fi）— 公開 API
 *
 *  実体は p4hosted_rpc.c（開発リポジトリの rpc_probe.c から切り出したもの。
 *  由来と差分はそのファイルの冒頭と IMPORT_PROVENANCE_p4.md 7 節）。
 *  ここに並ぶのは、Arduino 向けアダプタ（段 B2b の
 *  wifi/adapter/toppers_wifi_hosted_*.c）が呼ぶものだけである。出典では
 *  すべて `static` で、切り出しに際して**この 13 個だけ** `static` を外した。
 *
 *  戻り値の約束は出典のまま: `bool` を返すものは成功で true。
 *  `rp_scan_get_ap_num()` は成功で true（件数は出典どおり内部に持つ）、
 *  `rp_wait_connected()` は接続できたら true。
 */
#ifndef TOPPERS_P4HOSTED_RPC_H
#define TOPPERS_P4HOSTED_RPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  資格情報は**実行時に**渡す。出典（プローブ）は CMake が -include した
 *  生成ヘッダの WIFI_STA_SSID / WIFI_STA_PASS をコンパイル時に焼いていたが、
 *  Arduino の WiFi.begin(ssid, pass) は実行時に来る。切り出した本体は
 *  その 2 つのマクロを**そのまま**参照し続け、マクロがここで設定された
 *  ポインタを指す（本体の行は書き換えない）。ssid / pass は呼び出し側が
 *  所有し、切断するまで生かしておくこと（コピーしない）。 */
void	p4hosted_rpc_set_credentials(const char *ssid, const char *pass);

/*  SDIO 上の相手（C6）へ host 側の設定を送る。最初の 1 回。 */
int	rp_send_slave_config(uint8_t chip_id);

/*  Wi-Fi の起動（順に呼ぶ）。 */
bool	rp_wifi_init(void);
bool	rp_wifi_set_mode(uint32_t mode);
bool	rp_wifi_start(void);

/*  スキャン。get_ap_records は number 件を読み、見つけた SSID を印字する。 */
bool		rp_scan_start(void);
bool		rp_scan_get_ap_num(void);
bool		rp_scan_get_ap_records(uint32_t number);

/*  STA 接続。set_sta_config は上の資格情報を使う。 */
bool	rp_wifi_set_sta_config(void);
bool	rp_wifi_connect(void);
bool	rp_wifi_disconnect(void);
bool	rp_wait_connected(uint32_t seen_disc, uint32_t *tries);
bool	rp_get_mac(void);

/*  受信を回す（イベントと応答の取り込み）。stop_uid が 0 なら max_tries 回。 */
uint32_t	rp_pump(uint32_t max_tries, uint32_t stop_uid);

/*  802.3 データパスへトランスポートを束ねる（出典のプローブが接続後に
 *  p4hosted_net_bind_xport(&rp_net_xport) を呼んでいたのと同じこと)。 */
void	p4hosted_rpc_bind_xport(void);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_P4HOSTED_RPC_H */
