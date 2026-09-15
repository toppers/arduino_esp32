/*
 *  プロセッサ間割込み（M5Stamp-C5 / ESP32-C5・単一コア）
 *
 *  kernel/task.h:370 の update_schedtsk_dsp は p_pcb != p_my_pcb のときだけ
 *  request_dispatch_prc を呼ぶ。TNUM_PRCID=1 では到達しない。
 *  同様に request_ext_ker（startup.c:298）・request_set_hrt_event
 *  （time_event.c:431）も他プロセッサ向けにしか呼ばれない。
 *  到達したら設計の前提が崩れているので assert で止める。
 */
#ifndef TOPPERS_TARGET_IPI_H
#define TOPPERS_TARGET_IPI_H

#ifndef TOPPERS_MACRO_ONLY

Inline void
request_dispatch_prc(ID prcid)
{
	(void) prcid;
	assert(0);
}

Inline void
request_ext_ker(ID prcid)
{
	(void) prcid;
	assert(0);
}

Inline void
request_set_hrt_event(ID prcid)
{
	(void) prcid;
	assert(0);
}

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_TARGET_IPI_H */
