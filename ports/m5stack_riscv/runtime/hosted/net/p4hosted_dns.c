/*
 *  ESP32-P4 + ESP-Hosted — DNS（段 7g・W2）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  このファイルは `-DA1_P4_HOSTED_DNS=ON` のときだけコンパイルされる
 *  ============================================================================
 *  DNS を有効にするには lwIP の設定（`LWIP_DNS`）を変える必要があり、その設定は
 *  **取込み物**（`esp/eth/lwip_port/include/lwipopts.h`）に書かれている。
 *  取込み物は 1 バイトも触らない方針なので、**探索路の手前に重ね書きヘッダを置く**
 *  形にした——理由と手順の正本は `esp/p4hosted/net/lwipopts_dns/lwipopts.h` の冒頭。
 *
 *  ⇒ **既定（`A1_P4_HOSTED_DNS=OFF`）では、このファイルはリンクされない。**
 *    段 7f までの構成は 1 バイトも変わらない。
 *
 *  ============================================================================
 *  何を確かめる道具なのか（**「名前が引けた」を成果と呼ばないために**）
 *  ============================================================================
 *  DNS が通ることは、実は**下の層が全部通っていること**の言い換えでもある:
 *    - UDP のデータグラムが**外向きに**（GW の先の DNS サーバへ）出て行く
 *    - **戻りの経路**（NAT でない素の LAN なので、DNS サーバからの応答が
 *      自分の一時ポートへ届く）が生きている
 *    - lwIP の DNS 状態機械が、タイムアウトせずに応答を食える
 *  ⇒ だから **NC（実在しない名前）を同じ判定器に通すまでは、何も言えない**。
 *    「常に成功と答える判定器」は、上の 3 つが 1 つも成立していなくても
 *    「解決できました」と言える。
 */
#include <kernel.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4hosted_prt.h"
#include "p4hosted_netops.h"

#include "lwip/api.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

/*
 *  ----------------------------------------------------------------------------
 *  `LWIP_RAND` の実体（lwIP が DNS の問合せ ID とソースポートに使う）
 *  ----------------------------------------------------------------------------
 *  **暗号論的に安全ではない。** ここで要るのは「毎回同じ値を返さない」ことまでで、
 *  DNS 応答の偽装に耐える強度は**持っていない**（本 repo の用途は閉じた実験である）。
 *  外向きの本番用途に使うなら、ハードウェア乱数源（P4 の RNG）へ差し替えること。
 *
 *  種の混ぜ方:
 *    - xorshift32（周期 2^32-1・自己完結）
 *    - **タスク文脈でだけ** `get_tim()`（マイクロ秒）を混ぜる。
 *      lwIP は tcpip スレッド（＝タスク）から呼ぶので普通は混ざるが、
 *      **非タスク文脈から呼ばれても壊れない**ようにしておく
 *      （`get_tim()` は非タスク文脈で `E_CTX` を返す。それを黙って無視して
 *        未初期化の値を混ぜる、という形を作らない）。
 */
static uint32_t	p4h_rand_state = 0x2463534AU;	/* 0 以外なら何でもよい */

uint32_t
p4hosted_lwip_rand(void)
{
	uint32_t	x = p4h_rand_state;

	if (!sns_ctx()) {
		SYSTIM		now = 0;

		if (get_tim(&now) == E_OK) {
			x ^= (uint32_t) now;
		}
	}
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	if (x == 0U) { x = 0x9E3779B9U; }		/* 0 に落ちると以後ずっと 0 */
	p4h_rand_state = x;
	return(x);
}

/*
 *  ----------------------------------------------------------------------------
 *  DNS サーバの取得・指定
 *  ----------------------------------------------------------------------------
 *  DHCP が DNS サーバを配っていれば lwIP が自動で登録する
 *  （`dhcp.c:813` の `dns_setserver()`。`LWIP_DHCP_PROVIDE_DNS_SERVERS`）。
 *  配らなかったときのために、明示指定の口も出しておく（GW を差す等）。
 */
uint32_t
p4hosted_netops_dns_getserver(uint8_t idx)
{
	const ip_addr_t		*a = dns_getserver(idx);

	if (a == NULL) { return(0U); }
	return(ip_2_ip4(a)->addr);
}

void
p4hosted_netops_dns_setserver(uint8_t idx, uint32_t addr_be)
{
	ip_addr_t	a;

	ip_addr_set_ip4_u32(&a, addr_be);
	dns_setserver(idx, &a);
}

/*
 *  ----------------------------------------------------------------------------
 *  解決本体
 *  ----------------------------------------------------------------------------
 *  `netconn_gethostbyname()` を使う（`api_lib.c`）。**tcpip スレッドへ渡して
 *  待つ**形なので、呼び手のスレッドから安全に呼べる
 *  ——`dns_gethostbyname()` を直に呼ぶと lwIP のコアロック規約に反する。
 *
 *  時間切れは lwIP の DNS 側（`DNS_MAX_RETRIES` と内部タイマ）が持つので、
 *  `tmo_ms` は**印字のための目安**として受け取るだけである。
 *  ⇒ **「自分で待ち時間を決めているように見せない」**（実際には決めていない）。
 */
int
p4hosted_netops_dns_resolve(const char *name, uint32_t tmo_ms, uint32_t *p_addr,
							const char *tag)
{
	ip_addr_t	addr;
	err_t		e;
	uint32_t	a4 = 0U;
	SYSTIM		t0 = 0, t1 = 0;

	if (p_addr != NULL) { *p_addr = 0U; }
	(void) tmo_ms;

	memset(&addr, 0, sizeof(addr));
	(void) get_tim(&t0);
	e = netconn_gethostbyname(name, &addr);
	(void) get_tim(&t1);

	if (e == ERR_OK) {
		a4 = ip_2_ip4(&addr)->addr;
	}
	if (p_addr != NULL) { *p_addr = a4; }

	pbeg(); ps("RPROBE R9-a dns "); ps(tag); target_fput_log(' ');
	ps("name="); ps(name); target_fput_log(' ');
	pkv("err", (uint32_t)((e == ERR_OK) ? 0U : (uint32_t)(-e)));
	ps("addr=");
	{
		uint32_t	i;

		for (i = 0U; i < 4U; i++) {
			pu((a4 >> (8U * i)) & 0xFFU);
			if (i < 3U) { target_fput_log('.'); }
		}
	}
	target_fput_log(' ');
	pkv("elapsed_ms", (uint32_t)((t1 - t0) / 1000U));
	pnl();

	/*
	 *  **「err が 0 だった」ではなく「番地が取れた」を成功にする。**
	 *  0.0.0.0 を返して ERR_OK と言う実装があっても、それは解決ではない。
	 */
	return(((e == ERR_OK) && (a4 != 0U)) ? 1 : 0);
}
