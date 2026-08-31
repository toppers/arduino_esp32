/*
 *  M5 スタブ踏破トレース 実体（M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  設計意図は m5_stub_trace.h の冒頭コメント参照。
 *
 *  ★依存を最小にしている（target_fput_log だけ）理由：本ファイルは
 *    seam XIP ビルド（root CMakeLists.txt の m5 分岐）と QEMU ハーネス
 *    （m5/app/m5_display_smoke/qemu/CMakeLists.txt）の**両方**にリンクされる。
 *    printf（m5_libcxx_glue.c の自前実装）は vsnprintf 経由でスタックを
 *    ~256B 使うため、ISR 文脈から呼ばれ得るスタブでは使わない。
 */

#include "m5_stub_trace.h"

/*
 *  ★S5：出力は m5_putc_now_ext（有界）を通す。target_fput_log 直だと
 *  A1_CONSOLE_USJ=OFF（実機 Port C 採取）の UART0 経路が**無界スピン**で、
 *  計装自身がハングし観測が消える（m5_stub_trace.h の宣言部コメント参照）。
 */

static m5_stub_site_t  *m5_stub_list;		/* 登録リスト先頭（踏んだ順の逆） */
static unsigned int		m5_stub_nsites;

static void
m5_stub_puts(const char *s)
{
	if (s == 0) {
		s = "(null)";
	}
	while (*s != '\0') {
		m5_putc_now_ext(*s++);
	}
}

static void
m5_stub_putu(unsigned int v)
{
	char			buf[11];
	unsigned int	i = 0U;

	if (v == 0U) {
		m5_putc_now_ext('0');
		return;
	}
	while ((v != 0U) && (i < sizeof(buf))) {
		buf[i++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (i > 0U) {
		m5_putc_now_ext(buf[--i]);
	}
}

void
m5_stub_hit(m5_stub_site_t *site)
{
	if (site == 0) {
		return;
	}
	site->count++;
	if (site->count == 1U) {
		/*  未登録＝初回。登録して即時 1 行出す。 */
		site->next = m5_stub_list;
		m5_stub_list = site;
		m5_stub_nsites++;
		m5_stub_puts("[STUB] first call: ");
		m5_stub_puts(site->name);
		m5_putc_now_ext('\n');
	}
}

void
m5_stub_trace_dump(const char *tag)
{
	m5_stub_site_t	*p;

	m5_stub_puts("[STUB] ==== ");
	m5_stub_puts(tag);
	m5_stub_puts(" (nsites=");
	m5_stub_putu(m5_stub_nsites);
	m5_stub_puts(") ====\n");
	for (p = m5_stub_list; p != 0; p = p->next) {
		m5_stub_puts("[STUB]   ");
		m5_stub_puts(p->name);
		m5_stub_puts(" x");
		m5_stub_putu(p->count);
		m5_putc_now_ext('\n');
	}
}

unsigned int
m5_stub_trace_nsites(void)
{
	return(m5_stub_nsites);
}
