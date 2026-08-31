/*
 *  dual-core profile の自己診断アプリケーション
 *
 *  配布用の phase6_smp_app に、監視タスク（PRC1）と実演ワーカ（PRC2）を
 *  足しただけのもの。cfg も phase6_smp_app.cfg を INCLUDE して差分だけを書く。
 */

#ifndef TOPPERS_PHASE6_SMP_SELFTEST_H
#define TOPPERS_PHASE6_SMP_SELFTEST_H

#include "phase6_smp_app.h"

#define PHASE6_MONITOR_TASK_PRIORITY 11
#define PHASE6_MONITOR_TASK_STACK_SIZE 4096
#define PHASE6_PRC2_TASK_PRIORITY 10
#define PHASE6_PRC2_TASK_STACK_SIZE 4096

#if !defined(TOPPERS_MACRO_ONLY)
extern void phase6_monitor_task(EXINF exinf);
extern void phase6_prc2_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE6_SMP_SELFTEST_H */
