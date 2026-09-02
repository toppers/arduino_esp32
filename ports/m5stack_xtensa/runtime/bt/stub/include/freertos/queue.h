/*
 *  BTコントローラ（bt.c）・NimBLE用のFreeRTOS queue.h互換ヘッダ
 *
 *  ========================================================================
 *  2026-08-04: 写像先を DTQ（`esp_shim_queue_*`）から
 *     **シム所有のロックフリーリング（`esp_shim_ring_*`）へ移した**
 *  ========================================================================
 *  【なぜ】旧写像先 `esp_shim_queue_recv()` の実体は FMP3 の**データキュー**で、
 *  `trcv_dtq`→`prcv_dtq` は**どちらもタスク文脈専用**
 *  （`CHECK_TSKCTX_UNL_MYSTATE`＝`fmp3/fmp3_core/kernel/dataqueue.c:586`）。
 *  `iprcv_dtq` は存在しない。⇒ **ISR 文脈・CPU ロック中からの受信は 100% 失敗する。**
 *  そして `xQueueReceiveFromISR()` を呼ぶのは
 *    ・`esp/bt/hal/bt.c:1073`（`queue_recv_from_isr_wrapper`＝blob の osi_funcs）
 *    ・`npl_os_freertos.c:362`（NimBLE `ble_npl_eventq_get`）
 *  である。**取れないことと空であることが同じ 0 に畳まれる**——2026-08-03 に
 *  SD/SPI で実機ハング 4 run を費やした H4（`非公開作業記録/20260803-lx6-sd-h4/`）と同型。
 *
 *  T-3（`esp/shim/esp_shim_isr_ctx.c`）が入れた 3 値 API は「黙って『空』を装うのを
 *  やめる」ところまでで、**受信は成功しないまま**だった（同ファイル冒頭に明記）。
 *  ⇒ **写像先ごとリングへ移す**（`esp/shim/esp_shim_ring.{h,c}`）。
 *    リングの排他は `SIL_*`（PS 退避・復元）だけで**呼出し文脈に中立**なので、
 *    ISR からも CPU ロック中からも**実際に受信できる**。
 *
 *  【分岐を作らない】写像先をマクロで切り替えると、呼ぶ側が事前生成の
 *  `esp/lib/bt_<chip>_espidf/libbt_hal.a` である以上、ビルドスクリプトへ `-D` を渡し忘れた
 *  瞬間に `.a` だけが DTQ 側を見る。`esp_shim_queue_*` は実在するのでリンクは通り、
 *  欠陥が黙って復活する（2026-08-03 に S3 で実際にそれが起きた＝
 *  `非公開作業記録/20260803-h4-stale-ar-hypothesis/`）。⇒ **無条件写像**にする。
 *  番人は `esp/boot/build_bt_hal_lib_espidf_esp32{,s3}.sh` 末尾の `nm` 検査と、
 *  リンクのたびに走る `cmake/a1_libmap_audit.sh`（`cmake/a1_libmap_allow.txt`）。
 *
 *  【混ざらない根拠（実測）】BT 系統のキューハンドルは BT 系統の中で閉じている。
 *    ・`esp/shim/esp_wifi_adapter.c`（Wi-Fi osi_funcs → `esp_shim_queue_*`）は
 *      ble 構成にリンクされない（`build/seam-s3-ble/build.ninja` 実測 0 件）。
 *    ・`esp/bt/bt_shim.c` はキューを作らない（`flush` を呼ぶだけ）。
 *    生ログ: `非公開作業記録/20260804-bthal-ring/logs/01-ble-objs-queue-refs.txt`
 *
 *  【セマフォは移していない】`esp/bt/stub/include/freertos/semphr.h` の写像先は
 *    `esp_shim_sem_*` のままである（別件・T-3 の 3 値 API で診断だけ出している段階）。
 */
#ifndef TOPPERS_BT_FREERTOS_QUEUE_H
#define TOPPERS_BT_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"
/*  写像先の実体（`-I$REPO/esp/shim` は build_bt_incflags_esp32{,s3}_espidf.txt と
 *  CMake の ble 構成の双方に在る）。 */
#include "esp_shim_ring.h"

typedef void *QueueHandle_t;

#ifndef TOPPERS_MACRO_ONLY

static inline QueueHandle_t
xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
	return (QueueHandle_t) esp_shim_ring_create(uxQueueLength, uxItemSize);
}

static inline void
vQueueDelete(QueueHandle_t xQueue)
{
	esp_shim_ring_delete(xQueue);
}

static inline BaseType_t
xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		   TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_ring_send(xQueue, (void *) pvItemToQueue,
										   xTicksToWait, 0);
}

static inline BaseType_t
xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
				   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_ring_send_from_isr(xQueue,
													(void *) pvItemToQueue);
}

static inline BaseType_t
xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_ring_recv(xQueue, pvBuffer, xTicksToWait);
}

/*
 *  戻り値は **pdTRUE / errQUEUE_EMPTY(=0) の 2 値**のままにする。
 *    NimBLE `npl_freertos_eventq_get()` が
 *    `BLE_LL_ASSERT(ret == pdPASS || ret == errQUEUE_EMPTY)` で 2 値を要求し
 *    （`npl_os_freertos.c:368`）、blob 側も実測で `beqi a10,1`＝1 との等値比較を
 *    使っているため（根拠は `esp/shim/esp_shim_isr_ctx.c` 冒頭に全部書いてある）。
 *  ただし**意味が変わった**: 以前の 0 は「空 **または** 文脈が禁止で問えなかった」
 *    だったが、リングでは **ISR からも CPU ロック中からも実際に受信できる**ので、
 *    0 は「本当に空」だけを意味する。
 */
static inline BaseType_t
xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
					  BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) (esp_shim_ring_recv(xQueue, pvBuffer, 0U)
						 ? pdTRUE : errQUEUE_EMPTY);
}

static inline UBaseType_t
uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_ring_msg_waiting(xQueue);
}

/*
 *  W3(BlueDroidホスト)：bt/common/osi/thread.cのosi_thread_queue_wait_size
 *  が要求する（ワークキュー投入前の空き容量チェック用）。
 */
static inline UBaseType_t
uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_ring_spaces_available(xQueue);
}

/*
 *  NimBLE NPL（npl_os_freertos.c）が要求する追加API．
 *  `ToFront` は**リングが本当に先頭挿入を実装している**（旧 `esp_shim_queue_*` は
 *    先頭送信を持たず、通常送信で代用していた＝順序を黙って変えていた）。
 */
static inline BaseType_t
xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
				 TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_ring_send(xQueue, (void *) pvItemToQueue,
										   xTicksToWait, 0);
}

static inline BaseType_t
xQueueSendToBackFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
						BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_ring_send_from_isr(xQueue,
													(void *) pvItemToQueue);
}

static inline BaseType_t
xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
				  TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_ring_send(xQueue, (void *) pvItemToQueue,
										   xTicksToWait, 1);
}

static inline BaseType_t
xQueueSendToFrontFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
						 BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	/*  ISR 版はブロックしないので `esp_shim_ring_send(..., 0, to_front=1)`。 */
	return (BaseType_t) esp_shim_ring_send(xQueue, (void *) pvItemToQueue,
										   0U, 1);
}

static inline void
xQueueReset(QueueHandle_t xQueue)
{
	esp_shim_ring_reset(xQueue);
}

/*
 *  以前は `ref_dtq` も `CHECK_TSKCTX_UNL`（`dataqueue.c:733`）で ISR から
 *    問えず、0 を返すしかなかった（T-3 が診断だけ足していた）。
 *    リングでは件数は**どの文脈からでも正しく読める**。
 */
static inline UBaseType_t
uxQueueMessagesWaitingFromISR(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_ring_msg_waiting(xQueue);
}

static inline BaseType_t
xQueueIsQueueEmptyFromISR(QueueHandle_t xQueue)
{
	return (BaseType_t) ((esp_shim_ring_msg_waiting(xQueue) == 0U)
						 ? pdTRUE : pdFALSE);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_QUEUE_H */
