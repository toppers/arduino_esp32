/*
 *  TOPPERS/FMP3 ESP32-C6 移植 --
 *  INTMTX 版 ESP-IDF 割込み確保シムが使う CPU 割込み線の単一真実源
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef ESP_SHIM_INTR_INTMTX_LINES_H
#define ESP_SHIM_INTR_INTMTX_LINES_H

/*
 *  ============================================================================
 *  なぜこのヘッダが在るか（2026-09-14・段4 Task 3）
 *  ============================================================================
 *  型は P4 の esp/shim/esp_shim_intr_clic_lines.h（CLIC 版）。S3/LX6 で
 *  「シムと cfg の線番号が実際に食い違った」事故（esp_shim_intr_lines.h
 *  冒頭）の再発防止として、線の値は**このヘッダにしか書かない**。
 *  esp_shim_intr_intmtx.c と esp_shim_intr_intmtx.cfg の両方がここを読む。
 *
 *  ============================================================================
 *  C6 の線の使い方（段1 で決めた配置。fmp3/target/m5nanoc6_gcc/target_kernel_impl.c）
 *  ============================================================================
 *  CPU 割込み線は 1..31（線 0 は入口処理の番兵。TargetCheckCfgInt が cfg 時に
 *  拒否する。段4 fix wave 1 (C)）。
 *
 *    1..15   Wi-Fi/BT blob 用（本ヘッダの範囲）。blob は `_set_intr` で
 *            **自分で線番号を決めて**要求する（asp3 C6 実測: src=0/2 -> 線 1）。
 *            それを受けるため 1..15 を cfg で事前に CFG_INT/DEF_INH しておく
 *            （FMP3 に acre_int は無く、cfg に無い線は実行時に開けられない）。
 *    16      INTNO_TIMER（SYSTIMER target0 + FROM_CPU_0）  target_timer.h
 *    17      INTNO_SIO（USB-Serial/JTAG または UART0）      target_syssvc.h
 *    18      INTNO1（テスト。FROM_CPU_1）                   target_test.h
 *    19      INTNO_UNOPTED（テストが「未登録の線」として使う）test_dcre5.h
 *    20      INTNO2（テスト。FROM_CPU_2）                   target_test.h
 *    21      INTNO3（テスト。FROM_CPU_3）                   target_test.h
 *    22..31  空き
 *
 *  asp3 の cfg は線 1/2/3 だけを開けていた（esp/common/wifi/esp_shim.cfg:120-125）。
 *  段4 計画 F3 は 1..15 を静的予約する（blob が別の線を選んだときに
 *  「cfg に無いので開けられない」で止まらないようにするため）。
 *  15 本 x (CFG_INT + DEF_INH) の生成コストは intcfg テーブルのフラグ 15 個と
 *  inh テーブル 15 エントリ（各 1 関数ポインタ）で、RAM 数十バイトである。
 */

/*
 *  参照する側の定義（線の衝突検査に使う）。
 *   - INTNO_TIMER  ... tick が使う線（target_timer.h）
 *   - INTNO_SIO    ... コンソールが使う線（target_syssvc.h）
 *   - INTMTX_TNUM_INT ... 線の本数（intmtx_kernel_impl.h。ただしそれは
 *     kernel 内部ヘッダなので、ここでは esp32c6.h から導ける上限 31 を直接書く）
 */
#include <kernel.h>			/* PRC1, HRTCNT 等（target_timer.h の前提） */
#include "target_timer.h"
#include "target_syssvc.h"

/*
 *  ============================================================================
 *  スロット（= 線）の範囲
 *  ============================================================================
 *  スロット i（0 起点）は線 ESP_SHIM_INTMTX_LINE_MIN + i。連番にしてあるのは
 *  実機ログで「線 n = スロット n-1」と即座に読めるようにするため。
 */
#define ESP_SHIM_INTMTX_LINE_MIN	1
#define ESP_SHIM_INTMTX_LINE_MAX	15
#define ESP_SHIM_INTMTX_INTR_NSLOT	(ESP_SHIM_INTMTX_LINE_MAX - ESP_SHIM_INTMTX_LINE_MIN + 1)
#define ESP_SHIM_INTMTX_LINE_OF_SLOT(i)	(ESP_SHIM_INTMTX_LINE_MIN + (i))
#define ESP_SHIM_INTMTX_SLOT_OF_LINE(n)	((n) - ESP_SHIM_INTMTX_LINE_MIN)

/*
 *  DEF_INH に書く割込みハンドラ番号。FMP3 の C6 port は INHNO を
 *  (prcid << 16) | 線番号 とする（fmp3/arch/riscv_gcc/esp32c6/chip_kernel.py の
 *  INHNO_VALID、target_timer.h の INHNO_TIMER_PRC1 と同じ形）。
 */
#define ESP_SHIM_INTMTX_INHNO(n)	((PRC1 << 16) | (n))

/*
 *  全スロット共通の FMP3 割込み優先度（外部表現）。
 *  S3/LX6 の esp_shim.cfg（CFG_INT(n, { TA_NULL, -2 })）と同じ -2。
 *  blob の _set_intr は PLIC_MX の優先度レジスタへ内部表現 2 を書く
 *  （esp_wifi_adapter.c の C6 分岐）ので、cfg 側の -2（INT_IPM で 2）と一致する。
 *  ここを変えるときは両方を一緒に変えること。
 */
#define ESP_SHIM_INTMTX_INTR_INTPRI	(-2)

/*
 *  ============================================================================
 *  ビルド時の機械照合
 *  ============================================================================
 *  esp_shim_intr_lines.h（S3/LX6）・esp_shim_intr_clic_lines.h（P4）と同じ
 *  作法: 条件で分岐せず、線の値そのものを見る。
 */

/*  (0) 参照する定義が見えていること（見えないと下の #if が素通りする）。  */
#if !defined(INTNO_TIMER)
#error "INTNO_TIMER が見えていない（target_timer.h）。tick 線の衝突検査が素通りする"
#endif
#if !defined(INTNO_SIO)
#error "INTNO_SIO が見えていない（target_syssvc.h）。コンソール線の衝突検査が素通りする"
#endif
#if !defined(PRC1)
#error "PRC1 が見えていない（kernel.h）。INHNO を組み立てられない"
#endif

/*  (A) tick 線（16）と重ならないこと。  */
#if (ESP_SHIM_INTMTX_LINE_MIN <= INTNO_TIMER) && (INTNO_TIMER <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が tick(INTNO_TIMER) を含んでいる。範囲を直すこと"
#endif

/*  (B) コンソール線（17）と重ならないこと。重なるとログが消える形で壊れる。  */
#if (ESP_SHIM_INTMTX_LINE_MIN <= INTNO_SIO) && (INTNO_SIO <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が コンソール(INTNO_SIO) を含んでいる。範囲を直すこと"
#endif

/*
 *  (C) テスト線 18/20/21（INTNO1/2/3、target_test.h）と 19（INTNO_UNOPTED）。
 *  target_test.h はテスト以外のビルドでは見えないので**値を直接見る**
 *  （見えていないマクロと比べて素通りする事故を避けるため）。
 *  target_kernel_impl.c の esp32c6_intmtx_route(FROM_CPU_1/2/3 -> 18/20/21) が正本。
 */
#define ESP_SHIM_INTMTX_FORBIDDEN_18	18
#define ESP_SHIM_INTMTX_FORBIDDEN_19	19
#define ESP_SHIM_INTMTX_FORBIDDEN_20	20
#define ESP_SHIM_INTMTX_FORBIDDEN_21	21
#if (ESP_SHIM_INTMTX_LINE_MIN <= ESP_SHIM_INTMTX_FORBIDDEN_18) && (ESP_SHIM_INTMTX_FORBIDDEN_18 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線 18 は INTNO1（target_test.h / FROM_CPU_1）。範囲を直すこと"
#endif
#if (ESP_SHIM_INTMTX_LINE_MIN <= ESP_SHIM_INTMTX_FORBIDDEN_19) && (ESP_SHIM_INTMTX_FORBIDDEN_19 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線 19 は INTNO_UNOPTED（test_dcre5.h。登録してはいけない線）。範囲を直すこと"
#endif
#if (ESP_SHIM_INTMTX_LINE_MIN <= ESP_SHIM_INTMTX_FORBIDDEN_20) && (ESP_SHIM_INTMTX_FORBIDDEN_20 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線 20 は INTNO2（target_test.h / FROM_CPU_2）。範囲を直すこと"
#endif
#if (ESP_SHIM_INTMTX_LINE_MIN <= ESP_SHIM_INTMTX_FORBIDDEN_21) && (ESP_SHIM_INTMTX_FORBIDDEN_21 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線 21 は INTNO3（target_test.h / FROM_CPU_3）。範囲を直すこと"
#endif

/*
 *  (C') target_test.h が見えているビルド（テスト構成）では、マクロの値そのもの
 *  とも比べる（値が動いたときに (C) の直値と両方で捕まえる。Task 3 minor round）。
 */
#if defined(INTNO1)
#if (ESP_SHIM_INTMTX_LINE_MIN <= INTNO1) && (INTNO1 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が INTNO1（target_test.h）を含んでいる"
#endif
#endif
#if defined(INTNO2)
#if (ESP_SHIM_INTMTX_LINE_MIN <= INTNO2) && (INTNO2 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が INTNO2（target_test.h）を含んでいる"
#endif
#endif
#if defined(INTNO3)
#if (ESP_SHIM_INTMTX_LINE_MIN <= INTNO3) && (INTNO3 <= ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が INTNO3（target_test.h）を含んでいる"
#endif
#endif

/*
 *  (D) 有効範囲。線 0 は存在しない（TMIN_INTNO=1、chip_kernel_impl.h）。
 *  上限 31 は esp32c6.h / intmtx_kernel_impl.h の INTMTX_TNUM_INT。
 */
#if (ESP_SHIM_INTMTX_LINE_MIN < 1) || (ESP_SHIM_INTMTX_LINE_MAX > 31) \
 || (ESP_SHIM_INTMTX_LINE_MIN > ESP_SHIM_INTMTX_LINE_MAX)
#error "esp_shim_intr_intmtx: 線の範囲が 1..31 の外か、MIN > MAX になっている"
#endif

/*
 *  (E) スロット数と cfg/.c の展開本数が食い違っていないこと。
 *  esp_shim_intr_intmtx.cfg と esp_shim_intr_intmtx.c は 1..15 を**文字どおり
 *  15 本**展開している。範囲を変えるときはその 2 ファイルも一緒に直す。
 */
#if (ESP_SHIM_INTMTX_INTR_NSLOT != 15) || (ESP_SHIM_INTMTX_LINE_MIN != 1)
#error "esp_shim_intr_intmtx: NSLOT/LINE_MIN を変えたら .cfg と .c の展開（1..15）も一緒に直すこと"
#endif

#endif /* ESP_SHIM_INTR_INTMTX_LINES_H */
