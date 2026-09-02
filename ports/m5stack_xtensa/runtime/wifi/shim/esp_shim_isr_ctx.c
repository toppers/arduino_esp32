/*
 *  TOPPERS/FMP3 ESP32-S3 port
 *
 *  ISR／CPU ロック文脈からの取得：「取れなかった」と「空だった」を分ける
 *
 *  ========================================================================
 *  2026-08-15（BL-G-3 段3）: **キュー側は役目を終えて消えた**
 *  ========================================================================
 *  本ファイルは元々「キューとセマフォの両方」を担当していたが、
 *  キュー側（`esp_shim_queue_recv_from_isr()` / `_msg_waiting_from_isr()` /
 *  `_recv_report_ectx()` と 3 カウンタ）は撤去した。
 *  理由は**穴そのものが無くなった**からである——下の「この修正で直らないこと」が
 *  「真の解決はシム所有リングへの作り替え」と書いていた、その作り替えが
 *  段1（Wi-Fi 系統・2026-08-14）と段2（m5 系統・2026-08-15）で完了し、
 *  DTQ 実装（`esp_shim_queue_*`）は段3 で撤去された。
 *  ⇒ 今の写像先 `esp_shim_ring_recv()` は **ISR 文脈で実際に成功する**
 *    （実測: S3 factory で `recv_isrctx=1356`）。
 *    「取れなかったのに空を装う」経路がもう存在しないので、それを数える
 *    仕掛けも要らない。**残っているのはセマフォ側だけ**である。
 *
 *  以下は当時の記述（キューとセマフォに共通する設計判断なので残す）。
 *
 *  ========================================================================
 *  戻り値を（blob から見て）変えない理由 — 実測に基づく設計判断
 *  ========================================================================
 *  レビュー（`非公開作業記録/20260803-main-review/README.md` A-2）は
 *   (1) 「E_CTX を 0 とは別値で返して失敗と空を分ける」
 *   (2) 「`esp_shim_queue_recv_from_isr()` を新設する」
 *  の 2 案を挙げている。**採ったのは (2)、却下したのは (1)** である。
 *  却下の根拠は「FreeRTOS の作法だから」ではなく、**呼出し側の実物を読んだ
 *  結果**である：
 *
 *  ・[in-tree の呼出し側＝ソースで読める]
 *    `esp-idf/components/bt/host/nimble/nimble/porting/npl/freertos/src/
 *     npl_os_freertos.c`（seam-s3-ble で**ソースからコンパイルされる**）
 *      :362  ret = xQueueReceiveFromISR(...);
 *      :368  BLE_LL_ASSERT(ret == pdPASS || ret == errQUEUE_EMPTY);
 *      :435  ret = xQueueReceiveFromISR(...); BLE_LL_ASSERT(ret == pdPASS);
 *      :693  ret = xSemaphoreTakeFromISR(...);
 *            return ret == pdPASS ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
 *    `errQUEUE_EMPTY` は本ポートでも 0（`esp/bt/stub/include/freertos/
 *    FreeRTOS.h:26`）。⇒ **pdPASS(1) と 0 以外の値を返すとアサートに掛かる。**
 *
 *  ・[blob 側＝逆アセンブルで調べた]
 *    `libbtdm_app.a`(esp32s3) の `r_osi_funcs_p` 経由の全参照を機械的に列挙した
 *    （手順と全出力: `非公開作業記録/20260803-t3-osi-ectx/blob-osi-usage.txt`）。
 *      - `_queue_recv_from_isr`(+96) / `_semphr_take_from_isr`(+44) の
 *        **呼出しは .a 内に 0 件**（使われているのは `_queue_recv`(+92) 1 件・
 *        `_semphr_take`(+52) 8 件・`_queue_send_from_isr`(+88) 1 件など）。
 *      - 見つかった `*_from_isr` の戻り値判定は **`beqi a10,1` / `bnei a10,1`**
 *        ＝ **pdTRUE(1) との等値比較**（task.o `r_btdm_task_post_from_isr_impl`、
 *        vhci.o `r_vhci_flow_on`）。
 *    ⇒ **「.a には呼出しが無い」ことは「ROM 側にも無い」ことを意味しない**
 *      （S3 の BLE コントローラは ROM にも実体を持ち、ROM は .a に入っていない）。
 *      **blob 全体について「-1 が安全」と言い切る材料は無い。**
 *      一方で「1 との等値比較」が実際に使われている証拠は在るので、
 *      -1 を返す案の期待利得は**ゼロ**（どちらの読み方でも「失敗」に落ちるか、
 *      `if (ret)` 系なら**未初期化の item を消費させる**＝現状より悪い）。
 *
 *  ⇒ **FreeRTOS 互換境界（`esp/bt/stub/include/freertos/` の queue.h/semphr.h）の戻り値は
 *    pdTRUE/0 の 2 値のまま**にし、3 値は**シム内部の API** として新設した。
 *    失敗は**診断ログとカウンタ**で外に出す（沈黙させない）。
 *
 *  ========================================================================
 *  この修正で直らないこと（達成していないことを達成したと書かない）
 *  ========================================================================
 *  ISR 文脈では `prcv_dtq` が必ず E_CTX になるので、**ISR からの受信は今も
 *  成功しない**。本修正が変えるのは「黙って『空』を装う」→「失敗として
 *  数え、ログに出す」だけである。真の解決は m5 側の `m5_que_*`
 *  （`m5/shim/m5_kernel_shim.c`）と同じ「シム所有リング＋SIL 排他」への
 *  作り替えで、wifi/ble 双方のキュー実装の全面差し替えになるため範囲外。
 *  ——**2026-08-15 にその作り替えが完了した**（上の追記）。この節が言う
 *  「直らないこと」はキューについてはもう当てはまらない。
 *  セマフォ（`esp_shim_sem_take_from_isr`）については**今も当てはまる**
 *  （`pol_sem` はタスク文脈専用のまま。リング化していない）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_shim_isr_ctx.h"

/*  2026-08-15（BL-G-3 段3）: キュー側の 3 カウンタ
 *  （`_ectx_q_recv` / `_ectx_q_recv_blk` / `_ectx_q_count`）は**消した**。
 *  数える相手（DTQ 実装 `esp_shim_queue_*`）が撤去されたので、
 *  残しても**永久に 0 のまま**＝「測って 0 だった」と「測る対象が無い」を
 *  混同させる表示になる。⇒ 0 を出し続けるより、無いことを明示する。
 *  最後の実測値は 0（`非公開作業記録/20260814-deltsk-measure/logs/run{1,2}-*.txt`
 *  の 2 ワークロードとも `ectx_q_recv=0` / `_blk=0`）。 */
volatile uint32_t	esp_shim_isr_ctx_ectx_sem_take;
volatile uint32_t	esp_shim_isr_ctx_ectx_sem_give;
volatile uint32_t	esp_shim_isr_ctx_ectx_sem_take_blk;

/*
 *  `CHECK_TSKCTX_UNL*`（`fmp3_core/kernel/check.h:152-190`）と同じ条件。
 *  真なら prcv_dtq / pol_sem / ref_dtq は**必ず** E_CTX を返す。
 *  `sns_ctx()`/`sns_loc()` はどちらも任意の文脈・状態から呼べる。
 */
int32_t
esp_shim_isr_ctx_forbidden(void)
{
	return((sns_ctx() || sns_loc()) ? 1 : 0);
}

/*
 *  `CHECK_UNL_MYSTATE`（`fmp3_core/kernel/check.h:122-137`）と同じ条件。
 *  `check_unl_mystate()` が返すのは `sense_lock()` **だけ**である
 *    （コンテキストと p_runtsk は out 引数で返すだけで判定に使わない）。
 *  ⇒ `sig_sem` は**非タスク文脈からは通り**、**CPU ロック中だけ** E_CTX になる。
 *  本ポートの `sense_lock()` は PS.INTLEVEL!=0（`core_kernel_impl.h:197-202`）で、
 *    L1/L3 の C ハンドラは INTLEVEL=0 で走る（`core_support.S:707`／`:1008`）。
 *    ⇒ **ISR から give は通る。**禁止されるのは `rsil` 保持中
 *      （`esp_shim_bt_enter_critical` 等）である。
 */
int32_t
esp_shim_isr_ctx_lock_forbidden(void)
{
	return(sns_loc() ? 1 : 0);
}

/*
 *  診断（沈黙させない）。ISR からも CPU ロック中からも呼ばれ得るので
 *  `syslog()`（`SIL_PRE_LOC`/`SIL_LOC_INT` で退避・復元する＝ネスト安全）
 *  を使う。氾濫を避けるため最初の 4 回とその後 1024 回ごとに出す。
 *  「出す回数を絞る」ことと「出さない」ことは違う——カウンタは常に進む。
 */
static void
esp_shim_isr_ctx_report(const char *what, volatile uint32_t *p_cnt)
{
	uint32_t	n;
	SIL_PRE_LOC;

	SIL_LOC_INT();
	n = *p_cnt + 1U;
	*p_cnt = n;
	SIL_UNL_INT();

	/*  2026-08-04: 文言を give にも通じるものへ一般化した（旧: "reported as
	 *    EMPTY (not empty!)"。give では「空」ではなく「渡せなかった」なので
	 *    誤読を招く）。意味は同じ＝**呼び手へは 0 を返すが、本当の失敗ではない**。 */
	if ((n <= 4U) || ((n % 1024U) == 0U)) {
		syslog(LOG_ERROR,
			   "esp_shim: %s from forbidden ctx (ctx=%d lock=%d) n=%d "
			   "-> folded to 0 (NOT a real empty/full!)",
			   what, (int_t) (sns_ctx() ? 1 : 0), (int_t) (sns_loc() ? 1 : 0),
			   (int_t) n);
	}
}

int32_t
esp_shim_sem_take_from_isr(void *sem)
{
	if (sem == NULL) {
		return(ESP_SHIM_ISR_EMPTY);
	}
	if (esp_shim_isr_ctx_forbidden() != 0) {
		esp_shim_isr_ctx_report("sem_take_from_isr",
								&esp_shim_isr_ctx_ectx_sem_take);
		return(ESP_SHIM_ISR_ECTX);
	}
	return((esp_shim_sem_take(sem, 0U) != 0)
		   ? ESP_SHIM_ISR_OK : ESP_SHIM_ISR_EMPTY);
}

/*
 *  `xSemaphoreGiveFromISR` 相当（送出系）。
 *  禁止条件は take と違って **CPU ロック中だけ**（上の `esp_shim_isr_ctx_lock_forbidden`）。
 *  戻り値の意味: OK=渡せた／EMPTY=渡せなかった（sig_sem の E_QOVR＝資源数上限）／
 *    ECTX=CPU ロック中で問えなかった。
 *  下位（`esp_shim_sem_give` → `sig_sem`）は**禁止文脈でも従来どおり呼ぶ**。
 *    `esp_shim.c` の `diag_event(DIAG_EV_SEM_GIVE, …)` を含む観測経路を変えないため
 *    （足すのは診断だけで、既存の副作用は 1 つも減らさない）。
 */
int32_t
esp_shim_sem_give_from_isr(void *sem)
{
	int32_t	r;

	if (sem == NULL) {
		return(ESP_SHIM_ISR_EMPTY);
	}
	if (esp_shim_isr_ctx_lock_forbidden() != 0) {
		esp_shim_isr_ctx_report("sem_give_from_isr",
								&esp_shim_isr_ctx_ectx_sem_give);
		(void) esp_shim_sem_give(sem);		/* 必ず E_CTX。副作用経路は変えない */
		return(ESP_SHIM_ISR_ECTX);
	}
	r = esp_shim_sem_give(sem);
	return((r != 0) ? ESP_SHIM_ISR_OK : ESP_SHIM_ISR_EMPTY);
}

/*
 *  ブロッキング take の診断つきラッパ。
 *  **戻り値も副作用も `esp_shim_sem_take()` そのもの**（必ず委譲する）。
 *    禁止文脈では `twai_sem`→`pol_sem` がどちらも E_CTX で必ず 0 になるが、
 *    その 0 を**返すのをやめる**のではなく、**黙って返すのをやめる**。
 */
int32_t
esp_shim_sem_take_diag(void *sem, uint32_t block_time_tick)
{
	if (esp_shim_isr_ctx_forbidden() != 0) {
		esp_shim_isr_ctx_report("sem_take(blocking)",
								&esp_shim_isr_ctx_ectx_sem_take_blk);
	}
	return(esp_shim_sem_take(sem, block_time_tick));
}

#ifdef TOPPERS_S3_BT_VHCI_DIAG
/*
 *  ============================================================================
 *  HCI フロー制御セマフォの失敗を数える（2026-08-07・LM-D-3 調査）
 *  ============================================================================
 *
 *  なぜ要るか: `esp_nimble_hci.c` の送信は
 *      if (xSemaphoreTake(vhci_send_sem, 2000ms) == pdTRUE) { 送る }
 *      else { rc = BLE_HS_ETIMEOUT_HCI; }   <- 送らずに捨てる
 *  で、返すのはコントローラのコールバック `controller_rcv_pkt_ready()` の
 *  `xSemaphoreGive` である。**give が失われれば HCI コマンドが捨てられる。**
 *  ところが平の take/give は診断ラッパを通っておらず（`*_from_isr` 版だけが
 *  3 値 API と診断を持つ）、**失敗しても誰も数えていなかった。**
 *
 *  何をするか: **数えるだけ。** 戻り値も副作用も委譲先そのままで、
 *  **挙動を 1 ビットも変えない**（畳んでいるものを畳み直したりしない）。
 *
 *  カウンタの増分は atomic ではない。診断としては十分だが、
 *    **厳密な値ではない**（取りこぼし得る）ことを承知して読むこと。
 *  本ブロックは `TOPPERS_S3_BT_VHCI_DIAG` の内側にあり、
 *    **既定ビルドには 1 バイトも入らない**（golden 不変）。
 */
volatile uint32_t	esp_shim_semdiag_take_n;
volatile uint32_t	esp_shim_semdiag_take_fail;
volatile uint32_t	esp_shim_semdiag_give_n;
volatile uint32_t	esp_shim_semdiag_give_fail;
volatile uint32_t	esp_shim_semdiag_last_fail_sem;

int32_t
esp_shim_sem_take_hcidiag(void *sem, uint32_t block_time_tick)
{
	int32_t	r;

	esp_shim_semdiag_take_n++;
	r = esp_shim_sem_take(sem, block_time_tick);
	if (r == 0) {
		esp_shim_semdiag_take_fail++;
		esp_shim_semdiag_last_fail_sem = (uint32_t) sem;
	}
	return(r);
}

int32_t
esp_shim_sem_give_hcidiag(void *sem)
{
	int32_t	r;

	esp_shim_semdiag_give_n++;
	r = esp_shim_sem_give(sem);
	if (r == 0) {
		esp_shim_semdiag_give_fail++;
		esp_shim_semdiag_last_fail_sem = (uint32_t) sem;
	}
	return(r);
}
#endif /* TOPPERS_S3_BT_VHCI_DIAG */
