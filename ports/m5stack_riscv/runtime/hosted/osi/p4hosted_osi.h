/*
 *  ESP32-P4 + M5Stack Stamp-AddOn C6 (ESP-Hosted) 移植
 *  os_adapter 公開ヘッダ: hosted_osi_funcs_t の FMP3 実装
 *
 *  esp-hosted host（`host/esp_hosted_os_abstraction.h` の `hosted_osi_funcs_t`,
 *  約66関数）を FMP3 プリミティブで埋めるための公開 I/F。
 *
 *  【設計の要点：静的プール払い出し方式】
 *  FMP3 は完全な静的コンフィギュレーションのカーネルであり、acre_tsk/acre_sem の
 *  ような「実行時にオブジェクトを新規生成する」サービスコールを持たない。一方で
 *  esp-hosted host は `_h_thread_create`/`_h_create_queue`/`_h_create_semaphore`/
 *  `_h_create_mutex`/`_h_timer_start` を実行時に動的呼び出しする。
 *
 *  この不整合を、次の方式で橋渡しする:
 *    - cfg (`p4hosted.cfg`) で タスク/セマフォ/データキュー/ミューテックス/
 *      アラームハンドラを「あらかじめ決めた個数 N」だけ静的に CRE_ してプールを作る。
 *    - `_h_create_*` は空きプール要素を 1 つ確保し、その要素を指す構造体ポインタを
 *      「不透明ハンドル」として呼び出し元に返す。
 *    - `_h_destroy_*` は要素をプールへ返却する。
 *    - 確保/返却は `p4hosted_pools.c` 内の 1 本の管理用ミューテックス
 *      (`HOSTED_POOL_META_MTX`) で保護する（タスク文脈からのみ呼ばれる想定。
 *      ISR 文脈から `_h_create_*`/`_h_destroy_*` を呼ぶことは esp-hosted の設計上
 *      無い）。
 *
 *  プールサイズは以下の #define で集中管理する。過不足が判明した場合は実機検証
 *  (フェーズ1以降)で調整する（TODO）。
 *
 *  【時間単位に関する重要な確認事項】
 *  FMP3 の RELTIM/TMO は「マイクロ秒」単位である
 *  （`t_stddef.h` の `TMAX_RELTIM = 4000000000U` ≒ 66分40秒 ≒ 4,000,000,000us
 *  であることから確認済み）。ただし実際のタイマ分解能（tick 周期。本ターゲットは
 *  概ね 1ms 周期）はこれより粗いため、tick 未満の待ち・遅延は丸め上げされるか、
 *  ビジーウェイト（mcycle 直読み）で代替する必要がある（§8 参照）。
 *
 *  【統合時の差し替え注意】
 *  esp-hosted の実ヘッダ（`host/esp_hosted_os_abstraction.h` 等）は統合時に
 *  managed_components から取得される。このファイルの `hosted_osi_funcs_t` 等の
 *  型定義は、現段階（実機無し・upstream ヘッダ未取得のビルド環境）向けの自前の
 *  前方宣言であり、統合時に upstream ヘッダへ差し替えること。
 */
#ifndef P4HOSTED_OSI_H
#define P4HOSTED_OSI_H

#include <kernel.h>
#include <t_syslog.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  =====================================================================
 *  esp-hosted OSAL 契約の自前定義（統合時に upstream ヘッダへ差替え）
 *  =====================================================================
 *  出典: gh api repos/espressif/esp-hosted-mcu/contents/host/esp_hosted_os_abstraction.h
 *  取得日: 本ポーティング作業時点。フィールド順序・シグネチャは upstream に合わせた。
 */

/* esp_event_base.h 相当（最小前方宣言） */
#ifndef ESP_EVENT_BASE_DECLARED
#define ESP_EVENT_BASE_DECLARED
typedef const char *esp_event_base_t;
#endif

/*
 *  トランスポート選択（本移植は SDIO 固定: M5Stamp AddOn C6 は 20-pin SDIO 接続）。
 *  upstream は #if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO で SDIO 関数群を
 *  vtable に含める。SPI/SPI-HD/UART は本移植では対象外（フィールド自体を含めない）。
 */
#define H_TRANSPORT_SPI     1
#define H_TRANSPORT_SDIO    2
#define H_TRANSPORT_SPI_HD  3
#define H_TRANSPORT_UART    4
#define H_TRANSPORT_IN_USE  H_TRANSPORT_SDIO

/*
 *  H_USE_MEMPOOL: upstream は既定で未定義（FreeRTOS 参照実装も #ifdef 任意）。
 *
 *  【重要・実機クラッシュの根本原因(2026-07-09)】この値の「定義の有無」自体が
 *  hosted_osi_funcs_t の実際のフィールドレイアウト（構造体サイズ）を変える
 *  （#ifdef H_USE_MEMPOOL で _h_create_lock_mempool 等4個のフィールドが
 *  丸ごと増減するため）。esp_hosted_core（IDF側、port_fmp3/
 *  port_esp_hosted_host_config.h で H_USE_MEMPOOL=0 を明示的に define 済み
 *  ＝ #ifdef としては「定義あり」扱い）と、本ファイル（FMP3側、従来は
 *  コメントアウトで「定義なし」）とで H_USE_MEMPOOL の定義有無が食い違って
 *  いたため、esp_hosted_core 側が期待するオフセットと FMP3 側が実際に
 *  埋めるオフセットがずれ、g_h.funcs->_h_hosted_init_hook() の呼び出しが
 *  実際には別スロット(_h_sdio_write_reg 相当)を叩いて不正ポインタ参照で
 *  クラッシュしていた（JTAGで実機確認・特定）。
 *  mempool ロック実装自体（p4hosted_pools.c）は FreeRTOS 非依存で常に
 *  用意済みのため、esp_hosted_core 側の定義に合わせてここも define する
 *  （＝両側で「定義あり」に統一し、フィールドレイアウトを一致させる）。
 *  今後この値を変える場合は、p4hosted_osi.h/.c/.cfg の3ファイルと
 *  esp_hosted_core/port_fmp3/port_esp_hosted_host_config.h を必ず揃えること。
 */
#define H_USE_MEMPOOL 1

/*  timer type（upstream の esp_hosted_timer_type_t 相当の自前定義） */
#define H_TIMER_TYPE_ONESHOT    0
#define H_TIMER_TYPE_PERIODIC   1

typedef struct {
	/* ---- Memory ---- */
	void*  (*_h_memcpy)(void* dest, const void* src, uint32_t size);
	void*  (*_h_memset)(void* buf, int val, size_t len);
	void*  (*_h_malloc)(size_t size);
	void*  (*_h_calloc)(size_t blk_no, size_t size);
	void   (*_h_free)(void* ptr);
	void*  (*_h_realloc)(void *mem, size_t newsize);
	void*  (*_h_malloc_align)(size_t size, size_t align);
	void   (*_h_free_align)(void* ptr);

	/* ---- Thread ---- */
	void*  (*_h_thread_create)(const char *tname, uint32_t tprio, uint32_t tstack_size,
			void (*start_routine)(void const *), void *sr_arg);
	int    (*_h_thread_cancel)(void *thread_handle);
	void   (*_h_thread_yield)(void);

	/* ---- Sleeps ---- */
	unsigned int (*_h_msleep)(unsigned int mseconds);
	unsigned int (*_h_usleep)(unsigned int useconds);
	unsigned int (*_h_sleep)(unsigned int seconds);

	/* ---- Blocking non-sleepable delay ---- */
	unsigned int (*_h_blocking_delay)(unsigned int number);

	/* ---- Queue ---- */
	int    (*_h_queue_item)(void * queue_handle, void *item, int timeout);
	void*  (*_h_create_queue)(uint32_t qnum_elem, uint32_t qitem_size);
	int    (*_h_dequeue_item)(void * queue_handle, void *item, int timeout);
	int    (*_h_queue_msg_waiting)(void * queue_handle);
	int    (*_h_destroy_queue)(void * queue_handle);
	int    (*_h_reset_queue)(void * queue_handle);

	/* ---- Mutex ---- */
	int    (*_h_unlock_mutex)(void * mutex_handle);
	void*  (*_h_create_mutex)(void);
	int    (*_h_lock_mutex)(void * mutex_handle, int timeout_ms);
	int    (*_h_destroy_mutex)(void * mutex_handle);

	/* ---- Semaphore ---- */
	int    (*_h_post_semaphore)(void * semaphore_handle);
	int    (*_h_post_semaphore_from_isr)(void * semaphore_handle);
	void*  (*_h_create_semaphore)(int maxCount);
	int    (*_h_get_semaphore)(void * semaphore_handle, int timeout_ms);
	int    (*_h_destroy_semaphore)(void * semaphore_handle);

	/* ---- Timer ---- */
	int    (*_h_timer_stop)(void *timer_handle);
	void*  (*_h_timer_start)(const char *name, int duration_ms, int type,
			void (*timeout_handler)(void *), void *arg);
	uint64_t (*_h_get_time_ms)(void);

#ifdef H_USE_MEMPOOL
	/* ---- Mempool lock ---- */
	void*  (*_h_create_lock_mempool)(void);
	void   (*_h_lock_mempool)(void *lock_handle);
	void   (*_h_unlock_mempool)(void *lock_handle);
	void   (*_h_destroy_lock_mempool)(void *lock_handle);
#endif

	/* ---- GPIO ---- */
	int (*_h_config_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t mode);
	int (*_h_config_gpio_as_interrupt)(void* gpio_port, uint32_t gpio_num, uint32_t intr_type,
			void (*gpio_isr_handler)(void* arg), void *arg);
	int (*_h_teardown_gpio_interrupt)(void* gpio_port, uint32_t gpio_num);
	int (*_h_read_gpio)(void* gpio_port, uint32_t gpio_num);
	int (*_h_write_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t value);
	int (*_h_pull_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t pull_value, uint32_t enable);
	int (*_h_hold_gpio)(void* gpio_port, uint32_t gpio_num, uint32_t hold_value);
	int (*_h_get_host_wakeup_or_reboot_reason)(void);

	/* ---- All Transports - Init ---- */
	void * (*_h_bus_init)(void);
	int (*_h_bus_deinit)(void*);

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI
	int (*_h_do_bus_transfer)(void *transfer_context);
#endif

	int  (*_h_event_wifi_post)(int32_t event_id, void* event_data, size_t event_data_size,
			uint32_t ticks_to_wait);
	void (*_h_printf)(int level, const char *tag, const char *format, ...);
	void (*_h_hosted_init_hook)(void);

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SDIO
	/* ---- Transport - SDIO ---- */
	int (*_h_sdio_card_init)(void *ctx, bool show_config);
	int (*_h_sdio_card_deinit)(void *ctx);
	int (*_h_sdio_read_reg)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_sdio_write_reg)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_sdio_read_block)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_sdio_write_block)(void *ctx, uint32_t reg, uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_sdio_wait_slave_intr)(void *ctx, uint32_t ticks_to_wait);
#endif

#if H_TRANSPORT_IN_USE == H_TRANSPORT_SPI_HD
	int (*_h_spi_hd_read_reg)(uint32_t reg, uint32_t *data, int poll, bool lock_required);
	int (*_h_spi_hd_write_reg)(uint32_t reg, uint32_t *data, bool lock_required);
	int (*_h_spi_hd_read_dma)(uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_spi_hd_write_dma)(uint8_t *data, uint16_t size, bool lock_required);
	int (*_h_spi_hd_set_data_lines)(uint32_t data_lines);
	int (*_h_spi_hd_send_cmd9)(void);
#endif

#if H_TRANSPORT_IN_USE == H_TRANSPORT_UART
	int (*_h_uart_read)(void *ctx, uint8_t *data, uint16_t size);
	int (*_h_uart_write)(void *ctx, uint8_t *data, uint16_t size);
	int (*_h_uart_flush_input)(void *ctx);
#endif

	int (*_h_restart_host)(void);

	int (*_h_config_host_power_save_hal_impl)(uint32_t power_save_type, void* gpio_port,
			uint32_t gpio_num, int level);
	int (*_h_start_host_power_save_hal_impl)(uint32_t power_save_type);
	int (*_h_event_post)(esp_event_base_t event_base, int32_t event_id, void* event_data,
			size_t event_data_size, uint32_t ticks_to_wait);
} hosted_osi_funcs_t;

struct hosted_config_t {
	hosted_osi_funcs_t *funcs;
};

extern hosted_osi_funcs_t g_hosted_osi_funcs;
extern struct hosted_config_t g_h;

#define HOSTED_CONFIG_INIT_DEFAULT() {			\
	.funcs = &g_hosted_osi_funcs,				\
}

/* upstream の HOSTED_WAKEUP_* 定数（実ヘッダ未取得のための暫定自前定義） */
#define HOSTED_WAKEUP_NORMAL_REBOOT		0
#define HOSTED_WAKEUP_DEEP_SLEEP		1
#define HOSTED_WAKEUP_UNDEFINED			2

/*
 *  upstream の戻り値定数（実ヘッダ未取得のための暫定自前定義。値は upstream の
 *  一般的な慣例（0=成功, 負値=失敗）に合わせた。統合時に upstream ヘッダへ差替え）。
 */
#ifndef RET_OK
#define RET_OK				0
#endif
#ifndef RET_FAIL
#define RET_FAIL			(-1)
#endif
#ifndef RET_INVALID
#define RET_INVALID			(-2)
#endif
#ifndef RET_FAIL_TIMEOUT
#define RET_FAIL_TIMEOUT	(-3)
#endif

/*
 *  =====================================================================
 *  静的プールのサイズ（集中管理）
 *  =====================================================================
 *  初期値は暫定。esp-hosted host のタスク/RPC構成（tx/rx task, control task,
 *  event task 等）を実機で確認したうえで過不足を調整すること（TODO）。
 */
#ifndef HOSTED_POOL_NUM_TASK
#define HOSTED_POOL_NUM_TASK		8	/* 動的スレッド生成プール数 */
#endif
#ifndef HOSTED_POOL_NUM_SEM
#define HOSTED_POOL_NUM_SEM			16	/* 動的セマフォ生成プール数 */
#endif
#ifndef HOSTED_POOL_NUM_QUEUE
/*  実機で判明した必要数(2026-07-09): sdio_drv.c が MAX_PRIORITY_QUEUES(3)×2
 *  (rx/tx)=6、rpc_core.c の rpc_init が rpc_rx_q/rpc_tx_q で2、
 *  serial_ll_if.c が1、rpc_wrap.c の rpc_supp_cb_thread_q(DPP等使用時)で1、
 *  計10前後を同時消費する。8では不足し rpc_init が
 *  "Failed to create app rpc msg Q" で ESP_FAIL となり
 *  ESP_ERROR_CHECK 経由でクラッシュすることを実機で確認済み。余裕を見て
 *  12 とする。 */
#define HOSTED_POOL_NUM_QUEUE		12	/* 動的データキュー生成プール数 */
#endif
#ifndef HOSTED_POOL_NUM_MUTEX
#define HOSTED_POOL_NUM_MUTEX		8	/* 動的ミューテックス生成プール数 */
#endif
#ifndef HOSTED_POOL_NUM_TIMER
#define HOSTED_POOL_NUM_TIMER		8	/* 動的タイマ(アラーム)生成プール数 */
#endif

/*  データキュー1本あたりの最大要素数（固定）。
 *  esp-hosted 側の qnum_elem がこれを超える要求をした場合はこの値へクランプし，
 *  警告ログを出す（TODO: 実測して十分な値か確認）。 */
#ifndef HOSTED_QUEUE_MAX_ELEMS
#define HOSTED_QUEUE_MAX_ELEMS		32
#endif

/*  タスクプールの既定スタックサイズ・優先度レンジ。
 *  FMP3 のタスク優先度は TMIN_TPRI(=1, 最高) 〜 TMAX_TPRI(=16, 最低) の静的範囲。
 *  esp-hosted の tprio（FreeRTOS 流：大きいほど高優先度、範囲は呼び出し側依存）を
 *  この範囲へ簡易的に線形写像する（hosted_pool_map_prio()）。正確な対応関係は
 *  esp-hosted の実際の呼び出し値を実機で確認して調整すること（TODO）。 */
#ifndef HOSTED_POOL_TASK_STACK_SIZE
/*  実機で判明した必要値(2026-07-09): hosted_pool_thread_create() は
 *  呼び出し側が要求する tstack_size を無視し、常にこの静的サイズを使う
 *  （実装コメント参照）。esp_hosted_core 側の RPC_TASK_STACK_SIZE/
 *  DFLT_TASK_STACK_SIZE（port_fmp3/port_esp_hosted_host_os.h）が
 *  どちらも 5120 バイトを要求するのに対し、旧値 4096 では不足しスタック
 *  オーバーフローで隣接メモリを破壊し、不定なタイミング・箇所での
 *  クラッシュ/ハングを引き起こしていた（JTAGで実機確認・特定）。
 *  余裕を見て 8192 とする。 */
#define HOSTED_POOL_TASK_STACK_SIZE	8192
#endif
#ifndef HOSTED_POOL_TASK_PRI_DEFAULT
#define HOSTED_POOL_TASK_PRI_DEFAULT	8	/* tprio 変換に失敗した場合の既定優先度 */
#endif

/*  スレッド名を保持する簡易バッファ長（デバッグ表示用。esp-hosted 側は短い
 *  固定名（"rx_thread"等）を渡す想定）。 */
#ifndef HOSTED_THREAD_NAME_MAX
#define HOSTED_THREAD_NAME_MAX		16
#endif

/*  セマフォプールの静的 maxsem（CRE_SEM の maxsem は cfg で固定する必要があるため，
 *  _h_create_semaphore(maxCount) の maxCount 引数はこの値でクランプされる）。 */
#ifndef HOSTED_SEM_STATIC_MAXCNT
#define HOSTED_SEM_STATIC_MAXCNT	1024
#endif

/*  _h_usleep の busy-wait 切替閾値(us)。これ未満は sil_dly_nse ビジーウェイト、
 *  以上は dly_tsk（tick 待ち）にする。実機で tick 周期を確認して調整（TODO）。 */
#ifndef HOSTED_USLEEP_DLY_THRESHOLD_US
#define HOSTED_USLEEP_DLY_THRESHOLD_US	2000
#endif

/*
 *  タイマコールバックディスパッチタスク（p4hosted_pools.c のタイマ節参照。
 *  アラームハンドラ（非タスク文脈）から dtq 経由で通知を受け、コールバックを
 *  タスク文脈で呼ぶ。upstream(FreeRTOS/esp_timer) のタイマタスク相当）。
 *  優先度はプールタスク既定より高め（タイマの遅延を抑えるため）。
 */
#ifndef HOSTED_TMR_DISPATCH_PRI
#define HOSTED_TMR_DISPATCH_PRI			6
#endif
#ifndef HOSTED_TMR_DISPATCH_STACK_SIZE
#define HOSTED_TMR_DISPATCH_STACK_SIZE	4096
#endif
#ifndef HOSTED_TMR_DTQ_CAPACITY
#define HOSTED_TMR_DTQ_CAPACITY			(HOSTED_POOL_NUM_TIMER * 4)
#endif

/*
 *  =====================================================================
 *  内部 API（p4hosted_pools.c 実装／p4hosted_osi.c から使用）
 *  =====================================================================
 *  各関数は静的プールから空き要素を確保・返却しつつ、対応する FMP3 プリミティブを
 *  呼び出す。シグネチャは hosted_osi_funcs_t の対応スロットとそのまま一致するよう
 *  設計しているため、p4hosted_osi.c の vtable 初期化子には多くの場合これらの
 *  関数ポインタをそのまま代入するだけでよい。
 */

/* ---- スレッド ---- */
extern void *hosted_pool_thread_create(const char *tname, uint32_t tprio,
		uint32_t tstack_size, void (*start_routine)(void const *), void *sr_arg);
extern int   hosted_pool_thread_cancel(void *thread_handle);

/* ---- セマフォ ---- */
extern void *hosted_pool_create_semaphore(int maxCount);
extern int   hosted_pool_destroy_semaphore(void *semaphore_handle);
extern int   hosted_pool_post_semaphore(void *semaphore_handle);
extern int   hosted_pool_get_semaphore(void *semaphore_handle, int timeout_ms);

/* ---- ミューテックス ---- */
extern void *hosted_pool_create_mutex(void);
extern int   hosted_pool_destroy_mutex(void *mutex_handle);
extern int   hosted_pool_lock_mutex(void *mutex_handle, int timeout_ms);
extern int   hosted_pool_unlock_mutex(void *mutex_handle);

/* ---- データキュー ---- */
extern void *hosted_pool_create_queue(uint32_t qnum_elem, uint32_t qitem_size);
extern int   hosted_pool_destroy_queue(void *queue_handle);
extern int   hosted_pool_queue_item(void *queue_handle, void *item, int timeout);
extern int   hosted_pool_dequeue_item(void *queue_handle, void *item, int timeout);
extern int   hosted_pool_queue_msg_waiting(void *queue_handle);
extern int   hosted_pool_reset_queue(void *queue_handle);

/* ---- タイマ（アラーム） ---- */
extern void *hosted_pool_timer_start(const char *name, int duration_ms, int type,
		void (*timeout_handler)(void *), void *arg);
extern int   hosted_pool_timer_stop(void *timer_handle);

#ifdef H_USE_MEMPOOL
/* ---- mempool ロック（フェーズ2以降で必要になれば有効化） ---- */
extern void *hosted_pool_create_lock_mempool(void);
extern void  hosted_pool_lock_mempool(void *lock_handle);
extern void  hosted_pool_unlock_mempool(void *lock_handle);
extern void  hosted_pool_destroy_lock_mempool(void *lock_handle);
#endif

/*
 *  =====================================================================
 *  【追加 D-8（本 repo・段7b）】写像層の計数
 *  =====================================================================
 *  BL-H-7 の教訓（「長く待つつもりが全く待たない」が**黙って**起きる）に対して、
 *  写像層で起きうる縮退を数字で見えるようにする。**0 でも印字する**
 *  （「数えていない」と「0 だった」は違う）。
 */
#ifndef P4HOSTED_SDIO_WAIT_CAP_MS
/*  `_h_sdio_wait_slave_intr` の待ち上限[ms]。出典と同じ 100ms。
 *  永久待ちを作らず、タイムアウトを成功として返してポーリングへ落とすための値
 *  （出典 §31 の安全網。上流 RX ループは成功のたびにレジスタを読み直す）。 */
#define P4HOSTED_SDIO_WAIT_CAP_MS	100U
#endif

extern volatile uint32_t	p4hosted_n_wait_call;		/* 待ちを呼んだ回数 */
extern volatile uint32_t	p4hosted_n_wait_ok;			/* E_OK（割込み or 先読み成立） */
extern volatile uint32_t	p4hosted_n_wait_tmout;		/* E_TMOUT（ポーリング契機） */
extern volatile uint32_t	p4hosted_n_wait_err;		/* それ以外（必ず 0 のはず） */
extern volatile uint32_t	p4hosted_n_wait_capped;		/* 上限に当てた回数 */
extern volatile uint32_t	p4hosted_n_event_post_unwired;	/* D-2: 未配線イベント投函 */

/*
 *  =====================================================================
 *  初期化関数
 *  =====================================================================
 *  アプリのブートタスクから一度だけ呼び出す。プールの静的オブジェクトは
 *  CRE_* によって既にカーネル起動時に生成済みなので、ここでは
 *    - 各プールの "used" フラグが全クリア済みであることの防御的な再初期化
 *    - g_h（HOSTED_CONFIG_INIT_DEFAULT 相当）の設定
 *  のみ行う。実 HW 初期化は含まない（GPIO/SDIO 等は個別のドライバ初期化で行う）。
 */
extern void p4hosted_osi_init(void);

/*
 *  =====================================================================
 *  cfg 側から参照されるプールタスク本体・アラームハンドラ（トランポリン）
 *  =====================================================================
 *  p4hosted.cfg の CRE_TSK/CRE_ALM から exinf=プール添字(0起点) で
 *  呼び出される。アプリからは通常呼び出さない。
 */
extern void hosted_pool_task_trampoline(EXINF exinf);
extern void hosted_pool_alarm_trampoline(EXINF exinf);
extern void hosted_pool_timer_dispatch_task(EXINF exinf);

/*
 *  【改変 D-4（本 repo・段7b）】ESP-IDF crosscore 割込み（線 17）の宣言を削除。
 *  理由は p4hosted_osi.c の同名コメント参照（FreeRTOS を 1 記号もリンクしない
 *  ので引取りの対象が居ない。かつ線は C-1 CLIC シムの利用者表が正本）。
 */

#ifdef __cplusplus
}
#endif

#endif /* P4HOSTED_OSI_H */
