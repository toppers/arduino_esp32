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
 *  Wi-Fi os_adapter shimの基盤プリミティブ（ASP3用）
 *
 *  Wi-Fiバイナリblobが要求するFreeRTOS流の動的生成（task/queue/
 *  semaphore/mutex/timer/malloc）を，ASP3の静的生成オブジェクトの
 *  プール＋shim実装で提供する（設計はdocs/wifi-shim.md）．
 *  ここはカーネル外（アプリ/ライブラリ層）＝AGENTS.md禁則②の対象外
 *  だが，ヒープ自体は静的配列上に実装する．
 *
 *  時間の単位：blobとのやりとりの「tick」は1ms（_task_ms_to_tick等で
 *  blobへそう申告する）．ASP3のタイムアウトはμs（TMO）へ変換する．
 */

#ifndef ESP_SHIM_H
#define ESP_SHIM_H

#include <kernel.h>
#include <t_syslog.h>
#include "esp_shim_isr_ctx.h"	/* ISR/CPUロック文脈用の3値API（下の queue 節参照） */

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ブロック指定（blob側の定義と一致：OSI_FUNCS_TIME_BLOCKING）
 */
#define ESP_SHIM_BLOCK_FOREVER  0xffffffffU

/*
 *  tick（1ms）→ASP3タイムアウト（μs）変換
 */
extern TMO esp_shim_tick_to_tmo(uint32_t tick);

/*
 *  ヒープ（静的配列上のfirst-fit・カーネル外）
 */
extern void *esp_shim_malloc(size_t size);
extern void *esp_shim_calloc(size_t n, size_t size);
extern void *esp_shim_realloc(void *ptr, size_t size);
extern void esp_shim_free(void *ptr);
extern size_t esp_shim_heap_free_size(void);
extern size_t esp_shim_heap_largest_free_block(void);	/* W3: heap_caps_get_largest_free_block用 */

/*
 *  セマフォ（動的生成＝acre_sem/del_sem．生成／削除の実体は
 *  esp/shim/esp_shim_sem.c．take/give/get_count は esp_shim.c のまま）
 *
 *  2026-08-04（段2）: 静的プール（CRE_SEM(SHIM_SEM1..96) ＋ shim_sem_id[]）を
 *  廃止した。create は**タスク文脈かつ CPU ロック外**で呼ぶこと
 *  （acre_sem の CHECK_TSKCTX_UNL）。
 *  ハンドルは**従来どおりセマフォ ID を裸で入れた void ***（mtx と表現が違う。
 *  理由は esp_shim_sem.c 冒頭——take/give/get_count が ID として直に使うため）。
 */
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);
extern uint32_t esp_shim_sem_get_count(void *sem);	/* FreeRTOS uxSemaphoreGetCount相当 */

/*  診断カウンタ（エラー値は翻訳せずそのまま残す．§7-4） */
extern volatile uint32_t	esp_shim_sem_acre_fail;
extern volatile int32_t		esp_shim_sem_acre_last_ercd;
extern volatile uint32_t	esp_shim_sem_del_fail;
extern volatile int32_t		esp_shim_sem_del_last_ercd;
extern volatile uint32_t	esp_shim_sem_del_foreign;	/* 範囲外ハンドル（NimBLE の mtx 経路） */
extern volatile uint32_t	esp_shim_sem_live;			/* 生きている本数（診断用） */

/*
 *  ミューテックス（動的生成＝acre_mtx/del_mtx．実体は esp/shim/esp_shim_mtx.c．
 *  再帰対応はshimでラップ）
 *
 *  2026-08-04（段1）: 静的プール（CRE_MTX ×20 ＋ shim_mtx[]）を廃止した。
 *  create は**タスク文脈かつ CPU ロック外**で呼ぶこと（acre_mtx の CHECK_TSKCTX_UNL）。
 *  ハンドルは実アドレスであって ID ではない（理由は esp_shim_mtx.c 冒頭。
 *  ID を裸で返すと NimBLE の vSemaphoreDelete 誤用経路がセマフォを壊す）。
 */
extern void *esp_shim_mutex_create(bool_t recursive);
extern void esp_shim_mutex_delete(void *mtx);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_trylock(void *mtx);	/* 非ブロッキング（ploc_mtx委譲） */
extern uint_t esp_shim_mutex_pool_used(void);		/* 生きている本数（診断用） */
extern int32_t esp_shim_mutex_unlock(void *mtx);

/*  診断カウンタ（エラー値は翻訳せずそのまま残す．§7-4） */
extern volatile uint32_t	esp_shim_mtx_acre_fail;
extern volatile int32_t		esp_shim_mtx_acre_last_ercd;
extern volatile uint32_t	esp_shim_mtx_del_fail;
extern volatile int32_t		esp_shim_mtx_del_last_ercd;
extern volatile uint32_t	esp_shim_mtx_stale_handle;
extern volatile uint32_t	esp_shim_mtx_lock_fail;
extern volatile int32_t		esp_shim_mtx_lock_last_ercd;

/*
 *  イベントフラグ（CRE_FLGプール．W3④ SPPのEventGroupHandle_t用．
 *  TOPPERS_ESP32_BT_BLUEDROID_CLASSIC限定のプールを使う実装で，
 *  それ以外のビルドでは未定義シンボル（esp_shim.c側が#ifdef
 *  ESP_SHIM_NUM_FLGで実体を出し分ける）だが，freertos/event_groups.h
 *  はBlueDroid Classic（btc_spp.c）以外からincludeされないため
 *  W1(Wi-Fi)/W2(BLE)のリンクには影響しない）
 */
extern void *esp_shim_flag_create(void);
extern void esp_shim_flag_delete(void *flg);
extern uint32_t esp_shim_flag_set_bits(void *flg, uint32_t bits_to_set);
extern uint32_t esp_shim_flag_clear_bits(void *flg, uint32_t bits_to_clear);
extern uint32_t esp_shim_flag_wait_bits(void *flg, uint32_t bits_to_wait_for,
										 bool_t clear_on_exit, bool_t wait_for_all,
										 uint32_t block_time_tick);

/*
 *  キュー（`esp_shim_queue_*`）と、その実体だったデータキュー生成の口
 *  （`esp_shim_dtq_*`）の宣言は **2026-08-15（BL-G-3 段3）で削除した**。
 *  実装ごと撤去したためである（経緯は `esp/shim/esp_shim.c` の該当箇所）。
 *  今のキューは `esp/shim/esp_shim_ring.h` の `esp_shim_ring_*`。
 */

/*
 *  上の take は写像先が**タスク文脈専用**のサービスコール（pol_sem）なので，
 *  ISR文脈・CPUロック中では E_CTX で必ず失敗し，**0＝「空」と区別が付かない**．
 *  ISR経路から呼ぶときは esp_shim_isr_ctx.h の3値APIを使うこと
 *  （実装は esp/shim/esp_shim_isr_ctx.c）．
 *  2026-08-15（段3）: **キュー側にこの穴はもう無い**——写像先が
 *  `esp_shim_ring_*`（シム所有リング）になり，ISR 文脈でも実際に受信できる．
 *  2026-08-04 追記（.steering/20260804-coex-ectx/）:
 *   ・`esp_shim_sem_give`（sig_sem）も**同型の穴を持つ**——ただし禁止条件が違う。
 *     sig_sem は CHECK_UNL_MYSTATE ＝ `sense_lock()` だけを見るので
 *     **ISR からは通り，CPU ロック中（rsil 保持）で E_CTX＝0** になる。
 *     ISR/クリティカルセクション経路からは `esp_shim_sem_give_from_isr()` を使うこと。
 *   ・リンク先は「ble 構成だけ」ではなくなった——**coex をリンクする 7 構成**
 *     （wifi / ble / m5-wifi / m5-factory 系）に入る（CMakeLists.txt）。
 */

/*
 *  タスク（2026-08-04・段4: **動的生成**．共通エントリ＋関数ポインタ渡し）
 *
 *  実体は `AID_TSK` ＋ `acre_tsk`/`mact_tsk`/`del_tsk`（`esp/shim/esp_shim_tsk.c`）。
 *  スタックはシムが供給する（案B）。記録: `.steering/20260804-dcre-stage4-tsk/`。
 */
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									uint32_t stack_size, void *param,
									uint32_t freertos_prio, void **task_handle);
/*
 *  「コア親和性の指定なし」を表す値（＝ESP-IDF の `CONFIG_FREERTOS_NO_AFFINITY`。
 *  実測: `esp/boot/seam_s3/build/config/sdkconfig.h:542` に `0x7FFFFFFF` として実在する。
 *   ⇒ 「どちらでもよい」は**仮定ではなく実際に渡され得る値**である）。
 */
#define ESP_SHIM_TASK_NO_AFFINITY	0x7FFFFFFFU

/*
 *  コア指定つきの生成（`xTaskCreatePinnedToCore` 相当）。
 *  **`core_id` を捨てない**——`esp_shim_tsk.c` の `shim_tsk_prcid()` が
 *  `mact_tsk(tskid, prcid)` の `prcid` へ写像する。「どちらでもよい」は
 *  `ESP_SHIM_TASK_NO_AFFINITY` を渡すこと（＝`CONFIG_FREERTOS_NO_AFFINITY`）。
 */
extern int32_t esp_shim_task_create_pinned(void (*entry)(void *), const char *name,
										   uint32_t stack_size, void *param,
										   uint32_t freertos_prio,
										   void **task_handle, uint32_t core_id);
extern void esp_shim_task_delete(void *task_handle);  /* NULL=自タスク */

/*
 *  動的タスクの生成／起動／回収（`esp/shim/esp_shim_tsk.c`）。
 *  呼ぶのは `esp_shim.c` のプール管理だけ＝**シム内部の口**である。
 *  **CPU ロックの外・タスク文脈で呼ぶこと**（`CHECK_TSKCTX_UNL`）。
 */
extern ID esp_shim_tsk_create(uint_t slot, TASK task, EXINF exinf, PRI itskpri);
#ifdef M5_SHIM_AUDIO
/*  段7: 音声プール専用の生成（スタック長と `TA_FPU` が blob と違うため別の口）。
 *  起動／回収（`esp_shim_tsk_activate`／`_reap`／`_terminate`）は blob と**共通**である。 */
extern ID esp_shim_audio_tsk_create(uint_t slot, TASK task, EXINF exinf, PRI itskpri);
#endif /* M5_SHIM_AUDIO */
extern bool_t esp_shim_tsk_activate(ID tskid, uint32_t core_id);
extern bool_t esp_shim_tsk_reap(ID tskid);
extern bool_t esp_shim_tsk_terminate(ID tskid);
extern void esp_shim_task_delay(uint32_t tick);
extern void *esp_shim_task_get_current(void);
/*  FreeRTOS の task notification（M5Unified の Speaker/Mic が使う）。 */
extern uint32_t esp_shim_task_notify_take(int clear_on_exit, uint32_t timeout_ms);
extern void esp_shim_task_notify_give(void *task_handle);
extern void esp_shim_task_yield(void);

/*
 *  ets_timer（shim専用タイマタスク＋リスト．コールバックはタスク文脈）
 */
struct ets_timer;   /* blob側定義（rom/ets_sys.h）と互換のopaque扱い */
extern void esp_shim_timer_setfn(void *ptimer, void (*pfunc)(void *),
								 void *parg);
extern void esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat);
/*  ms 指定は **必ずこれを使う**（`ms * 1000U` を 32bit で書くと折り返す。
 *  2026-08-14 の全系停止の引き金。esp_shim.c の同関数のコメントを読むこと）。 */
extern void esp_shim_timer_arm_ms(void *ptimer, uint32_t ms, bool_t repeat);
extern void esp_shim_timer_arm_us64(void *ptimer, uint64_t us, bool_t repeat);
extern void esp_shim_timer_disarm(void *ptimer);
extern void esp_shim_timer_done(void *ptimer);
/*  タイマタスクの待ちが E_OK/E_TMOUT 以外で返った回数（0 が正常）  */
extern volatile uint32_t esp_shim_timer_wait_err;

/*
 *  クリティカルセクション（loc_cpu／unl_cpuのネスト対応ラッパ）
 */
extern uint32_t esp_shim_int_disable(void);
extern void esp_shim_int_restore(uint32_t state);

/*
 *  割込みディスパッチ（Wi-Fi系のCPU割込み線の動的ハンドラ登録）
 *
 *  cfgでDEF_INHした共通入口（esp_shim_wifi_int_handler）から，
 *  set_isrで登録された関数を呼び出す．
 */
extern void esp_shim_set_isr(int32_t cpu_intno, void *handler, void *arg);
extern void esp_shim_wifi_mac_inthdr(void);
extern void esp_shim_wifi_pwr_inthdr(void);

/*
 *  時刻・乱数
 */
extern int64_t esp_shim_time_us(void);   /* 起動からのμs（SYSTIMER） */
extern uint32_t esp_shim_random(void);

/*
 *  shim全体の初期化（ヒープ・プール管理の初期化．Wi-Fi使用前に呼ぶ）
 */
extern void esp_shim_initialize(void);

/*
 *  coexアダプタの登録（WiFi初期化前に呼ぶ）
 */
extern void esp_shim_coex_adapter_register(void);

/*
 *  BTベースバンド／MACのクロック有効化＋リセット解除
 *  （esp_bt_controller_init()の直前に呼ぶ．実装・経緯はesp/bt/bt_shim.c）
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  ログ（blobの_log_write系の折返し先）
 */
extern void esp_shim_log_write(const char *format, ...);
extern void esp_shim_log_emerg(const char *format, ...);

/*
 *  実行中コアの0起点ID（xPortGetCoreID の写像先。M5は原則コア0固定）
 */
extern int32_t esp_shim_get_core_id(void);

#ifdef __cplusplus
}
#endif

/*
 *  ESP32-C6 / ESP32-C5（計画 3 段4 Task 3、2026-09-16）: HW RNG の読出し番地。
 *  esp_shim.c の SHIM_WDEV_RND_REG が参照する。**esp_shim.h の末尾に追記**するのは
 *  seam-c6-wifi の golden（DWARF 感受性）を動かさないため（同じ理由で esp_shim.c
 *  側は #elif の条件と #define の右辺だけを同じ行数で書き換えた。C6 の値は不変）。
 *    C6: WDEV_RND_REG = LPPERI_RNG_DATA_REG      = 0x600B2800 + 0x8
 *        （soc/esp32c6/register/soc/lpperi_reg.h:139、wdev_reg.h:13）
 *    C5: WDEV_RND_REG = LPPERI_RNG_DATA_SYNC_REG = 0x600B2800 + 0x28
 *        （soc/esp32c5/include/soc/wdev_reg.h、fmp3/arch/riscv_gcc/esp32c5/esp32c5.h の
 *          ESP32C5_WDEV_RND_REG と同値。asp3 esp/c5/wifi_v8/esp_shim_chip_regs.h:20）
 */
#if defined(TOPPERS_ESP32C6)
#define ESP_SHIM_RISCV_WDEV_RND_REG		0x600B2808U
#elif defined(TOPPERS_ESP32C5)
#define ESP_SHIM_RISCV_WDEV_RND_REG		0x600B2828U
#endif
/*
 *  同じく eFuse の MAC レジスタのベース（esp_shim_blobglue.c の EFUSE_RD_MAC_SPI_SYS_0/1_REG
 *  = base + 0x44 / 0x48。C6/C5 とも efuse_reg.h の EFUSE_RD_MAC_SYS_0/1_REG）。
 *    C6: DR_REG_EFUSE_BASE = 0x600B0800（soc/esp32c6/register/soc/reg_base.h）
 *    C5: DR_REG_EFUSE_BASE = 0x600B4800（soc/esp32c5/register/soc/reg_base.h:107、
 *        fmp3/arch/riscv_gcc/esp32c5/esp32c5.h の ESP32C5_EFUSE_BASE、asp3 C5 blobglue）
 */
#if defined(TOPPERS_ESP32C6)
#define ESP_SHIM_RISCV_EFUSE_BASE		0x600B0800U
#elif defined(TOPPERS_ESP32C5)
#define ESP_SHIM_RISCV_EFUSE_BASE		0x600B4800U
#endif

#endif /* ESP_SHIM_H */
