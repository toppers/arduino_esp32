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
 *  クラッシュ/ハング診断用 recorder（M5NanoC6 / ESP32-C6 用・FMP3）の縮小実装
 *
 *  段4 Task 3（2026-09-14）。何を縮めたかは diag_recorder.h 冒頭（Low#3-5）。
 *  出典（リングの形）: fmp3/target/esp32s3_devkitc_gcc/diag_recorder.c:44-56,
 *  212-246（diag_event）。ts は S3 の CCOUNT ではなく SYSTIMER の下位 32bit
 *  （16MHz tick）。リンクは cmake/a1_c6_stage1.cmake の A1_C6_WIFI ブロック
 *  だけ（非 wifi の hello には入らない）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "diag_recorder.h"
#include "target_timer.h"		/* esp32c6_systimer_read */

#define DIAG_RING_LEN	64U

typedef struct {
	uint32_t	ts;			/* SYSTIMER 下位 32bit */
	uint8_t		id;
	uint8_t		core;		/* 単一コアなので常に 0 */
	uint16_t	reserved;
	uint32_t	arg1;
	uint32_t	arg2;
} diag_ring_entry_t;

typedef struct {
	uint32_t			ring_head;
	diag_ring_entry_t	ring[DIAG_RING_LEN];
	uint32_t			heartbeat;
	uint32_t			panic_count;
	uint32_t			panic_excinf;
} diag_region_t;

/*  .bss（起動時にゼロ）。永続化しない（Low#3）。JTAG/gdb で読むための非 static。  */
diag_region_t diag_region;

static inline uint32_t
diag_ts(void)
{
	return((uint32_t) esp32c6_systimer_read());
}

void
diag_event(uint8_t id, uint32_t arg1, uint32_t arg2)
{
	uint32_t	idx;
	uint32_t	mstatus;

	/*  短い割込み禁止区間（printf/syslog/カーネル API 非依存）。  */
	Asm("csrrci %0, mstatus, 8" : "=r"(mstatus) :: "memory");
	idx = diag_region.ring_head;
	diag_region.ring[idx].ts = diag_ts();
	diag_region.ring[idx].id = id;
	diag_region.ring[idx].core = 0U;
	diag_region.ring[idx].reserved = 0U;
	diag_region.ring[idx].arg1 = arg1;
	diag_region.ring[idx].arg2 = arg2;
	diag_region.ring_head = (idx + 1U) % DIAG_RING_LEN;
	if ((mstatus & 8U) != 0U) {
		Asm("csrsi mstatus, 8" ::: "memory");
	}
}

void
diag_heartbeat(void)
{
	diag_region.heartbeat = diag_ts();		/* 1 ストア */
}

void
diag_panic_snapshot(const void *p_excinf)
{
	/*  Low#4: レジスタのスナップショットは取らない。回数と番地だけ。  */
	diag_region.panic_count++;
	diag_region.panic_excinf = (uint32_t)(uintptr_t) p_excinf;
}

void
diag_boot_check_and_dump(intptr_t exinf)
{
	/*  Low#5: 永続化が無いので前回の内容は無い。呼ばれたことだけ残す。  */
	(void) exinf;
	syslog(LOG_NOTICE, "diag(c6): RAM ring only, no cross-reset persistence");
}
