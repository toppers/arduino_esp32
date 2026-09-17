/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  SDIO スレーブ（ESP32-C6）のボード側電源・リセット制御
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef P4SDIO_BOARD_H
#define P4SDIO_BOARD_H

#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>

/*
 *  C6 の電源を入れる（ボードにより実体が違う。`p4sdio_pins.h` の
 *  `P4SDIO_PWR_KIND` で分岐）。
 *    P4SDIO_PWR_NONE     … 何もしない（常時給電のボード）
 *    P4SDIO_PWR_EXPANDER … I2C IO エキスパンダの 1 ビットを立てる
 *  戻り値は 0=OK、負なら失敗した段の番号。
 */
extern int	p4sdio_board_c6_power(bool on);

/*
 *  スレーブのハードリセット（active low）＋ブート待ち。
 *  タスクコンテキストから呼ぶこと（`dly_tsk` を使う）。
 */
extern void	p4sdio_board_slave_reset(void);

/*
 *  ボードの状態を印字する（拡張器の全ポートなど。書込みはしない）。
 */
extern void	p4sdio_board_report(void);

/*
 *  「触ってはならないビット」ガードの実演（positive control）。
 *  意図的に禁止ビットへの書込みを要求し、**拒否されること**を確かめる。
 *  戻り値 true = ガードが働いた（合格）。
 */
extern bool	p4sdio_board_guard_selftest(void);

/*  診断カウンタ  */
extern volatile uint32_t	p4sdio_board_n_write;		/* 拡張器への書込み回数 */
extern volatile uint32_t	p4sdio_board_n_guard_hit;	/* ガードが拒否した回数 */
extern volatile uint32_t	p4sdio_board_n_other_dev;	/* 想定外デバイスへの書込み(必ず 0) */

/*  拡張器の前後スナップショット（8 レジスタ: 0x01/03/05/07/09/0B/0D/0F）  */
extern volatile uint32_t	p4sdio_exp_before[8];
extern volatile uint32_t	p4sdio_exp_after[8];

#endif /* P4SDIO_BOARD_H */
