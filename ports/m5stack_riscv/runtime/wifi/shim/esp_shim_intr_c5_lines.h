/*
 *  TOPPERS/FMP3 ESP32-C5 移植 --
 *  Wi-Fi blob 用 CLIC 割込み線シム（esp_shim_intr_c5.{c,cfg}）の線表の単一真実源
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef ESP_SHIM_INTR_C5_LINES_H
#define ESP_SHIM_INTR_C5_LINES_H

/*
 *  ============================================================================
 *  何のためのヘッダか（計画 3 段4 Task 3、2026-09-16）
 *  ============================================================================
 *  C6 の esp/shim/esp_shim_intr_intmtx_lines.h（blob 用の CPU 割込み線 1..15）と
 *  P4 の esp/shim/esp_shim_intr_clic_lines.h（CLIC の予約線表と #error 検査）を型に、
 *  C5 の Wi-Fi blob が `_set_intr(cpu, src, intr_num, prio)` で選ぶ番号
 *  `intr_num`（C6 実測 1、src 0/2 = WIFI_MAC/WIFI_PWR）を FMP3 の INTNO
 *  （= CLIC 線番号、外部線 16..47）へ写像する表と、その衝突検査を置く。
 *  .c と .cfg は同じ線でなければならない（S3 で実際に破れた反省 =
 *  esp_shim_intr_lines.h 冒頭）ので、両方がこのヘッダを読む。
 *
 *  【写像】line = ESP_SHIM_C5_WIFI_LINE_BASE + intr_num（intr_num 1..15 -> 線 25..39）
 *  asp3 C5 は intr_num + 16（asp3 の INTNO 体系 = CLIC 線 - 16）だったが、FMP3 C5 の
 *  INTNO は CLIC 線そのものなので +16 では 17 = INTNO_SIO と衝突する。C6 と同じ
 *  15 本を静的に CFG_INT/DEF_INH する（純粋な動的プールは CFG_INT が静的 API なので
 *  構造的に不可能 = esp_shim_intr_clic_lines.h の記述どおり）。
 *
 *  【予約線（使えない）】いずれも出典つき。
 *    16      INTNO_TIMER（systimer tick）        fmp3/target/m5stampc5_gcc/target_timer.h
 *    17      INTNO_SIO（USB-Serial/JTAG）        fmp3/target/m5stampc5_gcc/target_syssvc.h
 *    18      INTNO1（テスト）                    fmp3/target/m5stampc5_gcc/target_test.h
 *    19      INTNO_UNOPTED（= INTNO1 + 1）       fmp3_core/test/test_dcre5.h（未登録でなければならない）
 *    20, 21  INTNO2 / INTNO3（テスト）           target_test.h
 *    22      IDF が割込みマトリクスの切断先に使う番号（INT_MUX_DISABLED_INTNO 6 + 16）
 *            fmp3/arch/riscv_gcc/esp32c5/esp32c5.h の INTMTX_MAP コメント（安全側で空ける）
 *    40..44  IDF の vectors_clic.S が予約（T1WDT/CACHEERR/MEMPROT/ASSIST_DEBUG/IPC_ISR
 *            = 外部番号 24..28 + 16。P4 の esp_shim_intr_clic_lines.h と同じ番号）
 *  【空いている外部線】23..39, 45..47。本シムは 25..39 の 15 本を使う（23/24 と
 *  45..47 を将来の周辺/テスト用に残す）。
 *
 *  【優先度】cfg の CFG_INT は -2（CLIC level 2 = INT_IPM(-2) の CTL）。blob の
 *  要求 prio は無視する（S3/C6/asp3 と同じ方針）。ATTR（level/machine/SHV=0）は
 *  chip_initialize と clic_config_int が設定する（chip 層）。
 */

#include "esp32c5.h"
#include "target_syssvc.h"
#include "target_timer.h"

#define ESP_SHIM_C5_WIFI_LINE_BASE		24
#define ESP_SHIM_C5_WIFI_INTR_NUM_MIN	1		/* blob が選ぶ intr_num の範囲（C6 と同じ 1..15） */
#define ESP_SHIM_C5_WIFI_INTR_NUM_MAX	15
#define ESP_SHIM_C5_WIFI_LINE(n)		(ESP_SHIM_C5_WIFI_LINE_BASE + (n))
#define ESP_SHIM_C5_WIFI_LINE_MIN		ESP_SHIM_C5_WIFI_LINE(ESP_SHIM_C5_WIFI_INTR_NUM_MIN)	/* 25 */
#define ESP_SHIM_C5_WIFI_LINE_MAX		ESP_SHIM_C5_WIFI_LINE(ESP_SHIM_C5_WIFI_INTR_NUM_MAX)	/* 39 */
#define ESP_SHIM_C5_WIFI_INTPRI			(-2)

/*
 *  ============================================================================
 *  ビルド時の機械照合（条件で分岐せず、線の値そのものを見る = P4 型）
 *  ============================================================================
 */
#if !defined(CLIC_EXT_OFFSET) || !defined(CLIC_TNUM_INTNO)
#error "CLIC_EXT_OFFSET / CLIC_TNUM_INTNO が見えていない（esp32c5.h）。範囲検査が素通りする"
#endif
#if !defined(INTNO_SIO)
#error "INTNO_SIO が見えていない（target_syssvc.h）。コンソール線の衝突検査が素通りする"
#endif
#if !defined(INTNO_TIMER)
#error "INTNO_TIMER が見えていない（target_timer.h）。tick 線の衝突検査が素通りする"
#endif

/*  (A) 範囲: 外部線（CLIC_EXT_OFFSET 以上）かつ CLIC_TNUM_INTNO 未満。  */
#if (ESP_SHIM_C5_WIFI_LINE_MIN < CLIC_EXT_OFFSET) || (ESP_SHIM_C5_WIFI_LINE_MAX > (CLIC_TNUM_INTNO - 1))
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が外部線の範囲(CLIC_EXT_OFFSET .. CLIC_TNUM_INTNO-1)の外にある"
#endif

/*  (B) tick / コンソール線との衝突。  */
#if (INTNO_TIMER >= ESP_SHIM_C5_WIFI_LINE_MIN) && (INTNO_TIMER <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が tick(INTNO_TIMER) と衝突している"
#endif
#if (INTNO_SIO >= ESP_SHIM_C5_WIFI_LINE_MIN) && (INTNO_SIO <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が コンソール(INTNO_SIO) と衝突している"
#endif

/*
 *  (C) テスト線 18/19/20/21 と IDF の切断先 22 は **値を直接見る**（INTNO1/2/3 は
 *  target_test.h にあり、テスト以外のビルドでは見えないので、見えないマクロとの
 *  比較で素通りする事故を避ける）。
 */
#define ESP_SHIM_C5_FORBIDDEN_LO	18
#define ESP_SHIM_C5_FORBIDDEN_HI	22
#if (ESP_SHIM_C5_WIFI_LINE_MIN <= ESP_SHIM_C5_FORBIDDEN_HI) && (ESP_SHIM_C5_WIFI_LINE_MAX >= ESP_SHIM_C5_FORBIDDEN_LO)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が INTNO1/INTNO_UNOPTED/INTNO2/INTNO3/IDF 切断先(18..22) と重なる"
#endif
/*  (C') INTNO1/2/3 が見えているとき（test ビルド）は名前でも検査する。  */
#if defined(INTNO1) && (INTNO1 >= ESP_SHIM_C5_WIFI_LINE_MIN) && (INTNO1 <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が INTNO1 と衝突している"
#endif
#if defined(INTNO2) && (INTNO2 >= ESP_SHIM_C5_WIFI_LINE_MIN) && (INTNO2 <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が INTNO2 と衝突している"
#endif
#if defined(INTNO3) && (INTNO3 >= ESP_SHIM_C5_WIFI_LINE_MIN) && (INTNO3 <= ESP_SHIM_C5_WIFI_LINE_MAX)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が INTNO3 と衝突している"
#endif

/*  (D) IDF vectors_clic.S の予約 40..44 と重ならない。  */
#define ESP_SHIM_C5_IDF_RESERVED_LO	40
#define ESP_SHIM_C5_IDF_RESERVED_HI	44
#if (ESP_SHIM_C5_WIFI_LINE_MIN <= ESP_SHIM_C5_IDF_RESERVED_HI) && (ESP_SHIM_C5_WIFI_LINE_MAX >= ESP_SHIM_C5_IDF_RESERVED_LO)
#error "esp_shim_intr_c5: Wi-Fi 用 CLIC 線が IDF vectors_clic.S の予約線(40..44) と重なる"
#endif

/*  (E) 本数: .cfg と .c は 1..15 を無条件に展開するので範囲は固定。  */
#if (ESP_SHIM_C5_WIFI_INTR_NUM_MIN != 1) || (ESP_SHIM_C5_WIFI_INTR_NUM_MAX != 15)
#error "esp_shim_intr_c5: intr_num の範囲は 1..15 固定（.c/.cfg の展開本数と一緒に直すこと）"
#endif

#endif /* ESP_SHIM_INTR_C5_LINES_H */
