/*
 *  TOPPERS/FMP3 ESP32-S3 port
 *
 *  「取れなかった（文脈が禁止）」と「空だった」を分ける ISR 用受信 API
 *
 *  ------------------------------------------------------------------------
 *  なぜ要るか（H4 と同型の欠陥・2026-08-03）
 *  ------------------------------------------------------------------------
 *  `esp_shim_queue_recv()` / `esp_shim_sem_take()` は FMP3 のサービスコール
 *  （`trcv_dtq`→`prcv_dtq`／`twai_sem`→`pol_sem`）へ写像している。この
 *  フォールバック先はいずれも**タスク文脈専用**である：
 *
 *      prcv_dtq : CHECK_TSKCTX_UNL_MYSTATE  (fmp3_core/kernel/dataqueue.c:586)
 *      pol_sem  : CHECK_TSKCTX_UNL          (fmp3_core/kernel/semaphore.c)
 *      ref_dtq  : CHECK_TSKCTX_UNL          (fmp3_core/kernel/dataqueue.c:733)
 *
 *  `CHECK_TSKCTX_UNL*` は `sense_context() || sense_lock()` が真なら E_CTX を
 *  返す（`fmp3_core/kernel/check.h:152-190`）。⇒ **ISR 文脈（非タスク文脈）
 *  または CPU ロック中からは必ず失敗する。**
 *
 *  従来はこの E_CTX を呼出し側へ **0＝「空だった」** として返していた。
 *  「問い合わせることすらできなかった」と「本当に空だった」が同じ 0 に畳まれ、
 *  呼出し側は「仕事が無い」と誤判断する。これは 2026-08-03 に SD/SPI で
 *  実機ハング 4 run を費やした H4 と**同型**である
 *  （`非公開作業記録/20260803-lx6-sd-h4/RESULT.md`）。
 *
 *  ------------------------------------------------------------------------
 *  何をするか／何をしないか
 *  ------------------------------------------------------------------------
 *  ・本 API は **3 値**を返す：取れた(1)／空(0)／文脈が禁止で問えなかった(-1)。
 *  ・-1 のときは**必ず診断を出す**（沈黙しない）。カウンタも公開する。
 *  ・**FreeRTOS 互換層（`esp/bt/stub/include/freertos/` の queue.h/semphr.h）は -1 を
 *    pdFALSE(=errQUEUE_EMPTY=0) へ畳んで blob／NimBLE へ返す。**
 *    理由は `esp_shim_isr_ctx.c` 冒頭の「戻り値を変えない理由」を参照。
 *  ・**2026-08-04 追記**: 同型の欠陥を持っていた `esp/shim/esp_coex_adapter.c`
 *    （`coex_semphr_take_from_isr_wrapper` ほか）も本 API へ載せ替えた。
 *    T-3 の時点では wifi 側の golden 基準線が未確定だったため保留していたもの。
 *    記録: `非公開作業記録/20260804-coex-ectx/`。これに伴い本 TU は ble だけでなく
 *    **wifi / m5-wifi 構成にもリンクされる**（`CMakeLists.txt`）。
 *  ・本 API は**禁止文脈で受信を成功させはしない**（カーネルにその手段が
 *    無い）。直せるのは「黙って空を装うのをやめる」ところまでである。
 *    真の解決は m5 側（`m5/shim/m5_kernel_shim.c` の `m5_que_*`）と同じ
 *    「シム所有のリング＋SIL 排他」への作り替えで、これは wifi/ble 双方の
 *    キュー実装の全面差し替えになるため本修正の範囲外（記録:
 *    `非公開作業記録/20260803-t3-osi-ectx/DESIGN.md`）。
 */
#ifndef ESP_SHIM_ISR_CTX_H
#define ESP_SHIM_ISR_CTX_H

#include <stdint.h>

/*  3 値の戻り値（本ヘッダが唯一の定義元） */
#define ESP_SHIM_ISR_OK			((int32_t)  1)	/* 取れた／渡せた           */
#define ESP_SHIM_ISR_EMPTY		((int32_t)  0)	/* 本当に空だった（give では「資源数上限で
												 * 渡せなかった」＝E_QOVR に相当）        */
#define ESP_SHIM_ISR_ECTX		((int32_t) -1)	/* 文脈が禁止で問えなかった */

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  「この文脈では prcv_dtq / pol_sem / ref_dtq が必ず E_CTX になる」か．
 *  ＝ sns_ctx() || sns_loc()（CHECK_TSKCTX_UNL* と同じ条件）．
 */
extern int32_t esp_shim_isr_ctx_forbidden(void);

/*
 *  **取得系と送出系で禁止条件が違う**（2026-08-04・coex 載せ替えで確認）．
 *    取得系（prcv_dtq / pol_sem / twai_sem / ref_dtq）… CHECK_TSKCTX_UNL* /
 *      CHECK_DISPATCH_MYSTATE ＝ **sns_ctx() || sns_loc()** で禁止．
 *    送出系（sig_sem）……………………… CHECK_UNL_MYSTATE ＝ **sns_loc() だけ**で禁止
 *      （`fmp3_core/kernel/check.h:122-137` の `check_unl_mystate()` は
 *       `sense_lock()` しか返さない＝**非タスク文脈は許す**）．
 *  本ポートの ISR は PS.INTLEVEL=0 で C ハンドラを走らせる
 *    （`fmp3/arch/xtensa_gcc/common/core_support.S` の L1 入口 `:707` と
 *     L3 入口 `:1008` の「擬似 call4：INTLEVEL=0」）＝ ISR 中は
 *    **sns_ctx()=真 / sns_loc()=偽**．⇒ **give は ISR から通り，take は通らない。**
 *    禁止されるのは CPU ロック中（`rsil` 保持＝BT クリティカルセクション等）である．
 */
extern int32_t esp_shim_isr_ctx_lock_forbidden(void);

extern int32_t esp_shim_sem_take_from_isr(void *sem);
extern int32_t esp_shim_sem_give_from_isr(void *sem);
/*
 *  ブロッキング take（`esp_shim_sem_take()`）の**診断つきラッパ**．
 *  戻り値も副作用も `esp_shim_sem_take()` そのもの（必ず委譲する）．
 *    足しているのは「禁止文脈から呼ばれたら数えて言う」だけである．
 *  なぜ要るか: 呼び手（blob）は `_is_in_isr` で ISR かどうかだけを見て
 *    ブロッキング側を選ぶが、**CPU ロック中のタスク文脈**では `_is_in_isr` が
 *    偽になるためブロッキング側へ来る．そこも E_CTX で必ず 0 になる．
 */
extern int32_t esp_shim_sem_take_diag(void *sem, uint32_t block_time_tick);
/*  2026-08-15（BL-G-3 段3）: ここに在ったキュー側の宣言
 *  （`esp_shim_queue_msg_waiting_from_isr()` と、ブロッキング受信の
 *   E_CTX 畳み込みを黙らせないための報告口 `esp_shim_queue_recv_report_ectx()`）
 *  は**削除した**。写像先が `esp_shim_ring_*` になり DTQ 実装が撤去されたため
 *  （経緯は `esp_shim_isr_ctx.c` 冒頭）。 */

/*  診断カウンタ（JTAG／将来のダンプから読めるように公開する）．
 *  2026-08-15（段3）: キュー側の 3 本（`_ectx_q_recv` / `_ectx_q_recv_blk` /
 *  `_ectx_q_count`）は削除した（数える相手が無くなったため）． */
extern volatile uint32_t	esp_shim_isr_ctx_ectx_sem_take;
extern volatile uint32_t	esp_shim_isr_ctx_ectx_sem_give;
extern volatile uint32_t	esp_shim_isr_ctx_ectx_sem_take_blk;

#ifdef TOPPERS_S3_BT_VHCI_DIAG
/*
 *  HCI フロー制御セマフォの失敗計数（LM-D-3 調査・2026-08-07）。
 *  数えるだけで挙動は変えない。既定ビルドには入らない。
 */
extern volatile uint32_t	esp_shim_semdiag_take_n;
extern volatile uint32_t	esp_shim_semdiag_take_fail;
extern volatile uint32_t	esp_shim_semdiag_give_n;
extern volatile uint32_t	esp_shim_semdiag_give_fail;
extern volatile uint32_t	esp_shim_semdiag_last_fail_sem;
extern int32_t	esp_shim_sem_take_hcidiag(void *sem, uint32_t block_time_tick);
extern int32_t	esp_shim_sem_give_hcidiag(void *sem);
#endif /* TOPPERS_S3_BT_VHCI_DIAG */

#ifdef __cplusplus
}
#endif

#endif /* ESP_SHIM_ISR_CTX_H */
