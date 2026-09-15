/*
 *  TOPPERS/FMP3 ESP32-C6 移植 --
 *  INTMTX 版 ESP-IDF 割込み確保シム（`esp_intr_alloc` 系）と、blob の
 *  `_set_intr` が使う線の使用中管理の公開面
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef ESP_SHIM_INTR_INTMTX_H
#define ESP_SHIM_INTR_INTMTX_H

#include <stdint.h>

/*
 *  ============================================================================
 *  型の ABI 互換（P4 の esp_shim_intr_clic.h と同じ合わせ方）
 *  ============================================================================
 *  本ヘッダは cfg（kernel_cfg.c）からも、esp-idf のヘッダを見ている翻訳単位
 *  （esp/wifi/hal_src、esp/shim）からも読まれる。esp_err_t と flags は
 *  「IDF の esp_err.h / esp_intr_alloc.h が既に見えているか」で分岐し、
 *  typedef 名（intr_handler_t / intr_handle_t）は導入しない（再定義を避ける）。
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
 *  ESP-IDF 互換 API（P4 版と同じ 5 本）
 *  ============================================================================
 *  段4 Task 3 の Wi-Fi 構成（wifi_sta）では**誰も呼ばない**（Wi-Fi blob は
 *  osi 表の _set_intr/_set_isr 経由で線を要求し、esp_intr_alloc は使わない。
 *  nm で実測: 6 blob + hal_src + wpa + lwip の未定義記号に esp_intr_alloc は
 *  無い）。それでも用意しておくのは、BT コントローラ（asp3 C6 の bt_shim.c は
 *  esp_intr_alloc を呼ぶ）や esp-idf ドライバを載せるときに、線 1..15 の
 *  払い出しを blob の _set_intr と**同じ帳簿**で管理するためである。
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
 *  blob の _set_intr / _clear_intr が使う線の配線（esp_wifi_adapter.c の C6 分岐）
 *  ============================================================================
 *  esp_shim_intmtx_route(source, line):
 *    esp32c6_intmtx_route(source, line)（chip_kernel_impl.c。INTMTX の MAP
 *    レジスタ書込みに加えて、prb_int が読む intmtx_srcmask を更新する）の
 *    薄い包み。線を「blob が使用中」として帳簿に記す。
 *    戻り値: 0 = 配線した / -1 = 線が範囲外（1..15 の外）か、esp_intr_alloc が
 *    既に払い出した線（配線しない。呼出し側は blob なので戻り値を見ないが、
 *    syslog に残す）。
 *  esp_shim_intmtx_unroute(source, line):
 *    INTMTX の MAP レジスタを 0（線 0 = 未接続）へ戻す。帳簿の線は
 *    「blob 使用中」のままにする（blob は clear の後に別 source を同じ線へ
 *    set_intr し直すことがあり、線の所有は変わらないため）。
 *    intmtx_srcmask（prb_int 用）は戻さない -- kernel 側に unroute が無く、
 *    prb_int を Wi-Fi 線に使う者も居ない。
 */
int			esp_shim_intmtx_route(int source, int line);
int			esp_shim_intmtx_unroute(int source, int line);

/*
 *  ============================================================================
 *  診断（IDF には無い。本シム固有。P4 版と同じ口）
 *  ============================================================================
 *  段4 最終レビュー是正（2026-09-15）: 呼び手は esp_shim_intmtx_diag_dump()
 *  （esp_shim_intr_intmtx.c 末尾、cmake の A1_C6_WIFI_DIAG=ON のときだけ
 *  リンク。esp/app/wifi_sta.c の [C6-SHIM] 報告から呼ばれる）。呼び手の無かった
 *  esp_shim_intmtx_intr_line_of / esp_shim_intmtx_intr_slot_of_source は削除した
 *  （line は slot_info が返す。source -> slot は esp_intr_alloc の利用者が
 *  居ないので要らない）。
 */

/*  スロット数（= 線 1..15 の本数）。  */
int			esp_shim_intmtx_intr_nslot(void);

/*
 *  スロットの総覧。in_use（blob 配線 or alloc 払い出し）なら 1 を返し、
 *  owner（0=未使用/1=blob/2=alloc）・source・line・n_isr（ISR 発火数）を書く。
 */
int			esp_shim_intmtx_intr_slot_info(int idx, int *owner, int *source,
										   int *line, uint32_t *n_isr);

/*
 *  割込みマトリクスの MAP レジスタを**読み返す**（書いた側の主張ではなく
 *  レジスタから確かめるための口）。source が範囲外なら 0xFFFFFFFF。
 */
uint32_t	esp_shim_intmtx_intr_read_map(int source);

/*  PLIC_MX の ENABLE / TYPE / PRI(line) を読み返す（Task 4 の AC-4d 用）。  */
uint32_t	esp_shim_intmtx_intr_read_plic_enable(void);
uint32_t	esp_shim_intmtx_intr_read_plic_type(void);
uint32_t	esp_shim_intmtx_intr_read_plic_pri(int line);

/*  帳簿とレジスタ実値の総覧を syslog へ（A1_C6_WIFI_DIAG=ON のときだけ存在）。  */
#if defined(A1_C6_WIFI_DIAG)
void		esp_shim_intmtx_diag_dump(void);
#endif /* A1_C6_WIFI_DIAG */

/*  診断カウンタ（外から読めるように非 static）。  */
extern volatile uint32_t	esp_shim_intmtx_intr_n_alloc;
extern volatile uint32_t	esp_shim_intmtx_intr_n_alloc_fail;
extern volatile uint32_t	esp_shim_intmtx_intr_n_free;
extern volatile uint32_t	esp_shim_intmtx_intr_n_route;
extern volatile uint32_t	esp_shim_intmtx_intr_n_route_fail;
extern volatile uint32_t	esp_shim_intmtx_intr_n_flag_iram;
extern volatile uint32_t	esp_shim_intmtx_intr_n_flag_shared;
extern volatile uint32_t	esp_shim_intmtx_intr_n_flag_level;
extern volatile uint32_t	esp_shim_intmtx_intr_n_flag_edge;
extern volatile uint32_t	esp_shim_intmtx_intr_n_ena_fail;
extern volatile uint32_t	esp_shim_intmtx_intr_n_dis_fail;
extern volatile uint32_t	esp_shim_intmtx_intr_n_unowned_isr;

/*
 *  cfg（esp_shim_intr_intmtx.cfg）が DEF_INH で登録する割込みハンドラ本体。
 *  線 n の入口。alloc 払い出し済みならそのハンドラを、そうでなければ
 *  esp_shim.c の shim_isr_tbl（blob が _set_isr で登録したもの）を呼ぶ。
 */
extern void esp_shim_intmtx_inthdr_1(void);
extern void esp_shim_intmtx_inthdr_2(void);
extern void esp_shim_intmtx_inthdr_3(void);
extern void esp_shim_intmtx_inthdr_4(void);
extern void esp_shim_intmtx_inthdr_5(void);
extern void esp_shim_intmtx_inthdr_6(void);
extern void esp_shim_intmtx_inthdr_7(void);
extern void esp_shim_intmtx_inthdr_8(void);
extern void esp_shim_intmtx_inthdr_9(void);
extern void esp_shim_intmtx_inthdr_10(void);
extern void esp_shim_intmtx_inthdr_11(void);
extern void esp_shim_intmtx_inthdr_12(void);
extern void esp_shim_intmtx_inthdr_13(void);
extern void esp_shim_intmtx_inthdr_14(void);
extern void esp_shim_intmtx_inthdr_15(void);

#endif /* ESP_SHIM_INTR_INTMTX_H */
