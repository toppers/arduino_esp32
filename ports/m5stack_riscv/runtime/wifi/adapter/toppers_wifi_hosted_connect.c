/*
 *  hosted Wi-Fi のアダプタ (3/3): 接続と、その上の DNS / TCP
 *
 *  `WiFi.begin(ssid, pass)` -> 相手チップへ STA 設定と connect を頼み、
 *  接続イベントを待ってから、こちら側の netif を上げて DHCP を待つ。
 *  順序は開発リポジトリのプローブが実機で通したもの（rpc_probe.c の task:
 *  set_sta_config -> connect -> wait_connected -> get_mac ->
 *  net_bind_xport -> netops_bringup）。
 *
 *  `status()` は 3 つの観測だけで決める: 立ち上げに失敗していれば
 *  CONNECT_FAILED、相手が接続していなければ DISCONNECTED、netif に
 *  アドレスが載っていれば CONNECTED。**推測しない**（begin が成功したか
 *  どうかを覚えて返す、という作り方をすると、切れたあとも CONNECTED を
 *  返し続ける）。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>

#include "p4hosted_rpc.h"
#include "p4hosted_net.h"
#include "p4hosted_netops.h"
#include "netif_esp_hosted.h"
#include "toppers_wifi_hosted.h"

/*  TCP は lwIP のソケットで直接話す。netops 側の TCP は測定用の形
 *  （送ったバイト数を数える / HTTP GET のステータスを返す）で、Arduino の
 *  「要求を送って応答を読む」には合わない。native 側の同名関数
 *  （toppers_wifi_connect.c）と同じ組み立てで、違うのは名前解決の出どころだけ。 */
#include "lwip/sockets.h"
#include "lwip/inet.h"

/*  ToppersFMP3_WiFi.h の値（src/ が正本。ここは同じ数を使うだけ）。 */
#define TOPPERS_WL_IDLE_STATUS		0U
#define TOPPERS_WL_CONNECTED		3U
#define TOPPERS_WL_CONNECT_FAILED	4U
#define TOPPERS_WL_DISCONNECTED		6U

/*  2 度目以降の begin で netif のアドレスを待つ上限（ms）。最初の begin の
 *  待ちは持ち込んだ p4hosted_netops_bringup() の中（30 秒）にある。 */
#define HOSTED_REBIND_WAIT_MS	15000U

static bool		hosted_begun;
static bool		hosted_link_up;
/*  netif / lwIP / 受信スレッドを立てたか。**2 度立てない**——
 *  p4hosted_netops_bringup() は受信スレッドの起動と netif の追加を含んでおり、
 *  出典（プローブ）は 1 回しか呼ばない前提で書かれている。 */
static bool		hosted_net_started;

uint8_t
toppers_fmp3_wifi_begin(const char *ssid, const char *password)
{
	uint32_t	tries = 0U;

	if ((ssid == NULL) || (ssid[0] == '\0')) {
		syslog(LOG_WARNING, "[WiFiHosted] begin: empty SSID");
		return(TOPPERS_WL_CONNECT_FAILED);
	}
	if (!toppers_hosted_core_ready()) {
		return(TOPPERS_WL_CONNECT_FAILED);
	}

	/*  資格情報は切り出した RPC 側が参照する（コピーはしない。呼び出し側の
	 *  文字列が生きている必要がある——Arduino の WiFi.begin は定数文字列か
	 *  スケッチの変数を渡す）。 */
	p4hosted_rpc_set_credentials(ssid, password);

	if (hosted_begun) {
		/*  2 度目の begin: 先に切ってから同じ順序をもう一度通す。実機で
		 *  確かめるまでは「動くはず」以上のことは言わない（計画 P11）。 */
		(void) rp_wifi_disconnect();
		hosted_link_up = false;
	}
	if (!rp_wifi_set_sta_config()) {
		syslog(LOG_WARNING, "[WiFiHosted] set_sta_config failed");
		return(TOPPERS_WL_CONNECT_FAILED);
	}
	if (!rp_wifi_connect()) {
		syslog(LOG_WARNING, "[WiFiHosted] connect request refused");
		return(TOPPERS_WL_CONNECT_FAILED);
	}
	hosted_begun = true;
	if (!rp_wait_connected(0U, &tries)) {
		syslog(LOG_NOTICE, "[WiFiHosted] not connected after %u tries",
			   (uint_t) tries);
		return(TOPPERS_WL_DISCONNECTED);
	}
	/*  MAC を取り、802.3 のデータパスへトランスポートを束ねる。 */
	(void) rp_get_mac();
	p4hosted_rpc_bind_xport();

	if (!hosted_net_started) {
		/*  netif を上げて DHCP を待つ（この中に 30 秒の有限ループがある）。
		 *  probe_be は「DHCP が取れなかったときに生 ARP を撃つ相手」で、
		 *  出典は切り分け用に PC の番地を渡していた。こちらは相手を知らない
		 *  ので 0 を渡す——0.0.0.0 宛ての ARP は返事を期待しない診断で、
		 *  出典の測定（送信経路が電波に出ているか）だけが残る。 */
		if (!p4hosted_netops_bringup(0U)) {
			syslog(LOG_NOTICE, "[WiFiHosted] link up but no address yet");
			return(TOPPERS_WL_DISCONNECTED);
		}
		hosted_net_started = true;
	}
	else {
		/*  2 度目以降: netif も受信スレッドも既に在る。立て直さずに
		 *  アドレスが載るのを待つ（DHCP は netif 側が回している）。 */
		uint32_t	waited_ms = 0U;

		while ((netif_esp_hosted_get_ipaddr() == 0U)
			   && (waited_ms < HOSTED_REBIND_WAIT_MS)) {
			(void) dly_tsk(100U * 1000U);
			waited_ms += 100U;
		}
		if (netif_esp_hosted_get_ipaddr() == 0U) {
			syslog(LOG_NOTICE, "[WiFiHosted] link up but no address yet");
			return(TOPPERS_WL_DISCONNECTED);
		}
	}
	hosted_link_up = true;
	syslog(LOG_NOTICE, "[WiFiConnect] connected and DHCP completed");
	return(TOPPERS_WL_CONNECTED);
}

uint8_t
toppers_fmp3_wifi_status(void)
{
	if (!hosted_begun) {
		return(TOPPERS_WL_IDLE_STATUS);
	}
	if (!hosted_link_up) {
		return(TOPPERS_WL_DISCONNECTED);
	}
	return((netif_esp_hosted_get_ipaddr() != 0U)
		   ? TOPPERS_WL_CONNECTED : TOPPERS_WL_DISCONNECTED);
}

void
toppers_fmp3_wifi_disconnect(void)
{
	if (hosted_begun) {
		(void) rp_wifi_disconnect();
		hosted_link_up = false;
	}
}

void
toppers_fmp3_wifi_log_line(const char *message)
{
	if (message != NULL) {
		syslog(LOG_NOTICE, "%s", message);
	}
}

uint32_t
toppers_fmp3_wifi_local_ip(void)
{
	return(netif_esp_hosted_get_ipaddr());
}

uint32_t
toppers_fmp3_wifi_gateway_ip(void)
{
	return(netif_esp_hosted_get_gw());
}

uint32_t
toppers_fmp3_wifi_subnet_mask(void)
{
	return(netif_esp_hosted_get_netmask());
}

int
toppers_fmp3_wifi_host_by_name(const char *host, uint32_t *address)
{
	uint32_t	addr_be = 0U;

	if ((host == NULL) || (address == NULL)) {
		return(-1);
	}
	if (!hosted_link_up) {
		return(-1);
	}
	/*  5 秒（出典の既定）。**戻り値は 1 が成功**（0 = 解決しなかった）。 */
	if (p4hosted_netops_dns_resolve(host, 5000U, &addr_be, "[WiFiHosted]") != 1) {
		return(-1);
	}
	*address = addr_be;
	return(0);
}

int
toppers_fmp3_wifi_tcp_request(const char *host, uint16_t port,
							  const char *request, char *response,
							  uint32_t capacity, uint32_t timeout_ms)
{
	struct sockaddr_in	address;
	uint32_t			resolved = 0U;
	int					socket_fd, received;
	int					receive_timeout_ms = (int) timeout_ms;

	if ((host == NULL) || (request == NULL) || (response == NULL)
		|| (capacity <= 1U)) {
		return(-1);
	}
	if (!hosted_link_up) {
		return(-1);
	}
	if (toppers_fmp3_wifi_host_by_name(host, &resolved) != 0) {
		return(-1);
	}
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = lwip_htons(port);
	address.sin_addr.s_addr = resolved;
	socket_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		syslog(LOG_WARNING, "[WiFiConnect] socket creation failed");
		return(-2);
	}
	/*  LWIP_SO_SNDRCVTIMEO_NONSTANDARD = 1 のこの port では SO_RCVTIMEO は
	 *  ミリ秒の int（native 側と同じ。struct timeval ではない）。 */
	(void) lwip_setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
						   &receive_timeout_ms, sizeof(receive_timeout_ms));
	if (lwip_connect(socket_fd, (struct sockaddr *) &address,
					 sizeof(address)) != 0) {
		syslog(LOG_WARNING, "[WiFiConnect] TCP connect failed port=%d",
			   (int_t) port);
		lwip_close(socket_fd);
		return(-3);
	}
	if (lwip_send(socket_fd, request, strlen(request), 0) < 0) {
		syslog(LOG_WARNING, "[WiFiConnect] TCP send failed");
		lwip_close(socket_fd);
		return(-4);
	}
	received = lwip_recv(socket_fd, response, capacity - 1U, 0);
	if (received >= 0) {
		response[received] = '\0';
	}
	lwip_close(socket_fd);
	syslog((received >= 0) ? LOG_NOTICE : LOG_WARNING,
		   "[WiFiConnect] TCP received=%d", (int_t) received);
	return(received);
}
