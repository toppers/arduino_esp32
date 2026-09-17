/*
 *  ESP32-P4 + C6（ESP-Hosted）— lwIP netif グルー・段7d
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  出典と改変（正本は `esp/p4hosted/IMPORT_PROVENANCE.md` §3-E）
 *  ============================================================================
 *  取込み元: `~/TOPPERS/ESP32/esp32_p4/wifi_p4_module/lwip_port/netif_esp_hosted.c`
 *            （245 行・**読み取り専用**）
 *
 *  出典の構造（`tcpip_init` -> `tcpip_init_done` で `netif_add`、`tcpip_callback`
 *  でリンク up/down を tcpip_thread 文脈へ委譲、`netif_status_cb` で DHCP 完了を
 *  検出）は**そのまま**である。差し替えたのは**データ経路の 3 呼出しだけ**
 *  ——本 repo の段E-seam が `netif_esp_eth.c` で行ったのと同じ手術を、
 *  今度は逆向き（EMAC ではなく esp-hosted）に当てている。
 *
 *    E-1  TX      `esp_wifi_internal_tx(WIFI_IF_STA, ...)` -> `p4hosted_net_tx()`
 *    E-2  RX 登録 `esp_wifi_internal_reg_rxcb(WIFI_IF_STA, cb)`
 *                                              -> `p4hosted_net_set_rx_cb()`
 *    E-3  MAC     `esp_wifi_get_mac(WIFI_IF_STA, mac)` -> `p4hosted_net_get_mac()`
 *
 *  いずれも理由は同じ: 本 repo は `esp_wifi_remote` チャネル層をリンクしない
 *  （7b/7c の scoping）。詳細は `net/p4hosted_net.h` の冒頭。
 *
 *    E-4  出力を `syslog` から**同期出力**へ（改変 D-3 と同じ規律。
 *         非同期出力はハング直前の行を落とし、「起きなかった」と
 *         「出力が間に合わなかった」を区別できなくする）
 *    E-5  DHCP の**リース時間**とネットマスクを公開（AC R2-c）。
 *         出典にも段E-seam にも無かった
 *    E-6  TX 計数を足した（出典は RX 3 種のみ・段E-seam は TX 1 種のみ。
 *         両方の良いところを採る）
 *    E-7  出典のログ文字列にあった U+2605 を落とした（本 repo の禁止文字）
 *
 *  ============================================================================
 *  受信バッファの所有権（**出典と契約が違う**ので繰り返す）
 *  ============================================================================
 *  出典の `wifi_rx_cb` は第 3 引数 `eb` を**解放する責務**を負っていた。
 *  本 repo のコールバックは**借りているだけ**で、戻ると無効になる。
 *  ⇒ `pbuf_take` で写した時点で用は済む。**解放してはならない。**
 */
#include <kernel.h>
#include <string.h>

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

#include "p4hosted_net.h"
#include "netif_esp_hosted.h"

/*
 *  ---- 同期出力（改変 E-4）----
 *  7a の `p4sdio_puts` を使う（`_h_printf` の出力先＝改変 D-3 と同じもの）。
 *  1 行を不可分にするため `loc_cpu` で囲む（7a/7b/7c の probe と同じ作法）。
 */
extern void	p4sdio_puts(const char *s);

static void
nh_beg(void)
{
	(void) loc_cpu();
}

static void
nh_end(void)
{
	p4sdio_puts("\n");
	(void) unl_cpu();
}

static void
nh_s(const char *s)
{
	p4sdio_puts(s);
}

static void
nh_u(uint32_t v)
{
	char		buf[12];
	int			i = (int) sizeof(buf) - 1;

	buf[i] = '\0';
	do {
		buf[--i] = (char) ('0' + (v % 10U));
		v /= 10U;
	} while (v != 0U && i > 0);
	p4sdio_puts(&buf[i]);
}

/*  ネットワークバイトオーダの u32 を a.b.c.d で出す  */
static void
nh_ip(uint32_t be)
{
	nh_u((be >> 0) & 0xFFU);  nh_s(".");
	nh_u((be >> 8) & 0xFFU);  nh_s(".");
	nh_u((be >> 16) & 0xFFU); nh_s(".");
	nh_u((be >> 24) & 0xFFU);
}

static struct netif	s_netif;
static bool			s_dhcp_started;
static bool			s_ip_reported;

/*  診断カウンタ（出典の RX 3 種 ＋ 改変 E-6 の TX 1 種）  */
static volatile uint32_t	s_rx_total;
static volatile uint32_t	s_rx_pbuf_fail;
static volatile uint32_t	s_rx_input_fail;
static volatile uint32_t	s_tx_out_count;

static volatile uint32_t	s_lease_sec;

/*
 *  ---- 送信（tcpip_thread 文脈。`linkoutput` は pbuf を解放しない＝呼出し元の責務）
 *  チェーン pbuf を 1 個の静的バッファへ線形化してから 1 回で渡す
 *  （出典・段E-seam と同じ。`p4hosted_net_tx` は渡したバッファを
 *   送信前に自分のフレームバッファへ写すので、戻った後に再利用してよい）。
 */
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
	static uint8_t	txbuf[1600];
	struct pbuf		*q;
	uint16_t		total = 0;

	(void) netif;
	for (q = p; q != NULL; q = q->next) {
		if ((size_t)(total + q->len) > sizeof(txbuf)) {
			return ERR_BUF;
		}
		memcpy(&txbuf[total], q->payload, q->len);
		total += q->len;
	}
	if (p4hosted_net_tx(txbuf, total) != 0) {
		return ERR_IF;
	}
	s_tx_out_count++;
	return ERR_OK;
}

/*
 *  ---- 受信（esp-hosted の受信ポンプのタスク文脈。**ISR ではない**）----
 *  `tcpip_input()` は任意のタスク文脈から安全に呼べる lwIP 公式の注入点。
 */
static void
hosted_rx_cb(const uint8_t *buffer, uint16_t len)
{
	struct pbuf	*p;

	s_rx_total++;
	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p != NULL) {
		(void) pbuf_take(p, buffer, len);
		if (tcpip_input(p, &s_netif) != ERR_OK) {
			s_rx_input_fail++;
			pbuf_free(p);
		}
	}
	else {
		s_rx_pbuf_fail++;
	}
	/*  **解放しない**（借りているだけ。冒頭「所有権」参照）  */
}

void
netif_esp_hosted_get_rx_stats(uint32_t *total, uint32_t *pbuf_fail, uint32_t *input_fail)
{
	if (total != NULL)      { *total = s_rx_total; }
	if (pbuf_fail != NULL)  { *pbuf_fail = s_rx_pbuf_fail; }
	if (input_fail != NULL) { *input_fail = s_rx_input_fail; }
}

uint32_t
netif_esp_hosted_get_tx_count(void)
{
	return s_tx_out_count;
}

/*
 *  ---- netif 初期化（`netif_add` から 1 度だけ呼ばれる）----
 */
static err_t
netif_esp_hosted_init(struct netif *netif)
{
	uint8_t	mac[6];

	memset(mac, 0, sizeof(mac));
	/*  改変 E-3: MAC はスレーブ(C6)が持つ。アプリが RPC で取って渡してある。
	 *  取れていなければ 0 埋めのまま——**でっち上げた値を入れない**
	 *  （0 埋めなら「取れていない」と分かるが、適当な値だと分からない）。 */
	(void) p4hosted_net_get_mac(mac);
	memcpy(netif->hwaddr, mac, sizeof(mac));
	netif->hwaddr_len = sizeof(mac);
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;

	nh_beg();
	nh_s("NETIF init hwaddr=");
	{
		int		i;
		static const char hex[] = "0123456789abcdef";

		for (i = 0; i < 6; i++) {
			char	b[3];

			b[0] = hex[(mac[i] >> 4) & 0xF];
			b[1] = hex[mac[i] & 0xF];
			b[2] = '\0';
			nh_s(b);
			if (i != 5) { nh_s(":"); }
		}
	}
	nh_s(" mtu=1500");
	nh_end();
	return ERR_OK;
}

/*
 *  ---- DHCP 完了検出（tcpip_thread 文脈。アドレス変化のたびに呼ばれる）----
 */
static void
netif_status_cb(struct netif *netif)
{
	struct dhcp	*d;

	if (s_ip_reported || ip4_addr_isany_val(*netif_ip4_addr(netif))) {
		return;
	}

	/*  改変 E-5: リース時間（秒）。lwIP は `offered_t0_lease` に秒で持つ。  */
	d = netif_dhcp_data(netif);
	s_lease_sec = (d != NULL) ? (uint32_t) d->offered_t0_lease : 0U;

	nh_beg();
	nh_s("NETIF DHCP bound ip=");
	nh_ip(ip4_addr_get_u32(netif_ip4_addr(netif)));
	nh_s(" mask=");
	nh_ip(ip4_addr_get_u32(netif_ip4_netmask(netif)));
	nh_s(" gw=");
	nh_ip(ip4_addr_get_u32(netif_ip4_gw(netif)));
	nh_s(" lease_sec=");
	nh_u(s_lease_sec);
	nh_end();
	s_ip_reported = true;
}

/*
 *  ---- リンク up/down（`tcpip_callback` 経由で tcpip_thread 文脈）----
 */
static void
handle_link_up(void *ctx)
{
	(void) ctx;
	p4hosted_net_set_rx_cb(hosted_rx_cb);		/* 改変 E-2 */
	netif_set_link_up(&s_netif);
	netif_set_up(&s_netif);
	nh_beg(); nh_s("NETIF link up, starting DHCP"); nh_end();
	(void) dhcp_start(&s_netif);
	s_dhcp_started = true;
	s_ip_reported = false;
}

static void
handle_link_down(void *ctx)
{
	(void) ctx;
	nh_beg(); nh_s("NETIF link down"); nh_end();
	if (s_dhcp_started) {
		dhcp_release_and_stop(&s_netif);
		s_dhcp_started = false;
	}
	netif_set_down(&s_netif);
	netif_set_link_down(&s_netif);
	p4hosted_net_set_rx_cb(NULL);				/* 改変 E-2 */
	s_ip_reported = false;
}

/*
 *  ---- 公開 API（アプリタスク文脈。lwIP には触れず委譲する）----
 */
void
netif_esp_hosted_notify_link(bool up)
{
	err_t	err;

	/*
	 *  出典（2026-07-16 の修正）を**そのまま引き継ぐ**: `tcpip_callback()` は
	 *  tcpip mbox へノンブロッキングで投函し、満杯なら `ERR_MEM` を返す契約。
	 *  戻り値を捨てると、link up 通知が黙って失われて `dhcp_start()` が
	 *  実行されず「DHCP がタイムアウトした」という間欠フレークになる。
	 *  ここでリトライすると呼出し元をブロックしうるので通知に留める。
	 */
	err = tcpip_callback(up ? handle_link_up : handle_link_down, NULL);
	if (err != ERR_OK) {
		nh_beg();
		nh_s("NETIF link notify FAILED err=");
		nh_u((uint32_t)(int32_t) err);
		nh_s(" (tcpip mbox 輻輳の可能性。DHCP が開始されない恐れ)");
		nh_end();
	}
}

uint32_t
netif_esp_hosted_get_ipaddr(void)
{
	return ip4_addr_get_u32(netif_ip4_addr(&s_netif));
}

uint32_t
netif_esp_hosted_get_gw(void)
{
	return ip4_addr_get_u32(netif_ip4_gw(&s_netif));
}

uint32_t
netif_esp_hosted_get_netmask(void)
{
	return ip4_addr_get_u32(netif_ip4_netmask(&s_netif));
}

uint32_t
netif_esp_hosted_get_lease_sec(void)
{
	return s_lease_sec;
}

bool
netif_esp_hosted_is_up(void)
{
	return netif_is_up(&s_netif) && !ip4_addr_isany_val(*netif_ip4_addr(&s_netif));
}

/*
 *  ---- 初期化（`tcpip_init()` の init_done。tcpip_thread 文脈で 1 度だけ）----
 */
static void
tcpip_init_done(void *arg)
{
	ip4_addr_t	anyaddr;

	(void) arg;
	IP4_ADDR(&anyaddr, 0, 0, 0, 0);

	(void) netif_add(&s_netif, &anyaddr, &anyaddr, &anyaddr, NULL,
					 netif_esp_hosted_init, tcpip_input);
	netif_set_default(&s_netif);
	netif_set_status_callback(&s_netif, netif_status_cb);
	nh_beg(); nh_s("NETIF tcpip init done (netif added)"); nh_end();
}

void
netif_esp_hosted_start(void)
{
	tcpip_init(tcpip_init_done, NULL);
}
