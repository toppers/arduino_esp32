/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ESP32-C3 Wi-Fi用lwIP netif実装（ASP3．NO_SYS=0．BSDソケット互換化）
 *
 *  lwIP自身が生成するtcpip_thread（＝cfg生成のNET_TSK．port/
 *  sys_arch.c参照）だけがlwIPコアAPIを直接呼ぶ．
 *    - wifi_rx_cb（Wi-Fiドライバのタスク文脈）はpbuf_alloc/pbuf_take
 *      してtcpip_input()に渡すのみ（tcpip_input()は任意の文脈から
 *      安全に呼べる，lwIPが公式に提供するinjectionポイント）．
 *    - リンクup/downはtcpip_callback()でtcpip_thread文脈へ委譲する
 *      （dhcp_start等のraw API呼出しをtcpip_thread内に限定するため）．
 *    - DHCP完了検出はnetif_set_status_callback()（ポーリング不要）．
 *    - netif_add／tcpecho_raw_init等の初期化はtcpip_init()のinit_done
 *      コールバック内（＝tcpip_thread起動直後の文脈）で行う．
 *  docs/tcpip-integration.md は**存在しない**（2026-07-22 実測。git 履歴にも一度も無い。外部指摘 `../esp32_s31/docs/reference_repo_issues.md` S3-1）。設計・経緯は `esp/debug/JTAG_DEBUG.md` と `docs/status.md` の Wi-Fi/lwIP 節を参照．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "kernel_cfg.h"

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

/*
 *  arduino_esp32 (2026-09-15, stage 5 Task 2, decision S5-3; the R12
 *  exception is recorded in IMPORT_PROVENANCE.md): the dev demo's
 *  network diagnostics in this file - the gateway ping chain started on
 *  DHCP bound, the TCP/UDP echo servers on port 7, and the ip=/gw= text
 *  of the "DHCP bound" line - compile only with TOPPERS_C6_NET_DIAG=1
 *  (runtime/CMakeLists.txt option TOPPERS_C6_NET_DIAG, default OFF). A
 *  shipped Arduino runtime must not open listening sockets or send ICMP
 *  unasked. Stage 4's hardware record was taken with them ON. Every
 *  #if TOPPERS_C6_NET_DIAG block below is that change; the rest of the
 *  file is the dev original. The "net: DHCP bound" prefix is kept in
 *  both arms (scripts/capture_c6_usj.sh counts it).
 */
#ifndef TOPPERS_C6_NET_DIAG
#define TOPPERS_C6_NET_DIAG	0
#endif
#if TOPPERS_C6_NET_DIAG
#include "tcpecho_raw.h"
#include "udpecho_raw.h"
#endif

#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_mac.h"

#include "esp_shim.h"
#include "net_cfg.h"
#include "netif_esp32s3.h"
#if TOPPERS_C6_NET_DIAG
#include "ping.h"
#endif
#include "diag_recorder.h"		/* 常設recorder基盤（クラッシュ/ハング診断） */

static struct netif	s_netif;
static bool_t		s_dhcp_started;
static bool_t		s_ip_reported;

/*
 *  2026-08-14 A-4 の「esp_wifi_disconnect() で全タスクが止まる」を局在させる
 *  ための計装（opt-in・既定 OFF＝golden 構成には 1 バイトも届かない）。
 *  syslog は logtask 経由なので**止まった後は読めない**。同期出力
 *  （m5_log_now＝target_fput_log 直）を使う。
 *  記録: .steering/20260814-wifi-disconnect-hang/
 */
#ifdef M5_A4_HANGDIAG
extern void m5_log_now(const char *msg);
extern void m5_log_now_u32(const char *msg, unsigned int v);
#define HD(s)			m5_log_now("[HD] " s)
#define HD_U32(s, v)	m5_log_now_u32("[HD] " s, (unsigned int)(v))
volatile uint32_t	net_hd_tx_calls;	/* linkoutput 回数（送信の暴走検知） */
#else
#define HD(s)			do { } while (0)
#define HD_U32(s, v)	do { } while (0)
#endif

/*
 *  ---- 送信（tcpip_thread文脈．linkoutputはpbufを解放しない＝呼出し元の
 *  責務）----
 *
 *  esp_wifi_internal_txは渡したバッファのコピーを取ってから送信する
 *  （呼出し後は再利用可）ため，チェーンpbufを1個の静的バッファへ
 *  線形化してから渡す．
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
			return(ERR_BUF);
		}
		memcpy(&txbuf[total], q->payload, q->len);
		total += q->len;
	}
	{
		err_t	ret;
#ifdef M5_A4_HANGDIAG
		net_hd_tx_calls++;
#endif
		ret = (esp_wifi_internal_tx(WIFI_IF_STA, txbuf, total) == ESP_OK)
			  ? ERR_OK : ERR_IF;
		diag_event(DIAG_EV_NET_TX, (uint32_t) total, (uint32_t) ret);
		return(ret);
	}
}

/*
 *  ---- 受信コールバック（Wi-Fiドライバのタスク文脈）----
 *
 *  pbuf_alloc/pbuf_takeはSYS_ARCH_PROTECTで保護されており任意の文脈
 *  から呼んでよい．tcpip_input()はまさにこの目的（外部文脈からの
 *  安全なパケット注入）でlwIPが提供するAPI．bufferの実体はeb解放まで
 *  有効＝コピー後に解放する．
 */
static esp_err_t
wifi_rx_cb(void *buffer, uint16_t len, void *eb)
{
	struct pbuf	*p;

	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p != NULL) {
		(void) pbuf_take(p, buffer, len);
		diag_event(DIAG_EV_NET_RX, (uint32_t) len, 1U);
		if (tcpip_input(p, &s_netif) != ERR_OK) {
			pbuf_free(p);
		}
	}
	else {
		diag_event(DIAG_EV_PBUF_ALLOC_FAIL, (uint32_t) len, 0U);
	}
	if (eb != NULL) {
		esp_wifi_internal_free_rx_buffer(eb);
	}
	return(ESP_OK);
}

/*
 *  ---- netif初期化コールバック（netif_addから一度だけ呼ばれる）----
 */
static err_t
netif_esp32s3_init(struct netif *netif)
{
	uint8_t	mac[6];

	(void) esp_read_mac(mac, ESP_MAC_WIFI_STA);
	memcpy(netif->hwaddr, mac, sizeof(mac));
	netif->hwaddr_len = sizeof(mac);
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;
	return(ERR_OK);
}

#if TOPPERS_C6_NET_DIAG
/*
 *  ---- ping（lwip契約のcontrib/apps/ping．PING_USE_SOCKETS=0固定
 *  ＝raw API版．sys_timeoutベースでtcpip_thread内蔵のタイマ処理から
 *  自動的に動く）----
 */
void
net_ping_result(int ok)
{
	syslog(LOG_NOTICE, "net: ping gateway -> %s", ok ? "OK" : "timeout");
}

/*
 *  2026-08-14 BL-H-8（現行バグの根治）: **生涯に一度しか起動しない．**
 *
 *  $ESPIDF v5.5.4 の `contrib/apps/ping/ping.c` の `ping_raw_init()` は
 *      ping_pcb = raw_new(IP_PROTO_ICMP);              <- 前の pcb を捨てない
 *      sys_timeout(PING_DELAY, ping_timeout, ping_pcb); <- 自己再発行する鎖を足す
 *  であり，同ファイルに `raw_remove` は **1 か所も無い**（実測．grep で 0 件）．
 *  ⇒ 呼ぶたびに **RAW pcb を 1 個**（`MEMP_NUM_RAW_PCB`=3）と
 *    **sys_timeout の鎖を 1 本**（`MEMP_NUM_SYS_TIMEOUT`=8）を恒久的に消費する．
 *  本関数は `netif_status_cb()` から **DHCP bound のたび**に呼ばれていたので，
 *  再接続を重ねるほど枯渇へ近づき，次に誰かが `sys_timeout()` を要求した瞬間
 *  lwIP の assert（`"sys_timeout: ... pool MEMP_SYS_TIMEOUT is empty"`）が
 *  発火する．その assert が tcpip_thread を止め，系全体が飢餓していた
 *  （実測の内訳・A/B は `.steering/20260814-lx6-r2b-hang/`）．
 *
 *  停止 API が無い以上「1 度だけ起動する」しか安全な使い方が無い．
 *  副作用: 再接続で GW が変わっても ping 先は初回の GW のまま
 *  （ping は診断であり，どの判定表にも使っていない）．
 */
static bool_t	s_ping_started;

void
netif_esp32s3_ping_gateway(void)
{
	if (s_ping_started) {
		return;
	}
	s_ping_started = true;
	ping_init(netif_ip4_gw(&s_netif));
}
#endif /* TOPPERS_C6_NET_DIAG */

/*
 *  ---- DHCP完了検出（ポーリング不要．netifのアドレスが変化する度に
 *  tcpip_thread文脈で呼ばれる）----
 */
static void
netif_status_cb(struct netif *netif)
{
#if TOPPERS_C6_NET_DIAG
	char	ip_buf[16], gw_buf[16];
#endif

	if (s_ip_reported || ip4_addr_isany_val(*netif_ip4_addr(netif))) {
		return;
	}
#if TOPPERS_C6_NET_DIAG
	(void) ip4addr_ntoa_r(netif_ip4_addr(netif), ip_buf, sizeof(ip_buf));
	(void) ip4addr_ntoa_r(netif_ip4_gw(netif), gw_buf, sizeof(gw_buf));
	syslog(LOG_NOTICE, "net: DHCP bound ip=%s gw=%s", ip_buf, gw_buf);
#else
	/*  S5-3: no addresses on the console in the shipped runtime (the
	 *  sketch reads them through WiFi.localIP() etc. if it wants them). */
	syslog(LOG_NOTICE, "net: DHCP bound");
#endif
	s_ip_reported = true;
#if TOPPERS_C6_NET_DIAG
	netif_esp32s3_ping_gateway();
#endif
}

/*
 *  ---- リンクup/down処理（tcpip_callback()経由でtcpip_thread文脈から
 *  呼ばれる．raw API（dhcp_start等）はこの文脈でのみ呼んでよい）----
 */
static void
handle_link_up(void *ctx)
{
	(void) ctx;
	HD("handle_link_up 入口（tcpip_thread 文脈）");
	(void) esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx_cb);
	HD("reg_rxcb 済");
	netif_set_link_up(&s_netif);
	HD("netif_set_link_up 済");
	netif_set_up(&s_netif);
	HD("netif_set_up 済");
#ifdef WIFI_STATIC_IP
	/*  実験M（JTAG_DEBUG.md参照）：DHCP T1更新タイマー仮説の検証用。
	 *  DHCPを完全にバイパスし固定IPを設定する（検証後に削除すること）。 */
	{
		ip4_addr_t	ip, mask, gw;
		IP4_ADDR(&ip, 192, 168, 1, 199);
		IP4_ADDR(&mask, 255, 255, 255, 0);
		IP4_ADDR(&gw, 192, 168, 1, 1);
		syslog(LOG_NOTICE, "net: link up, static IP (DHCP無効, 実験M)");
		netif_set_addr(&s_netif, &ip, &mask, &gw);
		s_dhcp_started = false;
	}
#else
	syslog(LOG_NOTICE, "net: link up, starting DHCP");
	HD_U32("dhcp_start 直前 dhcp state=",
		   (netif_dhcp_data(&s_netif) != NULL)
		   ? (unsigned int) netif_dhcp_data(&s_netif)->state : 0xffU);
	(void) dhcp_start(&s_netif);
	HD("dhcp_start から返った");
	s_dhcp_started = true;
#endif
	s_ip_reported = false;
	HD("handle_link_up 出口");
}

#if TOPPERS_C6_NET_DIAG && defined(TOPPERS_ESPIDF_SUPPLY)
/*  $ESPIDF v5.5.4 の lwip-contrib ping.c は ping_stop() を持たない(TOPPERS が $HAL 版へ
 *  独自追加していた)ので、ここで no-op を置いて link-down 経路のリンクを通している。
 *
 *  2026-08-14 訂正（BL-H-8）: 旧コメントは「ping_init() 側が再 init で
 *  raw_remove(ping_pcb) を行い自動クリーンするため no-op で足りる」と書いていたが、
 *  **これは事実ではない**——$ESPIDF v5.5.4 の contrib ping.c に `raw_remove` は
 *  1 か所も無く、再 init は pcb も sys_timeout の鎖も**捨てずに増やす**。
 *  この誤った説明が「再 init しても大丈夫」という前提を作り、
 *  netif_esp32s3_ping_gateway() を DHCP bound のたびに呼ぶ設計を正当化していた。
 *  ⇒ 停止できないという事実は変わらないので no-op のままだが、**足す側**を
 *  一度きりにした（netif_esp32s3_ping_gateway() のコメント参照）。 */
void ping_stop(void) { }
#endif

static void
handle_link_down(void *ctx)
{
	(void) ctx;
	HD("handle_link_down 入口（tcpip_thread 文脈）");
	syslog(LOG_NOTICE, "net: link down");
#if TOPPERS_C6_NET_DIAG
	ping_stop();
#endif
	HD_U32("ping_stop 後 dhcp_started=", s_dhcp_started ? 1U : 0U);
	if (s_dhcp_started) {
		HD_U32("dhcp_release_and_stop 直前 dhcp state=",
			   (netif_dhcp_data(&s_netif) != NULL)
			   ? (unsigned int) netif_dhcp_data(&s_netif)->state : 0xffU);
		dhcp_release_and_stop(&s_netif);
		HD("dhcp_release_and_stop から返った");
		s_dhcp_started = false;
	}
	netif_set_down(&s_netif);
	HD("netif_set_down 済");
	netif_set_link_down(&s_netif);
	HD("netif_set_link_down 済");
	(void) esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
	HD("handle_link_down 出口（rxcb 解除済）");
	s_ip_reported = false;
}

/*
 *  ---- 公開API（tcpip_thread以外から呼ぶ．lwIPには一切触れず
 *  tcpip_callback()でtcpip_thread文脈へ処理を委譲するのみ）----
 */
void
netif_esp32s3_notify_link(bool up)
{
#ifdef M5_A4_HANGDIAG
	err_t	rc;

	HD_U32("notify_link 入口 up=", up ? 1U : 0U);
	rc = tcpip_callback(up ? handle_link_up : handle_link_down, NULL);
	HD_U32("notify_link 出口 tcpip_callback rc=", (unsigned int)(int) rc);
#else
	(void) tcpip_callback(up ? handle_link_up : handle_link_down, NULL);
#endif
}

uint32_t
netif_esp32s3_get_ipaddr(void)
{
	return(ip4_addr_get_u32(netif_ip4_addr(&s_netif)));
}

/*
 *  ---- 初期化（tcpip_init()のinit_doneコールバック．tcpip_thread
 *  起動直後にその文脈で一度だけ呼ばれる．netif_add等のraw API呼出しは
 *  ここで行う）----
 */
static void
tcpip_init_done(void *arg)
{
	ip4_addr_t	anyaddr;

	(void) arg;
	IP4_ADDR(&anyaddr, 0, 0, 0, 0);

	(void) netif_add(&s_netif, &anyaddr, &anyaddr, &anyaddr, NULL,
					  netif_esp32s3_init, tcpip_input);
	netif_set_default(&s_netif);
	netif_set_status_callback(&s_netif, netif_status_cb);

#if TOPPERS_C6_NET_DIAG
	/*
	 *  TCPエコーサーバ（ポート7．IP_ANY_TYPEでbindするためlink up前でも
	 *  呼べる）
	 */
	tcpecho_raw_init();
	udpecho_raw_init();	/* UDP echo(port7) */
#endif
}

/*
 *  ---- 起動（アプリから一度だけ呼ぶ．tcpip_init()がtcpip_thread
 *  （NET_TSK）を起動し，その文脈でtcpip_init_done()が実行される）----
 */
void
netif_esp32s3_start(void)
{
	tcpip_init(tcpip_init_done, NULL);
}
