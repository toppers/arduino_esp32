/*
 *  BTコントローラ（bt.c）用のFreeRTOS semphr.h互換ヘッダ（コンパイル専用）
 *
 *  xSemaphoreCreateMutex()もxSemaphoreCreateCounting()も同じ
 *  esp_shim_sem_create()（counting／binary両対応）に統一する
 *  （FreeRTOSのSemaphoreHandle_tは生成元を問わず同じ型で以後
 *  take/give/deleteされるため，型で分岐する必要が無いよう単純化．
 *  非再帰ミューテックスとしての用途には十分．bt.cが真の再帰
 *  ロックを要求する箇所は無いことをビルド時に確認する）．
 */
#ifndef TOPPERS_BT_FREERTOS_SEMPHR_H
#define TOPPERS_BT_FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"
#include "esp_shim_isr_ctx.h"	/* 3値の take_from_isr と診断（queue.h と同じ） */

typedef void *SemaphoreHandle_t;

#ifndef TOPPERS_MACRO_ONLY
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);
extern uint32_t esp_shim_sem_get_count(void *sem);
/*  再帰対応ミューテックス（owner/countをesp_shim側が追跡．NimBLE NPL用）  */
extern void *esp_shim_mutex_create(bool_t recursive);
extern void esp_shim_mutex_delete(void *mtx);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_unlock(void *mtx);

static inline SemaphoreHandle_t
xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(uxMaxCount, uxInitialCount);
}

static inline SemaphoreHandle_t
xSemaphoreCreateMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 1U);
}

/*
 *  バイナリセマフォ（NimBLE esp_nimble_hci.cが使用）．FreeRTOSの
 *  バイナリセマフォは生成直後は空（取得前にgiveが必要）＝max=1／init=0．
 */
static inline SemaphoreHandle_t
xSemaphoreCreateBinary(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 0U);
}

static inline void
vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	esp_shim_sem_delete(xSemaphore);
}

static inline BaseType_t
xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
#ifdef TOPPERS_S3_BT_VHCI_DIAG
	/*  LM-D-3 調査（2026-08-07）: HCI フロー制御セマフォ（vhci_send_sem）の
	 *  失敗を数える。数えるだけで戻り値も副作用も同一。
	 *  既定ビルドではこの分岐ごと消える（golden 不変）。 */
	return (BaseType_t) esp_shim_sem_take_hcidiag(xSemaphore, xTicksToWait);
#else
	return (BaseType_t) esp_shim_sem_take(xSemaphore, xTicksToWait);
#endif
}

static inline BaseType_t
xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
#ifdef TOPPERS_S3_BT_VHCI_DIAG
	return (BaseType_t) esp_shim_sem_give_hcidiag(xSemaphore);
#else
	return (BaseType_t) esp_shim_sem_give(xSemaphore);
#endif
}

/*
 *  ［レビュー指摘#3と同型の制約（esp/shim/esp_coex_adapter.cの
 *  coex_semphr_take_from_isr_wrapper参照）］esp_shim_sem_take()は
 *  FMP3カーネルのtwai_sem/pol_semがいずれもCHECK_DISPATCH_MYSTATE／
 *  CHECK_TSKCTX_UNLで非タスク文脈（ISR）を拒否するため，ISRから呼ぶと
 *  常に0（取得失敗）を返す。xSemaphoreGiveFromISR（下記）はsig_semが
 *  非タスク文脈を許可するため正しく動作する（takeのみの制約）。
 *  詳細・対応方針は非公開作業記録/20260716-review-fixes/
 *  evidence-06-runtime-arch.txtおよびesp_coex_adapter.cのコメント参照。
 *
 *  2026-08-04 訂正（非公開作業記録/20260804-coex-ectx/）: 直前の
 *  「giveは正しく動作する（takeのみの制約）」は**言い過ぎだった**。
 *  `sig_sem` の `CHECK_UNL_MYSTATE` は `sense_lock()` を見る
 *  （`fmp3_core/kernel/check.h:122-137`）ので、
 *   ・**ISR からは通る**（本ポートの L1/L3 は INTLEVEL=0 で C ハンドラを
 *     走らせる。`core_support.S:707`／`:1008`）＝この部分は正しかった。
 *   ・しかし **CPU ロック中（`rsil` 保持）では give も E_CTX** で失敗し、
 *     それが 0 に畳まれて**黙っていた**。⇒ take と同じ 3 値 API へ載せ替えた
 *     （`esp_shim_sem_give_from_isr()`。戻り値は pdTRUE/0 のまま）。
 *
 *  この差替えが**今どのバイナリに入っているか**（実測 2026-08-04・
 *    `非公開作業記録/20260804-coex-ectx/link-sets.txt` §4）:
 *   ・**入っている**: `npl_os_freertos.c`（NimBLE。seam-s3-ble で**ソースから
 *     コンパイル**される。`npl_freertos_sem_release()` が呼ぶ）。
 *   ・**まだ入っていない**: `esp/bt/hal/bt.c` の `semphr_give_from_isr_wrapper`。
 *     bt.c は**事前生成の `esp/lib/bt_*_espidf/libbt_hal.a` に入っている**ので、
 *     `esp/boot/build_bt_hal_lib_espidf_*.sh` で **.a を作り直すまで反映されない**
 *     （実測: 現 .a の未定義参照は `esp_shim_sem_give` のまま）。
 *     本作業は golden 基準線を汚さないため .a を再生成していない。
 *     次に .a を再生成した人へ: そのとき ble/lx6-ble の golden は動く。
 *
 *  2026-08-03（統合レビュー A-2 の処置・2026-07-16 から自認したまま
 *  未修正だったもの）: 上の制約は**今も解消していない**（カーネルに
 *  ISR 安全な非ブロッキング取得が無い）。変えたのは
 *  「**0＝『取れなかった』に黙って畳む**」のをやめた点だけである：
 *  `esp_shim_sem_take_from_isr()` が「文脈が禁止で問えなかった」を
 *  3 値で返し、**診断ログとカウンタ**に残す。
 *  戻り値は pdTRUE/0 の 2 値のまま（NimBLE `npl_freertos_sem_pend()` は
 *  `ret == pdPASS` で判定・blob も実測で 1 との等値比較。根拠は
 *  `esp/shim/esp_shim_isr_ctx.c` 冒頭）。
 */
static inline BaseType_t
xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
					   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) ((esp_shim_sem_take_from_isr(xSemaphore)
						  == ESP_SHIM_ISR_OK) ? pdTRUE : pdFALSE);
}

static inline BaseType_t
xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
					   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) ((esp_shim_sem_give_from_isr(xSemaphore)
						  == ESP_SHIM_ISR_OK) ? pdTRUE : pdFALSE);
}

static inline UBaseType_t
uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore)
{
	return (UBaseType_t) esp_shim_sem_get_count(xSemaphore);
}

/*
 *  NimBLE NPLの再帰ミューテックス（os_mutex＝同一タスク再取得可）．
 *  esp_shim_mutex_*（owner/count追跡．bt.cのglobal_int_mux用に既に
 *  実装済み）へ委譲する．TakeRecursiveのタイムアウト引数は無視
 *  （下地はloc_mtxで永久ブロック．NimBLEのos_mutex_pendは実質FOREVER運用）．
 */
static inline SemaphoreHandle_t
xSemaphoreCreateRecursiveMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_mutex_create(true);
}

static inline BaseType_t
xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait)
{
	(void) xTicksToWait;
	return (BaseType_t) esp_shim_mutex_lock(xMutex);
}

static inline BaseType_t
xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex)
{
	return (BaseType_t) esp_shim_mutex_unlock(xMutex);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_SEMPHR_H */
