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
 *  セマフォ（CRE_SEMプール．counting／binary両対応）
 */
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);
extern uint32_t esp_shim_sem_get_count(void *sem);	/* FreeRTOS uxSemaphoreGetCount相当 */

/*
 *  ミューテックス（CRE_MTXプール．再帰対応はshimでラップ）
 */
extern void *esp_shim_mutex_create(bool_t recursive);
extern void esp_shim_mutex_delete(void *mtx);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_trylock(void *mtx);	/* 非ブロッキング（ploc_mtx委譲） */
extern uint_t esp_shim_mutex_pool_used(void);		/* 使用中スロット数（診断用） */
extern int32_t esp_shim_mutex_unlock(void *mtx);

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
 *  キュー（ヒープ上リングバッファ＋セマフォ．任意item長・ISR送信対応）
 */
extern void *esp_shim_queue_create(uint32_t len, uint32_t item_size);
extern void esp_shim_queue_delete(void *que);
extern int32_t esp_shim_queue_send(void *que, void *item,
								   uint32_t block_time_tick, bool_t to_front);
extern int32_t esp_shim_queue_send_from_isr(void *que, void *item);
extern int32_t esp_shim_queue_recv(void *que, void *item,
								   uint32_t block_time_tick);
extern uint32_t esp_shim_queue_msg_waiting(void *que);
extern uint32_t esp_shim_queue_spaces_available(void *que);	/* W3: uxQueueSpacesAvailable用 */
extern void esp_shim_queue_reset(void *que);	/* FreeRTOS xQueueReset相当 */

/*
 *  タスク（CRE_TSKプール．共通エントリ＋関数ポインタ渡し）
 */
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									uint32_t stack_size, void *param,
									uint32_t freertos_prio, void **task_handle);
extern void esp_shim_task_delete(void *task_handle);  /* NULL=自タスク */

/*
 *  タスクの動的生成——wifi/shim/esp_shim_tsk.c
 *
 *  上流 fmp3_esp_idf の esp/shim/esp_shim.h と同じ契約。上の
 *  esp_shim_task_create() が FreeRTOS 形の入口で、その下でこれらを呼ぶ。
 *
 *  親和性は FreeRTOS 起点（0..TNUM_PRCID-1）で渡す。「どちらでもよい」は
 *  ESP_SHIM_TASK_NO_AFFINITY（= CONFIG_FREERTOS_NO_AFFINITY）で、PRC1 を選ぶ。
 */
#define ESP_SHIM_TASK_NO_AFFINITY	0x7FFFFFFFU

extern ID esp_shim_tsk_create(uint_t slot, TASK task, EXINF exinf, PRI itskpri);
extern bool_t esp_shim_tsk_activate(ID tskid, uint32_t core_id);
extern bool_t esp_shim_tsk_reap(ID tskid);
extern bool_t esp_shim_tsk_terminate(ID tskid);

/*  診断カウンタ（esp_shim_tsk.c が持つ）。 */
extern volatile uint32_t	esp_shim_tsk_live;
extern volatile uint32_t	esp_shim_tsk_reaped;
extern volatile uint32_t	esp_shim_tsk_acre_fail;
extern volatile uint32_t	esp_shim_tsk_mact_fail;
extern volatile uint32_t	esp_shim_tsk_del_fail;
extern volatile uint32_t	esp_shim_tsk_core_clamped;
extern void esp_shim_task_delay(uint32_t tick);
extern void *esp_shim_task_get_current(void);
/*  ★FreeRTOS の task notification（M5Unified の Speaker/Mic が使う）。 */
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
extern void esp_shim_timer_disarm(void *ptimer);
extern void esp_shim_timer_done(void *ptimer);

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

#endif /* ESP_SHIM_H */
