/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: 診断タスク（判定 I-6 / I-7 / I-10 / I-12 の採取）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜ別タスクなのか
 *  ============================================================================
 *  判定アプリ `esp/eth/app/fmp_eth_sta/fmp_eth_sta.c` は**取込み層**であり、
 *  段E との surface 比較（E-5）を成立させるために **1 バイトも変えない**。
 *  一方で seam でしか測れないもの（`__real_*` スタブが呼ばれた回数、
 *  C-1 シムがどの線を使ったか、シムの非対応カウンタ）は、どこかで出さないと
 *  「動いた」以上のことが分からない。
 *  ⇒ **アプリに触らず、cfg を 1 本足して独立したタスクで出す。**
 *
 *  出力は `target_fput_log()` 直呼びの同期（`p4shim_misc.c` の `p4shim_puts`）。
 *  `syslog()` にすると logtask の非同期出力に行が食われることがある
 *  （C-1 §5-4 の実測）。
 *
 *  **優先度は最低**（`P4SHIM_DIAG_PRI`）にしてある。判定アプリ・tcpip_thread・
 *  EMAC RX タスクのどれよりも後に走るので、測ることで測られる側を乱さない。
 */

#include <kernel.h>
#include <stdint.h>

#include "p4shim.h"
#include "esp_shim_intr_clic.h"
#include "p4shim_diag.h"

extern void target_fput_log(char c);

/*  ETS_ETH_MAC_INTR_SOURCE。C-1 の監査台本が esp-idf の enum と照合済み。 */
#define P4SHIM_SRC_ETH_MAC		92

static void
put_dec(uint32_t v)
{
	char	buf[12];
	int		i = 0;

	if (v == 0U) {
		target_fput_log('0');
		return;
	}
	while (v > 0U && i < (int) sizeof(buf)) {
		buf[i++] = (char) ('0' + (v % 10U));
		v /= 10U;
	}
	while (i > 0) {
		target_fput_log(buf[--i]);
	}
}

static void
put_str(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

/*
 *  C-1 シムの実利用実績（AC I-7）。
 *  **「配線した」を書いた側の主張ではなく、割込みマトリクスの MAP レジスタを
 *  読み返す**（C-1 §5-2 と同じ作法）。あわせて ISR の到達回数を出す。
 */
static void
p4shim_diag_clic(void)
{
	int			n = esp_shim_clic_intr_nslot();
	int			i;
	int			slot = esp_shim_clic_intr_slot_of_source(P4SHIM_SRC_ETH_MAC);
	uint32_t	map = esp_shim_clic_intr_read_map(P4SHIM_SRC_ETH_MAC);

	(void) loc_cpu();
	put_str("P4SHIM CLIC src=");
	put_dec((uint32_t) P4SHIM_SRC_ETH_MAC);
	put_str(" slot=");
	if (slot < 0) {
		put_str("-1");
	}
	else {
		put_dec((uint32_t) slot);
	}
	put_str(" map(読み返し)=");
	put_dec(map);
	put_str(" nslot=");
	put_dec((uint32_t) n);
	target_fput_log('\n');
	(void) unl_cpu();

	for (i = 0; i < n; i++) {
		int			src = 0;
		int			line = 0;
		uint32_t	n_isr = 0;
		uint32_t	n_call = 0;

		if (esp_shim_clic_intr_slot_info(i, &src, &line, &n_isr, &n_call) == 0) {
			continue;			/* 未使用スロットは出さない */
		}
		(void) loc_cpu();
		put_str("P4SHIM CLIC slot=");
		put_dec((uint32_t) i);
		put_str(" src=");
		put_dec((uint32_t) src);
		put_str(" line=");
		put_dec((uint32_t) line);
		put_str(" n_isr=");
		put_dec(n_isr);
		put_str(" n_call=");
		put_dec(n_call);
		target_fput_log('\n');
		(void) unl_cpu();
	}

	(void) loc_cpu();
	put_str("P4SHIM CLIC n_alloc=");
	put_dec(esp_shim_clic_intr_n_alloc);
	put_str(" n_alloc_fail=");
	put_dec(esp_shim_clic_intr_n_alloc_fail);
	put_str(" n_free=");
	put_dec(esp_shim_clic_intr_n_free);
	put_str(" n_ena_fail=");
	put_dec(esp_shim_clic_intr_n_ena_fail);
	put_str(" n_flag_iram=");
	put_dec(esp_shim_clic_intr_n_flag_iram);
	put_str(" n_flag_level=");
	put_dec(esp_shim_clic_intr_n_flag_level);
	put_str(" n_flag_edge=");
	put_dec(esp_shim_clic_intr_n_flag_edge);
	target_fput_log('\n');
	(void) unl_cpu();
}

/*
 *  cfg: `CRE_TSK(P4SHIM_DIAG_TASK, { TA_ACT, ... })`。
 *  判定アプリが一通り終わるのを待ってから 1 回出し、その後は一定間隔で出す
 *  （長時間の安定性を見るとき、カウンタが増え続けているかが分かるように）。
 */
#define P4SHIM_DIAG_FIRST_US	(25U * 1000U * 1000U)	/* 25 秒 */
#define P4SHIM_DIAG_EVERY_US	(30U * 1000U * 1000U)	/* 以後 30 秒ごと */

void
p4shim_diag_task(EXINF exinf)
{
	uint32_t	round = 0;

	(void) exinf;
	(void) dly_tsk((RELTIM) P4SHIM_DIAG_FIRST_US);

	for (;;) {
		(void) loc_cpu();
		put_str("P4SHIM ======== diag round ");
		put_dec(round);
		put_str(" ========");
		target_fput_log('\n');
		(void) unl_cpu();

		p4shim_diag_clic();
		p4shim_report();

		(void) loc_cpu();
		put_str("P4SHIM ======== diag end ");
		put_dec(round);
		put_str(" ========");
		target_fput_log('\n');
		(void) unl_cpu();

		round++;
		(void) dly_tsk((RELTIM) P4SHIM_DIAG_EVERY_US);
	}
}
