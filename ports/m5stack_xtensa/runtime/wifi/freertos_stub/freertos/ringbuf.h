/*
 *  BlueDroid SPPプロファイル（btc_spp.c）用のFreeRTOS ring buffer互換
 *  ヘッダ（コンパイル専用スタブ、W3④）。
 *
 *  btc_spp.cは各SPP接続スロットの送信バイトストリームを`RingbufHandle_t
 *  ringbuf_write`（RINGBUF_TYPE_BYTEBUFのみ使用）で管理する。実物の
 *  ESP-IDF `esp_ringbuf`コンポーネント（`components/esp_ringbuf/`）は
 *  no-split/allow-split/byte-bufの3種＋ISR版APIを持つ大きな実装だが、
 *  btc_spp.c内の実使用箇所（grep確認済み、6関数のみ・全呼び出しで
 *  xTicksToWait=0の非ブロッキング呼び出しのみ）に限定すれば十分小さい
 *  実装で足りる：
 *    xRingbufferCreate / vRingbufferDelete / xRingbufferSend /
 *    xRingbufferReceiveUpTo / vRingbufferReturnItem / vRingbufferGetInfo
 *  実体は `esp/bt/classic/ringbuf_shim.c`（RAM予算が厳しいW3のため、
 *  esp_shim_malloc/freeでconnection単位に動的確保、静的常時確保プールは
 *  持たない）。
 */
#ifndef TOPPERS_BT_FREERTOS_RINGBUF_H
#define TOPPERS_BT_FREERTOS_RINGBUF_H

#include "freertos/FreeRTOS.h"

typedef void *RingbufHandle_t;

typedef enum {
	RINGBUF_TYPE_NOSPLIT = 0,
	RINGBUF_TYPE_ALLOWSPLIT,
	RINGBUF_TYPE_BYTEBUF,
	RINGBUF_TYPE_MAX,
} RingbufferType_t;

#ifndef TOPPERS_MACRO_ONLY
extern RingbufHandle_t xRingbufferCreate(size_t xBufferSize, RingbufferType_t xBufferType);
extern void vRingbufferDelete(RingbufHandle_t xRingbuffer);
extern BaseType_t xRingbufferSend(RingbufHandle_t xRingbuffer, const void *pvItem,
								   size_t xItemSize, TickType_t xTicksToWait);
extern void *xRingbufferReceiveUpTo(RingbufHandle_t xRingbuffer, size_t *pxItemSize,
									 TickType_t xTicksToWait, size_t xMaxSize);
extern void vRingbufferReturnItem(RingbufHandle_t xRingbuffer, void *pvItem);
extern void vRingbufferGetInfo(RingbufHandle_t xRingbuffer, UBaseType_t *uxFree,
								UBaseType_t *uxRead, UBaseType_t *uxWrite,
								UBaseType_t *uxAcquire, size_t *uxItemsWaiting);
/*  [2026-07-15 W3④] 最後の引数のみsize_t*（UBaseType_t*ではない）。
 *  実ESP-IDFのesp_ringbuf.hのシグネチャに合わせた（btc_spp.cが
 *  `size_t items_waiting;`をそのまま渡すため、UBaseType_t*では
 *  -Wincompatible-pointer-typesで実コンパイルエラーになることを確認
 *  済み）。 */
#endif

#endif /* TOPPERS_BT_FREERTOS_RINGBUF_H */
