/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  SYSTIMER の 64bit 読出し（割込み・プリエンプションに対して整合）
 *  （M5Stamp-C5 / ESP32-C5 用・FMP3・段4 Task 3 minor round、2026-09-14）
 *
 *  target_timer.h の target_hrt_get_current64() マクロの実体。呼び手は
 *  esp/shim/esp_shim.c の esp_shim_time_us()（タスク文脈）。
 *  diag_recorder.c は 32bit の esp32c5_systimer_read() を MIE=0 で使い、
 *  こちらは呼ばない（fix wave 2 でコメント訂正）。カーネル用の
 *  esp32c5_systimer_read()（target_timer.h。CPU ロック下で使う）と違い、
 *  hi と lo の読出しの間に別文脈の snapshot（timer_unit_update）が挟まって
 *  ラッチが更新されても、lo -> hi -> lo の再読で整合した組を返す。
 *  出典: esp-idf components/hal/systimer_hal.c:46-68
 *  systimer_hal_get_counter_value()（lo/hi/lo 形）。
 *  リンクは cmake/a1_c5_stage1.cmake（段4 で足す） の A1_C5_WIFI ブロックだけ
 *  （非 wifi の hello には入らない）。
 */

#include <kernel.h>
#include "target_timer.h"

static uint64_t
esp32c5_systimer_read64(void)
{
	uint32_t	lo, lo_start, hi;

	systimer_ll_counter_snapshot(&SYSTIMER, 0U);
	while (!systimer_ll_is_counter_value_valid(&SYSTIMER, 0U)) ;
	lo_start = systimer_ll_get_counter_value_low(&SYSTIMER, 0U);
	do {
		lo = lo_start;
		hi = systimer_ll_get_counter_value_high(&SYSTIMER, 0U);
		lo_start = systimer_ll_get_counter_value_low(&SYSTIMER, 0U);
	} while (lo_start != lo);
	return(((uint64_t)hi << 32) | (uint64_t)lo);
}

int64_t
esp32c5_systimer_read64_us(void)
{
	return((int64_t)(esp32c5_systimer_read64() / ESP32C5_SYSTIMER_TICKS_PER_US));
}
