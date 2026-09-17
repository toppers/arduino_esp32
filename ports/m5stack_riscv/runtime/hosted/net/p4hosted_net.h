/*
 *  ESP32-P4 + M5Stack Tab5 内蔵 C6（ESP-Hosted）— 802.3 データパスの境界（段7d）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  この 1 枚が「netif（lwIP 側）」と「SDIO データパス（esp-hosted 側）」の
 *  唯一の接触面である。
 *  ============================================================================
 *
 *  【なぜ出典と違う形なのか】出典（`~/TOPPERS/ESP32/esp32_p4` の
 *  `wifi_p4_module/lwip_port/netif_esp_hosted.c`）は、
 *  `esp_wifi_internal_tx()` / `esp_wifi_internal_reg_rxcb()` という
 *  **`esp_wifi_remote` チャネル層の API** を呼んでいた。本 repo は 7b/7c の
 *  射程どおり **上流のドライバ層（`sdio_drv.c` / `transport_drv.c` /
 *  `esp_hosted_api.c` / `esp_wifi_remote`）を 1 行もリンクしない**ので、
 *  その API の実体が存在しない。
 *
 *  ⇒ 同じ 2 本の呼出しを、**本 repo が実際に持っているもの**（7c で実機実証
 *     済みの `_h_sdio_write_block` 送信と、割込み駆動の受信ポンプ）へ
 *     差し替える。差し替えは**この 1 枚のヘッダの中**に閉じており、
 *     netif 側は「バイト列を出す／入る」以外を知らない。
 *
 *  【受信バッファの所有権（出典と違うので明記する）】
 *    出典: `wifi_rx_cb(void *buffer, uint16_t len, void *eb)` で、`eb` を
 *          **コールバックが解放する責務**を持っていた（`_h_malloc` 由来）。
 *    本 repo: 受信フレームは**ポンプの作業バッファの中**を指すポインタで、
 *          **コールバックから戻った瞬間に無効になる**。コールバックは
 *          必要ならその場で写す（lwIP なら `pbuf_take`）。
 *          **解放してはならない。** 所有権の質問そのものを消してある。
 */
#ifndef P4HOSTED_NET_H
#define P4HOSTED_NET_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ============================================================================
 *  下側の境界（段 7f で足した）: データパスが**トランスポートに要求する**もの
 *  ============================================================================
 *  【なぜ関数ポインタなのか】7d/7e はデータパスの実体を `rpc_probe.c` の中に
 *  置いていた（7d の判断: 境界だけ先に切り、実体は probe に残す）。
 *  7f でその実体を `p4hosted_net.c` へ移したが、送信・受信ポンプ・クレジットの
 *  実装は **SDIO フレーミング側（probe が持つ `rp_send`/`rp_pump`/`rp_credit`）**
 *  にある。データパス側からそれらを直接呼ぶと、括り出したはずの依存が
 *  逆向きに残る（`p4hosted_net.c` -> `rpc_probe.c` のリンク依存）。
 *  ⇒ **上側（netif）と同じく、下側も 1 枚の契約にする。**
 *     トランスポートを持つ側が `p4hosted_net_bind_xport()` で差す。
 *
 *  これで `p4hosted_net.c` は「802.3 のバイト列」以外を何も知らない
 *  ——上は `p4hosted_net_rx_fn`、下は `struct p4hosted_net_xport` だけである。
 */
struct p4hosted_net_xport {
	/*  802.3 フレームを 1 本、STA インタフェースとして送る。0=送った  */
	int			(*send_sta)(const uint8_t *buf, uint16_t len);
	/*  受信を 1 回分回す（タスク文脈。戻り値は使わない）  */
	void		(*pump_once)(void);
	/*  スレーブ受信バッファの空き。取れなければ false（`*p_avail` は不定）  */
	bool		(*credit_avail)(uint32_t *p_avail);
	/*  ホスト側が数えている送信バッファ通番（診断用）  */
	uint32_t	(*host_txcnt)(void);
};

/*
 *  トランスポートを差す（**bringup の前に必ず呼ぶ**）。
 *  NULL を差すと送信は失敗を返し、受信ポンプは空回りする
 *  ——**黙って動いているように見せない**。
 */
extern void	p4hosted_net_bind_xport(const struct p4hosted_net_xport *x);

/*
 *  差されたトランスポートへの薄い問い合わせ（診断用）。
 *  未接続なら `credit` は false を返し `*p_avail` を 0 にする／`txcnt` は 0。
 *  **「未接続だから 0」と「本当に 0」を混ぜないため、credit は真偽を返す。**
 */
extern bool		p4hosted_net_xport_credit(uint32_t *p_avail);
extern uint32_t	p4hosted_net_xport_txcnt(void);

/*
 *  受信コールバック。
 *  文脈: esp-hosted 受信ポンプのタスク文脈（**ISR ではない**）。
 *  `buf` は**戻るまでの間だけ**有効（上記「所有権」参照）。
 */
typedef void (*p4hosted_net_rx_fn)(const uint8_t *buf, uint16_t len);

/*  受信コールバックの登録・解除（NULL で解除）。  */
extern void	p4hosted_net_set_rx_cb(p4hosted_net_rx_fn fn);

/*
 *  802.3 フレームを 1 本送る（`if_type = ESP_STA_IF`）。
 *  戻り値: 0 = 送った / 非 0 = 送れなかった。
 *  文脈: タスク文脈（lwIP の `tcpip_thread` から呼ばれる）。
 */
extern int	p4hosted_net_tx(const uint8_t *buf, uint16_t len);

/*
 *  netif の hwaddr に載せる MAC（C6 の STA MAC）。
 *  アプリが RPC（`Req_GetMACAddress`）で取ってから `set` する。
 *  未設定なら `get` は 0 埋めを返す（**でっち上げた値を返さない**）。
 */
extern void	p4hosted_net_set_mac(const uint8_t mac[6]);
extern bool	p4hosted_net_get_mac(uint8_t mac[6]);

/*
 *  計数（**0 でも印字するため常に公開する**。「数えていない」と「0 だった」は違う）。
 */
extern volatile uint32_t	p4hosted_net_n_tx_call;		/* 送信要求 */
extern volatile uint32_t	p4hosted_net_n_tx_ok;		/* 送れた */
extern volatile uint32_t	p4hosted_net_n_tx_fail;		/* 送れなかった */
extern volatile uint32_t	p4hosted_net_n_rx_frame;	/* STA フレームを受けた */
extern volatile uint32_t	p4hosted_net_n_rx_nocb;		/* 受けたが cb 未登録で捨てた */

/*  フレームの**要約統計**を印字する（生ダンプはしない。実装側の冒頭コメント参照）  */
extern void	p4hosted_net_frame_stats(void);

/*
 *  受信ポンプが解いた STA フレームを 1 本渡す（**トランスポート側から呼ぶ**）。
 *  戻り値: true = netif へ渡した / false = 渡していない（cb 未登録、
 *  または `P4HOSTED_NET_NO_RX` の negative control 構成）。
 *  **呼び手は false を「捨てた」として数えること**（黙って落とさない）。
 */
extern bool	p4hosted_net_rx_deliver(const uint8_t *buf, uint16_t len);

/*  受信ポンプのスレッドを起こす／止める（bringup が使う）  */
extern bool		p4hosted_net_rx_thread_start(void);
extern void		p4hosted_net_rx_thread_stop(void);
extern uint32_t	p4hosted_net_rx_loops(void);

#ifdef __cplusplus
}
#endif

#endif /* P4HOSTED_NET_H */
