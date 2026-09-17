/*
 *  ESP32-P4 + M5Stack Tab5 内蔵 C6（ESP-Hosted）— 802.3 データパスの**実体**
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  この 1 枚は何か（段 7f・F2 の括り出し）
 *  ============================================================================
 *  `p4hosted_net.h` が宣言する境界の実装である。上は netif（lwIP）、
 *  下は `struct p4hosted_net_xport`（SDIO フレーミング）で、
 *  **802.3 のバイト列以外のことを何も知らない。**
 *
 *  【7d/7e ではどこに在ったか（経緯を残す）】
 *  この実体は 7d が `app/rpc_probe/rpc_probe.c` の中に書いた。7d はその判断を
 *  こう記録している——「フレーミング・クレジット管理・受信ポンプは 7c が
 *  probe に書いて実機で通したコードである。ライブラリへ括り出すのが構造としては
 *  正しいが、7d と同じ一手でやると 7a/7b/7c の非退行の測定が括り出しの回帰と
 *  混ざる。⇒ **境界だけ先に切って、実体はここに置く**」。
 *
 *  7e で DHCP と ping が通り、非退行の基準（7e の実測値）が確定したので、
 *  **7f はその判断の後半（実体の移動）を実行した**。移動であって書き直しではない
 *  ——移した関数の一覧と、意味を変えた箇所は
 *  `.steering/20260816-p4-7f-consolidation/README.md` §「括り出しの設計」に
 *  全数を出してある。
 *
 *  ============================================================================
 *  なぜ生ダンプをしないのか（7c §9-1 の事故）
 *  ============================================================================
 *  「生ダンプが秘密を運ぶ経路になる」事故を実際に起こした。データフレームに
 *  SSID は乗らないが hostname 等は乗り得る。⇒ **数えるだけ**にし、
 *  先頭 14 バイト（Ethernet ヘッダ＝宛先/送信元 MAC と EtherType）だけを
 *  最初の数本に限って出す。MAC と EtherType は秘密ではない。
 *
 *  【何を切り分けたいか】「受けている」と「lwIP に届いている」と
 *  「自分宛のものが来ている」は別である。数えないと区別できない。
 */

#include <kernel.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4hosted_osi.h"
#include "p4hosted_prt.h"
#include "p4hosted_net.h"

/*
 *  ----------------------------------------------------------------------------
 *  下側の境界（トランスポート）
 *  ----------------------------------------------------------------------------
 */
static const struct p4hosted_net_xport	*np_xport;

void
p4hosted_net_bind_xport(const struct p4hosted_net_xport *x)
{
	np_xport = x;
}

bool
p4hosted_net_xport_credit(uint32_t *p_avail)
{
	*p_avail = 0U;
	if ((np_xport == NULL) || (np_xport->credit_avail == NULL)) {
		return(false);
	}
	return(np_xport->credit_avail(p_avail));
}

uint32_t
p4hosted_net_xport_txcnt(void)
{
	if ((np_xport == NULL) || (np_xport->host_txcnt == NULL)) {
		return(0U);
	}
	return(np_xport->host_txcnt());
}

/*
 *  ----------------------------------------------------------------------------
 *  状態
 *  ----------------------------------------------------------------------------
 */
static p4hosted_net_rx_fn	np_rx_cb;
static void					*np_tx_mtx;
static uint8_t				np_mac[6];
static bool					np_mac_valid;

volatile uint32_t	p4hosted_net_n_tx_call;
volatile uint32_t	p4hosted_net_n_tx_ok;
volatile uint32_t	p4hosted_net_n_tx_fail;
volatile uint32_t	p4hosted_net_n_rx_frame;
volatile uint32_t	p4hosted_net_n_rx_nocb;

void
p4hosted_net_set_rx_cb(p4hosted_net_rx_fn fn)
{
	np_rx_cb = fn;
}

/*
 *  ----------------------------------------------------------------------------
 *  フレームの**要約統計**（生ダンプはしない。冒頭コメント参照）
 *  ----------------------------------------------------------------------------
 */
/*
 *  【移動にあたって変えた唯一のこと（記録する）】外部リンケージ -> `static`。
 *  7d は probe の中でこれらを非 static で置いていたが、参照はこの 1 枚の中
 *  （`np_classify` と `p4hosted_net_frame_stats`）だけである（全数 grep で確認）。
 *  **値の意味・数え方は 1 つも変えていない。**
 */
static uint32_t	rp_nf_rx_bcast, rp_nf_rx_tome, rp_nf_rx_other;
static uint32_t	rp_nf_rx_arp, rp_nf_rx_ip4, rp_nf_rx_etype_other;
static uint32_t	rp_nf_rx_udp, rp_nf_rx_dhcp_c;
static uint32_t	rp_nf_tx_bcast, rp_nf_tx_arp, rp_nf_tx_ip4, rp_nf_tx_udp,
				rp_nf_tx_dhcp_s;
static uint32_t	rp_nf_hdr_shown;

static void
np_classify(bool tx, const uint8_t *f, uint16_t len)
{
	uint16_t	etype;
	bool		bcast;

	if (len < 14U) {
		return;
	}
	etype = (uint16_t)(((uint16_t) f[12] << 8) | f[13]);
	bcast = (f[0] == 0xFFU) && (f[1] == 0xFFU) && (f[2] == 0xFFU)
			&& (f[3] == 0xFFU) && (f[4] == 0xFFU) && (f[5] == 0xFFU);

	if (tx) {
		if (bcast) { rp_nf_tx_bcast++; }
		if (etype == 0x0806U) { rp_nf_tx_arp++; }
		else if (etype == 0x0800U) {
			rp_nf_tx_ip4++;
			if ((len >= 34U) && (f[23] == 17U)) {
				uint16_t	hl = (uint16_t)((f[14] & 0x0FU) * 4U);
				uint16_t	dp;

				if ((uint16_t)(14U + hl + 4U) <= len) {
					rp_nf_tx_udp++;
					dp = (uint16_t)(((uint16_t) f[14 + hl + 2] << 8) | f[14 + hl + 3]);
					if (dp == 67U) { rp_nf_tx_dhcp_s++; }
				}
			}
		}
		return;
	}

	if (bcast) { rp_nf_rx_bcast++; }
	else if (np_mac_valid && (memcmp(f, np_mac, 6) == 0)) { rp_nf_rx_tome++; }
	else { rp_nf_rx_other++; }

	if (etype == 0x0806U) { rp_nf_rx_arp++; }
	else if (etype == 0x0800U) {
		rp_nf_rx_ip4++;
		if ((len >= 34U) && (f[23] == 17U)) {
			uint16_t	hl = (uint16_t)((f[14] & 0x0FU) * 4U);
			uint16_t	dp;

			if ((uint16_t)(14U + hl + 4U) <= len) {
				rp_nf_rx_udp++;
				dp = (uint16_t)(((uint16_t) f[14 + hl + 2] << 8) | f[14 + hl + 3]);
				if (dp == 68U) { rp_nf_rx_dhcp_c++; }
			}
		}
	}
	else { rp_nf_rx_etype_other++; }

	/*  先頭 8 本だけ Ethernet ヘッダを出す（**本体は出さない**）  */
	if (rp_nf_hdr_shown < 8U) {
		int		i;

		rp_nf_hdr_shown++;
		pbeg(); ps("RPROBE R3-e rx_ethhdr dst=");
		for (i = 0; i < 6; i++) { pb(f[i]); }
		ps(" src=");
		for (i = 6; i < 12; i++) { pb(f[i]); }
		target_fput_log(' ');
		pkx("etype", etype); pkv("len", len);
		pnl();
	}
}

void
p4hosted_net_frame_stats(void)
{
	pbeg(); ps("RPROBE R3-e rx_class ");
	pkv("bcast", rp_nf_rx_bcast); pkv("tome", rp_nf_rx_tome);
	pkv("other", rp_nf_rx_other);
	pkv("arp", rp_nf_rx_arp); pkv("ip4", rp_nf_rx_ip4);
	pkv("etype_other", rp_nf_rx_etype_other);
	pkv("udp", rp_nf_rx_udp); pkv("dhcp_to_client", rp_nf_rx_dhcp_c);
	pnl();
	pbeg(); ps("RPROBE R3-e tx_class ");
	pkv("bcast", rp_nf_tx_bcast); pkv("arp", rp_nf_tx_arp);
	pkv("ip4", rp_nf_tx_ip4); pkv("udp", rp_nf_tx_udp);
	pkv("dhcp_to_server", rp_nf_tx_dhcp_s);
	pnl();
}

void
p4hosted_net_set_mac(const uint8_t mac[6])
{
	memcpy(np_mac, mac, sizeof(np_mac));
	np_mac_valid = true;
}

bool
p4hosted_net_get_mac(uint8_t mac[6])
{
	if (!np_mac_valid) {
		return(false);		/*  **でっち上げた値を返さない**  */
	}
	memcpy(mac, np_mac, sizeof(np_mac));
	return(true);
}

int
p4hosted_net_tx(const uint8_t *buf, uint16_t len)
{
	int		rc;

	p4hosted_net_n_tx_call++;
	if ((buf == NULL) || (len == 0U)) {
		p4hosted_net_n_tx_fail++;
		return(-1);
	}
	if ((np_xport == NULL) || (np_xport->send_sta == NULL)) {
		/*  **トランスポート未接続。黙って成功を返さない。**  */
		p4hosted_net_n_tx_fail++;
		return(-1);
	}

	/*
	 *  送信はトランスポート側の送信バッファとクレジットを触るので直列化する。
	 *  現状の呼び手は lwIP の tcpip_thread 1 本だけだが、**1 本だと分かって
	 *  いることに依存しない**——後から呼び手が増えたときに黙って壊れる形を
	 *  作らない。
	 */
	if (np_tx_mtx != NULL) {
		if (g_h.funcs->_h_lock_mutex(np_tx_mtx, 1000) != RET_OK) {
			p4hosted_net_n_tx_fail++;
			return(-1);
		}
	}
	np_classify(true, buf, len);
	rc = np_xport->send_sta(buf, len);
	if (np_tx_mtx != NULL) {
		(void) g_h.funcs->_h_unlock_mutex(np_tx_mtx);
	}

	if (rc == 0) {
		p4hosted_net_n_tx_ok++;
		return(0);
	}
	p4hosted_net_n_tx_fail++;
	return(-1);
}

/*
 *  ----------------------------------------------------------------------------
 *  受信: トランスポートが解いた STA フレームを 1 本受け取る
 *  ----------------------------------------------------------------------------
 *  `buf` はポンプの作業バッファの中を指す——**戻ると無効**（ヘッダの
 *  「受信バッファの所有権」の節）。渡した先（lwIP）はその場で写す。
 */
bool
p4hosted_net_rx_deliver(const uint8_t *buf, uint16_t len)
{
#if defined(P4HOSTED_NET_NO_RX)
	/*
	 *  **negative control（7d R2-e / 7e E2-b）**: 受けたが netif へ渡さない。
	 *  データパスを切ると DHCP が成立しないことの実演。
	 *  **受けたこと自体は数える**（「来ていない」と「渡していない」を分ける）。
	 */
	(void) buf; (void) len;
	p4hosted_net_n_rx_frame++;
	p4hosted_net_n_rx_nocb++;
	return(false);
#else
	if (np_rx_cb == NULL) {
		/*  cb 未登録（netif がまだ上がっていない）。**捨てたことを数える。**  */
		p4hosted_net_n_rx_nocb++;
		return(false);
	}
	/*  1 本ごとの印字はしない（要約統計だけ。生ダンプの理由と同じ）  */
	p4hosted_net_n_rx_frame++;
	np_classify(false, buf, len);
	np_rx_cb(buf, len);
	return(true);
#endif /* P4HOSTED_NET_NO_RX */
}

/*
 *  ----------------------------------------------------------------------------
 *  受信ポンプのスレッド
 *  ----------------------------------------------------------------------------
 *  `_h_thread_create` 経由（＝7d R1 の修正——「隣のタスクを起こしていた」の
 *  修正が効いている前提）。**ISR ではなくタスク文脈**で回す。
 */
static volatile bool		np_rx_run;
static volatile uint32_t	np_rx_loops;

static void
np_rx_thread(void const *arg)
{
	(void) arg;
	pbeg(); ps("RPROBE R2-a rx_thread_entered"); pnl();
	while (np_rx_run) {
		if ((np_xport != NULL) && (np_xport->pump_once != NULL)) {
			np_xport->pump_once();
		}
		np_rx_loops++;
	}
	pbeg(); ps("RPROBE R2-a rx_thread_exit"); pnl();
}

bool
p4hosted_net_rx_thread_start(void)
{
	void	*thr;

	if ((np_xport == NULL) || (np_xport->pump_once == NULL)) {
		/*  **トランスポート未接続で起こさない**（空回りするスレッドを作らない）  */
		pbeg(); ps("RPROBE R2-a rx_thread **xport 未接続**"); pnl();
		return(false);
	}
	if (np_tx_mtx == NULL) {
		np_tx_mtx = g_h.funcs->_h_create_mutex();
		if (np_tx_mtx == NULL) {
			pbeg(); ps("RPROBE R2 tx mutex **作れない**"); pnl();
		}
	}
	np_rx_run = true;
	thr = g_h.funcs->_h_thread_create("hosted_rx", 24U, 4096U, np_rx_thread, NULL);
	if (thr == NULL) {
		np_rx_run = false;
		pbeg(); ps("RPROBE R2-a rx_thread **作れない**"); pnl();
		return(false);
	}
	return(true);
}

void
p4hosted_net_rx_thread_stop(void)
{
	np_rx_run = false;
}

uint32_t
p4hosted_net_rx_loops(void)
{
	return(np_rx_loops);
}
