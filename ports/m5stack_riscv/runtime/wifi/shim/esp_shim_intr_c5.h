/*
 *  TOPPERS/FMP3 ESP32-C5 移植 --
 *  Wi-Fi blob 用 CLIC 割込み線シム（計画 3 段4 Task 3、2026-09-16）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  型は C6 の esp/shim/esp_shim_intr_intmtx.{c,h,cfg}（blob の線 1..15 を静的
 *  宣言し、_set_intr の要求をルーティングする）。C6 の intmtx シムは
 *  esp_intr_alloc 系（P4 の esp_shim_intr_clic.c 型のスロット払い出し）も
 *  同居させていたが、Wi-Fi 構成では呼び手が無い（C6 段4 3-5 節）ので C5 では
 *  持たない。線表は esp_shim_intr_c5_lines.h（.cfg と共用の単一真実源）。
 *
 *  なぜ C6 の esp_shim_intr_intmtx.* や P4 の esp_shim_intr_clic.* に C5 分岐を
 *  足さないか: どちらも golden preset（seam-c6-wifi / seam-p4-*）がコンパイルする
 *  ファイルで、C6/P4 の seam 像は app_desc の app_elf_sha256 に ELF 全体の
 *  sha256 を持つため、行の挿入で golden が動く（.steering/20260915-c5-plan/
 *  stage4/asp3-c5-inventory.md 4 節の実測）。C5 専用の新規ファイルなら 18 golden は
 *  構造的に不変。
 */
#ifndef ESP_SHIM_INTR_C5_H
#define ESP_SHIM_INTR_C5_H

#include "esp_shim_intr_c5_lines.h"

/*  cfg の DEF_INH 用（FMP3: prcid を上位 16 ビットへ。target_timer.h と同じ形）  */
#define ESP_SHIM_C5_WIFI_INHNO(n)	((PRC1 << 16) | ESP_SHIM_C5_WIFI_LINE(n))

#ifndef TOPPERS_MACRO_ONLY
#include <stdint.h>

/*
 *  blob の _set_intr(cpu, src, intr_num, prio) から: ソース src を CLIC 線
 *  ESP_SHIM_C5_WIFI_LINE(intr_num) へ配線する（割込みマトリクス MAP）。
 *  intr_num が 1..15 の外なら配線せず -1（syslog 1 行）。
 */
extern int esp_shim_c5_wifi_route(int intr_source, int intr_num);
/*  blob の _clear_intr: ソースを線 0（切断）へ。  */
extern int esp_shim_c5_wifi_unroute(int intr_source, int intr_num);
/*
 *  blob の _set_isr(n, f, arg): esp_shim.c の shim_isr_tbl[n] へ登録し、
 *  線 ESP_SHIM_C5_WIFI_LINE(n) を ena_int する（f != NULL のとき）。
 *  戻り値は ena_int の ER（範囲外なら E_PAR）。
 */
extern int esp_shim_c5_wifi_set_isr(int32_t n, void *f, void *arg);
/*
 *  blob の _ints_on/_ints_off(mask): mask の bit n ごとに線 ESP_SHIM_C5_WIFI_LINE(n)
 *  の CLIC IE を 1/0 にする（bit 0 と 16 以上は無視）。割込み禁止下で行う。
 */
extern void esp_shim_c5_wifi_ints_on(uint32_t mask);
extern void esp_shim_c5_wifi_ints_off(uint32_t mask);

/*  診断: 配線の帳簿（A1_C5_WIFI_DIAG のときだけ中身がある）  */
extern void esp_shim_c5_wifi_diag_dump(void);
/*  DEF_INH の入口（1..15。esp_shim_intr_c5.cfg が参照）  */
extern void esp_shim_c5_wifi_inthdr_1(void);
extern void esp_shim_c5_wifi_inthdr_2(void);
extern void esp_shim_c5_wifi_inthdr_3(void);
extern void esp_shim_c5_wifi_inthdr_4(void);
extern void esp_shim_c5_wifi_inthdr_5(void);
extern void esp_shim_c5_wifi_inthdr_6(void);
extern void esp_shim_c5_wifi_inthdr_7(void);
extern void esp_shim_c5_wifi_inthdr_8(void);
extern void esp_shim_c5_wifi_inthdr_9(void);
extern void esp_shim_c5_wifi_inthdr_10(void);
extern void esp_shim_c5_wifi_inthdr_11(void);
extern void esp_shim_c5_wifi_inthdr_12(void);
extern void esp_shim_c5_wifi_inthdr_13(void);
extern void esp_shim_c5_wifi_inthdr_14(void);
extern void esp_shim_c5_wifi_inthdr_15(void);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* ESP_SHIM_INTR_C5_H */
