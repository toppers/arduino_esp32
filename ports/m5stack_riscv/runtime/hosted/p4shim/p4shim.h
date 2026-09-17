/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  seam 版 Ethernet 用 ESP-IDF システム層シム（公開面と診断カウンタ）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  この層は何か（`esp/shim` との違いを最初に書く）
 *  ============================================================================
 *  `esp/shim` は **Xtensa（S3/LX6）の Wi-Fi/BT blob 用**の OS 抽象化シムで、
 *  タスク・キュー・セマフォ・タイマの FMP3 写像（36 記号）を持つ。
 *  **本層はそれとは別系統である。** P4 seam の Ethernet が要求するのは
 *  OS 抽象ではなく、**ESP-IDF の「システム層」＝ GPIO ドライバ・クロック木・
 *  キャッシュ同期・eFuse・heap_caps・ログ**である。
 *
 *  OS 抽象の側（FreeRTOS 12 記号）は **既に埋まっている**——
 *  `esp/eth/os/eth_os_fmp3.c` が 12 本すべてに FMP3 ネイティブ実装を持っており、
 *  `__real_*` への委譲は「IDF が起動中で FMP3 がまだ CPU を掌握していない期間」
 *  専用の枝で、seam にはその期間が存在しない（bootloader → FMP3 直行）。
 *  ⇒ **`esp/shim` は 1 記号も持ち込まない。**
 *  実測の根拠は `.steering/20260815-p4-seam-eth/AC.md` §1。
 *
 *  ============================================================================
 *  なぜ IDF の .c を引かず、自前で書くのか（実測に基づく判断）
 *  ============================================================================
 *  必要記号を種にして `fmp_loader` の IDF ビルド（1,054 obj）で定義側を辿る
 *  閉包を計算すると、
 *    - 無制限                                    … **187 obj**
 *      （bootloader・FreeRTOS・spi_flash・esp_system・panic まで来る）
 *    - hal/soc/esp_hw_support/esp_mm/esp_rom/esp_driver_gpio に限っても
 *                                                … **42 obj ＋ 未解決 63**
 *      （`rtc_clk.c`・`clk_ctrl_os.c` が sleep/PMU/regdma/touch/vbat を引き込む）
 *  ⇒ **IDF の header-only（static inline）LL 層だけを使い、`.c` は引かない。**
 *  唯一の例外は `esp-idf/components/soc/esp32p4/emac_periph.c`
 *  （**未定義シンボル 0** の純データ表）で、これは `libeth.a` 側へ足す。
 *
 *  **この判断の帰結（正直に書く）**: seam の Ethernet は、方式(a) と違って
 *  GPIO/クロック/キャッシュ/eFuse の面が自前実装である。E-1〜E-5 が方式(a) と
 *  一致しなかったときの第一容疑者はここである。
 *
 *  ============================================================================
 *  黙って無視しない・黙って成功にしない
 *  ============================================================================
 *  本層は「必要な経路だけを満たす最小実装」であって IDF と意味論は同一でない。
 *  非対応の入力を受けたら **失敗値を返し、かつ数える**（下のカウンタ）。
 *  カウンタは `p4shim_report()` が実機ログへ全数を出す（AC I-10）。
 *  **0 であることは PASS 条件にしていない**——非対応の枝を通っても動く場合が
 *  あるため（例: 内部 SRAM への `esp_cache_msync` は IDF 本体でも
 *  `ESP_ERR_INVALID_ARG` を返し、`esp_eth` 側がそれを許容する）。
 *  「0 でなければ異常」と決め打つと、正常を異常と読む。
 */

#ifndef ESP_P4SHIM_H
#define ESP_P4SHIM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 *  `esp_err_t` は「IDF の `esp_err.h` が既に見えているか」で分岐する
 *  （C-1 の `esp_shim_intr_clic.h` と同じ作法。判定に `ESP_OK` を使うのは、
 *  これが `esp_err.h` の中で必ず定義されるオブジェクト形式マクロだからである）。
 */
#if !defined(ESP_OK)
typedef int		esp_err_t;
#define ESP_OK						0
#define ESP_FAIL					(-1)
#define ESP_ERR_NO_MEM				0x101
#define ESP_ERR_INVALID_ARG			0x102
#define ESP_ERR_INVALID_STATE		0x103
#define ESP_ERR_NOT_FOUND			0x105
#define ESP_ERR_NOT_SUPPORTED		0x106
#endif /* !defined(ESP_OK) */

/*
 *  ============================================================================
 *  診断カウンタ（AC §2 の「非対応時の振る舞い」を数える口）
 *  ============================================================================
 *  すべて「本来通らないはずの枝を通った回数」である。
 */
extern volatile uint32_t	p4shim_n_gpio_badarg;		/* 範囲外 pin */
extern volatile uint32_t	p4shim_n_gpio_rtcio;		/* RTC/LP 側の pin を要求された */
extern volatile uint32_t	p4shim_n_heap_fail;			/* heap_caps_* が NULL を返した */
extern volatile uint32_t	p4shim_n_mac_fail;			/* esp_read_mac が失敗した */
extern volatile uint32_t	p4shim_n_cache_badarg;		/* esp_cache_msync 非対応領域 */
extern volatile uint32_t	p4shim_n_cache_sync;		/* esp_cache_msync 実施回数 */
extern volatile uint32_t	p4shim_n_clk_unsupported;	/* 未対応の clk_src を要求された */
extern volatile uint32_t	p4shim_n_mpll_notouch;		/* MPLL を設定せず現状を返した */
extern volatile uint32_t	p4shim_n_intr_reserve;		/* esp_intr_reserve が呼ばれた */
extern volatile uint32_t	p4shim_n_tx_dropped;		/* ホスト不在で捨てたログ文字数（段BLUE） */

/*
 *  **fail-closed スタブが呼ばれた回数**（AC I-6）。
 *  `--wrap` の `__real_*` が解決する先＝素の FreeRTOS 名、および
 *  `--wrap` していない `__real_esp_intr_*`。seam では
 *  `g_fmp3_active` が常に真なので **1 回も呼ばれないはず**である。
 *  「呼ばれないはず」を主張するために**数える**。
 */
extern volatile uint32_t	p4shim_n_real_stub;

/*
 *  ============================================================================
 *  同期出力（`syslog()` は使わない）
 *  ============================================================================
 *  `syslog()` は logtask 経由の非同期出力で、判定行が別タスクの出力に割り込まれて
 *  壊れることがある（C-1 §5-4 で実際に 1 行が食われた）。本層の報告は
 *  `target_fput_log()` 直呼びの同期出力にし、1 行の間だけ CPU ロックする。
 */
void	p4shim_puts(const char *s);
void	p4shim_put_kv(const char *k, uint32_t v);	/* "<key>=<10進>" を 1 行 */
void	p4shim_report(void);		/* カウンタの全数を 1 行ずつ出す（AC I-10）*/

/*
 *  MPLL の実測値（`periph_rtc_mpll_freq_set` が読んだ値・Hz）。
 *  AC §9 M-1 の「未確認」を実機で埋めるための口。0 なら未読。
 */
extern volatile uint32_t	p4shim_mpll_freq_hz;
/*  RMII 用に設定した PLL_F50M の分周比と、結果の周波数（Hz）。 */
extern volatile uint32_t	p4shim_f50m_div;
extern volatile uint32_t	p4shim_f50m_freq_hz;
/*  `esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_SYS)` が返した値（Hz）。 */
extern volatile uint32_t	p4shim_sys_freq_hz;
/*  `.bss_high`（sram_high 側）のクリア実績（AC I-12）。 */
extern volatile uint32_t	p4shim_bss_high_words;
extern volatile uint32_t	p4shim_bss_high_dirty;

/*
 *  ============================================================================
 *  PSRAM（段v-2・`A1_P4_PSRAM=ON` のときだけ実体がある）
 *  ============================================================================
 *  判定基準 `.steering/20260816-tab5-display-v2/AC.md` §2-1・§3-1。
 *  実装は `esp/p4shim/p4shim_psram.c`（`P4SHIM_PSRAM` マクロで囲ってある）。
 */
#if defined(P4SHIM_PSRAM)
int		p4shim_psram_init(void);			/* 0=成功／負値は段階番号 */
void   *p4shim_psram_alloc(size_t size, size_t align);	/* bump。解放しない */
bool	p4shim_psram_ptr_in_psram(const void *p);
bool	p4shim_psram_other_core_stalled(void);
void	p4shim_ldo_enable(int chan_id, int voltage_mv);	/* DSI(ch3) でも使う */
void	p4shim_psram_report(void);

extern volatile int			p4shim_psram_ran;
extern volatile int			p4shim_psram_rc;
extern volatile int			p4shim_psram_map_half;		/* negative control 用 */
extern volatile int			p4shim_psram_core1_stalled;
extern volatile uint32_t	p4shim_psram_size;
extern volatile uint32_t	p4shim_psram_avail;
extern volatile uint32_t	p4shim_psram_vaddr;
extern volatile uint32_t	p4shim_psram_mapped;
extern volatile uint32_t	p4shim_psram_mpll_hz;
extern volatile uint32_t	p4shim_psram_n_freeze;
extern volatile uint32_t	p4shim_psram_n_stall;
extern volatile uint32_t	p4shim_psram_n_stall_bad;
extern volatile uint32_t	p4shim_psram_n_alloc;
extern volatile uint32_t	p4shim_psram_n_alloc_fail;
extern volatile uint32_t	p4shim_psram_alloc_top;
extern volatile uint32_t	p4shim_psram_ldo_mv;
extern volatile uint32_t	p4shim_psram_dbg_sbss_word;
extern volatile uint32_t	p4shim_psram_dbg_sbss_addr;
#endif /* P4SHIM_PSRAM */

#endif /* ESP_P4SHIM_H */
