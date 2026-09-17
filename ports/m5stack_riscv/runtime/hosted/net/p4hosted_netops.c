/*
 *  ESP32-P4 + ESP-Hosted — netif の立ち上げと疎通の道具（実体）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  段 7f の括り出し（F2）で `app/rpc_probe/rpc_probe.c` から**移動**した。
 *  役割分担（ここは「やること」・probe は「判定」）は
 *  `p4hosted_netops.h` の冒頭コメントを参照。
 *
 *  ============================================================================
 *  出力は同期（`target_fput_log()` 直呼び）
 *  ============================================================================
 *  `syslog()` は logtask 経由の非同期出力なので停止直前の行を取りこぼす。
 *  「応答が無かった」と「出力が間に合わなかった」を区別できなくなる。
 *  ⇒ 段E-seam の `esp/eth/app/net_probe_tests.c` を流用せずここに書いた理由が
 *     これである（あちらは `syslog`）。**手順は同じ・出力だけ違う。**
 */

#include <kernel.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4hosted_prt.h"
#include "p4hosted_net.h"
#include "p4hosted_netops.h"
#include "netif_esp_hosted.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip4.h"

/*  ネットワークバイトオーダの u32 を a.b.c.d で出す（同期出力）  */
static void
np_put_ip(uint32_t be)
{
	uint32_t	i;

	for (i = 0U; i < 4U; i++) {
		uint32_t	o = (be >> (8U * i)) & 0xFFU;
		char		b[4];
		int			k = 3;

		b[k] = '\0';
		do {
			b[--k] = (char) ('0' + (o % 10U));
			o /= 10U;
		} while ((o != 0U) && (k > 0));
		ps(&b[k]);
		if (i != 3U) { target_fput_log('.'); }
	}
}

/*
 *  ----------------------------------------------------------------------------
 *  ping（raw ICMP）
 *  ----------------------------------------------------------------------------
 *  手順は `esp/eth/app/net_probe_tests.c:37-134`（段E-seam で 4/4 を出した実装）
 *  と同じ: raw socket(IPPROTO_ICMP) -> `icmp_echo_hdr` を組んで `inet_chksum`
 *  -> 受信は IP ヘッダぶん（`IPH_HL_BYTES`）読み飛ばして `ICMP_ER` で判定。
 *
 *  【段7e で潰した現行バグ 2 件を含む。戻さないこと】
 *
 *  (B) `SO_RCVTIMEO` の値は **`int`（ミリ秒）**である。本 port の
 *      `esp/eth/lwip_port/include/lwipopts.h:119` が
 *      **`LWIP_SO_SNDRCVTIMEO_NONSTANDARD = 1`** だからで、
 *      `struct timeval` **ではない**。7d は `timeval{2,0}` を渡しており、
 *      lwIP が先頭 4 バイトを `int` として読んだ結果、**受信待ちが 2 ミリ秒**に
 *      なっていた（`optlen` が `sizeof(int)` 以上なので `setsockopt` は
 *      **成功し、黙って間違った値が入る**）。Wi-Fi + SDIO の実測 RTT は
 *      4.9〜34.9 ms なので全部タイムアウトしていた。
 *      **段E-seam（有線）で顕在化しなかったのは RTT が 2ms 未満だったから。**
 *
 *  (C) raw ICMP ソケットには**自分宛の ICMP が全部**入るので、1 発でも
 *      取りこぼすと**以後ずっと 1 つ前の応答を読む**。7d は `recv` を 1 回だけ
 *      呼び `id` しか照合していなかったため `sent=4 replies=3` / `rtt_us=115`
 *      という**偽陽性**を出していた（PC 側 tcpdump では 4 本とも返っていた）。
 *      ⇒ **期限まで待ち、`id` と `seqno` の両方が一致するものだけ数える。**
 *         一致しないものは捨てて**数えて印字する**（`dropped=`）。
 */
#define NP_PING_ID		0x5A7DU
#define NP_PING_DATA	32

int
p4hosted_netops_ping(uint32_t dst_be, int count, uint32_t tmo_ms, const char *tag)
{
	int			sock;
	int			replies = 0;
	int			seq;
	uint8_t		snd[sizeof(struct icmp_echo_hdr) + NP_PING_DATA];
	uint8_t		rcv[128];
	struct sockaddr_in		to;
	struct icmp_echo_hdr	*ping = (struct icmp_echo_hdr *)(void *) snd;

	sock = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (sock < 0) {
		pbeg(); ps("RPROBE R3 ping socket **fail** "); ps(tag); pnl();
		return(0);
	}
	{
		int		tmo = (int) tmo_ms;		/* ミリ秒。**timeval ではない**（上の (B)） */

		(void) lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
	}
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = dst_be;

	for (seq = 1; seq <= count; seq++) {
		SYSTIM	t0 = 0, t1 = 0;
		int		n = 0;

		memset(snd, 0, sizeof(snd));
		ICMPH_TYPE_SET(ping, ICMP_ECHO);
		ICMPH_CODE_SET(ping, 0);
		ping->chksum = 0;
		ping->id = lwip_htons(NP_PING_ID);
		ping->seqno = lwip_htons((uint16_t) seq);
		ping->chksum = inet_chksum(snd, sizeof(snd));

		(void) get_tim(&t0);
		if (lwip_sendto(sock, snd, sizeof(snd), 0,
						(struct sockaddr *) &to, sizeof(to)) < 0) {
			pbeg(); ps("RPROBE R3 ping sendto **fail** "); ps(tag);
			pkv("seq", (uint32_t) seq); pnl();
			continue;
		}

		{
			bool		got = false;
			uint32_t	other = 0U;
			uint32_t	last_type = 0xFFU;

			for (;;) {
				SYSTIM		tnow = 0;
				uint32_t	el_ms;
				int			tmo_left;

				(void) get_tim(&tnow);
				el_ms = (uint32_t)((tnow - t0) / 1000U);
				if (el_ms >= tmo_ms) { break; }

				/*  **int（ミリ秒）**。上の setsockopt と同じ理由  */
				tmo_left = (int)(tmo_ms - el_ms);
				(void) lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
									   &tmo_left, sizeof(tmo_left));

				n = lwip_recv(sock, rcv, sizeof(rcv), 0);
				(void) get_tim(&t1);
				if (n < (int)(sizeof(struct ip_hdr)
							  + sizeof(struct icmp_echo_hdr))) {
					/*  タイムアウト（n<0）か短すぎる。**どちらも打ち切る**  */
					if (n >= 0) { other++; }
					break;
				}
				{
					struct ip_hdr			*iph = (struct ip_hdr *)(void *) rcv;
					uint16_t				hl = (uint16_t) IPH_HL_BYTES(iph);
					struct icmp_echo_hdr	*rj =
						(struct icmp_echo_hdr *)(void *)(rcv + hl);

					if ((uint16_t) n < (hl + sizeof(*rj))) { other++; continue; }
					last_type = (uint32_t) ICMPH_TYPE(rj);
					if ((ICMPH_TYPE(rj) == ICMP_ER)
							&& (lwip_ntohs(rj->id) == NP_PING_ID)
							&& (lwip_ntohs(rj->seqno) == (uint16_t) seq)) {
						got = true;
						break;
					}
					other++;
				}
			}

			if (got) {
				replies++;
				pbeg(); ps("RPROBE R3 ping reply "); ps(tag);
				target_fput_log(' ');
				pkv("seq", (uint32_t) seq);
				pkv("bytes", (uint32_t) n);
				pkv("rtt_us", (uint32_t)(t1 - t0));
				pkv("dropped", other);
				pnl();
			}
			else {
				/*  **0 でも印字する**（「無かった」と「数えていない」を分ける）  */
				pbeg(); ps("RPROBE R3 ping **no reply** "); ps(tag);
				target_fput_log(' ');
				pkv("seq", (uint32_t) seq);
				pkv("dropped", other); pkx("last_icmp_type", last_type);
				pnl();
			}
		}
		(void) dly_tsk(200U * 1000U);
	}
	(void) lwip_close(sock);
	pbeg(); ps("RPROBE R3 ping result "); ps(tag); target_fput_log(' ');
	pkv("sent", (uint32_t) count); pkv("replies", (uint32_t) replies); pnl();
	return(replies);
}

/*
 *  ----------------------------------------------------------------------------
 *  TCP（段 7f・F3。**7e までは 1 度も張っていない**）
 *  ----------------------------------------------------------------------------
 *  【なぜ TCP を別立てで撃つか】ping と DHCP は 1 パケット単位で完結する。
 *  TCP は **再送・順序・ウィンドウ**を使うので、
 *    - 送信の**取りこぼし**（7e で潰した `rp_tx[640]` の潜在バグの類）
 *    - 受信の**取り違え**（束ねて来るパケットの歩き方）
 *  がここで初めて露見し得る。⇒ 「ping が通る」は「TCP が通る」を意味しない。
 *
 *  【秘密の扱い】**ペイロードを印字しない。** 送ったバイト数・受けたバイト数・
 *  一致したか、だけを出す。エコーの内容は自分が送った定数なので秘密ではないが、
 *  「フレームを生で出す道具」をここに作らない（7c §9-1 の事故の再発防止）。
 */
static void
np_sock_tmo(int sock, uint32_t tmo_ms)
{
	int		tmo = (int) tmo_ms;		/* **int（ミリ秒）**。ping の (B) と同じ */

	(void) lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
	(void) lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
}

int
p4hosted_netops_tcp_echo(uint32_t server_be, uint16_t port, uint32_t tmo_ms,
						 const char *tag)
{
	static const char	msg[] = "HELLO-FROM-FMP3-P4-HOSTED-TCP\n";
	const uint32_t		mlen = (uint32_t)(sizeof(msg) - 1U);
	int					sock;
	int					rc;
	char				buf[64];
	uint32_t			got = 0U;
	SYSTIM				t0 = 0, t1 = 0;
	bool				same = false;

	sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		pbeg(); ps("RPROBE R5-a tcp socket **fail** "); ps(tag); pnl();
		return(0);
	}
	np_sock_tmo(sock, tmo_ms);

	{
		struct sockaddr_in	sa;

		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = lwip_htons(port);
		sa.sin_addr.s_addr = server_be;

		(void) get_tim(&t0);
		rc = lwip_connect(sock, (struct sockaddr *) &sa, sizeof(sa));
	}
	if (rc != 0) {
		(void) get_tim(&t1);
		/*  **失敗も数字で出す**（negative control がここに来る）  */
		pbeg(); ps("RPROBE R5-a tcp connect **fail** "); ps(tag);
		target_fput_log(' ');
		ps("dst="); np_put_ip(server_be); target_fput_log(':');
		pu((uint32_t) port); target_fput_log(' ');
		pkv("errno", (uint32_t) errno);
		pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
		pnl();
		(void) lwip_close(sock);
		return(0);
	}

	if (lwip_send(sock, msg, mlen, 0) < 0) {
		pbeg(); ps("RPROBE R5-a tcp send **fail** "); ps(tag);
		pkv("errno", (uint32_t) errno); pnl();
		(void) lwip_close(sock);
		return(0);
	}

	/*  **期限まで読む**（1 回の recv で全部来るとは限らない）  */
	while (got < mlen) {
		SYSTIM		tn = 0;
		int			n;

		(void) get_tim(&tn);
		if ((uint32_t)((tn - t0) / 1000U) >= tmo_ms) { break; }
		n = lwip_recv(sock, &buf[got], (int)(sizeof(buf) - 1U - got), 0);
		if (n <= 0) { break; }
		got += (uint32_t) n;
	}
	(void) get_tim(&t1);
	if (got == mlen) {
		same = (memcmp(buf, msg, mlen) == 0);
	}
	(void) lwip_close(sock);

	pbeg(); ps("RPROBE R5-a tcp echo "); ps(tag); target_fput_log(' ');
	ps("dst="); np_put_ip(server_be); target_fput_log(':');
	pu((uint32_t) port); target_fput_log(' ');
	pkv("sent", mlen); pkv("recv", got);
	pkv("match", (uint32_t)(same ? 1U : 0U));
	pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
	ps("(**内容は印字しない**)");
	pnl();
	return(same ? 1 : 0);
}

uint32_t
p4hosted_netops_tcp_send(uint32_t server_be, uint16_t port, uint32_t total,
						 uint32_t tmo_ms, uint32_t *p_ms, const char *tag)
{
	static uint8_t	chunk[1024];
	int				sock;
	int				rc;
	uint32_t		sent = 0U;
	uint32_t		ms = 0U;
	SYSTIM			t0 = 0, t1 = 0;

	if (p_ms != NULL) { *p_ms = 0U; }

	/*  中身は問わない（**秘密を含まない定数**）。0 埋めだと圧縮の余地を疑われるので通番を入れる  */
	{
		uint32_t	i;

		for (i = 0U; i < sizeof(chunk); i++) { chunk[i] = (uint8_t)(i & 0xFFU); }
	}

	sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		pbeg(); ps("RPROBE R5-b tcp_send socket **fail** "); ps(tag); pnl();
		return(0U);
	}
	np_sock_tmo(sock, tmo_ms);
	{
		struct sockaddr_in	sa;

		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_port = lwip_htons(port);
		sa.sin_addr.s_addr = server_be;
		rc = lwip_connect(sock, (struct sockaddr *) &sa, sizeof(sa));
	}
	if (rc != 0) {
		pbeg(); ps("RPROBE R5-b tcp_send connect **fail** "); ps(tag);
		pkv("errno", (uint32_t) errno); pnl();
		(void) lwip_close(sock);
		return(0U);
	}

	(void) get_tim(&t0);
	while (sent < total) {
		uint32_t	want = total - sent;
		int			n;

		if (want > sizeof(chunk)) { want = (uint32_t) sizeof(chunk); }
		n = lwip_send(sock, chunk, (int) want, 0);
		if (n <= 0) {
			pbeg(); ps("RPROBE R5-b tcp_send **止まった** "); ps(tag);
			target_fput_log(' ');
			pkv("sent", sent); pkv("errno", (uint32_t) errno); pnl();
			break;
		}
		sent += (uint32_t) n;
	}
	(void) get_tim(&t1);
	ms = (uint32_t)((t1 - t0) / 1000U);
	(void) lwip_close(sock);
	if (p_ms != NULL) { *p_ms = ms; }

	pbeg(); ps("RPROBE R5-b tcp_send "); ps(tag); target_fput_log(' ');
	ps("dst="); np_put_ip(server_be); target_fput_log(':');
	pu((uint32_t) port); target_fput_log(' ');
	pkv("bytes", sent); pkv("elapsed_ms", ms);
	/*
	 *  kbit/s を整数で出す（`bytes*8/ms` = bit/ms = kbit/s）。
	 *  **速い遅いの評価はしない**——比較対象を持っていない（AC §0）。
	 */
	pkv("kbit_per_s", (ms > 0U) ? ((sent / ms) * 8U) : 0U);
	pnl();
	return(sent);
}

/*
 *  ============================================================================
 *  段 7g: UDP（**7f までは 1 度も送っていない**）
 *  ============================================================================
 *  【なぜ TCP の次に UDP なのか。「もう TCP が通ったから簡単」ではない】
 *  TCP は lwIP が再送・順序・ウィンドウを持つので、**下の層が 1 通落としても
 *  上では見えない**。UDP にはそれが無い——1 通のデータグラムが、そのまま
 *  esp-hosted の TLV に載り、SDIO の 512 B ブロックに載り、C6 の Wi-Fi から出て、
 *  返ってくる。⇒ **取りこぼしがあれば、そのまま欠ける。**
 *
 *  だから本段は 2 つを別に測る:
 *    (a) **往復するか**（W1-a / W1-c）——判定に使う
 *    (b) **連射して何通返るか**（W1-b）——**数を記録するだけ**。期待値を置かない
 *        （欠けた分が実装の欠陥か無線区間の性質かを、この道具では分けられない）
 */
static int
np_udp_open(uint32_t tmo_ms)
{
	int		sock;

	sock = lwip_socket(AF_INET, SOCK_DGRAM, 0);
	if (sock >= 0) {
		np_sock_tmo(sock, tmo_ms);
	}
	return(sock);
}

static void
np_sa_init(struct sockaddr_in *sa, uint32_t server_be, uint16_t port)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_port = lwip_htons(port);
	sa->sin_addr.s_addr = server_be;
}

int
p4hosted_netops_udp_echo(uint32_t server_be, uint16_t port, uint32_t tmo_ms,
						 const char *tag)
{
	static const char	msg[] = "HELLO-FROM-FMP3-P4-HOSTED-UDP\n";
	const uint32_t		mlen = (uint32_t)(sizeof(msg) - 1U);
	struct sockaddr_in	sa;
	char				buf[64];
	int					sock;
	int					n;
	uint32_t			got = 0U;
	bool				same = false;
	SYSTIM				t0 = 0, t1 = 0;

	sock = np_udp_open(tmo_ms);
	if (sock < 0) {
		pbeg(); ps("RPROBE R7-a udp socket **fail** "); ps(tag); pnl();
		return(0);
	}
	np_sa_init(&sa, server_be, port);

	(void) get_tim(&t0);
	n = lwip_sendto(sock, msg, (int) mlen, 0,
					(struct sockaddr *) &sa, sizeof(sa));
	if (n != (int) mlen) {
		(void) get_tim(&t1);
		pbeg(); ps("RPROBE R7-a udp sendto **fail** "); ps(tag);
		target_fput_log(' ');
		pkv("rc", (uint32_t)(n < 0 ? 0U : (uint32_t) n));
		pkv("errno", (uint32_t) errno); pnl();
		(void) lwip_close(sock);
		return(0);
	}

	/*
	 *  **1 回だけ recvfrom する**（TCP と違い、UDP はデータグラム単位で届くので
	 *  「分割されて 2 回来る」形が無い）。相手が居なければ ICMP port unreachable が
	 *  返り、`recvfrom` は `ECONNREFUSED` で落ちる——**NC はここに来る**。
	 */
	n = lwip_recvfrom(sock, buf, (int) sizeof(buf), 0, NULL, NULL);
	(void) get_tim(&t1);
	if (n > 0) {
		got = (uint32_t) n;
		if (got == mlen) { same = (memcmp(buf, msg, mlen) == 0); }
	}
	(void) lwip_close(sock);

	pbeg(); ps("RPROBE R7-a udp echo "); ps(tag); target_fput_log(' ');
	ps("dst="); np_put_ip(server_be); target_fput_log(':');
	pu((uint32_t) port); target_fput_log(' ');
	pkv("sent", mlen); pkv("recv", got);
	pkv("match", (uint32_t)(same ? 1U : 0U));
	pkv("errno", (uint32_t)((n > 0) ? 0U : (uint32_t) errno));
	pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
	ps("(**内容は印字しない**)");
	pnl();
	return(same ? 1 : 0);
}

int
p4hosted_netops_udp_burst(uint32_t server_be, uint16_t port, uint32_t count,
						  uint32_t len, uint32_t tmo_ms, uint32_t *p_sent,
						  const char *tag)
{
	static uint8_t		dg[128];
	struct sockaddr_in	sa;
	char				buf[160];
	int					sock;
	uint32_t			i;
	uint32_t			sent = 0U;
	uint32_t			recvd = 0U;
	uint32_t			bad = 0U;
	uint32_t			nosend = 0U;
	SYSTIM				t0 = 0, t1 = 0;

	if (p_sent != NULL) { *p_sent = 0U; }
	if (len > sizeof(dg)) { len = (uint32_t) sizeof(dg); }
	if (len < 4U)		 { len = 4U; }

	sock = np_udp_open(tmo_ms);
	if (sock < 0) {
		pbeg(); ps("RPROBE R7-b udp_burst socket **fail** "); ps(tag); pnl();
		return(0);
	}
	np_sa_init(&sa, server_be, port);

	(void) get_tim(&t0);
	for (i = 0U; i < count; i++) {
		int		n;

		/*
		 *  **先頭 4 バイトに通番を入れる**（中身は秘密を含まない）。
		 *  返ってきた物の通番を見るので、「1 通だけ来て 100 回数えた」形にならない。
		 */
		dg[0] = (uint8_t)(i & 0xFFU);
		dg[1] = (uint8_t)((i >> 8) & 0xFFU);
		dg[2] = 0x7EU;
		dg[3] = 0x7EU;
		{
			uint32_t	k;

			for (k = 4U; k < len; k++) { dg[k] = (uint8_t)(k & 0xFFU); }
		}

		n = lwip_sendto(sock, dg, (int) len, 0,
						(struct sockaddr *) &sa, sizeof(sa));
		if (n != (int) len) {
			/*
			 *  **送れなかった 1 通の返信を待たない。**
			 *  待つと、経路が落ちているときに「1 通あたり `tmo_ms`」を 100 回
			 *  積んでしまう（2026-08-18 の実測で 100 秒を空費した。
			 *  採取窓を食い潰す形なので、これは判定以前の欠陥である）。
			 */
			nosend++;
			continue;
		}
		sent++;

		/*
		 *  **1 通ごとに待つ**（撃ちっ放しにすると、返信が受信キューに溜まり切れず
		 *  「実装が落とした」のか「自分のキューが溢れた」のか分けられなくなる）。
		 */
		n = lwip_recvfrom(sock, buf, (int) sizeof(buf), 0, NULL, NULL);
		if (n == (int) len) {
			uint32_t	seq = (uint32_t) buf[0] & 0xFFU;

			seq |= ((uint32_t) buf[1] & 0xFFU) << 8;
			if (seq == i) { recvd++; } else { bad++; }
		}
		else if (n > 0) {
			bad++;
		}
		else {
			/*  時間切れ or 拒否。**欠けた 1 通**として数える（何も足さない）  */
		}
	}
	(void) get_tim(&t1);
	(void) lwip_close(sock);
	if (p_sent != NULL) { *p_sent = sent; }

	pbeg(); ps("RPROBE R7-b udp_burst "); ps(tag); target_fput_log(' ');
	ps("dst="); np_put_ip(server_be); target_fput_log(':');
	pu((uint32_t) port); target_fput_log(' ');
	pkv("count", count); pkv("len", len);
	pkv("sent", sent); pkv("recv", recvd);
	pkv("send_fail", nosend);
	pkv("mismatch_seq", bad);
	pkv("lost", (uint32_t)((sent > recvd) ? (sent - recvd) : 0U));
	pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
	ps("(**期待値は置かない**: 段 7g AC W1-b)");
	pnl();
	return((int) recvd);
}

/*
 *  ============================================================================
 *  段 7g: HTTP GET（**アプリケーション層まで届いたことの実演**）
 *  ============================================================================
 *  【7f の TCP エコーと何が違うのか】エコーは「送った物が返る」だけで、
 *  **相手が中身を解釈していない**。HTTP は相手がこちらの送った行を**構文解析して
 *  応答を組み立てる**ので、こちらが出したバイト列が正しいことまで込みで確かめられる。
 *
 *  【判定器が生きていることの実演（W3-c）】存在しないパスへ**同じ判定器**を通す。
 *  404 が返るなら、判定器はステータス行を**実際に読んでいる**——「200 が返った」を
 *  常に答える判定器ではない。
 *
 *  **本文は先頭の数バイトしか印字しない。** 全文を出す道具をここに作らない。
 */
#define NP_HTTP_HDR		512U
#define NP_HTTP_PEEK	16U

static uint32_t
np_dec(const char *p, uint32_t n)
{
	uint32_t	v = 0U;
	uint32_t	i;

	for (i = 0U; i < n; i++) {
		if ((p[i] < '0') || (p[i] > '9')) { break; }
		v = (v * 10U) + (uint32_t)(p[i] - '0');
	}
	return(v);
}

int
p4hosted_netops_http_get(uint32_t server_be, uint16_t port, const char *path,
						 uint32_t tmo_ms, uint32_t *p_clen, uint32_t *p_body,
						 const char *tag)
{
	static char			hdr[NP_HTTP_HDR];
	static char			req[192];
	struct sockaddr_in	sa;
	int					sock;
	int					rc;
	int					status = 0;
	uint32_t			clen = 0U;
	uint32_t			body = 0U;
	uint32_t			hn = 0U;
	uint32_t			hdr_end = 0U;		/* '\r\n\r\n' の直後 */
	bool				hdr_done = false;
	char				peek[NP_HTTP_PEEK + 1U];
	uint32_t			peek_n = 0U;
	SYSTIM				t0 = 0, t1 = 0;

	if (p_clen != NULL) { *p_clen = 0U; }
	if (p_body != NULL) { *p_body = 0U; }

	/*  要求を組む。**`Connection: close`** にして、本文の終端を切断で知る  */
	{
		uint32_t	k = 0U;
		const char	*s;

		for (s = "GET "; *s != '\0'; s++) { req[k++] = *s; }
		for (s = path;   *s != '\0'; s++) { req[k++] = *s; }
		for (s = " HTTP/1.0\r\nHost: fmp3-p4\r\nConnection: close\r\n\r\n";
			 *s != '\0'; s++) {
			req[k++] = *s;
		}
		req[k] = '\0';
		hn = k;
	}

	sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		pbeg(); ps("RPROBE R8-a http socket **fail** "); ps(tag); pnl();
		return(0);
	}
	np_sock_tmo(sock, tmo_ms);
	np_sa_init(&sa, server_be, port);

	(void) get_tim(&t0);
	rc = lwip_connect(sock, (struct sockaddr *) &sa, sizeof(sa));
	if (rc != 0) {
		(void) get_tim(&t1);
		pbeg(); ps("RPROBE R8-a http connect **fail** "); ps(tag);
		target_fput_log(' ');
		ps("dst="); np_put_ip(server_be); target_fput_log(':');
		pu((uint32_t) port); target_fput_log(' ');
		pkv("errno", (uint32_t) errno);
		pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U)); pnl();
		(void) lwip_close(sock);
		return(0);
	}
	if (lwip_send(sock, req, (int) hn, 0) != (int) hn) {
		pbeg(); ps("RPROBE R8-a http send **fail** "); ps(tag);
		pkv("errno", (uint32_t) errno); pnl();
		(void) lwip_close(sock);
		return(0);
	}

	/*
	 *  ヘッダが `hdr[]` に収まるまで読み、ヘッダ終端の後は**数えるだけ**にする
	 *  （本文を貯めない＝RAM を食わないし、全文が出る経路も作らない）。
	 */
	hn = 0U;
	for (;;) {
		char		tmp[256];
		int			n;
		uint32_t	i;

		n = lwip_recv(sock, tmp, (int) sizeof(tmp), 0);
		if (n <= 0) { break; }			/* close か時間切れ */

		for (i = 0U; i < (uint32_t) n; i++) {
			if (!hdr_done) {
				if (hn < (NP_HTTP_HDR - 1U)) { hdr[hn++] = tmp[i]; }
				if ((hn >= 4U)
					&& (hdr[hn - 4U] == '\r') && (hdr[hn - 3U] == '\n')
					&& (hdr[hn - 2U] == '\r') && (hdr[hn - 1U] == '\n')) {
					hdr_done = true;
					hdr_end = hn;
				}
			}
			else {
				if (peek_n < NP_HTTP_PEEK) { peek[peek_n++] = tmp[i]; }
				body++;
			}
		}
	}
	(void) get_tim(&t1);
	(void) lwip_close(sock);
	(void) hdr_end;

	/*  ステータス行: `HTTP/1.x NNN ...`  */
	if (hn >= 12U) {
		if ((hdr[0] == 'H') && (hdr[1] == 'T') && (hdr[2] == 'T')
			&& (hdr[3] == 'P') && (hdr[4] == '/')) {
			status = (int) np_dec(&hdr[9], 3U);
		}
	}
	/*  `Content-Length:`（PC 側の台本が返す綴りに合わせる。無ければ 0）  */
	{
		static const char	key[] = "Content-Length: ";
		const uint32_t		klen = (uint32_t)(sizeof(key) - 1U);
		uint32_t			i;

		if (hn > klen) {
			for (i = 0U; i <= (hn - klen); i++) {
				if (memcmp(&hdr[i], key, klen) == 0) {
					clen = np_dec(&hdr[i + klen], 10U);
					break;
				}
			}
		}
	}
	if (p_clen != NULL) { *p_clen = clen; }
	if (p_body != NULL) { *p_body = body; }

	pbeg(); ps("RPROBE R8-a http get "); ps(tag); target_fput_log(' ');
	ps("dst="); np_put_ip(server_be); target_fput_log(':');
	pu((uint32_t) port); ps(" path="); ps(path); target_fput_log(' ');
	pkv("status", (uint32_t) status);
	pkv("content_length", clen);
	pkv("body_read", body);
	pkv("hdr_bytes", hn);
	pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
	/*  **本文は先頭 16 バイトだけ**（全文ダンプはしない。AC W3）  */
	ps("body_head=\"");
	{
		uint32_t	i;

		for (i = 0U; i < peek_n; i++) {
			char	c = peek[i];

			target_fput_log(((c >= 0x20) && (c < 0x7F)) ? c : '.');
		}
	}
	target_fput_log('"');
	pnl();
	return(status);
}

/*
 *  ----------------------------------------------------------------------------
 *  送信経路そのものを試す最小の道具: **生の ARP 要求**（lwIP を通さない）
 *  ----------------------------------------------------------------------------
 *  【なぜ要るか】DHCP が成立しないとき、原因は 2 つに分かれる:
 *    (i)  こちらの送信がそもそも電波に出ていない
 *    (ii) 出ているが DHCP サーバが応じていない
 *  lwIP 経由の観測だけでは分けられない。**42 バイトの ARP 要求を手で組んで
 *  同じ送信経路へ流し、PC 側（tcpdump / `ip neigh`）で見る**と切り分けられる。
 *  ARP は IP アドレスの割当てを必要としない（送信元 IP に 0.0.0.0 を使う
 *  ＝ ARP Probe。RFC 5227 の形）ので、DHCP 前でも撃てる。
 *
 *  **秘密は 1 バイトも含まない**（MAC と IP だけ）。
 */
int
p4hosted_netops_raw_arp(uint32_t target_be, uint32_t count)
{
	uint8_t		f[42];
	uint8_t		mac[6];
	uint32_t	i;
	int			ok = 0;

	if (!p4hosted_net_get_mac(mac)) {
		pbeg(); ps("RPROBE R3-x arp **MAC 未取得**"); pnl();
		return(0);
	}

	memset(f, 0, sizeof(f));
	memset(&f[0], 0xFF, 6);				/* dst = broadcast */
	memcpy(&f[6], mac, 6);				/* src = 自分 */
	f[12] = 0x08; f[13] = 0x06;			/* EtherType = ARP */
	f[14] = 0x00; f[15] = 0x01;			/* HTYPE = Ethernet */
	f[16] = 0x08; f[17] = 0x00;			/* PTYPE = IPv4 */
	f[18] = 6;    f[19] = 4;			/* HLEN / PLEN */
	f[20] = 0x00; f[21] = 0x01;			/* OPER = request */
	memcpy(&f[22], mac, 6);				/* SHA */
	/*  SPA = 0.0.0.0（ARP Probe）。**でっち上げの IP を名乗らない**  */
	/*  THA = 0（未知） */
	f[38] = (uint8_t)((target_be >> 0) & 0xFFU);
	f[39] = (uint8_t)((target_be >> 8) & 0xFFU);
	f[40] = (uint8_t)((target_be >> 16) & 0xFFU);
	f[41] = (uint8_t)((target_be >> 24) & 0xFFU);

	for (i = 0U; i < count; i++) {
		uint32_t	av0 = 0U, av1 = 0U;
		int			rc;

		(void) p4hosted_net_xport_credit(&av0);
		rc = p4hosted_net_tx(f, (uint16_t) sizeof(f));
		if (rc == 0) { ok++; }
		(void) dly_tsk(100U * 1000U);
		(void) p4hosted_net_xport_credit(&av1);
		pbeg(); ps("RPROBE R3-x arp_tx ");
		pkv("i", i); pkv("rc", (uint32_t)(rc == 0 ? 0U : 1U));
		pkv("avail_before", av0); pkv("avail_after", av1);
		pkv("host_txcnt", p4hosted_net_xport_txcnt());
		ps("(after > before なら**スレーブが消費して返した**)");
		pnl();
		(void) dly_tsk(200U * 1000U);
	}
	pbeg(); ps("RPROBE R3-x raw_arp sent_ok "); pkv("n", (uint32_t) ok);
	pkv("of", count);
	ps(" (PC 側で `tcpdump arp and ether src <STA MAC>` を見ること)");
	pnl();
	return(ok);
}

/*
 *  ----------------------------------------------------------------------------
 *  立ち上げ本体
 *  ----------------------------------------------------------------------------
 *  戻り値は「DHCP でアドレスが取れたか」。
 */
bool
p4hosted_netops_bringup(uint32_t probe_be)
{
	uint32_t	waited_ms = 0U;

	/*  受信ポンプを起こす（**7d R1 の修正が効いていなければここで止まる**）  */
	if (!p4hosted_net_rx_thread_start()) {
		return(false);
	}

	/*  lwIP を起こして netif を足す（tcpip_thread は lwIP 自身のプールで作られる）  */
	netif_esp_hosted_start();
	(void) dly_tsk(200U * 1000U);

	/*  リンクアップ通知 -> rx コールバック登録 + netif up + DHCP 開始  */
	netif_esp_hosted_notify_link(true);

	/*  **有限回ループの同期待ち**（sleep 放置にしない）  */
	while ((netif_esp_hosted_get_ipaddr() == 0U) && (waited_ms < 30000U)) {
		(void) dly_tsk(100U * 1000U);
		waited_ms += 100U;
		if ((waited_ms % 5000U) == 0U) {
			uint32_t	rt = 0U, pf = 0U, inf = 0U;
			uint32_t	av = 0U;

			netif_esp_hosted_get_rx_stats(&rt, &pf, &inf);
			(void) p4hosted_net_xport_credit(&av);
			pbeg(); ps("RPROBE R2-c dhcp_wait ");
			pkv("ms", waited_ms);
			pkv("rx_total", rt); pkv("pbuf_fail", pf); pkv("input_fail", inf);
			pkv("tx_ok", p4hosted_net_n_tx_ok);
			pkv("tx_fail", p4hosted_net_n_tx_fail);
			pkv("rx_frame", p4hosted_net_n_rx_frame);
			pkv("rx_nocb", p4hosted_net_n_rx_nocb);
			pkv("pump_loops", p4hosted_net_rx_loops());
			pkv("slave_avail", av);
			pkv("host_txcnt", p4hosted_net_xport_txcnt());
			pnl();
			p4hosted_net_frame_stats();
		}
	}

	pbeg(); ps("RPROBE R2-c dhcp ");
	ps("ip=");   np_put_ip(netif_esp_hosted_get_ipaddr());
	ps(" mask="); np_put_ip(netif_esp_hosted_get_netmask());
	ps(" gw=");  np_put_ip(netif_esp_hosted_get_gw());
	target_fput_log(' ');
	pkv("lease_sec", netif_esp_hosted_get_lease_sec());
	pkv("waited_ms", waited_ms);
	pnl();

	if (netif_esp_hosted_get_ipaddr() == 0U) {
		/*
		 *  DHCP が成立しなかった。**「動かない」で終わらせず、送信経路が
		 *  電波に出ているかどうかを分けて測る**（`p4hosted_netops_raw_arp` 参照）。
		 */
		(void) p4hosted_netops_raw_arp(probe_be, 10U);
		p4hosted_net_frame_stats();
	}

	return(netif_esp_hosted_get_ipaddr() != 0U);
}
