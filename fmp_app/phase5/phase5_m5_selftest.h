/*
 *  M5Unified profile の自己診断アプリケーション
 *
 *  配布用の phase5_m5_app に監視タスクを1本足しただけのもの。
 *  cfg も phase5_m5_app.cfg を INCLUDE して差分だけを書く。
 */

#ifndef TOPPERS_PHASE5_M5_SELFTEST_H
#define TOPPERS_PHASE5_M5_SELFTEST_H

#include "phase5_m5_app.h"

#define PHASE5_MONITOR_TASK_PRIORITY 11
#define PHASE5_MONITOR_TASK_STACK_SIZE 4096

/*  SMP 分離検査。 */
#define PHASE5_SMP_MONITOR_TASK_PRIORITY 11
#define PHASE5_SMP_MONITOR_TASK_STACK_SIZE 4096
#define PHASE5_PRC2_TASK_PRIORITY 10
#define PHASE5_PRC2_TASK_STACK_SIZE 4096

#if !defined(TOPPERS_MACRO_ONLY)
extern void phase5_monitor_task(EXINF exinf);
extern void phase5_smp_monitor_task(EXINF exinf);
extern void phase5_prc2_task(EXINF exinf);
#endif

#endif  /* TOPPERS_PHASE5_M5_SELFTEST_H */
