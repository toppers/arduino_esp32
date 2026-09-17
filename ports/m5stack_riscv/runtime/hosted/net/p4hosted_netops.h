/*
 *  ESP32-P4 + ESP-Hosted — netif の立ち上げと疎通の道具（段 7f で括り出した）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  probe との役割分担（段 7f・F2 で決めた線）
 *  ============================================================================
 *  ここに在るのは「**やること**」——netif を上げる・DHCP を待つ・ping を打つ・
 *  TCP を張る。**PASS/FAIL を決めない**（`verdict()` を呼ばない）。
 *
 *  probe に残るのは「**判定**」——期待値を書き、ここが返した数と突き合わせ、
 *  PASS/FAIL を印字して数える。7d/7e は両方が 1 枚に混ざっていた。
 *
 *  ⇒ **戻り値は「実際に起きたこと」の数**である（成功数・往復の可否・所要時間）。
 *     「うまくいったか」を bool 1 個に潰さない
 *     ——潰すと、判定側が「4/4 と 3/4」を区別できなくなる。
 */
#ifndef P4HOSTED_NETOPS_H
#define P4HOSTED_NETOPS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  netif を上げて DHCP を待つ。戻り値は「アドレスが取れたか」。
 *  `probe_be` は、取れなかったときの切り分け（生 ARP を撃つ相手）に使う。
 *  **有限回ループの同期待ち**（既定 30 秒）で、進捗を 5 秒ごとに印字する。
 */
extern bool	p4hosted_netops_bringup(uint32_t probe_be);

/*
 *  raw ICMP echo を `count` 回撃つ。戻り値は**受け取った応答の数**。
 *  `tmo_ms` は 1 発ごとの待ち（**ミリ秒**。`struct timeval` ではない
 *  ——`LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1` の port である。7e の真因 B）。
 */
extern int	p4hosted_netops_ping(uint32_t dst_be, int count, uint32_t tmo_ms,
								 const char *tag);

/*
 *  TCP を 1 往復（connect -> send -> recv -> 内容照合）。
 *  戻り値: 1 = 往復した / 0 = しなかった。**ペイロードは印字しない**
 *  （バイト数と一致判定のみ。生ダンプは秘密を運ぶ経路になる）。
 */
extern int	p4hosted_netops_tcp_echo(uint32_t server_be, uint16_t port,
									 uint32_t tmo_ms, const char *tag);

/*
 *  TCP で `total` バイトを一方向に送り、所要時間を測る（簡易スループット）。
 *  戻り値: 送れたバイト数（`total` に満たなければ途中で止まった）。
 *  `p_ms` に所要ミリ秒を入れる（NULL 可）。**値の評価はしない**（判定は呼び手）。
 */
extern uint32_t	p4hosted_netops_tcp_send(uint32_t server_be, uint16_t port,
										 uint32_t total, uint32_t tmo_ms,
										 uint32_t *p_ms, const char *tag);

/*
 *  生の ARP 要求を `count` 本、データパスへ直接流す（lwIP を通さない）。
 *  戻り値: 送れた本数。DHCP が成立しないときの切り分け用。
 */
extern int	p4hosted_netops_raw_arp(uint32_t target_be, uint32_t count);

/*
 *  ============================================================================
 *  段 7g で足したもの（UDP / HTTP / DNS）
 *  ============================================================================
 *  【7f までとの違い】7f が通したのは TCP だけである。TCP は lwIP が再送・順序・
 *  ウィンドウを面倒みるので、**下の層の取りこぼしが上で隠れる**——「TCP が通る」は
 *  「1 通のデータグラムが素通しで往復する」を意味しない。UDP はそれを直に見る。
 */

/*
 *  UDP を 1 往復（sendto -> recvfrom -> 内容照合）。
 *  戻り値: 1 = 往復した / 0 = しなかった。**ペイロードは印字しない**。
 */
extern int	p4hosted_netops_udp_echo(uint32_t server_be, uint16_t port,
									 uint32_t tmo_ms, const char *tag);

/*
 *  小さなデータグラムを `count` 個**連射**し、返ってきた数を数える。
 *  戻り値: 受け取った応答の数（`p_sent` に送れた数を入れる。NULL 可）。
 *
 *  【期待値を置かない量である】UDP は再送しないので、欠けた分が
 *  「実装の欠陥」か「無線区間の性質」かを**この道具では分けられない**。
 *  ⇒ 呼び手は**数を記録する**にとどめ、PASS/FAIL の材料にしないこと
 *    （段 7g AC の W1-b）。
 */
extern int	p4hosted_netops_udp_burst(uint32_t server_be, uint16_t port,
									  uint32_t count, uint32_t len,
									  uint32_t tmo_ms, uint32_t *p_sent,
									  const char *tag);

/*
 *  HTTP GET を 1 回。戻り値: 受け取った**ステータスコード**（0 = 取れなかった）。
 *  `p_clen` に `Content-Length` の値、`p_body` に**実際に読んだ本文の長さ**を入れる
 *  （どちらも NULL 可）。**本文は先頭の数バイトしか印字しない**（全文を出さない）。
 */
extern int	p4hosted_netops_http_get(uint32_t server_be, uint16_t port,
									 const char *path, uint32_t tmo_ms,
									 uint32_t *p_clen, uint32_t *p_body,
									 const char *tag);

#if defined(P4HOSTED_DNS)
/*
 *  ホスト名を解決する（`-DA1_P4_HOSTED_DNS=ON` のときだけ在る）。
 *  戻り値: 1 = 解決した / 0 = しなかった。`p_addr` に**ネットワークバイト順**の
 *  IPv4 番地を入れる（NULL 可）。
 */
extern int	p4hosted_netops_dns_resolve(const char *name, uint32_t tmo_ms,
										uint32_t *p_addr, const char *tag);

/*
 *  いま lwIP が持っている DNS サーバ（`idx` 番目）を返す。0 = 未設定。
 *  DHCP が配ったものは lwIP が自動登録する（`dhcp.c` の `dns_setserver()`）。
 */
extern uint32_t	p4hosted_netops_dns_getserver(uint8_t idx);

/*
 *  DNS サーバを明示指定する（DHCP が配らなかったときの手当て。GW を差す等）。
 */
extern void	p4hosted_netops_dns_setserver(uint8_t idx, uint32_t addr_be);
#endif /* P4HOSTED_DNS */

#ifdef __cplusplus
}
#endif

#endif /* P4HOSTED_NETOPS_H */
