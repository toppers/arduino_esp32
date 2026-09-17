/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  CLIC 版 ESP-IDF 割込み確保シム（`esp_intr_alloc` 系）の公開面
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef ESP_SHIM_INTR_CLIC_H
#define ESP_SHIM_INTR_CLIC_H

#include <stdint.h>

/*
 *  ============================================================================
 *  型の ABI 互換（AC D-2b）
 *  ============================================================================
 *  seam は ESP-IDF を 1 本もリンクしないので、`esp_err.h` も
 *  `esp_intr_alloc.h` も**この構成では include できない**（P4 側に
 *  `esp/config/esp32p4/sdkconfig.h` を起こしていないため）。
 *  一方、将来 `esp/eth` や `esp_lcd` を seam へ載せるときは、それらの翻訳単位が
 *  IDF のヘッダを見た状態で本シムの実体とリンクする。
 *  ⇒ **同じ実体に、両側から同じ ABI で届く**必要がある。
 *
 *  合わせ方（現物との対応。いずれも `esp-idf`（v5.5.4 pin）から）:
 *
 *  | IDF 側                                   | 本ヘッダ                              |
 *  |------------------------------------------|---------------------------------------|
 *  | `typedef int esp_err_t;`                 | 同（下の `#if` で二重定義を避ける）   |
 *  |   `components/esp_common/include/esp_err.h` |                                    |
 *  | `typedef void (*intr_handler_t)(void *);`| 型名を導入せず**関数ポインタを直書き**|
 *  |   `components/esp_hw_support/include/esp_intr_types.h` |                      |
 *  | `typedef struct intr_handle_data_t *intr_handle_t;` | `struct intr_handle_data_t *` を直書き |
 *
 *  **typedef 名（`intr_handler_t`/`intr_handle_t`）をここで導入しない**のは、
 *  IDF ヘッダと同時に見えたときに typedef の再定義になるのを避けるためである
 *  （C11 では同一 typedef の再定義は合法だが、本ポートの方言に依存させたくない）。
 *  Xtensa 版 `esp/shim/esp_shim_intr.c` も同じ理由で
 *  `struct intr_handle_data_t **ret_handle` を直書きしている。
 *
 *  `esp_err_t` は「IDF の `esp_err.h` が既に見えているか」で分岐する。
 *  判定に `ESP_OK` を使うのは、これが `esp_err.h` の中で必ず定義される
 *  オブジェクト形式マクロだからである（型名では `#if` で判定できない）。
 */
#if !defined(ESP_OK)
typedef int		esp_err_t;
#define ESP_OK						0
#define ESP_FAIL					(-1)
#define ESP_ERR_NO_MEM				0x101
#define ESP_ERR_INVALID_ARG			0x102
#define ESP_ERR_INVALID_STATE		0x103
#define ESP_ERR_NOT_FOUND			0x105
#endif /* !defined(ESP_OK) */

/*
 *  `esp_intr_alloc()` の flags（IDF `esp_intr_alloc.h` と同じ値）。
 *  本シムは **受け取るが動的には効かせない**（理由は `.c` の該当箇所）。
 *  IDF ヘッダが既に見えているなら、そちらの定義をそのまま使う。
 */
#if !defined(ESP_INTR_FLAG_LEVEL1)
#define ESP_INTR_FLAG_LEVEL1		(1 << 1)
#define ESP_INTR_FLAG_LEVEL2		(1 << 2)
#define ESP_INTR_FLAG_LEVEL3		(1 << 3)
#define ESP_INTR_FLAG_LEVEL4		(1 << 4)
#define ESP_INTR_FLAG_LEVEL5		(1 << 5)
#define ESP_INTR_FLAG_LEVEL6		(1 << 6)
#define ESP_INTR_FLAG_NMI			(1 << 7)
#define ESP_INTR_FLAG_SHARED		(1 << 8)
#define ESP_INTR_FLAG_EDGE			(1 << 9)
#define ESP_INTR_FLAG_IRAM			(1 << 10)
#define ESP_INTR_FLAG_INTRDISABLED	(1 << 11)
#endif /* !defined(ESP_INTR_FLAG_LEVEL1) */

struct intr_handle_data_t;

/*
 *  ============================================================================
 *  ESP-IDF 互換 API（AC D-2a: 実利用者の要求から決めた集合）
 *  ============================================================================
 *  この 5 本は「現物から抽出した」ものであって、思いつきで並べたものではない:
 *
 *   - `esp_intr_alloc` / `esp_intr_enable` / `esp_intr_free`
 *       … `esp/eth/os/eth_os_fmp3.c` が `--wrap` している 3 本（方式(a) 実績）。
 *          `esp-idf/components/esp_lcd/dsi/esp_lcd_panel_dpi.c` も
 *          `esp_intr_alloc(...brg_irq_id...)` を呼ぶ。
 *   - `esp_intr_disable`
 *       … IDF の `esp_intr_alloc.h` が `enable` と対で公開しており、
 *          ドライバの停止経路（`esp_eth_stop` 相当）が呼ぶ。片方だけ在ると
 *          「止めたつもりで止まらない」を作るので対で入れる。
 *   - `esp_intr_alloc_intrstatus`
 *       … `esp-idf/components/esp_lcd/` の RGB/i80/i2s 経路が使う
 *          （`esp_lcd_panel_rgb.c` 等）。DSI だけなら不要だが、
 *          `esp_intr_alloc` をこれの薄い包みとして実装するので**費用が増えない**。
 */

esp_err_t	esp_intr_alloc(int source, int flags,
						   void (*handler)(void *), void *arg,
						   struct intr_handle_data_t **ret_handle);

esp_err_t	esp_intr_alloc_intrstatus(int source, int flags,
									  uint32_t intrstatusreg, uint32_t intrstatusmask,
									  void (*handler)(void *), void *arg,
									  struct intr_handle_data_t **ret_handle);

esp_err_t	esp_intr_enable(struct intr_handle_data_t *handle);
esp_err_t	esp_intr_disable(struct intr_handle_data_t *handle);
esp_err_t	esp_intr_free(struct intr_handle_data_t *handle);

/*
 *  ============================================================================
 *  診断（IDF には無い。本シム固有）
 *  ============================================================================
 *  「黙って無視した」「黙って落とした」を残さないための口。
 *  試験側が**番号を決め打ちせずに**状態を読めるようにする（Xtensa 版で
 *  「試験が線番号を決め打ちしていたせいで偽 FAIL を出した」前例があるため）。
 */

/*  スロット数（＝同時に確保できる本数）。  */
int			esp_shim_clic_intr_nslot(void);

/*  スロット番号 -> CLIC 線番号。範囲外は -1（0 は線 0 と区別できないため）。  */
int			esp_shim_clic_intr_line_of(int idx);

/*  ソース -> スロット番号。未割当ては -1。  */
int			esp_shim_clic_intr_slot_of_source(int source);

/*
 *  スロットの総覧。in_use なら 1 を返し、`source`/`line`/`n_isr`/`n_call` を書く。
 *  戻り値は **int**（`bool_t` の実体に依存させない）。
 */
int			esp_shim_clic_intr_slot_info(int idx, int *source, int *line,
										 uint32_t *n_isr, uint32_t *n_call);

/*
 *  割込みマトリクスの MAP レジスタを**読み返す**（AC I-1）。
 *  「配線した」を書いた側の主張ではなくレジスタから確かめるための口。
 *  `source` が範囲外なら 0xFFFFFFFF を返す。
 */
uint32_t	esp_shim_clic_intr_read_map(int source);

/*  診断カウンタ（外から読めるように非 static）。  */
extern volatile uint32_t	esp_shim_clic_intr_n_alloc;
extern volatile uint32_t	esp_shim_clic_intr_n_alloc_fail;
extern volatile uint32_t	esp_shim_clic_intr_n_free;
extern volatile uint32_t	esp_shim_clic_intr_n_flag_iram;
extern volatile uint32_t	esp_shim_clic_intr_n_flag_shared;
extern volatile uint32_t	esp_shim_clic_intr_n_flag_level;
extern volatile uint32_t	esp_shim_clic_intr_n_flag_edge;
extern volatile uint32_t	esp_shim_clic_intr_n_ena_fail;
extern volatile uint32_t	esp_shim_clic_intr_n_dis_fail;
extern volatile uint32_t	esp_shim_clic_intr_n_clr_fail;

/*
 *  cfg（`esp_shim_intr_clic.cfg`）が `CRE_ISR` で登録する ISR 本体。
 *  `exinf` にスロット番号が入る。
 */
void		esp_shim_clic_intr_isr(intptr_t exinf);

#endif /* ESP_SHIM_INTR_CLIC_H */
