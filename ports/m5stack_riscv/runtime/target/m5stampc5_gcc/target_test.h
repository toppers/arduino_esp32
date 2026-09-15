/*
 *  テストプログラムのターゲット依存部（M5Stamp-C5 / ESP32-C5 用/FMP3）
 *
 *  出典: fmp3/target/m5stamp_esp32p4_gcc/target_test.h（P4 CLIC）の方式
 *  （software raise: CLIC エッジ線の IP を ras_int で立て、ISR で IP を落とす。
 *  割込みマトリクスのソースは割り付けない）に、線番号は C6 の
 *  m5nanoc6_gcc/target_test.h（18/20/21、19 は INTNO_UNOPTED 用に空ける）を
 *  使った。
 *
 *  **計画（PLAN-stage1-impl.md Task 3）の「INTNO1/2/3 = FROM_CPU_0-2 の CLIC 線」
 *  とは違う**（申告）。理由: (1) FROM_CPU_0 はタイマ強制（target_timer.h、C6/asp3
 *  と同じ多重マップ）が使っている。(2) chip_kernel_impl.h（P4 の型）の
 *  raise_int/clear_int/probe_int は CLIC_INT_CTRL の IP を操作する。FROM_CPU_n を
 *  割込みマトリクス経由のレベル線にすると ras_int（IP の software set）が
 *  レベル線では効かず、C6 型（INTPRI レジスタで raise/clear）へ chip 層を
 *  書き換える必要が出る。P4 の方式は P4 実機で test_int1 / test_ipmmask /
 *  test_dcre5 が PASS しており、chip 層を P4 と同じに保てる。
 *  C5 実機での確認は段3（未確認）。
 *
 *  INTNO1_INTPRI は TMAX_INTPRI（P4/S3/C6 と同じ。fmp3/test_ovr/test_ipmmask.c は
 *  chg_ipm(-1) で INTNO1 がマスクされる前提を持つ）。
 */
#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#define STACK_SIZE (1024)

#ifndef TOPPERS_MACRO_ONLY
#include <sil.h>
#include "esp32c5.h"
#endif /* TOPPERS_MACRO_ONLY */

/*
 *  テスト用の割込み（CLIC 外部線 18/20/21。target_kernel_impl.c の割付けと
 *  重ならないこと。線 19 は fmp3_core/test/test_dcre5.h の INTNO_UNOPTED
 *  (= INTNO1+1) として「有効範囲内だが未登録」に空ける）
 *   - TA_EDGE: software から IP を立てて発火させる（ras_int）にはエッジ線で
 *     あることが要る（fmp3_core/arch/riscv_gcc/common/clic_kernel_impl.c の
 *     clic_config_int）。
 *   - intnoN_clear() は ISR から呼ばれ、CLIC_INT_CTRL の IP を落とす。
 */
#define INTNO1          18
#define INTNO1_INTATR   (TA_ENAINT | TA_EDGE)
#define INTNO1_INTPRI   TMAX_INTPRI
#define INTNO2          20
#define INTNO2_INTATR   (TA_ENAINT | TA_EDGE)
#define INTNO2_INTPRI   (-4)
#define INTNO3          21
#define INTNO3_INTATR   (TA_ENAINT | TA_EDGE)
#define INTNO3_INTPRI   (-4)

#ifndef TOPPERS_MACRO_ONLY

Inline void
intno1_clear(void)
{
	sil_wrw_mem(CLIC_INT_CTRL(INTNO1),
				sil_rew_mem(CLIC_INT_CTRL(INTNO1)) & ~CLIC_INT_IP_BIT);
}

Inline void
intno2_clear(void)
{
	sil_wrw_mem(CLIC_INT_CTRL(INTNO2),
				sil_rew_mem(CLIC_INT_CTRL(INTNO2)) & ~CLIC_INT_IP_BIT);
}

Inline void
intno3_clear(void)
{
	sil_wrw_mem(CLIC_INT_CTRL(INTNO3),
				sil_rew_mem(CLIC_INT_CTRL(INTNO3)) & ~CLIC_INT_IP_BIT);
}
#endif /* TOPPERS_MACRO_ONLY */

#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
