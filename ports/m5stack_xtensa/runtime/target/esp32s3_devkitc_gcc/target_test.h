/*
 *  テストプログラム用のターゲット依存定義（ESP32-S3-DevKitC-1）
 *
 *  test_int1（割込み管理: dis/ena/clr/ras/prb_int）用にソフトウェア割込み
 *  INTNO1を定義する。ESP32-S3のXtensaコアのINT7はレベル1のソフトウェア
 *  割込み（XCHAL_INT7_TYPE=SOFTWARE, XCHAL_INT7_LEVEL=1）で、INTSET/
 *  INTCLEAR/INTENABLE/INTERRUPTレジスタで ras/clr/dis-ena/prb を実現できる。
 *  ras_intでの発火→ISRディスパッチは_kernel_l1int_dispatch（core_support.S
 *  経由でcore_kernel_impl.c）がINT7を判定してCRE_ISRのISRを呼ぶ。
 */
#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#include <sil.h>

#ifndef STACK_SIZE
#define STACK_SIZE	(4096)	/* windowed ABIのウィンドウスピル分、余裕をもつ */
#endif /* STACK_SIZE */

/*
 *  テストで使用するソフトウェア割込み（INT7：レベル1ソフトウェア割込み）
 */
#define INTNO1			7U
#define INTNO1_INTATR	(TA_ENAINT | TA_EDGE)
#define INTNO1_INTPRI	TMAX_INTPRI		/* レベル1（XCHAL_INT7_LEVEL） */

/*
 *  ISRが割込み要求をクリアするための処理（INTCLEARでINT7要因をクリア）
 */
#define intno1_clear()	clr_int(INTNO1)

#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
