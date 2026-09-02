/*
 *  ⚠ [孤児 / CMake 未対応] classic 全廃(2026-07-20)で本ファイルの consumer（BT Classic
 *     SPP=W3 の seam ビルド）は削除済み。W3 バリアントの CMake preset 化は統合レビュー
 *     B-2/P-2（方針決定待ち）。復元は branch archive/trb-classic-cfg 参照。
 */
/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  BlueDroid SPPプロファイル（btc_spp.c）用 RINGBUF_TYPE_BYTEBUF 最小実装
 *  （W3④、`esp/bt/stub/include/freertos/ringbuf.h`のプロトタイプ実体）。
 *
 *  btc_spp.cでの実使用（grep確認済み）は全て xTicksToWait=0 の
 *  非ブロッキング呼び出しのみ（BTCタスクコンテキストからのみ呼ばれ，
 *  データが無い／空きが無い場合は即座に失敗を返して呼び出し側が
 *  リトライ／次イベント待ちする設計）。そのため実物のESP-IDF
 *  esp_ringbuf（3種のバッファ型・ISR版API・xTicksToWaitでのブロック
 *  待ちを含む本格実装）を丸ごと移植せず，BYTEBUF一種・非ブロッキング
 *  のみに絞った最小実装とする（RAM/コード両面の予算節約，W3③の
 *  「残り約12.5KB」制約に対する意図的なスコープ縮小）。
 *
 *  ゼロコピー方針：xRingbufferReceiveUpTo()はESP-IDF実装と同様に内部
 *  バッファへの直接ポインタを返す（コピーしない）。バッファ終端で
 *  折り返している場合は要求元が複数回呼ぶ前提（ESP-IDF本家の仕様通り，
 *  btc_spp.c側もそれを前提にwhileループで呼んでいる）。一度に返却
 *  できる未返却(pending)区間は1つのみ（ESP-IDF本家のBYTEBUF制約と同じ）。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>

#include "freertos/ringbuf.h"
#include "esp_shim.h"

struct shim_ringbuf {
	uint8_t			*buf;
	size_t			size;		/* バッファ総サイズ */
	size_t			head;		/* 次に書き込む位置 */
	size_t			tail;		/* 未読データの先頭位置 */
	size_t			used;		/* 未読(pending込み)バイト数 */
	size_t			pending_len;	/* Receive済みだがReturn未のバイト数(0=無し) */
	portMUX_TYPE	mux;
};

RingbufHandle_t
xRingbufferCreate(size_t xBufferSize, RingbufferType_t xBufferType)
{
	struct shim_ringbuf	*rb;

	/*  btc_spp.cはBYTEBUFのみ使用．他タイプは本移植のスコープ外
	 *  （呼ばれた場合はログを出してNULLを返す＝呼び出し側のNULLチェック
	 *  で安全に失敗する）。 */
	if (xBufferType != RINGBUF_TYPE_BYTEBUF) {
		syslog(LOG_ERROR,
			   "ringbuf_shim: unsupported type %d (BYTEBUF only)",
			   (int_t) xBufferType);
		return(NULL);
	}
	if (xBufferSize == 0U) {
		return(NULL);
	}

	rb = (struct shim_ringbuf *) esp_shim_malloc(sizeof(*rb));
	if (rb == NULL) {
		return(NULL);
	}
	rb->buf = (uint8_t *) esp_shim_malloc(xBufferSize);
	if (rb->buf == NULL) {
		esp_shim_free(rb);
		return(NULL);
	}
	rb->size = xBufferSize;
	rb->head = 0U;
	rb->tail = 0U;
	rb->used = 0U;
	rb->pending_len = 0U;
	rb->mux.owner = 0U;
	rb->mux.count = 0U;
	return((RingbufHandle_t) rb);
}

void
vRingbufferDelete(RingbufHandle_t xRingbuffer)
{
	struct shim_ringbuf	*rb = (struct shim_ringbuf *) xRingbuffer;

	if (rb == NULL) {
		return;
	}
	esp_shim_free(rb->buf);
	esp_shim_free(rb);
}

BaseType_t
xRingbufferSend(RingbufHandle_t xRingbuffer, const void *pvItem,
				 size_t xItemSize, TickType_t xTicksToWait)
{
	struct shim_ringbuf	*rb = (struct shim_ringbuf *) xRingbuffer;
	size_t				free_bytes;
	size_t				first_chunk;

	(void) xTicksToWait;	/*  本実装は非ブロッキング固定（上記コメント参照） */

	if (rb == NULL) {
		return(pdFALSE);
	}
	if (xItemSize == 0U) {
		return(pdTRUE);
	}

	portENTER_CRITICAL(&rb->mux);
	free_bytes = rb->size - rb->used;
	if (xItemSize > free_bytes) {
		portEXIT_CRITICAL(&rb->mux);
		return(pdFALSE);
	}

	first_chunk = rb->size - rb->head;
	if (first_chunk >= xItemSize) {
		memcpy(&rb->buf[rb->head], pvItem, xItemSize);
	}
	else {
		memcpy(&rb->buf[rb->head], pvItem, first_chunk);
		memcpy(&rb->buf[0], (const uint8_t *) pvItem + first_chunk,
			   xItemSize - first_chunk);
	}
	rb->head = (rb->head + xItemSize) % rb->size;
	rb->used += xItemSize;
	portEXIT_CRITICAL(&rb->mux);
	return(pdTRUE);
}

void *
xRingbufferReceiveUpTo(RingbufHandle_t xRingbuffer, size_t *pxItemSize,
						TickType_t xTicksToWait, size_t xMaxSize)
{
	struct shim_ringbuf	*rb = (struct shim_ringbuf *) xRingbuffer;
	size_t				avail;
	size_t				contiguous;
	size_t				ret_len;
	void				*ret_ptr;

	(void) xTicksToWait;	/*  非ブロッキング固定 */

	if (rb == NULL || pxItemSize == NULL || xMaxSize == 0U) {
		return(NULL);
	}

	portENTER_CRITICAL(&rb->mux);
	if (rb->pending_len != 0U) {
		/*  ESP-IDF本家同様，未返却(Return未)区間が残っている間は
		 *  次のReceiveを許可しない（呼び出し側バグ検出のため）。 */
		portEXIT_CRITICAL(&rb->mux);
		syslog(LOG_ERROR, "ringbuf_shim: receive without prior return");
		return(NULL);
	}
	avail = rb->used - rb->pending_len;	/* pending_len==0なのでavail==used */
	if (avail == 0U) {
		portEXIT_CRITICAL(&rb->mux);
		return(NULL);
	}
	contiguous = rb->size - rb->tail;	/* バッファ終端までの連続区間 */
	ret_len = avail;
	if (ret_len > contiguous) {
		ret_len = contiguous;
	}
	if (ret_len > xMaxSize) {
		ret_len = xMaxSize;
	}
	ret_ptr = &rb->buf[rb->tail];
	rb->pending_len = ret_len;
	portEXIT_CRITICAL(&rb->mux);

	*pxItemSize = ret_len;
	return(ret_ptr);
}

void
vRingbufferReturnItem(RingbufHandle_t xRingbuffer, void *pvItem)
{
	struct shim_ringbuf	*rb = (struct shim_ringbuf *) xRingbuffer;

	if (rb == NULL || pvItem == NULL) {
		return;
	}

	portENTER_CRITICAL(&rb->mux);
	if (rb->pending_len != 0U && pvItem == (void *) &rb->buf[rb->tail]) {
		rb->tail = (rb->tail + rb->pending_len) % rb->size;
		rb->used -= rb->pending_len;
		rb->pending_len = 0U;
	}
	else {
		syslog(LOG_ERROR, "ringbuf_shim: return item mismatch");
	}
	portEXIT_CRITICAL(&rb->mux);
}

void
vRingbufferGetInfo(RingbufHandle_t xRingbuffer, UBaseType_t *uxFree,
					UBaseType_t *uxRead, UBaseType_t *uxWrite,
					UBaseType_t *uxAcquire, size_t *uxItemsWaiting)
{
	struct shim_ringbuf	*rb = (struct shim_ringbuf *) xRingbuffer;
	size_t				used = 0U;
	size_t				free_bytes = 0U;

	if (rb != NULL) {
		portENTER_CRITICAL(&rb->mux);
		used = rb->used;
		free_bytes = rb->size - rb->used;
		portEXIT_CRITICAL(&rb->mux);
	}

	if (uxFree != NULL) {
		*uxFree = (UBaseType_t) free_bytes;
	}
	if (uxRead != NULL) {
		*uxRead = 0U;
	}
	if (uxWrite != NULL) {
		*uxWrite = 0U;
	}
	if (uxAcquire != NULL) {
		*uxAcquire = 0U;
	}
	if (uxItemsWaiting != NULL) {
		*uxItemsWaiting = (UBaseType_t) used;
	}
}
