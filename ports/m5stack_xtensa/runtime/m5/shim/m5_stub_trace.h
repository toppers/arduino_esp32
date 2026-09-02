/*
 *  M5 スタブ踏破トレース（M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ★存在理由（段階1 の教訓 /  §3「S2 の必須方針」）
 *    段階1 で 6 ラウンド誤診した真因 4 件（1-6 クロックゲート / 1-7 FSPID_IN /
 *    1-8 MCU_SEL / 1-16 pad_driver）は、**いずれも「実ドライバの一手順が黙って
 *    欠落していた」**ものだった。黙って ESP_OK を返すスタブは、その欠落を
 *    「正常」と区別できない形で隠す。
 *    ⇒ 本ヘッダの M5_STUB_HIT() を通ったスタブは
 *       (a) **初回呼び出しで即時に 1 行**（target_fput_log 直・logtask 非経由）出し、
 *       (b) 呼び出し回数を数え、m5_stub_trace_dump() で一括再掲できる。
 *    「呼ばれたのに何の痕跡も残らないスタブ」を構造的に作れなくするのが目的。
 *
 *  ★出力経路を target_fput_log 直にする理由
 *    FMP3 の syslog() は logtask 経由の遅延出力で、スタブの直後にハングすると
 *    **過去分ごと消える** §2-2）。停止位置の判定に使う行は
 *    即時出力でなければ意味を持たない。
 */

#ifndef M5_STUB_TRACE_H
#define M5_STUB_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  1 スタブ 1 個の記録枠。M5_STUB_HIT() が関数内 static として置く。
 *  初回ヒット時に登録リストへ繋がれる（登録順＝踏んだ順）。
 */
typedef struct m5_stub_site {
	const char			*name;		/* スタブ名（静的文字列） */
	unsigned int		 count;		/* 呼び出し回数（飽和なし・32bit） */
	struct m5_stub_site	*next;		/* 登録リスト。NULL = 未登録 */
} m5_stub_site_t;

/*  初回のみ即時 1 行出力＋登録、以降はカウントのみ。 */
extern void	m5_stub_hit(m5_stub_site_t *site);

/*  登録済み全スタブを 1 行ずつ再掲する（tag は見出し）。 */
extern void	m5_stub_trace_dump(const char *tag);

/*  踏まれたスタブの種類数（0 なら 1 つも呼ばれていない）。 */
extern unsigned int	m5_stub_trace_nsites(void);

/*
 *  ★S5：**有界**な即時 1 文字出力（実体 = m5_diag.c）。
 *
 *  【なぜ target_fput_log を直接使わなくしたか】
 *    A1_CONSOLE_USJ=OFF（＝実機 CoreS3 で Grove Port C の外部 USB-UART から採取する
 *    S5 の構成）では target_fput_log は
 *      fmp3/arch/xtensa_gcc/esp32s3/chip_serial.c の esp32s3_uart0_putc
 *      → do { cnt = UART_STATUS_REG>>16 & 0x3FF; } while (cnt >= 100);
 *    という**上限の無いスピン**である（USJ 経路は 5000 回で bounded なのに
 *    UART 経路にだけ上限が無い＝ §2 の観測系欠陥 #3）。
 *    UART0 の TX が掃けなくなると、**計装そのものが永久に止まり観測が消える**。
 *    m5_diag.c の m5_putc_now は 200000 周（≒25ms）で打ち切り m5_uart_stall を
 *    増やして先へ進むので、文字は落ちてもシステムと観測は止まらない。
 *    USJ 構成では target_fput_log 自身が bounded なのでそのまま委譲する。
 *  ★fmp3/ 側（chip_serial.c）は触らない（golden 5 構成をバイト不変に保つため）。
 */
extern void	m5_putc_now_ext(char c);

/*  打ち切り（＝文字を落とした）回数。0 でなければログに欠落がある＝沈黙の一義化。 */
extern unsigned int	m5_uart_stall_count(void);

/*
 *  スタブ本体に 1 行だけ足す。name は必ず静的文字列リテラルを渡すこと
 *  （site は関数内 static なので寿命はプログラム全体）。
 */
#define M5_STUB_HIT(name)											\
	do {															\
		static m5_stub_site_t	m5_stub_site_ = { (name), 0U, 0 };	\
		m5_stub_hit(&m5_stub_site_);								\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif /* M5_STUB_TRACE_H */
