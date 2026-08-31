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
 *  Wi-Fi os_adapter（wifi_osi_funcs_t）のASP3実装
 *
 *  NuttXのesp_wifi_adapter.c（apache/nuttx
 *  arch/risc-v/src/esp32c3/esp_wifi_adapter.c）を設計テンプレートに，
 *  osi関数をshim基盤（esp_shim.[ch]）で実装したもの．
 *  NuttXと同じくevent group・NVSは未実装（スタブ）．
 *  設計はdocs/wifi-shim.md．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <stdio.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_shim_cfg.h"

#include "esp_attr.h"
#include "esp_private/wifi_os_adapter.h"
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_ESP_WIFI_TARGET_ESP32
/* 無印ESP32のosi_funcsが要求するPHY共通クロック制御（esp_phy_init.h実体、phy_init.c）。 */
extern void esp_phy_common_clock_enable(void);
extern void esp_phy_common_clock_disable(void);
#endif
#include "esp_private/wifi.h"
#include "private/esp_coexist_adapter.h"

/*
 *  リンク閉包で解決するesp-hal／blob側の関数（宣言のみ）
 */
extern void esp_phy_enable(int modem);
extern void esp_phy_disable(int modem);
extern void phy_wifi_enable_set(uint8_t enable);
extern int esp_phy_update_country_info(const char *country);
extern void periph_module_reset(int periph);
extern void wifi_module_enable(void);
extern void wifi_module_disable(void);
extern int esp_read_mac(uint8_t *mac, int type);

/*  PERIPH_WIFI_MODULE（esp_private/periph_ctrl.h相当．C3の値） */
#ifndef PERIPH_WIFI_MODULE
#define PERIPH_WIFI_MODULE  24
#endif
#ifndef PHY_MODEM_WIFI
#define PHY_MODEM_WIFI      1
#endif

/*
 *		割込み関連
 */
#if defined(TOPPERS_ESP32_LX6)
/*  無印ESP32(classic)の割込みマトリクスはDPORT系．ソースNのCPU線MAPレジスタは
 *  DPORT_PRO_MAC_INTR_MAP_REG(=DR_REG_DPORT_BASE+0x104=0x3ff00104)を起点に
 *  0x3ff00104 + source*4．マトリクス側の優先度レジスタ／一括enableビットマップは
 *  無く（S3の0x600C2114/0x600C2104は存在しない），優先度はCPU割込みレベルで決まり，
 *  enable/disableはXtensa INTENABLEで行う（下のints_on/off参照）．
 *  ★S3版は0x600C2000を叩いており，無印では非マップ番地への書込み＝esp_wifi_init中の
 *  例外/黙殺でWiFi割込みが全く配線されない（コンソール沈黙・MAC割込み不着の主因）． */
#define INTMTX_MAP_REG(src)  (0x3FF00104U + (src) * 4U)

static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	syslog(LOG_NOTICE, "wifi_adapter: set_intr src=%d intno=%d prio=%d",
		   (int_t)intr_source, (int_t)intr_num, (int_t)intr_prio);
	/*  ソース→CPU割込み線ルーティング（DPORT MAPレジスタ）．優先度はCPU線レベルで決定． */
	sil_wrw_mem((void *)(uintptr_t)INTMTX_MAP_REG(intr_source), intr_num);
	(void) cpu_no;
	(void) intr_prio;
}

static void
clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
	/*  無印ESP32：ソースを無効CPU線へ（未接続扱い）．ESP-IDFは未使用線番号へ退避する． */
	sil_wrw_mem((void *)(uintptr_t)INTMTX_MAP_REG(intr_source), 0U);
	(void) intr_num;
}
#else /* defined(TOPPERS_ESP32_LX6) : ESP32-S3 既存 */
#define INTMTX_BASE_ADDR   0x600C2000U
#define INTMTX_ENABLE_REG  (INTMTX_BASE_ADDR + 0x104U)
#define INTMTX_PRI_REG(n)  (INTMTX_BASE_ADDR + 0x114U + (n) * 4U)

static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	syslog(LOG_NOTICE, "wifi_adapter: set_intr src=%d intno=%d prio=%d",
		   (int_t)intr_source, (int_t)intr_num, (int_t)intr_prio);
	/*
	 *  割込みマトリクスのルーティング（ソース→CPU割込み線）と優先度．
	 *  blobが使う線はカーネル管理外扱い（cfgのDEF_INHは共通ディス
	 *  パッチャ＝esp_shim.cfg参照）のため直接レジスタを操作する．
	 *  優先度はblobの指定に関わらず内部表現2（外部-2）に固定する．
	 */
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), intr_num);
	sil_wrw_mem((void *)(uintptr_t)INTMTX_PRI_REG(intr_num), 2U);
	(void) cpu_no;
	(void) intr_prio;
}

static void
clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), 0U);
	(void) intr_num;
}
#endif /* defined(TOPPERS_ESP32_LX6) */

static void
set_isr_wrapper(int32_t n, void *f, void *arg)
{
	esp_shim_set_isr(n, f, arg);
	/*
	 *  CPU割込みをCPU側(Xtensa INTENABLE)でも許可する。blobの_ints_onは
	 *  INTMTX_ENABLE_REG（割込みマトリクス側）しか操作せず、Xtensaの
	 *  INTENABLEビットを立てないため、CFG_INTした線をena_intで許可しないと
	 *  WiFi ISR（例: MAC割込みは intno=0）が発火しない（実機でint_count=0を確認。
	 *   追記18）。ハンドラ登録(f!=NULL)時のみ許可する。
	 */
	if (f != NULL && n >= 0 && n <= ESP_SHIM_MAX_WIFI_INTNO) {
		(void) ena_int((INTNO) n);
	}
}

static void
ints_on_wrapper(uint32_t mask)
{
#if defined(TOPPERS_ESP32_LX6)
	/*  無印ESP32：割込みマトリクス側の一括enableビットマップは無い．CPU割込みの
	 *  許可は set_isr_wrapper が ena_int() で行い（線登録時），マトリクスは
	 *  set_intr のルーティング＝配線そのものが有効化に相当する．ブリングアップでは
	 *  ここは no-op（blobの一時マスクは wifi_int_disable/restore が担う）． */
	(void) mask;
#else
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)INTMTX_ENABLE_REG,
				sil_rew_mem((void *)INTMTX_ENABLE_REG) | mask);
	esp_shim_int_restore(lock);
#endif
}

static void
ints_off_wrapper(uint32_t mask)
{
#if defined(TOPPERS_ESP32_LX6)
	(void) mask;
#else
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)INTMTX_ENABLE_REG,
				sil_rew_mem((void *)INTMTX_ENABLE_REG) & ~mask);
	esp_shim_int_restore(lock);
#endif
}

static bool
is_from_isr_wrapper(void)
{
	return(sns_ctx());
}

/*
 *		環境・スピンロック（シングルコアのため割込み禁止で代用）
 */
static bool
env_is_chip_wrapper(void)
{
	return(true);	/* 実チップ（QEMUでもWi-Fiは動かないためtrue固定） */
}

static void *
spin_lock_create_wrapper(void)
{
	return((void *)1);	/* シングルコア：実体不要（非NULLを返す） */
}

static void
spin_lock_delete_wrapper(void *lock)
{
	(void) lock;
}

static uint32_t IRAM_ATTR
wifi_int_disable_wrapper(void *wifi_int_mux)
{
	(void) wifi_int_mux;
	return(esp_shim_int_disable());
}

static void IRAM_ATTR
wifi_int_restore_wrapper(void *wifi_int_mux, uint32_t tmp)
{
	(void) wifi_int_mux;
	esp_shim_int_restore(tmp);
}

static void IRAM_ATTR
task_yield_from_isr_wrapper(void)
{
	/* ASP3では割込み出口でディスパッチされるため何もしない */
}

/*
 *		セマフォ・ミューテックス
 */
static void *
semphr_create_wrapper(uint32_t max, uint32_t init)
{
	return(esp_shim_sem_create(max, init));
}

static void
semphr_delete_wrapper(void *semphr)
{
	esp_shim_sem_delete(semphr);
}

static int32_t
semphr_take_wrapper(void *semphr, uint32_t block_time_tick)
{
	return(esp_shim_sem_take(semphr, block_time_tick));
}

static int32_t
semphr_give_wrapper(void *semphr)
{
	return(esp_shim_sem_give(semphr));
}

extern void *esp_shim_thread_semphr_get(void);

static void *
mutex_create_wrapper(void)
{
	return(esp_shim_mutex_create(false));
}

static void *
recursive_mutex_create_wrapper(void)
{
	return(esp_shim_mutex_create(true));
}

static void
mutex_delete_wrapper(void *mutex)
{
	esp_shim_mutex_delete(mutex);
}

static int32_t
mutex_lock_wrapper(void *mutex)
{
	return(esp_shim_mutex_lock(mutex));
}

static int32_t
mutex_unlock_wrapper(void *mutex)
{
	return(esp_shim_mutex_unlock(mutex));
}

/*
 *		キュー
 */
static void *
queue_create_wrapper(uint32_t queue_len, uint32_t item_size)
{
	return(esp_shim_queue_create(queue_len, item_size));
}

static void
queue_delete_wrapper(void *queue)
{
	esp_shim_queue_delete(queue);
}

static int32_t
queue_send_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, false));
}

static int32_t IRAM_ATTR
queue_send_from_isr_wrapper(void *queue, void *item, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;	/* higher priority task woken：ASP3では不要 */
	}
	return(esp_shim_queue_send_from_isr(queue, item));
}

static int32_t
queue_send_to_back_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, false));
}

static int32_t
queue_send_to_front_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, true));
}

static int32_t
queue_recv_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_recv(queue, item, block_time_tick));
}

static uint32_t
queue_msg_waiting_wrapper(void *queue)
{
	return(esp_shim_queue_msg_waiting(queue));
}

static void *
wifi_create_queue_wrapper(int queue_len, int item_size)
{
	/*
	 *  blobはwifi_static_queue_t（{handle,storage}）形式を期待する
	 *  （NuttX実装と同じ）
	 */
	wifi_static_queue_t	*wq;

	wq = (wifi_static_queue_t *)esp_shim_calloc(1U,
												sizeof(wifi_static_queue_t));
	if (wq == NULL) {
		return(NULL);
	}
	wq->handle = esp_shim_queue_create((uint32_t)queue_len,
									   (uint32_t)item_size);
	if (wq->handle == NULL) {
		esp_shim_free(wq);
		return(NULL);
	}
	return(wq);
}

static void
wifi_delete_queue_wrapper(void *queue)
{
	wifi_static_queue_t	*wq = (wifi_static_queue_t *)queue;

	if (wq != NULL) {
		esp_shim_queue_delete(wq->handle);
		esp_shim_free(wq);
	}
}

/*
 *		event group（NuttXと同じく未実装：blobは通常経路では使わない）
 */
static void *
event_group_create_wrapper(void)
{
	syslog(LOG_ERROR, "wifi_adapter: event_group not supported");
	return(NULL);
}

static void
event_group_delete_wrapper(void *event)
{
	(void) event;
}

static uint32_t
event_group_set_bits_wrapper(void *event, uint32_t bits)
{
	(void) event;
	return(bits);
}

static uint32_t
event_group_clear_bits_wrapper(void *event, uint32_t bits)
{
	(void) event;
	return(bits);
}

static uint32_t
event_group_wait_bits_wrapper(void *event, uint32_t bits_to_wait_for,
							  int clear_on_exit, int wait_for_all_bits,
							  uint32_t block_time_tick)
{
	(void) event; (void) bits_to_wait_for; (void) clear_on_exit;
	(void) wait_for_all_bits; (void) block_time_tick;
	return(0U);
}

/*
 *		タスク
 */
static int32_t
task_create_wrapper(void *task_func, const char *name, uint32_t stack_depth,
					void *param, uint32_t prio, void *task_handle)
{
	return(esp_shim_task_create((void (*)(void *))task_func, name,
								stack_depth, param, prio,
								(void **)task_handle));
}

static int32_t
task_create_pinned_to_core_wrapper(void *task_func, const char *name,
								   uint32_t stack_depth, void *param,
								   uint32_t prio, void *task_handle,
								   uint32_t core_id)
{
	/*
	 *  ★core_id は意図的に捨てる。「単一コアだから」ではない——
	 *  m5-unified／all-in-one は TNUM_PRCID=2 である。下層の
	 *  esp_shim_tsk_activate() は mact_tsk() でコア固定を実装済みなので、
	 *  ここを繋げば blob のタスクは PRC2 へ移る。**繋いではいけない**:
	 *  上流の設計記録は blob のタスクの優先度・親和性を変えると blob が飢えると
	 *  明記している。
	 */
	(void) core_id;
	return(esp_shim_task_create((void (*)(void *))task_func, name,
								stack_depth, param, prio,
								(void **)task_handle));
}

static void
task_delete_wrapper(void *task_handle)
{
	esp_shim_task_delete(task_handle);
}

static void
task_delay_wrapper(uint32_t tick)
{
	esp_shim_task_delay(tick);
}

static int32_t
task_ms_to_tick_wrapper(uint32_t ms)
{
	return((int32_t)ms);	/* tick＝1ms */
}

static void *
task_get_current_task_wrapper(void)
{
	return(esp_shim_task_get_current());
}

static int32_t
task_get_max_priority_wrapper(void)
{
	return(25);		/* FreeRTOS互換の見かけの値（実際の写像はshim内） */
}

/*
 *		メモリ（全系統をshimヒープへ一本化）
 */
static void *
malloc_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void
free_wrapper(void *p)
{
	esp_shim_free(p);
}

static void *
malloc_internal_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void *
realloc_internal_wrapper(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

static void *
calloc_internal_wrapper(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

static void *
zalloc_internal_wrapper(size_t size)
{
	return(esp_shim_calloc(1U, size));
}

static void *
wifi_malloc_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void *
wifi_realloc_wrapper(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

static void *
wifi_calloc_wrapper(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

static void *
wifi_zalloc_wrapper(size_t size)
{
	return(esp_shim_calloc(1U, size));
}

static uint32_t
get_free_heap_size_wrapper(void)
{
	return((uint32_t)esp_shim_heap_free_size());
}

/*
 *		イベント（esp_event_shim.cの最小実装へ）
 */
#if !defined(TOPPERS_ESPIDF_SUPPLY)
/*  $HAL(esp-hal-3rdparty)供給ではesp_event.hのesp_event_postがvoid*版だが、本externと
 *  一致するため従来どおり局所宣言する。$ESPIDF v5.5.4供給はesp_event.hが const void* 版
 *  esp_event_postを宣言する(版差)＝本局所externと衝突するため、その場合はヘッダ宣言を使う。 */
extern int esp_event_post(const char *event_base, int32_t event_id,
						  void *event_data, size_t event_data_size,
						  uint32_t ticks_to_wait);
#endif

static int32_t
event_post_wrapper(const char *event_base, int32_t event_id,
				   void *event_data, size_t event_data_size,
				   uint32_t ticks_to_wait)
{
	return(esp_event_post(event_base, event_id, event_data,
						  event_data_size, ticks_to_wait));
}

#if defined(TOPPERS_ESPIDF_SUPPLY) && defined(TOPPERS_ESP32S3) && !defined(TOPPERS_ESPIDF_WIFI_BLOB)
/*
 *  S3のWi-Fi blob(esp-hal-3rdparty供給, esp/lib/esp32s3)は esp_wifi_sta_get_rsnxe
 *  (RSNXE＝WPA3/PMF管理フレーム保護用、esp_supplicant/src/esp_wifi_driver.h宣言)を
 *  含まない版（LX6のesp32 blobは含む＝S3固有の版差）。$ESPIDF v5.5.4供給の
 *  wpa_supplicant源(rsn_supp/wpa.c, esp_wpa3.c)はCONFIG_IEEE80211W有効時にこれを
 *  無条件で呼び出す。呼び出し側はrsnxe==NULLを想定内で処理する(rsnxe ? ... : 0)ため、
 *  NULL返却のローカル実装で安全に補う。WPA2-PSK到達には不要な機能（PMF/WPA3拡張のみ影響）。
 *  ★2026-07-16: $ESPIDF供給のS3 Wi-Fi blob(esp_wifi/lib/esp32s3)は本シンボルを含むと
 *  実測確認済み(nm)。-DTOPPERS_ESPIDF_WIFI_BLOB(blobもespidf供給に切替えたビルド)では
 *  本stubを無効化し、blob側の実体を使う。
 */
uint8_t *
esp_wifi_sta_get_rsnxe(uint8_t *bssid)
{
	(void)bssid;
	return(NULL);
}
#endif

/*
 *		電源・クロック・PHY
 */
static void
dport_access_stall_other_cpu_start_wrapper(void)
{
	/* シングルコア：不要 */
}

static void
dport_access_stall_other_cpu_end_wrapper(void)
{
	/* シングルコア：不要 */
}

static void
wifi_apb80m_request_wrapper(void)
{
	/* 省電力（auto sleep）非対応：不要 */
}

static void
wifi_apb80m_release_wrapper(void)
{
}

static void
phy_enable_wrapper(void)
{
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1U);
}

static void
phy_disable_wrapper(void)
{
	phy_wifi_enable_set(0U);
	esp_phy_disable(PHY_MODEM_WIFI);
}

static int
read_mac_wrapper(uint8_t *mac, unsigned int type)
{
	return(esp_read_mac(mac, (int)type));
}

static void
wifi_reset_mac_wrapper(void)
{
	/*
	 * 本ポートの periph_module_reset() は __PERIPH_CTRL_ALLOW_LEGACY_API 未定義で
	 * no-op のため、WiFi MAC リセットを直接行う。periph_ll_reset(PERIPH_WIFI_MODULE)
	 * 相当: WIFIMAC_RST(bit2) を SET→CLEAR（リセットパルス）。これが無いと blob の
	 * hal_init が MAC レディを永久に待ってハングする 追記15/16）。
	 * ★チップ差（監査A2）：リセットレジスタ番地がS3と無印ESP32で異なる．
	 *  - ESP32-S3 : SYSCON_WIFI_RST_EN = 0x60026018
	 *  - 無印ESP32: DPORT_CORE_RST_EN_REG = 0x3ff000D0（DPORT_WIFIMAC_RST=bit2）
	 *  bit2・SET→CLEARパルスは同一．無印でS3番地を叩くとMACリセット不成立→
	 *  hal_initがMACレディ待ちでハング＝コンソール停止の直接原因．
	 */
#if defined(TOPPERS_ESP32_LX6)
	volatile uint32_t *wifi_rst = (volatile uint32_t *)0x3ff000D0U; /* DPORT_CORE_RST_EN_REG */
#else
	volatile uint32_t *wifi_rst = (volatile uint32_t *)0x60026018;
#endif
	*wifi_rst |= (1u << 2);
	*wifi_rst &= ~(1u << 2);
	periph_module_reset(PERIPH_WIFI_MODULE); /* 将来 legacy API 有効化時のため残置 */
}

static void
wifi_clock_enable_wrapper(void)
{
	wifi_module_enable();
}

static void
wifi_clock_disable_wrapper(void)
{
	wifi_module_disable();
}

static void
wifi_rtc_enable_iso_wrapper(void)
{
	/* MAC/BBパワーダウン非対応：不要 */
}

static void
wifi_rtc_disable_iso_wrapper(void)
{
}

#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_ESP_WIFI_TARGET_ESP32
static uint32_t
slowclk_cal_get_wrapper(void)
{
	/*
	 *  RTCスローклックの較正値（Q13固定小数点）．
	 *  RTC_CNTL_STORE1に格納された値を返す（ROM/ブート時の設定を流用）．
	 *  未設定（0）の場合は150kHz RCの公称値を返す．
	 */
	uint32_t cal = sil_rew_mem((void *)0x600080B8U);	/* RTC_CNTL_STORE1 */
	if (cal == 0U) {
		cal = (uint32_t)((1000000ULL << 13) / 150000U);
	}
	return(cal);
}
#endif /* !CONFIG_IDF_TARGET_ESP32 */

/*
 *		タイマ
 */
static void
timer_arm_wrapper(void *timer, uint32_t tmout, bool repeat)
{
	esp_shim_timer_arm_us(timer, tmout * 1000U, repeat);
}

static void
timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
	esp_shim_timer_arm_us(ptimer, us, repeat);
}

static void
timer_disarm_wrapper(void *timer)
{
	esp_shim_timer_disarm(timer);
}

static void
timer_done_wrapper(void *ptimer)
{
	esp_shim_timer_done(ptimer);
}

static void
timer_setfn_wrapper(void *ptimer, void *pfunction, void *parg)
{
	esp_shim_timer_setfn(ptimer, (void (*)(void *))pfunction, parg);
}

static int64_t
esp_timer_get_time_wrapper(void)
{
	return(esp_shim_time_us());
}

/*
 *		NVS（NuttXと同じく未実装）
 */
static int
nvs_set_i8_wrapper(uint32_t handle, const char *key, int8_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_i8_wrapper(uint32_t handle, const char *key, int8_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_set_u8_wrapper(uint32_t handle, const char *key, uint8_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_u8_wrapper(uint32_t handle, const char *key, uint8_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_set_u16_wrapper(uint32_t handle, const char *key, uint16_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_u16_wrapper(uint32_t handle, const char *key, uint16_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_open_wrapper(const char *name, unsigned int open_mode,
				 uint32_t *out_handle)
{
	(void) name; (void) open_mode; (void) out_handle;
	return(-1);
}

static void
nvs_close_wrapper(uint32_t handle)
{
	(void) handle;
}

static int
nvs_commit_wrapper(uint32_t handle)
{
	(void) handle;
	return(-1);
}

static int
nvs_set_blob_wrapper(uint32_t handle, const char *key, const void *value,
					 size_t length)
{
	(void) handle; (void) key; (void) value; (void) length;
	return(-1);
}

static int
nvs_get_blob_wrapper(uint32_t handle, const char *key, void *out_value,
					 size_t *length)
{
	(void) handle; (void) key; (void) out_value; (void) length;
	return(-1);
}

static int
nvs_erase_key_wrapper(uint32_t handle, const char *key)
{
	(void) handle; (void) key;
	return(-1);
}

/*
 *		乱数・時刻
 */
static uint32_t
rand_wrapper(void)
{
	return(esp_shim_random());
}

static int
get_random_wrapper(uint8_t *buf, size_t len)
{
	size_t	i;

	for (i = 0U; i < len; i++) {
		buf[i] = (uint8_t)(esp_shim_random() & 0xFFU);
	}
	return(0);
}

static int
get_time_wrapper(void *t)
{
	struct {
		long	tv_sec;
		long	tv_usec;
	} *tv = t;
	int64_t	us = esp_shim_time_us();

	tv->tv_sec = (long)(us / 1000000);
	tv->tv_usec = (long)(us % 1000000);
	return(0);
}

static unsigned long
random_wrapper(void)
{
	return((unsigned long)esp_shim_random());
}

/*
 *		ログ
 */
/*
 *  syslog()の"%s"引数は文字列ポインタをそのままログエントリに積むだけで、
 *  実際の整形（呼出し元のバッファ参照）はlogtaskが後で非同期に行う
 *  （t_syslog.h の syslog_N マクロ／syslog.c の syslog_wri_log 参照）。
 *  ここをスタックローカルbufのまま渡すと、logtaskが整形する時点で
 *  呼出し元のスタックフレームは既に消えており、後続の深い呼出し
 *  （PHY較正等）がそのスタック領域を再利用した場合にダングリング
 *  参照となる（実機JTAG解析で確認：WiFiタスクのスタック内容がカーネル
 *  PCBへ書き込まれる破壊の一因。 追記32）。
 *  logtaskが整形するまで内容が生存する静的ローテーションバッファへ
 *  コピーしてから渡すことで、この寿命不整合を解消する。
 */
extern void esp_shim_syslog_vprintf(const char *format, va_list args);

static void
log_writev_wrapper(unsigned int level, const char *tag, const char *format,
				   va_list args)
{
	(void) level;
	(void) tag;
	esp_shim_syslog_vprintf(format, args);
}

static void
log_write_wrapper(unsigned int level, const char *tag, const char *format, ...)
{
	va_list	args;

	va_start(args, format);
	log_writev_wrapper(level, tag, format, args);
	va_end(args);
}

static uint32_t
log_timestamp_wrapper(void)
{
	return((uint32_t)(esp_shim_time_us() / 1000));
}

/*
 *		coexistence（libcoexist.aへのパススルー）
 */
extern int coex_init(void);
extern void coex_deinit(void);
extern int coex_enable(void);
extern void coex_disable(void);
extern uint32_t coex_status_get(void);
extern void coex_condition_set(uint32_t type, bool dissatisfy);
extern int coex_wifi_request(uint32_t event, uint32_t latency,
							 uint32_t duration);
extern int coex_wifi_release(uint32_t event);
extern int coex_wifi_channel_set(uint8_t primary, uint8_t secondary);
extern int coex_event_duration_get(uint32_t event, uint32_t *duration);
extern int coex_pti_get(uint32_t event, uint8_t *pti);
extern void coex_schm_status_bit_clear(uint32_t type, uint32_t status);
extern void coex_schm_status_bit_set(uint32_t type, uint32_t status);
extern int coex_schm_interval_set(uint32_t interval);
extern uint32_t coex_schm_interval_get(void);
extern uint8_t coex_schm_curr_period_get(void);
extern void *coex_schm_curr_phase_get(void);
extern int coex_schm_process_restart(void);
extern int coex_schm_register_callback(int type, int (*cb)(int));
/*
 *  blob側の実シンボル名は coex_register_start_cb（末尾_callbackでは
 *  ない）．nm確認済み（hal/components/esp_coex/lib/esp32c3/
 *  libcoexist.a）．
 */
extern int coex_register_start_cb(int (*cb)(void));
extern int coex_schm_flexible_period_set(uint8_t period);
extern uint8_t coex_schm_flexible_period_get(void);
extern void *coex_schm_get_phase_by_idx(int idx);

/*
 *		osiテーブル本体
 */
wifi_osi_funcs_t g_wifi_osi_funcs = {
	._version = ESP_WIFI_OS_ADAPTER_VERSION,
	._env_is_chip = env_is_chip_wrapper,
	._set_intr = set_intr_wrapper,
	._clear_intr = clear_intr_wrapper,
	._set_isr = set_isr_wrapper,
	._ints_on = ints_on_wrapper,
	._ints_off = ints_off_wrapper,
	._is_from_isr = is_from_isr_wrapper,
	._spin_lock_create = spin_lock_create_wrapper,
	._spin_lock_delete = spin_lock_delete_wrapper,
	._wifi_int_disable = wifi_int_disable_wrapper,
	._wifi_int_restore = wifi_int_restore_wrapper,
	._task_yield_from_isr = task_yield_from_isr_wrapper,
	._semphr_create = semphr_create_wrapper,
	._semphr_delete = semphr_delete_wrapper,
	._semphr_take = semphr_take_wrapper,
	._semphr_give = semphr_give_wrapper,
	._wifi_thread_semphr_get = esp_shim_thread_semphr_get,
	._mutex_create = mutex_create_wrapper,
	._recursive_mutex_create = recursive_mutex_create_wrapper,
	._mutex_delete = mutex_delete_wrapper,
	._mutex_lock = mutex_lock_wrapper,
	._mutex_unlock = mutex_unlock_wrapper,
	._queue_create = queue_create_wrapper,
	._queue_delete = queue_delete_wrapper,
	._queue_send = queue_send_wrapper,
	._queue_send_from_isr = queue_send_from_isr_wrapper,
	._queue_send_to_back = queue_send_to_back_wrapper,
	._queue_send_to_front = queue_send_to_front_wrapper,
	._queue_recv = queue_recv_wrapper,
	._queue_msg_waiting = queue_msg_waiting_wrapper,
	._event_group_create = event_group_create_wrapper,
	._event_group_delete = event_group_delete_wrapper,
	._event_group_set_bits = event_group_set_bits_wrapper,
	._event_group_clear_bits = event_group_clear_bits_wrapper,
	._event_group_wait_bits = event_group_wait_bits_wrapper,
	._task_create_pinned_to_core = task_create_pinned_to_core_wrapper,
	._task_create = task_create_wrapper,
	._task_delete = task_delete_wrapper,
	._task_delay = task_delay_wrapper,
	._task_ms_to_tick = task_ms_to_tick_wrapper,
	._task_get_current_task = task_get_current_task_wrapper,
	._task_get_max_priority = task_get_max_priority_wrapper,
	._malloc = malloc_wrapper,
	._free = free_wrapper,
	._event_post = event_post_wrapper,
	._get_free_heap_size = get_free_heap_size_wrapper,
	._rand = rand_wrapper,
	._dport_access_stall_other_cpu_start_wrap =
		dport_access_stall_other_cpu_start_wrapper,
	._dport_access_stall_other_cpu_end_wrap =
		dport_access_stall_other_cpu_end_wrapper,
	._wifi_apb80m_request = wifi_apb80m_request_wrapper,
	._wifi_apb80m_release = wifi_apb80m_release_wrapper,
	._phy_disable = phy_disable_wrapper,
	._phy_enable = phy_enable_wrapper,
	._phy_update_country_info = esp_phy_update_country_info,
	._read_mac = read_mac_wrapper,
	._timer_arm = timer_arm_wrapper,
	._timer_disarm = timer_disarm_wrapper,
	._timer_done = timer_done_wrapper,
	._timer_setfn = timer_setfn_wrapper,
	._timer_arm_us = timer_arm_us_wrapper,
	._wifi_reset_mac = wifi_reset_mac_wrapper,
	._wifi_clock_enable = wifi_clock_enable_wrapper,
	._wifi_clock_disable = wifi_clock_disable_wrapper,
	._wifi_rtc_enable_iso = wifi_rtc_enable_iso_wrapper,
	._wifi_rtc_disable_iso = wifi_rtc_disable_iso_wrapper,
	._esp_timer_get_time = esp_timer_get_time_wrapper,
	._nvs_set_i8 = nvs_set_i8_wrapper,
	._nvs_get_i8 = nvs_get_i8_wrapper,
	._nvs_set_u8 = nvs_set_u8_wrapper,
	._nvs_get_u8 = nvs_get_u8_wrapper,
	._nvs_set_u16 = nvs_set_u16_wrapper,
	._nvs_get_u16 = nvs_get_u16_wrapper,
	._nvs_open = nvs_open_wrapper,
	._nvs_close = nvs_close_wrapper,
	._nvs_commit = nvs_commit_wrapper,
	._nvs_set_blob = nvs_set_blob_wrapper,
	._nvs_get_blob = nvs_get_blob_wrapper,
	._nvs_erase_key = nvs_erase_key_wrapper,
	._get_random = get_random_wrapper,
	._get_time = get_time_wrapper,
	._random = random_wrapper,
	/*
	 *  osi_funcs のチップ依存メンバ（wifi_os_adapter.h の #if 分岐に整合）：
	 *   - S3等: _slowclk_cal_get（!CONFIG_IDF_TARGET_ESP32 のとき存在）
	 *   - 無印ESP32/S2: _phy_common_clock_enable/_disable（CONFIG_IDF_TARGET_ESP32
	 *     || ESP32S2 のとき存在。line84-87）
	 *  同一IDF(v5.5, VERSION=0x8)だがこの2メンバだけ排他的にレイアウトが変わる。
	 *  blob(esp32) の期待ABIに合わせるためチップ分岐。S3は従来どおり非回帰。
	 *  esp_phy_common_clock_enable/disable は esp_phy_init.h 宣言・phy_init.c 実体。
	 */
#if !CONFIG_IDF_TARGET_ESP32 && !CONFIG_ESP_WIFI_TARGET_ESP32
	._slowclk_cal_get = slowclk_cal_get_wrapper,
#else
	._phy_common_clock_enable = esp_phy_common_clock_enable,
	._phy_common_clock_disable = esp_phy_common_clock_disable,
#endif
	._log_write = log_write_wrapper,
	._log_writev = log_writev_wrapper,
	._log_timestamp = log_timestamp_wrapper,
	._malloc_internal = malloc_internal_wrapper,
	._realloc_internal = realloc_internal_wrapper,
	._calloc_internal = calloc_internal_wrapper,
	._zalloc_internal = zalloc_internal_wrapper,
	._wifi_malloc = wifi_malloc_wrapper,
	._wifi_realloc = wifi_realloc_wrapper,
	._wifi_calloc = wifi_calloc_wrapper,
	._wifi_zalloc = wifi_zalloc_wrapper,
	._wifi_create_queue = wifi_create_queue_wrapper,
	._wifi_delete_queue = wifi_delete_queue_wrapper,
	._coex_init = coex_init,
	._coex_deinit = coex_deinit,
	._coex_enable = coex_enable,
	._coex_disable = coex_disable,
	._coex_status_get = coex_status_get,
	._coex_condition_set = coex_condition_set,
	._coex_wifi_request = coex_wifi_request,
	._coex_wifi_release = coex_wifi_release,
	._coex_wifi_channel_set = coex_wifi_channel_set,
	._coex_event_duration_get = coex_event_duration_get,
	._coex_pti_get = coex_pti_get,
	._coex_schm_status_bit_clear = coex_schm_status_bit_clear,
	._coex_schm_status_bit_set = coex_schm_status_bit_set,
	._coex_schm_interval_set = coex_schm_interval_set,
	._coex_schm_interval_get = coex_schm_interval_get,
	._coex_schm_curr_period_get = coex_schm_curr_period_get,
	._coex_schm_curr_phase_get = coex_schm_curr_phase_get,
	._coex_schm_process_restart = coex_schm_process_restart,
	._coex_schm_register_cb = coex_schm_register_callback,
	._coex_register_start_cb = coex_register_start_cb,
	._coex_schm_flexible_period_set = coex_schm_flexible_period_set,
	._coex_schm_flexible_period_get = coex_schm_flexible_period_get,
	._coex_schm_get_phase_by_idx = coex_schm_get_phase_by_idx,
	._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};
