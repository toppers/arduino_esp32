/*
 *  TOPPERS/FMP3 Kernel — ESP32-S3 Wi-Fi
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  本ソフトウェアはTOPPERSライセンス下で無保証で提供される．
 */

/*
 *  Coexistence os_adapter（coex_adapter_funcs_t）のFMP3/S3実装
 *
 *  C3先行実装（asp3/target/esp32c3_espidf/esp/esp_coex_adapter.c）を
 *  ベースに、S3のcoex_adapter_funcs_t（_spin_lock_create/_delete・
 *  _int_disable/_enable・_slowclk_cal_getを含む拡張版）へ全フィールドを
 *  実装した。esp_wifi_init前に esp_shim_coex_adapter_register() で登録する
 *  （登録しないとlibcoexist.a内のcoexist_funcsが未初期化のまま、WiFi PMが
 *  NULLメソッドを呼びクラッシュ／INVALID_ARGとなる）。C3固有のregi2c診断は除去。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_shim.h"

/*  ★監査A3：coex_adapter_funcs_t の先頭4フィールド(_spin_lock_create/_delete/
 *  _int_disable/_int_enable)は esp_coexist_adapter.h で #if CONFIG_IDF_TARGET_ESP32
 *  ガードされる．本ファイルは従来 sdkconfig.h を include せず CONFIG_IDF_TARGET_ESP32
 *  が黙って0→無印ESP32でも簡易レイアウトでコンパイル→構造体16B短→blobの_magic
 *  オフセットずれ→esp_coex_adapter_register失敗→dummyフォールバック(coex黙殺)．
 *  sdkconfig.h を明示includeして無印=CONFIG_IDF_TARGET_ESP32=1/S3=未定義を正しく
 *  反映させる（esp32 incflagsの nuttx/esp32/include/sdkconfig.h 由来）． */
#include "sdkconfig.h"

#include "private/esp_coexist_adapter.h"

#define IRAM_ATTR __attribute__((section(".iram1")))

/*  esp_coex_adapter_register／coex_pre_init（libcoexist.a／esp_coex内） */
extern int esp_coex_adapter_register(coex_adapter_funcs_t *funcs);
extern int coex_pre_init(void);

/*  ROM/blob常駐のcoexistメソッドテーブルポインタ（フォールバック用） */
extern void *coexist_funcs;

static intptr_t
coex_noop(void)
{
	return(0);
}

static void *dummy_coexist_table[48];





static void
coex_task_yield_from_isr_wrapper(void)
{
	/* FMP3では割込み出口でディスパッチされるため何もしない */
}

static void *
coex_semphr_create_wrapper(uint32_t max, uint32_t init)
{
	return(esp_shim_sem_create(max, init));
}

static void
coex_semphr_delete_wrapper(void *semphr)
{
	esp_shim_sem_delete(semphr);
}

/*
 *  ［レビュー指摘#3：ISR文脈からの非ブロッキングsemphr_take構造的制約］
 *
 *  esp_shim_sem_take()（esp_shim.c）はtwai_sem→（E_CTX時）pol_semの順で
 *  試すが，両方ともFMP3カーネルの文脈チェックにより非タスク文脈
 *  （ISR）からは常にE_CTXを返す：
 *    - twai_sem：CHECK_DISPATCH_MYSTATE（kernel/task.h経由，check.h）＝
 *      非タスク文脈で無条件E_CTX（tmoutの値に関係無くsemcntすら見ない）。
 *    - pol_sem：CHECK_TSKCTX_UNL（NGKI1513/1514）＝同上，非タスク文脈で
 *      無条件E_CTX。
 *  ref_sem（資源数照会）も同じCHECK_TSKCTX_UNLで，ISRからの読み出しすら
 *  許されない（fmp3_trunk/kernel/semaphore.c，2026-07-16実カーネル
 *  ソース確認）。よって本wrapperはISRから呼ばれると常に0（取得失敗）を
 *  返す。C3先行実装（asp3_esp_idf/asp3/target/esp32c3_espidf/esp/
 *  esp_coex_adapter.c）にも同型の未解決の制約が存在する（本ポート固有の
 *  新規劣化ではない）。
 *
 *  対して_semphr_give_from_isr（下記）はsig_semがCHECK_UNL_MYSTATEで
 *  非タスク文脈を許可するため正しく動作する（本制約はtake方向のみ）。
 *
 *  真に安全な修正には(a)FMP3カーネル本体へISR安全な非ブロッキング
 *  取得APIを追加する（本ポートのスコープ外，kernel/共有資産の変更），
 *  または(b)esp_shim側でセマフォの資源カウントをshim所有のスピンロック
 *  付き影カウンタへ完全に置き換え，カーネルsemaphoreは待ちタスクの
 *  起床通知専用にする再設計（esp_shim_sem_take/giveの全利用箇所に
 *  影響する広範な変更，二重会計・見逃しウェイクアップ等の新規レース
 *  導入リスクが高い）のいずれかが必要で，どちらも本レビュー修正の
 *  バッチ規模を超える。coex機能面の影響は「ISR発の非ブロッキング取得は
 *  常に失敗として扱われる」に限られ（blobが定義済みのfailure pathを通る
 *  だけで，クラッシュ/ハングには至らない），esp/bt coexistenceの
 *  ISR起点調停が最適に働かない可能性がある。BT+WiFi同時使用時の
 *  coexistence不調（優先度制御の劣化）が観測された場合は，本コメントを
 *  出発点に(a)(b)いずれかの設計変更を別作業単位として起票すること。
 *  
 */
static int32_t
coex_semphr_take_from_isr_wrapper(void *semphr, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;
	}
	return(esp_shim_sem_take(semphr, 0U));		/* ポーリング（ISR文脈では常に0、上記コメント参照） */
}

static int32_t
coex_semphr_give_from_isr_wrapper(void *semphr, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;
	}
	return(esp_shim_sem_give(semphr));
}

static int32_t
coex_semphr_take_wrapper(void *semphr, uint32_t block_time_tick)
{
	return(esp_shim_sem_take(semphr, block_time_tick));
}

static int32_t
coex_semphr_give_wrapper(void *semphr)
{
	return(esp_shim_sem_give(semphr));
}

static int
coex_is_in_isr_wrapper(void)
{
	return((int) sns_ctx());
}

static void *
coex_malloc_internal_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void
coex_free_wrapper(void *p)
{
	esp_shim_free(p);
}

static int64_t
coex_esp_timer_get_time_wrapper(void)
{
	return(esp_shim_time_us());
}

static bool
coex_env_is_chip_wrapper(void)
{
	return(true);
}


static void
coex_timer_disarm_wrapper(void *timer)
{
	esp_shim_timer_disarm(timer);
}

static void
coex_timer_done_wrapper(void *ptimer)
{
	esp_shim_timer_done(ptimer);
}

static void
coex_timer_setfn_wrapper(void *ptimer, void *pfunction, void *parg)
{
	esp_shim_timer_setfn(ptimer, (void (*)(void *))pfunction, parg);
}

static void
coex_timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
	esp_shim_timer_arm_us(ptimer, us, repeat);
}

static int
coex_debug_matrix_init_wrapper(int event, int signal, bool rev)
{
	(void) event; (void) signal; (void) rev;
	return(0);
}

static int
coex_xtal_freq_get_wrapper(void)
{
	return(40);		/* ESP32-S3は40MHz固定 */
}

#if CONFIG_IDF_TARGET_ESP32
/*  無印ESP32限定の先頭4フィールド用wrapper（参照 hal/esp_coex/esp32/
 *  esp_coex_adapter.c:221-）．単一コアのためspin_lockは実体不要、int制御は
 *  shim共通の割込み禁止/復元へ委譲． */
static void *coex_spin_lock_create_wrapper(void) { return((void *)1); }
static void coex_spin_lock_delete_wrapper(void *lock) { (void)lock; }
static uint32_t coex_int_disable_wrapper(void *mux) { (void)mux; return(esp_shim_int_disable()); }
static void coex_int_enable_wrapper(void *mux, uint32_t tmp) { (void)mux; esp_shim_int_restore(tmp); }
#endif

/*  coex_adapter_funcs_tはチップ依存レイアウト（ヘッダの#if CONFIG_IDF_TARGET_ESP32）．
 *  無印ESP32は先頭に4フィールドを持つ．slowclk_cal_get(C2限定)は不使用． */
coex_adapter_funcs_t g_coex_adapter_funcs = {
	._version = COEX_ADAPTER_VERSION,
#if CONFIG_IDF_TARGET_ESP32
	._spin_lock_create = coex_spin_lock_create_wrapper,
	._spin_lock_delete = coex_spin_lock_delete_wrapper,
	._int_disable = coex_int_disable_wrapper,
	._int_enable = coex_int_enable_wrapper,
#endif
	._task_yield_from_isr = coex_task_yield_from_isr_wrapper,
	._semphr_create = coex_semphr_create_wrapper,
	._semphr_delete = coex_semphr_delete_wrapper,
	._semphr_take_from_isr = coex_semphr_take_from_isr_wrapper,
	._semphr_give_from_isr = coex_semphr_give_from_isr_wrapper,
	._semphr_take = coex_semphr_take_wrapper,
	._semphr_give = coex_semphr_give_wrapper,
	._is_in_isr = coex_is_in_isr_wrapper,
	._malloc_internal = coex_malloc_internal_wrapper,
	._free = coex_free_wrapper,
	._esp_timer_get_time = coex_esp_timer_get_time_wrapper,
	._env_is_chip = coex_env_is_chip_wrapper,
	._timer_disarm = coex_timer_disarm_wrapper,
	._timer_done = coex_timer_done_wrapper,
	._timer_setfn = coex_timer_setfn_wrapper,
	._timer_arm_us = coex_timer_arm_us_wrapper,
	._debug_matrix_init = coex_debug_matrix_init_wrapper,
	._xtal_freq_get = coex_xtal_freq_get_wrapper,
	._magic = COEX_ADAPTER_MAGIC,
};

/*
 *  coexアダプタの登録（WiFi初期化前に呼ぶ）。esp_coex_adapter_register()で
 *  g_coex_adapter_funcsを登録し、coex_pre_init()でcoexist_funcsを正しい実体
 *  （PTI設定含む）で初期化する。ESP-IDF本体はstartup中に自動で呼ぶが
 *  「#ifndef __NuttX__」でガードされ、Direct Boot/FMP3では通らないため明示。
 */
void
esp_shim_coex_adapter_register(void)
{
	static bool	done = false;
	int		ret;
	int		pre_ret;
	uint_t	i;

	if (done) {
		return;
	}
	done = true;

	ret = esp_coex_adapter_register(&g_coex_adapter_funcs);
	if (ret != 0) {
		syslog(LOG_ERROR, "esp_coex_adapter_register -> %d", (int_t)ret);
	}

	pre_ret = coex_pre_init();
	if (pre_ret != 0) {
		/*  coex_pre_init失敗時のみno-opテーブルへフォールバック（保険） */
		syslog(LOG_ERROR, "coex_pre_init -> %d (fallback no-op)", (int_t)pre_ret);
		for (i = 0U; i < 48U; i++) {
			dummy_coexist_table[i] = (void *)coex_noop;
		}
		coexist_funcs = dummy_coexist_table;
	}
}
