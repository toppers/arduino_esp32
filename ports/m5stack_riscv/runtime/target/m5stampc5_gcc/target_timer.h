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
 *  タイマドライバのターゲット依存部（M5Stamp-C5 / ESP32-C5 用・FMP3・esp-hal LL層版）
 *
 *  出典: asp3_esp_idf asp3/target/esp32c5_espidf/target_timer.h。単一コアの
 *  ため target_hrt_set_event/target_hrt_raise_event に prcid 引数を追加し
 *  （FMP3 の HRT 界面。musca_b1_gcc/target_timer.h 参照），
 *  target_hrt_clear_event(prcid) を新設した。診断用カウンタ
 *  （esp32c5_hrt_set_event_*・g_hrt_last_target）は落とした。
 *    - HRTCNT（us）はカウント値の1/16（較正済み．esp32c5.h参照．
 *      16.024 ticks/us実測＝16と0.15%以内で一致）．
 *    - 割込みの強制（過去時刻設定時のペンディング・raise_event）は，
 *      同じCPU割込み線に多重マップしたFROM_CPU_0ソース（INTPRI
 *      ペリフェラル．C3のSYSTEMレジスタとは異なるブロック）で行う
 *      （ASP3固有の機構のためLL化せずレジスタ直書きのまま）．
 *  SYSTIMERデバイス構造体インスタンスは esp-hal の
 *  esp32c5.peripherals.ld（リンカスクリプトからINCLUDE）が提供する．
 */

#ifndef TOPPERS_TARGET_TIMER_H
#define TOPPERS_TARGET_TIMER_H

#include <sil.h>
#include "esp32c5.h"

/*
 *  タイマ割込みハンドラ登録のための定数
 */
#define INTNO_TIMER        16                         /* 割込み番号（CLIC 外部線 16 = 最初の外部線。0-15 は内部線） */
#define INHNO_TIMER_PRC1   ((PRC1 << 16) | 16)        /* 割込みハンドラ番号（FMP3: prcid を上位 16 ビットへ） */
#define INTPRI_TIMER       (TMAX_INTPRI - 1)
#define INTATR_TIMER       TA_NULL

#ifndef TOPPERS_MACRO_ONLY

#include "hal/systimer_ll.h"

/*
 *  高分解能タイマの起動処理
 */
extern void target_hrt_initialize(intptr_t exinf);

/*
 *  高分解能タイマの停止処理
 */
extern void target_hrt_terminate(intptr_t exinf);

/*
 *  SYSTIMERの52bitカウンタの読出し（unit0．スナップショット方式）
 */
Inline uint64_t
esp32c5_systimer_read(void)
{
	uint32_t hi, lo;

	systimer_ll_counter_snapshot(&SYSTIMER, 0U);
	while (!systimer_ll_is_counter_value_valid(&SYSTIMER, 0U)) ;
	hi = systimer_ll_get_counter_value_high(&SYSTIMER, 0U);
	lo = systimer_ll_get_counter_value_low(&SYSTIMER, 0U);
	return(((uint64_t)hi << 32) | (uint64_t)lo);
}

/*
 *  現在時刻の読出し（HRTCNT＝us）
 */
Inline HRTCNT
target_hrt_get_current(void)
{
	return((HRTCNT)(esp32c5_systimer_read()
					/ ESP32C5_SYSTIMER_TICKS_PER_US));
}

/*
 *  タイマ割込みの強制（FROM_CPU_0ソースのアサート．levelソースの
 *  ためクリアはtarget_hrt_handler／target_hrt_terminateで行う）
 */
Inline void
target_timer_force_int(void)
{
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0, 1U);
}

/*
 *  次に割込みを発生させる時刻の設定
 */
Inline void
target_hrt_set_event(ID prcid, HRTCNT hrtcnt)
{
	uint64_t	current;
	uint64_t	target;

	(void) prcid;
	current = esp32c5_systimer_read();
	target = current + (uint64_t)hrtcnt * ESP32C5_SYSTIMER_TICKS_PER_US;

	/*
	 *  target0コンパレータへ比較値を設定する
	 *
	 *  「no time event」氾濫の根治（asp3 実施04）：oneshotアラームは発火後も
	 *  WORK_EN が残り，comparator target が過去になると level 再ラッチして
	 *  スプリアス再発火を繰り返す（handler->signal_time->time event 無し->
	 *  syslog 氾濫）．再arm 毎に disable->set->apply->enable の「クリーン再arm」
	 *  で古い level-latch をクリアしてから未来 target を武装する．
	 */
	systimer_ll_enable_alarm(&SYSTIMER, 0U, false);
	systimer_ll_set_alarm_target(&SYSTIMER, 0U, target);
	systimer_ll_apply_alarm_value(&SYSTIMER, 0U);
	systimer_ll_enable_alarm(&SYSTIMER, 0U, true);

	/*
	 *  設定完了時点で比較値を過ぎていたら割込みを強制する
	 *  （oneshotのコンパレータは過去時刻に対して発火しないため）
	 */
	if (esp32c5_systimer_read() >= target) {
		target_timer_force_int();
	}
}

/*
 *  高分解能タイマ割込みの要求
 */
Inline void
target_hrt_raise_event(ID prcid)
{
	(void) prcid;
	target_timer_force_int();
}

/*
 *  高分解能タイマ割込み要求のクリア
 */
Inline void
target_hrt_clear_event(ID prcid)
{
	(void) prcid;
	systimer_ll_enable_alarm(&SYSTIMER, 0U, false);
	systimer_ll_clear_alarm_int(&SYSTIMER, 0U);
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0, 0U);
}

/*
 *  割込みタイミングに指定する最大値
 */
#define HRTCNT_BOUND 4000000002U

/*
 *  高分解能タイマ割込みハンドラ
 */
extern void target_hrt_handler(void);

/*
 *  現在時刻の 64bit 読出し（us）
 *
 *  段4 Task 3（2026-09-14）: esp/shim/esp_shim.c の esp_shim_time_us()
 *  （ESP-IDF の esp_timer_get_time() 相当＝単調増加する 64bit マイクロ秒）が
 *  呼ぶ（diag_recorder.c は本マクロを呼ばない: MIE=0 の下で 32bit の
 *  esp32c5_systimer_read() を使う。2026-09-15 訂正）。S3/LX6 は CCOUNT の 32bit ラップを
 *  64bit 累積器で補う実装だが、C6 の SYSTIMER は 52bit・16MHz のフリーラン
 *  カウンタなので us へ割るだけでよい（2^52 / 16MHz = 8.9 年でラップ）。
 *
 *  実体は target_hrt64.c（esp32c5_systimer_read64()）。上の
 *  esp32c5_systimer_read() は hi -> lo を 1 回ずつ読むだけで、カーネル内の
 *  呼び手（CPU ロック下）では十分だが、本マクロの呼び手はタスク文脈・Wi-Fi ISR
 *  からで、hi と lo の間で割り込まれて別の snapshot が挟まると lo のラップ
 *  （268 秒ごと）を跨いで 2^32 ずれた値になる。esp32c5_systimer_read64() は
 *  esp-idf hal/systimer_hal.c の systimer_hal_get_counter_value() と同じ
 *  lo -> hi -> lo の再読ループで整合を取る（Task 3 minor round）。
 *
 *  マクロ + 別 TU にしてある理由: 本ヘッダはカーネル全 TU に入り、Inline 関数を
 *  1 つ足すと未使用でも GCC の関数番号（.LFBnnn）がずれて ELF の sha256 が変わる
 *  （C6 seam は app_desc に ELF の sha256 を入れるので hello の app_xip.bin まで
 *  変わる。Task 3 で実測）。ただし hello は preset golden ではないので、この理由は
 *  「変えない方が比較が楽」という緩いもの（soft）であり、必要なら Inline 関数に
 *  戻してよい。target_hrt64.c は cmake の A1_C5_WIFI ブロックだけがリンクする。
 */
extern int64_t esp32c5_systimer_read64_us(void);
#define target_hrt_get_current64()	esp32c5_systimer_read64_us()

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_TARGET_TIMER_H */
