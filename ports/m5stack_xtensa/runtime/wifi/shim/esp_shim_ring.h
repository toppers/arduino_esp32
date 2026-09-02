/*
 *  TOPPERS/FMP3 ESP32-S3 port
 *
 *  シム所有のリングバッファ（**ISR 文脈から受信できる**キュー）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ========================================================================
 *  なぜ要るか（2026-08-04・`非公開作業記録/20260804-bthal-ring/`）
 *  ========================================================================
 *  `esp/shim/esp_shim.c` の `esp_shim_queue_*` は FMP3 の**データキュー**が実体で、
 *  受信は `trcv_dtq`→`prcv_dtq`。**どちらも `CHECK_TSKCTX_UNL_MYSTATE`**
 *  （`fmp3/fmp3_core/kernel/dataqueue.c:586`）＝**タスク文脈専用**である。
 *  `iprcv_dtq` は存在しない。⇒ **ISR からの受信は 100% 失敗する。**
 *  送信側（`psnd_dtq`/`fsnd_dtq`）は `CHECK_UNL_MYSTATE` なので ISR から通る——
 *  **送信は可・受信だけタスク文脈限定**という非対称が仕様として在る。
 *
 *  T-3（`esp/shim/esp_shim_isr_ctx.c`）が直したのは「失敗を『空』に畳んで黙る」
 *  ところまでで、**受信は今も成功しない**。同ファイル冒頭が「真の解決は m5 側の
 *  `m5_que_*` と同じシム所有リングへの作り替え」と書いている。**本ファイルがそれ**である
 *  （ただし適用範囲は BT 系統のみ。Wi-Fi 系統は従来どおり `esp_shim_queue_*`）。
 *
 *  ========================================================================
 *  写像先に分岐を作らないこと（H4 第2段 `1765759` の教訓）
 *  ========================================================================
 *  呼ぶ側は**事前生成の書庫**（`esp/lib` 配下の `libbt_hal.a`）である。写像先をマクロで
 *  切り替えると、ビルドスクリプトへ `-D` を渡し忘れた瞬間に `.a` だけが DTQ 側を見る。
 *  `esp_shim_queue_*` は実在するのでリンクは通り、**欠陥が黙って復活する**。
 *  ⇒ `esp/bt/stub/include/freertos/queue.h` の写像先は**無条件に**本 API である。
 *    番人は 2 つ:
 *      - `esp/boot/build_bt_hal_lib_espidf_esp32{,s3}.sh` 末尾の `nm` 検査（作り直した時）
 *      - `cmake/a1_libmap_audit.sh`（**リンクのたび**＝古い `.a` の使い回しを捕まえる）
 *
 *  ========================================================================
 *  単一コア前提（`TNUM_PRCID == 1`）
 *  ========================================================================
 *  本実装の排他は `SIL_LOC_INT()`/`SIL_UNL_INT()`（自コアの PS.INTLEVEL を退避して
 *  15 へ上げ、退避値へ戻す）**だけ**である。スピンロックを取らず、`memw` も置かない。
 *  ⇒ **他コアと同時に触られたら壊れる。** `.c` 側で `#error` により
 *    コンパイルを止めてある（黙って壊れるのを防ぐ）。
 *  m5 の `m5_que_*`（`m5/shim/m5_kernel_shim.c`）と同じ前提・同じ理由である。
 */
#ifndef ESP_SHIM_RING_H
#define ESP_SHIM_RING_H

#include <stdint.h>
#include <stddef.h>

#ifndef TOPPERS_MACRO_ONLY

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  API は `esp_shim_queue_*` と**同じ引数・同じ戻り値の約束**にしてある
 *  （`esp/bt/stub/include/freertos/queue.h` を 1:1 で差し替えられるように）。
 *    戻り値 1 = 成功 / 0 = 失敗（空・満杯・引数不正・文脈不許可）。
 *  ただし本実装では「文脈不許可で受信できない」は**起こらない**
 *    （そこが `esp_shim_queue_*` との唯一かつ本質的な違いである）。
 */
extern void		*esp_shim_ring_create(uint32_t len, uint32_t item_size);
extern void		esp_shim_ring_delete(void *que);
extern void		esp_shim_ring_reset(void *que);
/*  block_time_tick は ms（`ESP_SHIM_BLOCK_FOREVER` = 無限待ち）。 */
extern int32_t	esp_shim_ring_send(void *que, void *item,
								   uint32_t block_time_tick, int to_front);
extern int32_t	esp_shim_ring_send_from_isr(void *que, void *item);
extern int32_t	esp_shim_ring_recv(void *que, void *item,
								   uint32_t block_time_tick);
extern uint32_t	esp_shim_ring_msg_waiting(void *que);
extern uint32_t	esp_shim_ring_spaces_available(void *que);

/*
 *  CPU ロック中（BT クリティカルセクション＝`rsil` 保持中）の送信で
 *  `wup_tsk` が E_CTX になったときに保留した起床を流す。
 *  `esp_shim_bt_exit_critical()` の最外解除から呼ばれる（そこが
 *    「INTLEVEL が 0 に戻り、サービスコールが発行できるようになった」点）。
 *  呼べる文脈でなければ**何もしないで返る**（fail-safe。保留は残る）。
 */
extern void		esp_shim_ring_flush_wakes(void);

/*
 *  診断カウンタ（JTAG／ダンプから読めるように公開する。黙らせない）。
 */
extern volatile uint32_t	esp_shim_ring_n_create;			/* 生成成功         */
extern volatile uint32_t	esp_shim_ring_n_create_fail;	/* 生成失敗         */
extern volatile uint32_t	esp_shim_ring_n_send_full;		/* 満杯で送れず     */
extern volatile uint32_t	esp_shim_ring_n_recv_isrctx;	/* 非タスク文脈での受信（成功） */
extern volatile uint32_t	esp_shim_ring_n_wake_deferred;	/* 起床を保留した   */
extern volatile uint32_t	esp_shim_ring_n_wake_flushed;	/* 保留起床を流した */
extern volatile uint32_t	esp_shim_ring_n_waiter_conflict;/* 受信待ちが2人以上 */
extern volatile uint32_t	esp_shim_ring_n_recv_nowait_ctx;/* 待てない文脈で timeout>0 */

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_MACRO_ONLY */

#endif /* ESP_SHIM_RING_H */
