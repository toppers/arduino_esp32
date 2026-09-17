/*
 *  esp_hosted の RPC とトランスポート（ESP32-P4、hosted Wi-Fi）— 公開 API
 *
 *  実体は p4hosted_rpc.c（開発リポジトリの rpc_probe.c から切り出したもの。
 *  由来と差分はそのファイルの冒頭と IMPORT_PROVENANCE_p4.md 7 節）。
 *  ここに並ぶのは、Arduino 向けアダプタ（段 B2b の
 *  wifi/adapter/toppers_wifi_hosted_*.c）が呼ぶものだけである。出典では
 *  すべて `static` で、切り出しに際して**この 13 個だけ** `static` を外した。
 *  加えて、外からは触れない file-static（資格情報・スキャンのフック・g_ctx）に
 *  届くための入口を 5 本足してある（`p4hosted_rpc_` で始まるもの）。
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

/*  スキャン結果を 1 件ずつ受け取るフック。ssid は NUL 終端されていない
 *  （長さは slen）。呼び出しはスキャン応答の解析中で、戻ったあとその領域は
 *  無効になる——必要ならコピーすること。NULL を渡すと外れる。 */
typedef void (*p4hosted_rpc_ap_cb_t)(uint32_t idx, const uint8_t *ssid,
									 uint32_t slen, int32_t rssi,
									 uint32_t chan, uint32_t authmode);
void	p4hosted_rpc_set_ap_cb(p4hosted_rpc_ap_cb_t cb);

/*  SDIO の相手（Stamp AddOn の ESP32-C6）を立ち上げる。電源投入から
 *  INIT event の取り込みと slave_config 送信までを 1 本にしたもの
 *  （実体の末尾。出典ではプローブの task に直に並んでいた）。
 *  成功で true。最初の 1 回だけ呼ぶ。 */
bool	p4hosted_rpc_bringup(void);

/*  SDIO 上の相手（C6）へ host 側の設定を送る。最初の 1 回
 *  （p4hosted_rpc_bringup() が内部で呼ぶ）。 */
int	rp_send_slave_config(uint8_t chip_id);

/*  Wi-Fi の起動（順に呼ぶ）。 */
bool	rp_wifi_init(void);
bool	rp_wifi_set_mode(uint32_t mode);
bool	rp_wifi_start(void);

/*  スキャン。get_ap_num は相手の言う件数を内部に控え（読み出しは
 *  p4hosted_rpc_scan_ap_num）、get_ap_records は number 件を読む。
 *  出典と同じく、要求する件数は呼ぶ側が clamp する。 */
bool		rp_scan_start(void);
bool		rp_scan_get_ap_num(void);
uint32_t	p4hosted_rpc_scan_ap_num(void);
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
