/*
 *  ESP32-P4 + ESP-Hosted — 同期出力の小道具（実体）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  段 7f の括り出し（F2）で `app/rpc_probe/rpc_probe.c` から**移動**した。
 *  中身は 1 文字も変えていない（`static` を外し、名前を長い形にしただけ）。
 *  設計の意図・踏んだ罠は `p4hosted_prt.h` の冒頭コメントを参照。
 */

#include <kernel.h>
#include <stdint.h>

#include "p4hosted_prt.h"

void
p4hosted_prt_beg(void)
{
	(void) loc_cpu();
}

void
p4hosted_prt_nl(void)
{
	target_fput_log('\n');
	(void) unl_cpu();
}

void
p4hosted_prt_str(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

void
p4hosted_prt_u32(uint32_t v)
{
	char	buf[12];
	int		i = 0;

	if (v == 0U) { target_fput_log('0'); return; }
	while ((v != 0U) && (i < 11)) { buf[i++] = (char)('0' + (v % 10U)); v /= 10U; }
	while (i > 0) { target_fput_log(buf[--i]); }
}

void
p4hosted_prt_hex32(uint32_t v)
{
	static const char	hex[] = "0123456789abcdef";
	int					i;

	target_fput_log('0'); target_fput_log('x');
	for (i = 28; i >= 0; i -= 4) { target_fput_log(hex[(v >> i) & 0xFU]); }
}

void
p4hosted_prt_byte(uint8_t b)
{
	static const char	hex[] = "0123456789abcdef";

	target_fput_log(hex[(b >> 4) & 0xFU]);
	target_fput_log(hex[b & 0xFU]);
}

void
p4hosted_prt_kv(const char *k, uint32_t v)
{
	p4hosted_prt_str(k);
	target_fput_log('=');
	p4hosted_prt_u32(v);
	target_fput_log(' ');
}

void
p4hosted_prt_kx(const char *k, uint32_t v)
{
	p4hosted_prt_str(k);
	target_fput_log('=');
	p4hosted_prt_hex32(v);
	target_fput_log(' ');
}

void
p4hosted_prt_dump(const char *tag, const uint8_t *p, uint32_t n)
{
	uint32_t	i;

	p4hosted_prt_beg();
	p4hosted_prt_str("RPROBE raw ");
	p4hosted_prt_str(tag);
	target_fput_log(' ');
	p4hosted_prt_kv("n", n);
	for (i = 0U; i < n; i++) {
		if ((i % 32U) == 0U) {
			p4hosted_prt_nl();
			p4hosted_prt_beg();
			p4hosted_prt_str("RPROBE raw   ");
		}
		p4hosted_prt_byte(p[i]);
		target_fput_log(' ');
	}
	p4hosted_prt_nl();
}
