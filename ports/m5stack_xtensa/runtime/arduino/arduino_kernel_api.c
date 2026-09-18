/*
 *  スケッチから使える FMP3 カーネル API の薄いラッパ（実装）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  設計の理由はヘッダ側のコメントを参照。
 */

#include <kernel.h>
#include <t_syslog.h>
#include "kernel_cfg.h"
#include "arduino_kernel_api.h"

/*
 *  ★RELTIM の単位はこのポートではマイクロ秒である（README の「制約」）。
 *  FreeRTOS の tick とも、素の TOPPERS の慣行とも違うので、API 名にも
 *  `_us` と書いて取り違えを防ぐ。
 */

static void (*volatile cyclic_callback)(void);
static void (*volatile alarm_callback)(void);

void
toppers_fmp3_sample_cyclic(intptr_t exinf)
{
	void	(*callback)(void) = cyclic_callback;

	(void) exinf;
	if (callback != NULL) {
		callback();
	}
}

void
toppers_fmp3_sample_alarm(intptr_t exinf)
{
	void	(*callback)(void) = alarm_callback;

	(void) exinf;
	if (callback != NULL) {
		callback();
	}
}

int32_t
toppers_fmp3_task_create(void (*body)(intptr_t), intptr_t exinf,
						 int32_t priority, void *stack, size_t stack_size)
{
	T_CTSK	ctsk;
	ID		tskid;
	ER_ID	result;

	if (body == NULL || stack == NULL || stack_size == 0U) {
		return((int32_t) E_PAR);
	}
	ctsk.tskatr = TA_NULL;
	ctsk.exinf = (EXINF) exinf;
	ctsk.task = (TASK) body;
	ctsk.itskpri = (PRI) priority;
	ctsk.stksz = stack_size;
	ctsk.stk = (STK_T *) stack;

	result = acre_tsk(&ctsk);
	if (result < 0) {
		return((int32_t) result);
	}
	tskid = (ID) result;
	return((int32_t) tskid);
}

int32_t
toppers_fmp3_task_activate(int32_t task_id)
{
	return((int32_t) act_tsk((ID) task_id));
}

int32_t
toppers_fmp3_task_wakeup(int32_t task_id)
{
	return((int32_t) wup_tsk((ID) task_id));
}

int32_t
toppers_fmp3_task_sleep(void)
{
	return((int32_t) slp_tsk());
}

int32_t
toppers_fmp3_task_delay_us(uint32_t micro_seconds)
{
	return((int32_t) dly_tsk((RELTIM) micro_seconds));
}

int32_t
toppers_fmp3_task_self(int32_t *task_id)
{
	ID	tskid;
	ER	result;

	if (task_id == NULL) {
		return((int32_t) E_PAR);
	}
	result = get_tid(&tskid);
	if (result >= 0) {
		*task_id = (int32_t) tskid;
	}
	return((int32_t) result);
}

int32_t
toppers_fmp3_task_change_priority(int32_t task_id, int32_t priority)
{
	return((int32_t) chg_pri((ID) task_id, (PRI) priority));
}

/*
 *  ★周期は cfg で固定である（`TOPPERS_FMP3_CYCLIC_PERIOD_US`）。静的に作った
 *  周期通知の周期を後から変える API は TOPPERS に無い（`T_CCYC.cyctim` は
 *  生成時に決まる）。可変にしたければ AID_CYC + acre_cyc だが、この縮約版の
 *  ためだけに ID 空間を増やす必要は無いと判断した。
 */
int32_t
toppers_fmp3_cyclic_start(void (*callback)(void))
{
	if (callback == NULL) {
		return((int32_t) E_PAR);
	}
	/*  走っていれば止めてから付け替える（多重 sta_cyc を避ける）。 */
	(void) stp_cyc(SAMPLE_CYC);
	cyclic_callback = callback;
	return((int32_t) sta_cyc(SAMPLE_CYC));
}

int32_t
toppers_fmp3_cyclic_stop(void)
{
	ER	result = stp_cyc(SAMPLE_CYC);

	cyclic_callback = NULL;
	return((int32_t) result);
}

int32_t
toppers_fmp3_alarm_start(void (*callback)(void), uint32_t after_us)
{
	if (callback == NULL) {
		return((int32_t) E_PAR);
	}
	(void) stp_alm(SAMPLE_ALM);
	alarm_callback = callback;
	return((int32_t) sta_alm(SAMPLE_ALM, (RELTIM) after_us));
}

int32_t
toppers_fmp3_alarm_stop(void)
{
	ER	result = stp_alm(SAMPLE_ALM);

	alarm_callback = NULL;
	return((int32_t) result);
}
