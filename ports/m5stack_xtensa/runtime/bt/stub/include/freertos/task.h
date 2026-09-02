/*
 *  BTコントローラ（bt.c）用のFreeRTOS task.h互換ヘッダ（コンパイル専用）
 */
#ifndef TOPPERS_BT_FREERTOS_TASK_H
#define TOPPERS_BT_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

/*
 *  コア親和性指定なし（ESP32-C3は単一コアのため意味を持たないが，
 *  bt.cのtask_create_wrapperがcore_id引数の比較・代入に使う）
 */
#define tskNO_AFFINITY	((BaseType_t) 0x7fffffff)

/*
 *  スケジューラ状態（NimBLE NPL npl_freertos_os_startedは
 *  != taskSCHEDULER_NOT_STARTEDで判定．ASP3はカーネル起動後は常に
 *  RUNNING．configTICK_RATE_HZはFreeRTOS.hで1000＝1ms/tick）
 */
#define taskSCHEDULER_NOT_STARTED	0
#define taskSCHEDULER_SUSPENDED		1
#define taskSCHEDULER_RUNNING		2

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>	/* bool_t／sns_ctx()／get_tid()／esp_shim_time_us() */
#include <t_syslog.h>	/* vTaskPrioritySet no-op警告用（W3） */
extern int64_t esp_shim_time_us(void);
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									 uint32_t stack_size, void *param,
									 uint32_t freertos_prio, void **task_handle);
extern int32_t esp_shim_task_create_pinned(void (*entry)(void *), const char *name,
									 uint32_t stack_size, void *param,
									 uint32_t freertos_prio, void **task_handle,
									 uint32_t core_id);
extern void esp_shim_task_delete(void *task_handle);
extern void esp_shim_task_delay(uint32_t tick);

/*
 *  2026-08-04（段4）: **`xCoreID` を捨てるのをやめた。**
 *  従来は `(void) xCoreID;` で落としていたので、BT コントローラが
 *  `cfg->controller_task_run_cpu` で要求したコアが**どこにも残らなかった**。
 *  いまは値をそのまま渡し、`esp/shim/esp_shim_tsk.c` の `shim_tsk_prcid()` が
 *  方針（`TNUM_PRCID` を見て PRC1 へ落とす／数える）で解決する。
 *  現行構成は `TNUM_PRCID == 1` なので**振る舞いは変わらない**——
 *  変わったのは「見た上で無視している」ことである。
 *  記録: `非公開作業記録/20260804-dcre-stage4-tsk/`、DESIGN-MEMO §3-2(a)(b)(f)。
 */
static inline BaseType_t
xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
						 uint32_t usStackDepth, void *pvParameters,
						 UBaseType_t uxPriority, TaskHandle_t *pvCreatedTask,
						 BaseType_t xCoreID)
{
	return (BaseType_t) esp_shim_task_create_pinned(pvTaskCode, pcName,
											  usStackDepth, pvParameters,
											  uxPriority, pvCreatedTask,
											  (uint32_t) xCoreID);
}

static inline void
vTaskDelete(TaskHandle_t task)
{
	esp_shim_task_delete(task);
}

static inline void
vTaskDelay(TickType_t ticks)
{
	esp_shim_task_delay(ticks);
}

/*
 *  bt.cのosi_funcs_ro（_task_yield）が直接指す関数ポインタ．ASP3の
 *  ラウンドロビン相当（同一優先度の他タスクへ回す）rot_rdq()を使う．
 */
static inline void
vPortYield(void)
{
	(void) rot_rdq(TPRI_SELF);
}

/*
 *  bt.cはISR文脈かどうかの判定にxPortInIsrContext()を使う．
 *  ASP3標準API sns_ctx()（非タスクコンテキストで真．<kernel.h>で
 *  既に宣言済み）をそのまま使う．
 */
static inline BaseType_t
xPortInIsrContext(void)
{
	return (BaseType_t) sns_ctx();
}

/*
 *  NimBLE NPL（npl_os_freertos.c）が要求する追加API．
 */
static inline TaskHandle_t
xTaskGetCurrentTaskHandle(void)
{
	ID	self = 0;

	(void) get_tid(&self);
	return (TaskHandle_t)(intptr_t) self;
}

static inline BaseType_t
xTaskGetSchedulerState(void)
{
	/*  ASP3はカーネル起動（sta_ker）後は常時スケジューリング動作中  */
	return (BaseType_t) taskSCHEDULER_RUNNING;
}

/*
 *  現在のtick（configTICK_RATE_HZ=1000＝1ms単位）．SYSTIMER（μs）を
 *  msへ換算．レジスタ読取りのみでISRセーフ．
 */
static inline TickType_t
xTaskGetTickCountFromISR(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}

static inline TickType_t
xTaskGetTickCount(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/thread.cのosi_thread_name()が
 *  ログ用途で参照する。本ポートはesp_shim側でタスクハンドル→名前の
 *  対応表を持たないため（コンパイル専用スタブ、2026-07-15）、固定文字列
 *  を返す。実行時の実害は無い（ログ整形にのみ使われる想定）。
 */
static inline const char *
pcTaskGetName(TaskHandle_t task)
{
	(void) task;
	return "bt";
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/thread.cのosi_thread_set_priority
 *  （FreeRTOS vTaskPrioritySet相当）がリンクのため要求する。ただし
 *  osi_thread_set_priority自体は現行BlueDroid CLASSIC_BT/SPP MVP経路
 *  （btu_task/btc_task起動〜esp_bluedroid_enable完走まで）からは
 *  呼び出されない（呼び出し元を`grep -rn osi_thread_set_priority`で
 *  確認済み、bt/common/osi/thread.c内の定義のみで呼出し側が無い）。
 *  本ポートは全タスクをesp_shim.cfgの静的CRE_TSKで固定優先度生成する
 *  設計（wifi_sta.h等のTA_FPU属性と同型）のため，実行時優先度変更は
 *  現時点で未対応。到達した場合に無言で無視すると発見しづらい不具合に
 *  なるため，syslogで一度警告してno-opとする（advisorレビュー方針：
 *  検証手段の無いスタブを安易に「実装した」と主張しない）。
 */
static inline void
vTaskPrioritySet(TaskHandle_t task, UBaseType_t newPriority)
{
	(void) task;
	syslog(LOG_WARNING,
		   "bt: vTaskPrioritySet(%u) not supported (static CRE_TSK priority, no-op)",
		   (unsigned int) newPriority);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_TASK_H */
