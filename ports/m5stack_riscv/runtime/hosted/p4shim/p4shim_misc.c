/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet: heap_caps / ログ / 時刻 / 報告
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#include <kernel.h>
#include <sil.h>
/*  USJ_EP1_CONF / USJ_SERIAL_IN_EP_DATA_FREE（同期出力の空き判定。下の §同期出力）  */
#include "esp32p4.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "p4shim.h"

/*
 *  **IDF の公開ヘッダを include してプロトタイプの照合をコンパイラにさせる。**
 *  怠ると「リンクは通り、ビルドも通り、実機でだけ壊れる」型の失敗になる
 *  ——run1 で実際に踏んだ（`gpio_func_sel` を void で書き、呼び手が
 *  戻り値を見ていたため a0 のゴミが非 0 と読まれ、SMI 初期化が失敗した）。
 */
#include "esp_heap_caps.h"
#include "esp_timer.h"

volatile uint32_t	p4shim_n_gpio_badarg;
volatile uint32_t	p4shim_n_gpio_rtcio;
volatile uint32_t	p4shim_n_heap_fail;
volatile uint32_t	p4shim_n_mac_fail;
volatile uint32_t	p4shim_n_cache_badarg;
volatile uint32_t	p4shim_n_cache_sync;
volatile uint32_t	p4shim_n_clk_unsupported;
volatile uint32_t	p4shim_n_mpll_notouch;
volatile uint32_t	p4shim_n_intr_reserve;

volatile uint32_t	p4shim_mpll_freq_hz;
volatile uint32_t	p4shim_f50m_div;
volatile uint32_t	p4shim_f50m_freq_hz;
volatile uint32_t	p4shim_sys_freq_hz;

extern void target_fput_log(char c);

/*
 *  ============================================================================
 *  同期出力
 *  ============================================================================
 *  `syslog()` は logtask（優先度が本タスクより高い）経由の非同期出力で、
 *  1 行の途中に別タスクの出力が混ざって**判定文字列ごと壊れる**ことがある
 *  （C-1 §5-4 の実測）。本層の出力はすべて `target_fput_log()` 直呼びの同期に
 *  し、1 行の間だけ CPU ロックする。`target_fput_log()` は上限つきスピンの
 *  ポーリング出力なのでハングしない（`target_kernel_impl.c` で確認済み）。
 *
 *  ----------------------------------------------------------------------------
 *  [改変] 2026-08-17・段BLUE — **CPU ロック中にスピンしない**ようにした
 *  ----------------------------------------------------------------------------
 *  上の「ハングしない」は正しいが、**十分ではなかった**。
 *
 *  `polafire_soc_kit_uart_fput`（`fmp3/target/m5stamp_esp32p4_gcc/
 *  target_kernel_impl.c:232-247`）は USB-Serial/JTAG の TX FIFO が空くまで
 *  **最大 100,000 回 x `sil_dly_nse(100)`（＝10 ms 超）**スピンしてから
 *  その 1 文字を捨てる。ホストが USB CDC を drain していないとき
 *  （＝**採取していないとき。ユーザーが画面を見ているとき**）、FIFO は
 *  永久に埋まったままなので **1 文字ごとに 10 ms 超**かかる。
 *  それを上の `loc_cpu()` が囲っているので、**1 行が 1 秒近い割込み禁止区間**
 *  になっていた。
 *
 *  これが Tab5 の「表示が青い画面と交互になる」の真因である（段BLUE で実測）:
 *  IDF の DPI パネルは `num_fbs=1` でも **1 フレームごとに DW-GDMA 完了 ISR が
 *  ソフトウェアで DMA を貼り直す**（`esp-idf/components/esp_lcd/dsi/
 *  esp_lcd_panel_dpi.c:70-90`。link list は SINGLY・`is_last=true`）ので、
 *  割込みを止めるとフレーム送出が**1 枚で止まる**。
 *  実測（`.steering/20260817-tab5-blue-alternation/logs/10-blue-run1.log`）:
 *    `loc_cpu()` 2000 ms 保持 -> 完了フレームは **1 枚**（割込みが生きていれば 114 枚）。
 *  ホスト不在の 351 秒間の実測では、実効 16.7fps（健全時 57.4fps）＝
 *  **フレームが流れていたのは 29% の時間だけ**だった。
 *
 *  直し方: **待つのは割込み許可のまま**にし、CPU ロックの中では
 *  「すぐ書ける」と分かっている文字しか出さない。
 *  FIFO の空きは `USJ_EP1_CONF` の `SERIAL_IN_EP_DATA_FREE` を読むだけで分かる
 *  （`fmp3/arch/riscv_gcc/esp32p4/esp32p4.h:148-152`。読み取りのみ・副作用なし）。
 *
 *  **失うもの（正直に書く）**: 従来は 1 行まるごとを CPU ロックで囲っていたので
 *  「1 行が他の出力に割られない」ことが保証されていた。本改変で保証の粒度は
 *  **1 行から 1 文字へ落ちる**。同時に出力する文脈が 2 つ以上ある構成では
 *  行が混ざりうる。**割込みを 1 秒止めるよりは軽い代償**と判断した
 *  （行を守るために spinlock を足す案は、ISR 文脈から呼ばれたときに
 *  デッドロックする——`loc_cpu` は入れ子で安全だが spinlock はそうではない——ので採らない）。
 *
 *  ここを直す理由（層の選択）: 真の欠陥は target 層の 10 ms スピンにもあるが、
 *  `target_kernel_impl.c` は **golden 構成 `seam-p4-smp` に入っている**ので
 *  触ると golden が動く。本層（`esp/p4shim` は golden 13 構成のどれにも
 *  リンクされていない——実測: `build/seam-p4-smp/build.ninja` に `p4shim_misc` が
 *  0 件）で直すと、**割込み禁止区間を作っているのは本層である**という
 *  真因の所在にも一致する。target 層の件は残件として申し送る。
 */

/*
 *  1 文字出せる状態か（USB-Serial/JTAG の TX FIFO に空きがあるか）。
 *  **読むだけ**。`chip_serial.c` の `usj_putready()` と同じ判定である。
 */
Inline bool_t
p4shim_usj_txready(void)
{
	return((sil_rew_mem(USJ_EP1_CONF) & USJ_SERIAL_IN_EP_DATA_FREE) != 0U);
}

/*
 *  空きを待つ上限（**割込み許可のまま**回るので、長くても表示は壊れない）。
 *  1 周が数十 ns なので 200,000 周で約 10 ms ＝ USB のフレーム 10 回分。
 *  ホストが居れば 1 ms 以内に空くので、この上限に届くのは
 *  「ホストが居ない」ときだけである。そのときは**その文字を捨てる**
 *  （ログの欠落は許容。表示を壊す方が高くつく）。
 */
#define P4SHIM_TXWAIT_SPIN		200000U

/*
 *  「ホストが居ない」ことの記憶（ラッチ）。
 *
 *  なぜ要るか（実測。段BLUE B-1）: 上限つきの待ちだけにすると、ホスト不在時は
 *  **1 文字ごとに上限いっぱい**（実測 16.8 ms）待つ。1 行 26 文字・4 行なら
 *  1 回の報告で 1.7 秒である。表示は壊れなくなる（割込みは通っている）が、
 *  **生存ループの周期が 1.00 秒から 2.75 秒へ延びた**（実測）。
 *  ⇒ 一度でも上限に達したら「ホストは居ない」と覚え、以後は待たずに捨てる。
 *  FIFO に空きが戻った瞬間（＝ホストが再接続して drain を始めた）に忘れる。
 */
static volatile bool_t	p4shim_tx_host_gone;

/*  観測用（判定には使わない。落とした文字数を数えるだけ）  */
volatile uint32_t	p4shim_n_tx_dropped;

/*
 *  1 文字の同期出力。**CPU ロックの中でスピンしない**のが本関数の要点。
 */
static void
p4shim_putc_sync(char c)
{
	uint32_t	spin;

	if (p4shim_tx_host_gone) {
		/*  ホスト不在と覚えている間は待たない。空きが戻ったら忘れる。  */
		if (!p4shim_usj_txready()) {
			p4shim_n_tx_dropped++;
			return;
		}
		p4shim_tx_host_gone = false;
	}
	for (spin = 0U; !p4shim_usj_txready(); spin++) {
		if (spin >= P4SHIM_TXWAIT_SPIN) {
			p4shim_tx_host_gone = true;
			p4shim_n_tx_dropped++;
			return;				/* ホスト不在。捨てて先へ進む */
		}
	}
	/*
	 *  ここへ来た時点で FIFO に空きがある ⇒ `target_fput_log()` は
	 *  スピンせずに戻る。CPU ロック区間はレジスタ書込み 2 回分しかない。
	 */
	(void) loc_cpu();
	target_fput_log(c);
	(void) unl_cpu();
}

void
p4shim_puts(const char *s)
{
	if (s == NULL) {
		return;
	}
	while (*s != '\0') {
		p4shim_putc_sync(*s++);
	}
	p4shim_putc_sync('\n');
}

void
p4shim_put_kv(const char *k, uint32_t v)
{
	char	buf[12];
	int		i = 0;

	while (*k != '\0') {
		p4shim_putc_sync(*k++);
	}
	p4shim_putc_sync('=');
	if (v == 0U) {
		p4shim_putc_sync('0');
	}
	else {
		while (v > 0U && i < (int) sizeof(buf)) {
			buf[i++] = (char) ('0' + (v % 10U));
			v /= 10U;
		}
		while (i > 0) {
			p4shim_putc_sync(buf[--i]);
		}
	}
	p4shim_putc_sync('\n');
}

/*
 *  カウンタの全数（AC I-10）。**0 であることを PASS 条件にしていない**ので、
 *  値がどうであれ全部出す。判定は読み手が段E の実測と突き合わせて行う。
 */
void
p4shim_report(void)
{
	p4shim_puts("P4SHIM ---- counters ----");
	p4shim_put_kv("P4SHIM n_real_stub", p4shim_n_real_stub);
	p4shim_put_kv("P4SHIM n_gpio_badarg", p4shim_n_gpio_badarg);
	p4shim_put_kv("P4SHIM n_gpio_rtcio", p4shim_n_gpio_rtcio);
	p4shim_put_kv("P4SHIM n_heap_fail", p4shim_n_heap_fail);
	p4shim_put_kv("P4SHIM n_mac_fail", p4shim_n_mac_fail);
	p4shim_put_kv("P4SHIM n_cache_badarg", p4shim_n_cache_badarg);
	p4shim_put_kv("P4SHIM n_cache_sync", p4shim_n_cache_sync);
	p4shim_put_kv("P4SHIM n_clk_unsupported", p4shim_n_clk_unsupported);
	p4shim_put_kv("P4SHIM n_mpll_notouch", p4shim_n_mpll_notouch);
	p4shim_put_kv("P4SHIM n_intr_reserve", p4shim_n_intr_reserve);
	p4shim_put_kv("P4SHIM mpll_freq_hz", p4shim_mpll_freq_hz);
	p4shim_put_kv("P4SHIM f50m_div", p4shim_f50m_div);
	p4shim_put_kv("P4SHIM f50m_freq_hz", p4shim_f50m_freq_hz);
	p4shim_put_kv("P4SHIM sys_freq_hz", p4shim_sys_freq_hz);
	p4shim_put_kv("P4SHIM bss_high_words", p4shim_bss_high_words);
	p4shim_put_kv("P4SHIM bss_high_dirty", p4shim_bss_high_dirty);
#if defined(P4SHIM_PSRAM)
	p4shim_psram_report();
#endif
}

/*
 *  ============================================================================
 *  `software_init_hook` — sram_high 側 .bss のゼロクリア
 *  ============================================================================
 *  `liblwip.a` の .bss は **97,664 バイト**あり（memp プール＋`MEM_SIZE`=16KB
 *  の ram_heap 等）、sram_low（0x2CBD0 = 183,248 B）に収まらない。
 *  ⇒ `esp32p4_xip.ld` が **アーカイブ名で限定して** `.bss_high`（sram_high・
 *    0x4FF40000〜）へ逃がしている。
 *
 *  **start.S がゼロクリアするのは `__sbss_start..__sbss_end` と
 *  `__bss_start..__bss_end` だけ**なので、`.bss_high` はここでクリアする。
 *  忘れると lwIP の memp/netif の静的変数がゴミで始まり、
 *  **「起動はするが通信だけおかしい」**という最も切り分けにくい形で壊れる。
 *
 *  `software_init_hook` は `fmp3_core/arch/riscv_gcc/common/start.S:166` が
 *  weak 定義を持ち、**マスタプロセッサだけ**が通る（同 :106-108 で
 *  slave は `slave_wait` へ分岐する。実測でそう読んだ）。
 *  ⇒ ここで多重クリアや競合は起きない。
 *
 *  この関数が実際に効いたことは実機で確かめる（AC I-12）:
 *  クリア前の非ゼロ語数を数えて出す。**0 なら「もともとゼロだった」**ので
 *  この処置が要ったかどうかは分からない、と正直に読めるようにしておく。
 */
extern char	__bss_high_start[];
extern char	__bss_high_end[];

volatile uint32_t	p4shim_bss_high_words;		/* クリアした語数 */
volatile uint32_t	p4shim_bss_high_dirty;		/* クリア前に非ゼロだった語数 */

void
software_init_hook(void)
{
	uint32_t	*p = (uint32_t *) __bss_high_start;
	uint32_t	*e = (uint32_t *) __bss_high_end;
	uint32_t	n = 0;
	uint32_t	d = 0;

	while (p < e) {
		if (*p != 0U) {
			d++;
		}
		*p++ = 0U;
		n++;
	}
	p4shim_bss_high_words = n;
	p4shim_bss_high_dirty = d;

#if defined(P4SHIM_PSRAM)
	/*
	 *  PSRAM の立ち上げ（段v-2）。**ここでやる理由**は
	 *  `p4shim_psram.c` 冒頭の「いつ呼ぶか」を読むこと（要約: カーネル初期化より
	 *  前＝core1 を起こす主体より前で、かつ他コアを明示的に止められる場所）。
	 *  戻り値は `p4shim_psram_rc` に残る。**ここでは止めない**——PSRAM が
	 *  立たなくてもカーネルは動くべきで、判定はアプリ側が行う。
	 */
	(void) p4shim_psram_init();
#endif
}

/*
 *  ============================================================================
 *  heap_caps
 *  ============================================================================
 *  `caps` は**無視する**（Xtensa 版 `esp/shim/esp_shim_libc.c` と同じ扱い）。
 *  seam のヒープは `target_kernel_impl.c` の静的配列（`_sbrk`）1 本しかなく、
 *  それは内部 SRAM（L2MEM）に置かれる＝IDF で言う
 *  `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA` に相当する。
 *  ⇒ 区別する先が無いので、区別しないのが正しい。
 *
 *  **ただしキャッシュ整合は別の話である**。P4 の CPU は L2MEM を L1 キャッシュ
 *  経由で見るので、EMAC DMA が触るバッファは `esp_cache_msync` が要る
 *  （`p4shim_cache.c`）。「内部 SRAM だから同期不要」ではない。
 *
 *  `esp_eth` の利用箇所（実測。IDF v5.5.4）:
 *    esp_eth_mac_esp.c:705      heap_caps_calloc(1, sizeof(emac_esp32_t), ...)
 *    esp_eth_mac_esp_dma.c:506  heap_caps_aligned_calloc(4, 1, desc_size, ...)
 *    esp_eth_mac_esp_dma.c:510  heap_caps_aligned_calloc(4, 1, RX buf, ...) x N
 *    esp_eth_mac_esp_dma.c:514  heap_caps_aligned_calloc(4, 1, TX buf, ...) x N
 */
#define P4SHIM_MALLOC_CAP_SPIRAM	(1 << 10)	/* esp_heap_caps.h */

void *
heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	void *p;

#if defined(P4SHIM_PSRAM)
	/*
	 *  **`MALLOC_CAP_SPIRAM` だけは捨てない**（段v-2・AC §2-2 の 6）。
	 *  DSI のフレームバッファは
	 *    `esp_lcd_panel_dpi.c:231` heap_caps_calloc(1, fb_size,
	 *        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA)
	 *  で 1,843,200 B を要求する。内蔵 SRAM のヒープでは**絶対に足りない**ので、
	 *  ここで PSRAM の bump アロケータへ回す。
	 *  （回さないと `no memory for frame buffer` で DPI パネル生成が失敗する
	 *    ——実測でそうなった。）
	 *  ゼロ埋めまで行う（calloc の契約）。
	 */
	if ((caps & P4SHIM_MALLOC_CAP_SPIRAM) != 0U) {
		size_t	total = n * size;
		size_t	i;
		uint8_t	*q;

		p = p4shim_psram_alloc(total, 64U);
		if (p == NULL) {
			p4shim_n_heap_fail++;
			return NULL;
		}
		q = (uint8_t *) p;
		for (i = 0U; i < total; i++) {
			q[i] = 0U;
		}
		return p;
	}
#endif
	(void) caps;
	p = calloc(n, size);
	if (p == NULL) {
		p4shim_n_heap_fail++;
	}
	return p;
}

/*
 *  `heap_caps_aligned_calloc(alignment, n, size, caps)`。
 *
 *  **`caps` を無視してはいけない唯一の関数である（実測に基づく）。**
 *  `esp_eth_mac_esp_dma.c:506,510,514` は `alignment` に**リテラルの 4** を
 *  渡すが、IDF 本体はそれを鵜呑みにせず `esp_heap_adjust_alignment_to_hw()` で
 *  **`MALLOC_CAP_DMA` のとき D-cache のライン長（P4 は 64）へ引き上げ、
 *  長さもそこへ切り上げる**。呼び手はその前提で
 *  `DMA_CACHE_WB` / `DMA_CACHE_INVALIDATE`（`ESP_CACHE_MSYNC_FLAG_UNALIGNED`
 *  を**付けない**）を撃つ。
 *
 *  ⇒ ここで 4 バイト境界のまま返すと、`p4shim_cache.c` が範囲をライン境界へ
 *  広げたときに**隣の確保の dirty な行を巻き添えにする**（受信データが
 *  無関係な変数を壊す、という最も追いにくい形の故障）。
 *  したがって同じ引き上げを行う。**「4 と書いてあるから 4 でよい」ではない。**
 *
 *  newlib の `aligned_alloc` は「size が alignment の倍数」を要求するので、
 *  切り上げてから呼ぶ。ゼロ埋めは自分で行う（`aligned_alloc` は初期化しない）。
 */
#define P4SHIM_MALLOC_CAP_DMA		(1 << 3)	/* esp_heap_caps.h */
#define P4SHIM_DCACHE_LINE			64U			/* p4shim_cache.c と同値 */

void *
heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps)
{
	size_t	total;
	void	*p;

	if (alignment == 0U) {
		alignment = 4U;
	}
	if ((caps & P4SHIM_MALLOC_CAP_DMA) != 0U && alignment < P4SHIM_DCACHE_LINE) {
		alignment = P4SHIM_DCACHE_LINE;
	}
	total = n * size;
	if (total == 0U) {
		return NULL;
	}
#if defined(P4SHIM_PSRAM)
	/*  SPIRAM 要求はこちらでも PSRAM から出す（上の calloc と同じ理由）。 */
	if ((caps & P4SHIM_MALLOC_CAP_SPIRAM) != 0U) {
		size_t	i;
		uint8_t	*q;

		p = p4shim_psram_alloc(total, alignment);
		if (p == NULL) {
			p4shim_n_heap_fail++;
			return NULL;
		}
		q = (uint8_t *) p;
		for (i = 0U; i < total; i++) {
			q[i] = 0U;
		}
		return p;
	}
#endif
	/*  size を alignment の倍数へ切り上げる（aligned_alloc の契約）  */
	total = (total + alignment - 1U) & ~(alignment - 1U);
	p = aligned_alloc(alignment, total);
	if (p == NULL) {
		p4shim_n_heap_fail++;
		return NULL;
	}
	memset(p, 0, total);
	return p;
}

/*
 *  `heap_caps_free` / `heap_caps_malloc` も、要求されたときのために置く。
 *  `heap_caps_free` は `free` と同じ（本層の確保はすべて newlib heap 由来）。
 */
void *
heap_caps_malloc(size_t size, uint32_t caps)
{
	void *p;

	(void) caps;
	p = malloc(size);
	if (p == NULL) {
		p4shim_n_heap_fail++;
	}
	return p;
}

void
heap_caps_free(void *p)
{
	free(p);
}

/*
 *  ============================================================================
 *  ログ（`esp_log` / `esp_log_timestamp`）
 *  ============================================================================
 *  `libeth.a` は IDF v5.5.4 のヘッダでコンパイルされているので、`ESP_LOGx` は
 *  最終的に `esp_log(config, tag, format, ...)` を呼ぶ。第 1 引数は IDF v5.5 の
 *  `esp_log_config_t`（32bit のビットフィールド共用体）で、**値渡しで
 *  レジスタ 1 本**に載る。ここでは `unsigned int` で受ける
 *  （Xtensa 版 `esp/shim/esp_shim_libc.c` が同じ作法で実機実績がある）。
 *
 *  出力は `vsnprintf` で 1 行に整えてから同期出力する。**非同期にしない**
 *  理由は本ファイル上部の `p4shim_puts` のコメントと同じ。
 *  ESP_LOG の書式は末尾に改行を持つので、そのまま出して二重改行にしないよう
 *  末尾の '\n' は落とす。
 */
void
esp_log(unsigned int config, const char *tag, const char *format, ...)
{
	static char	buf[192];
	va_list		ap;
	int			n;
	int			i;

	(void) config;
	(void) loc_cpu();
	va_start(ap, format);
	n = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	if (n < 0) {
		(void) unl_cpu();
		return;
	}
	if (n > (int) sizeof(buf) - 1) {
		n = (int) sizeof(buf) - 1;
	}
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
		n--;
	}
	target_fput_log('[');
	if (tag != NULL) {
		while (*tag != '\0') {
			target_fput_log(*tag++);
		}
	}
	target_fput_log(']');
	target_fput_log(' ');
	for (i = 0; i < n; i++) {
		target_fput_log(buf[i]);
	}
	target_fput_log('\n');
	(void) unl_cpu();
}

/*
 *  IDF の新しい方の入口（`esp_log_write.h`）。`libeth.a` が使っているのは
 *  `esp_log` の方だが、対で持っておく（片方だけ在ると「出したつもりで
 *  出ない」を作る。C-1 の `esp_intr_enable`/`disable` と同じ考え方）。
 */
void
esp_log_write(int level, const char *tag, const char *format, ...)
{
	static char	buf[192];
	va_list		ap;
	int			n;
	int			i;

	(void) level;
	(void) loc_cpu();
	va_start(ap, format);
	n = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	if (n < 0) {
		(void) unl_cpu();
		return;
	}
	if (n > (int) sizeof(buf) - 1) {
		n = (int) sizeof(buf) - 1;
	}
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
		n--;
	}
	(void) tag;
	for (i = 0; i < n; i++) {
		target_fput_log(buf[i]);
	}
	target_fput_log('\n');
	(void) unl_cpu();
}

uint32_t
esp_log_timestamp(void)
{
	extern int64_t esp_timer_get_time(void);

	return (uint32_t) (esp_timer_get_time() / 1000);	/* ms */
}

/*
 *  ============================================================================
 *  `esp_timer_get_time`（マイクロ秒・64bit）
 *  ============================================================================
 *  方式(a) では IDF の `esp_timer_impl_systimer.c`（systimer 直読み）が供給して
 *  いた。seam には IDF が無いので、**FMP3 が既に使っている時計**から作る。
 *
 *  `eth_os_fmp3.c` の `esp_timer` 周期実装（check_link_timer、既定 2000ms）が
 *  満了判定に使う。**単調増加であること**と**マイクロ秒であること**だけが要件で、
 *  絶対時刻の基準は問われない。
 *
 *  `mtimer_get_count()` は CLINT の mtime（64bit・`MTIMER_FREQ_MHZ` MHz）で、
 *  FMP3 の高分解能タイマがそのまま使っている実体である
 *  （`fmp3_core/arch/riscv_gcc/common/mtimer.h`）。ラップの心配が要らない
 *  （64bit・16MHz なら 3 万年）ので、HRTCNT（32bit）ではなくこちらを直接使う。
 */
#include "mtimer.h"

int64_t
esp_timer_get_time(void)
{
	return (int64_t) (mtimer_get_count() / MTIMER_FREQ_MHZ);
}
