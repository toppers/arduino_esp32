/*
 *  テストプログラムのターゲット依存部（M5NanoC6 / ESP32-C6 用・FMP3）
 *
 *  出典: asp3_esp_idf asp3/target/esp32c6_espidf/target_test.h（INTNO1 のみ）。
 *  FMP3 の core_test.h は INTNO1/2/3 の3本を要求するため，
 *  target_kernel_impl.c で FROM_CPU_1/2/3 を線18/20/21 へ割り当て，
 *  対応する intno1/2/3_clear を追加した。線19（INTNO1+1）は
 *  fmp3_core/test/test_dcre5.h の INTNO_UNOPTED（未コンフィグの線）として
 *  空けてある（P4 の target_test.h と同じ配慮。レビュー fix round 1）。
 *
 *  段3 Task 4 Step 0（2026-09-14/controller ルーリング）: INTNO1_INTPRI を
 *  asp3 由来の (-2) から TMAX_INTPRI（(-1)）へ変更した。
 *  fmp3/test_ovr/test_ipmmask.c:64-75 は chg_ipm(-1) で Level-1 をマスクした
 *  つもりで INTNO1 が発火しないことを assert するが，これは
 *  INTNO1_INTPRI == TMAX_INTPRI（P4/S3 と同じ前提，
 *  fmp3/target/m5stamp_esp32p4_gcc/target_test.h:75 参照）を要求する。
 *  asp3 の (-2) のままだと IPM=-1 では正当に未マスクのままで，
 *  「fix round の FAIL は intmtx の欠陥」ではなく「テストの前提と
 *  target_test.h の値が食い違っていた」だけだった。INTNO2/3 は (-4) のまま
 *  変更していない（test_ipmmask はINTNO1のみを使う）。
 */
#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#define STACK_SIZE (1024)

/*
 *  テスト用の割込み（FROM_CPU_1/2/3 -> 線 18/20/21。target_kernel_impl.c の
 *  esp32c6_intmtx_route と一致させる。線19 は INTNO_UNOPTED 用に未使用のまま
 *  空ける）
 */
#define INTNO1          18
#define INTNO1_INTATR   TA_ENAINT
#define INTNO1_INTPRI   TMAX_INTPRI
#define INTNO2          20
#define INTNO2_INTATR   TA_ENAINT
#define INTNO2_INTPRI   (-4)
#define INTNO3          21
#define INTNO3_INTATR   TA_ENAINT
#define INTNO3_INTPRI   (-4)

#ifndef TOPPERS_MACRO_ONLY
#include <sil.h>
#include "esp32c6.h"

Inline void
intno1_clear(void)
{
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_1, 0U);
}

Inline void
intno2_clear(void)
{
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_2, 0U);
}

Inline void
intno3_clear(void)
{
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_3, 0U);
}
#endif /* TOPPERS_MACRO_ONLY */

#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
