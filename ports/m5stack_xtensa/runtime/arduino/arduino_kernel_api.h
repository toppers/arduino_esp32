/*
 *  スケッチから使える FMP3 カーネル API の薄いラッパ（宣言）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ★なぜラッパを挟むか
 *    スケッチは `kernel.h` を見られない（platform の include 路に入って
 *    いない。実測: `#include <kernel.h>` は "No such file or directory"）。
 *    見せる案もあったが、そうすると `T_CTSK` や `ER`・`ID` といった
 *    カーネルの型と ABI が**配布物の公開面**になり、fmp3_core の pin を
 *    上げるたびに利用者のスケッチが壊れうる。⇒ 素の C の型だけを使う
 *    関数を stage 側に置き、宣言だけをライブラリの
 *    `ToppersFMP3_Kernel.h` から配る。
 *
 *  ★スタックは呼び出し側が渡す
 *    stage 側にスタックを持つと、この API を使わないスケッチまで RAM を
 *    払う（minimal profile は全 8 板の既定）。⇒ 実体はスケッチの静的配列。
 */

#ifndef TOPPERS_ARDUINO_KERNEL_API_H
#define TOPPERS_ARDUINO_KERNEL_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  戻り値は FMP3 の ER をそのまま返す（0 = E_OK、負 = エラー）。 */

int32_t	toppers_fmp3_task_create(void (*body)(intptr_t), intptr_t exinf,
								 int32_t priority, void *stack, size_t stack_size);
int32_t	toppers_fmp3_task_activate(int32_t task_id);
int32_t	toppers_fmp3_task_wakeup(int32_t task_id);
int32_t	toppers_fmp3_task_sleep(void);
int32_t	toppers_fmp3_task_delay_us(uint32_t micro_seconds);
int32_t	toppers_fmp3_task_self(int32_t *task_id);
int32_t	toppers_fmp3_task_change_priority(int32_t task_id, int32_t priority);

/*  周期ハンドラとアラームハンドラは cfg で静的に作ってある（AID_CYC /
 *  AID_ALM は「同じ種の静的生成が 1 個以上」を要求し、minimal profile には
 *  元々 1 個も無かったため。実測: cfg が E_OBJ で止まる）。ここで登録する
 *  のは、その静的ハンドラから呼ばれるコールバックである。 */
int32_t	toppers_fmp3_cyclic_start(void (*callback)(void));
int32_t	toppers_fmp3_cyclic_stop(void);
int32_t	toppers_fmp3_alarm_start(void (*callback)(void), uint32_t after_us);
int32_t	toppers_fmp3_alarm_stop(void);

/*  周期通知の周期は cfg 固定。スケッチが表示に使えるよう公開する。 */
#define TOPPERS_FMP3_CYCLIC_PERIOD_US	1000000U

/*  cfg から参照される静的ハンドラの本体（TNFY_HANDLER の通知先）。 */
void	toppers_fmp3_sample_cyclic(intptr_t exinf);
void	toppers_fmp3_sample_alarm(intptr_t exinf);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_ARDUINO_KERNEL_API_H */
