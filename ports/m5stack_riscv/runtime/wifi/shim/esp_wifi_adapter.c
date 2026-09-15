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
/*
 *  BL-G-3 段1（2026-08-14・`.steering/20260814-ring-unification/`）:
 *  Wi-Fi 系統のキュー写像先を **シム所有リング**（`esp_shim_ring_*`）へ移した。
 *  経緯・実測・却下した選択肢は AC.md。写像先に**マクロ分岐を作らない**
 *  （`1765759`＝H4 第2段の教訓。`esp/bt/stub/include/freertos/queue.h` と同じ作法）。
 */
#include "esp_shim_ring.h"

#include "esp_attr.h"
#include "esp_private/wifi_os_adapter.h"
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_ESP_WIFI_TARGET_ESP32
/* 無印ESP32のosi_funcsが要求するPHY共通クロック制御（esp_phy_init.h実体、phy_init.c）。 */
extern void esp_phy_common_clock_enable(void);
extern void esp_phy_common_clock_disable(void);
#endif
#include "esp_private/wifi.h"
#include "private/esp_coexist_adapter.h"
#if defined(TOPPERS_ESP32C6)
/*
 *  ESP32-C6（段4 Task 3・2026-09-14）: modem_clock サブシステム
 *  （SOC_MODEM_CLOCK_IS_INDEPENDENT）。WIFI モジュールの reset / LP クロック
 *  選択は esp-idf の esp_hw_support/modem_clock.c（原本を C6 構成でコンパイル
 *  する。cmake/a1_c6_stage1.cmake）が実体。出典: asp3 esp/c6/wifi/
 *  esp_wifi_adapter.c:30-40 の include 群のうち本ファイルの C6 分岐が使うもの。
 */
#include "soc/periph_defs.h"
#include "esp_private/esp_modem_clock.h"
#include "esp_shim_intr_intmtx.h"
#include "esp_shim_intr_intmtx_lines.h"		/* ESP_SHIM_INTMTX_LINE_MIN/MAX（線 1..15） */
void esp_wifi_adapter_c6_trace(uint32_t code);		/* Task 4 診断（本ファイル下部） */
/*
 *  ESP32-C6（段4 最終レビュー是正・2026-09-15）: 繰り返し出る診断行は
 *  cmake/a1_c6_stage1.cmake の A1_C6_WIFI_DIAG（既定 OFF）の下に置く。
 *  既定で残すのは「1 回きりの検出器」だけ: APM 読み戻し（DMA 拒否の検出、
 *  esp_wifi_adapter_c6_apm_readback）、最初の wifi_clock_enable の入口/出口の
 *  modem クロック読み戻し 1 対（AC-4c の根拠）、set_isr/ena_int の失敗時の行。
 *  DIAG=ON で戻るもの: 2 回目以降の clock_enable と phy_enable の読み戻し、
 *  after_lpclk_select の読み戻し、set_intr/ena_int の毎回の NOTICE。
 */
#if defined(A1_C6_WIFI_DIAG)
#define C6_DIAG_SYSLOG(...)			syslog(__VA_ARGS__)
#define C6_DIAG_CLK_READBACK(tag)	c6_modem_clk_readback(tag)
#else /* A1_C6_WIFI_DIAG */
#define C6_DIAG_SYSLOG(...)			((void) 0)
#define C6_DIAG_CLK_READBACK(tag)	((void) 0)
#endif /* A1_C6_WIFI_DIAG */
#endif

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
#include "esp_wifi_adapter_c5.inc"	/* ESP32-C5 分岐（段4 Task 3。空行の置換 = C6 golden を動かさない形。同 .inc 冒頭参照） */
/*  PERIPH_WIFI_MODULE（esp_private/periph_ctrl.h相当．C3の値） */
#if defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* C5 も enum（段4 Task 3） */
/*
 *  ESP32-C6（段4 Task 4・2026-09-14）: ここで #define しない。C6 では
 *  PERIPH_WIFI_MODULE は soc/periph_defs.h の **enum**（値 33）なので
 *  #ifndef が真になり、S3/LX6 向けの下の #define 24 が enum を隠して
 *  modem_clock_select_lp_clock_source()/modem_clock_module_mac_reset() に
 *  24（C6 の enum では PERIPH_MCPWM0_MODULE）を渡していた。実機の読み戻し
 *  （logs/task4-160-flash2.log）で LP クロック選択（0x600AF00C）と
 *  WIFIPWR_EN（0x600AF018 bit0）が select の直後も 0 のままで発覚
 *  （objdump: 呼び手 `li a0,24`、modem_clock.c 側の比較 `li a5,33`）。
 *  同じ 24 が modem_clock.c の lpclk_src[module - PERIPH_MODEM_MODULE_MIN]
 *  を添字 -9 で書いていた。S3/LX6 は periph_module_reset() が no-op なので
 *  値は使われず、従来どおり下の #define を残す（前処理結果は不変）。
 */
#else /* TOPPERS_ESP32C6 */
#ifndef PERIPH_WIFI_MODULE
#define PERIPH_WIFI_MODULE  24
#endif
#endif /* TOPPERS_ESP32C6 */
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
 *  S3版は0x600C2000を叩いており，無印では非マップ番地への書込み＝esp_wifi_init中の
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
#elif defined(TOPPERS_ESP32C6)
/*
 *  ESP32-C6（段4 Task 3・2026-09-14）。出典: asp3 esp/c6/wifi/esp_wifi_adapter.c
 *  :82-117（set_intr_wrapper / clear_intr_wrapper）。C6 はソースルーティング
 *  （INTMTX 0x60010000 + src*4）と CPU 割込み線制御（PLIC_MX 0x20001000:
 *  ENABLE +0x0 / TYPE +0x4 / PRI(n) +0x10+4n）が別ブロック
 *  （soc/esp32c6/register/soc/reg_base.h、plic_reg.h。fmp3/arch/riscv_gcc/esp32c6/
 *  intmtx_kernel_impl.h と同じ配置）。
 *
 *  asp3 との差: ルーティングは INTMTX の MAP へ直接書かず、
 *  esp_shim_intmtx_route()（esp/shim/esp_shim_intr_intmtx.c）へ委譲する。
 *  それは kernel の esp32c6_intmtx_route() を呼び、prb_int が読む
 *  intmtx_srcmask[] も更新し、線 1..15 の使用中帳簿（esp_intr_alloc と共用）に
 *  記す。優先度（PLIC_MX PRI = 2 固定。cfg の -2 と同じ）と LEVEL 型
 *  （TYPE の bit をクリア。純正 esp_wifi/esp32c6/esp_adapter.c:117-122 の
 *  esprv_int_set_type(INTR_TYPE_LEVEL) 相当）は asp3 と同じくここで書く。
 *  blob の要求 prio は無視する（S3/asp3 と同じ方針）。
 */
#define PLICMX_BASE_ADDR      0x20001000U
#define PLICMX_ENABLE_REG     (PLICMX_BASE_ADDR + 0x000U)
#define PLICMX_TYPE_REG       (PLICMX_BASE_ADDR + 0x004U)
#define PLICMX_PRI_REG(n)     (PLICMX_BASE_ADDR + 0x010U + (n) * 4U)

static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	C6_DIAG_SYSLOG(LOG_NOTICE, "wifi_adapter: set_intr src=%d intno=%d prio=%d",
				   (int_t)intr_source, (int_t)intr_num, (int_t)intr_prio);
	esp_wifi_adapter_c6_trace(0x40U | (intr_source & 0xFU));
	if (esp_shim_intmtx_route((int) intr_source, (int) intr_num) != 0) {
		/*  範囲外か alloc 済みの線。esp_shim_intmtx_route が syslog 済み。
		 *  blob は戻り値を持たないので、ここで止められるのはログだけ。  */
		return;
	}
	sil_wrw_mem((void *)(uintptr_t)PLICMX_PRI_REG(intr_num), 2U);
	sil_wrw_mem((void *)PLICMX_TYPE_REG,
				sil_rew_mem((void *)PLICMX_TYPE_REG) & ~(1UL << intr_num));
	(void) cpu_no;
	(void) intr_prio;
}

static void
clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
	(void) esp_shim_intmtx_unroute((int) intr_source, (int) intr_num);
}
#elif !defined(TOPPERS_ESP32C5) /* ESP32-S3 既存（C5 は esp_wifi_adapter_c5.inc） */
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
	 *  JTAG_DEBUG.md 追記18）。ハンドラ登録(f!=NULL)時のみ許可する。
	 */
#if defined(TOPPERS_ESP32C6)
	/*  ESP32-C6: blob に開放している線は 1..ESP_SHIM_INTMTX_LINE_MAX（15。
	 *  esp_shim_intr_intmtx_lines.h が単一真実源、cfg の CFG_INT/DEF_INH と同じ
	 *  範囲）。S3/LX6 の ESP_SHIM_MAX_WIFI_INTNO（27）は C6 の配線範囲より広い
	 *  （段4 最終レビュー是正・2026-09-15）。範囲外は ena_int せず 1 行残す。  */
	if (f != NULL && n >= ESP_SHIM_INTMTX_LINE_MIN && n <= ESP_SHIM_INTMTX_LINE_MAX) {
		/*  段4 Task 4: AC-4d「線 n が ena_int される」の証跡として戻り値を残す
		 *  （E_OK=0 以外なら cfg の CFG_INT(1..15) と食い違っている）。毎回の
		 *  NOTICE は A1_C6_WIFI_DIAG、失敗（E_OK 以外）は常に出す。  */
		ER	ercd = ena_int((INTNO) n);
		if (ercd != E_OK) {
			syslog(LOG_NOTICE, "wifi_adapter(c6): ena_int intno=%d -> %d (FAILED)",
				   (int_t) n, (int_t) ercd);
		}
		else {
			C6_DIAG_SYSLOG(LOG_NOTICE, "wifi_adapter(c6): ena_int intno=%d -> %d",
						   (int_t) n, (int_t) ercd);
		}
		esp_wifi_adapter_c6_trace(0x50U | ((uint32_t) n & 0xFU));
	}
	else if (f != NULL) {
		syslog(LOG_NOTICE, "wifi_adapter(c6): set_isr intno=%d outside %d..%d (not enabled)",
			   (int_t) n, (int_t) ESP_SHIM_INTMTX_LINE_MIN, (int_t) ESP_SHIM_INTMTX_LINE_MAX);
	}
#elif !defined(TOPPERS_ESP32C5) /* S3/LX6（C5 は esp_wifi_adapter_c5.inc、ここは別名の未使用関数） */
	if (f != NULL && n >= 0 && n <= ESP_SHIM_MAX_WIFI_INTNO) {
		(void) ena_int((INTNO) n);
	}
#endif /* TOPPERS_ESP32C6 */
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
#elif defined(TOPPERS_ESP32C6)
	/*  C6: PLIC_MX の ENABLE ビットマップを RMW（asp3 :125-141 と同じ）。
	 *  kernel の ena_int/dis_int が触るのと同じレジスタ（intmtx_enable_int）
	 *  なので割込み禁止下で行う。  */
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)PLICMX_ENABLE_REG,
				sil_rew_mem((void *)PLICMX_ENABLE_REG) | mask);
	esp_shim_int_restore(lock);
#elif !defined(TOPPERS_ESP32C5)	/* S3（C5 は esp_wifi_adapter_c5.inc） */
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
#elif defined(TOPPERS_ESP32C6)
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)PLICMX_ENABLE_REG,
				sil_rew_mem((void *)PLICMX_ENABLE_REG) & ~mask);
	esp_shim_int_restore(lock);
#elif !defined(TOPPERS_ESP32C5)	/* S3（C5 は esp_wifi_adapter_c5.inc） */
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
 *
 *  ========================================================================
 *  写像先は `esp_shim_ring_*`（シム所有リング）である
 *  ========================================================================
 *  BL-G-3 段1（2026-08-14）。旧写像先は `esp_shim_queue_*`（FMP3 の**データ
 *  キュー**が実体）だった。移した理由は**実測に基づく 2 点**である
 *  （`.steering/20260814-ring-unification/AC.md` §0-4）:
 *
 *   1. **`_queue_send_to_front` が黙って FIFO に落ちていた（潜在バグ）。**
 *      旧実体 `esp/shim/esp_shim.c` は `(void) to_front;` で捨てており、
 *      「先頭へ入れた」と「末尾へ入れた」を呼び手が区別できなかった。
 *      `esp_shim_ring_send()` は `to_front` を**実装している**。
 *   2. **系統の一本化**（`esp_shim_ring_*` は BT 系統が 2026-08-04 から使っている
 *      同じ実体である）。写像先が 1 つ減り、`esp_shim_queue_*` の利用者は 0 になった。
 *
 *  **ヒープは減らなかった（予測が外れたことを隠さない）。** 着手時は
 *  「DTQ 経路は 1 キューにつき `depth*item_size` に加えて空きスロットスタック
 *  （`depth*2`）・保留リング（`depth*2`）・dtqmb を確保するので、`len*item_size`
 *  だけのリングへ移せば純減する」と予測したが、**実機実測は逆で +72 バイト**
 *  だった（`seam-s3-m5-wifi`・同一ワークロード。高水位 76,864 → 76,936 B、
 *  最小空き 5,056 → 4,984 B）。差は 0.1% 未満で実害は無いが、
 *  **「減る」と書くのは誤り**である。原因は特定していない
 *  （`.steering/20260814-ring-unification/RESULT.md` §5）。
 *
 *  **「ISR 受信不能の穴を塞ぐため」ではない。** Wi-Fi の OSA ABI
 *  （`esp-idf/.../wifi_os_adapter.h`）には `_queue_recv_from_isr` という
 *  フィールドが**存在しない**ので、blob は ISR 受信を要求する術を持たない。
 *  実機でも `esp_shim_isr_ctx_ectx_q_recv` は 2 ワークロードとも **0**
 *  だった（`.steering/20260814-deltsk-measure/logs/run{1,2}-*.txt`）。
 *  ⇒ 本移行は**欠陥修正 1 件（上の 1.）と一本化**であって、
 *    「ISR 受信を直した」と書くのは誇張である。
 *
 *  **写像先にマクロ分岐を作らない**（`1765759`＝H4 第2段の教訓）。
 *  分岐にすると `-D` を渡し忘れた翻訳単位だけが旧実体を見て、
 *  リンクは通り、欠陥が黙って復活する。
 *
 *  保留起床の flush を `wifi_int_restore_wrapper`（`IRAM_ATTR`）へは
 *  **足していない**。`esp_shim_ring_send()` 入口の機会的 flush と 50ms の
 *  安全網で必ず流れる一方、`esp_shim_ring_flush_wakes()` は `IRAM_ATTR`
 *  ではなく、cache 無効文脈から呼ばれ得るホットパスへ足すのは新しい危険
 *  である（AC.md §2）。
 */
static void *
queue_create_wrapper(uint32_t queue_len, uint32_t item_size)
{
	return(esp_shim_ring_create(queue_len, item_size));
}

static void
queue_delete_wrapper(void *queue)
{
	esp_shim_ring_delete(queue);
}

static int32_t
queue_send_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_ring_send(queue, item, block_time_tick, 0));
}

static int32_t IRAM_ATTR
queue_send_from_isr_wrapper(void *queue, void *item, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;	/* higher priority task woken：ASP3では不要 */
	}
	return(esp_shim_ring_send_from_isr(queue, item));
}

static int32_t
queue_send_to_back_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_ring_send(queue, item, block_time_tick, 0));
}

static int32_t
queue_send_to_front_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_ring_send(queue, item, block_time_tick, 1));
}

static int32_t
queue_recv_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_ring_recv(queue, item, block_time_tick));
}

static uint32_t
queue_msg_waiting_wrapper(void *queue)
{
	return(esp_shim_ring_msg_waiting(queue));
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
	wq->handle = esp_shim_ring_create((uint32_t)queue_len,
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
		esp_shim_ring_delete(wq->handle);
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
	 *  2026-08-04（段4）: **`core_id` を捨てるのをやめた。**
	 *
	 *  従来はここで `(void) core_id;` と書いて「シングルコア」と註を付けていた。
	 *  ⇒ 「blob がコア固定を要求したかどうか」が**どこにも残らなかった**。
	 *  いまは値をそのまま `esp_shim_task_create_pinned()` へ渡し、
	 *    `esp/shim/esp_shim_tsk.c` の `shim_tsk_prcid()` が
	 *    **方針として** `mact_tsk(tskid, prcid)` の `prcid` を決める:
	 *      ・`core_id < TNUM_PRCID`        → `prcid = core_id + 1`
	 *      ・`ESP_SHIM_TASK_NO_AFFINITY`   → PRC1（＝現行と同じ場所・非退行）
	 *      ・存在しないコアを名指し        → PRC1 へ落として**数える＋診断**
	 *  現行 10 preset で本 TU をリンクする構成はすべて `TNUM_PRCID == 1` なので、
	 *    **振る舞いは変わらない。変わったのは「見た上で無視している」ことである。**
	 *    カウンタ `esp_shim_tsk_core_clamped` が 0 でなければ、
	 *    それが「blob は実際にコア固定を要求している」実行時の証拠になる。
	 *  記録: `.steering/20260804-dcre-stage4-tsk/`、DESIGN-MEMO §3-2(a)(b)(f)。
	 */
	return(esp_shim_task_create_pinned((void (*)(void *))task_func, name,
									   stack_depth, param, prio,
									   (void **)task_handle, core_id));
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
 *  2026-07-16: $ESPIDF供給のS3 Wi-Fi blob(esp_wifi/lib/esp32s3)は本シンボルを含むと
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

#if defined(TOPPERS_ESP32C6)
#if defined(A1_C6_BOOT_TRACE)
/*
 *  Task 4: 真cold で無音になった経路を特定するための **リセットを跨ぐ足跡**
 *  （診断。asp3 の RTC RAM 計数と同じ発想だが、番地は HP SRAM の
 *  0x40868000 = アプリ .bss 終端（0x40849640）と bootloader の dram_seg
 *  （0x4086B910-）/iram_loader_seg（0x4086E610-）のどちらからも外れた領域。
 *  bootloader.ld:10-23 の download-mode 共有バッファ 0x4086AD08- より手前）。
 *  HP システムリセット（esptool の hard_reset = rst:0x15）では SRAM の内容が
 *  残るので、cold で黙って止まった run の足跡を次の warm run の先頭で読める。
 *  電源断（真cold）では消える（magic 不一致 = 前 run 無し）。
 *  [0]=magic [1]=書込み総数 [2..15]=直近 14 個のコード（リング）。
 */
#define C6_TRACE_BASE	0x40868000U
#define C6_TRACE_MAGIC	0xC6B00701U
#define C6_TRACE_SLOTS	14U

void esp_wifi_adapter_c6_trace(uint32_t code);
void
esp_wifi_adapter_c6_trace(uint32_t code)
{
	volatile uint32_t	*t = (volatile uint32_t *) C6_TRACE_BASE;
	uint32_t			n;

	if (t[0] != C6_TRACE_MAGIC) {
		t[0] = C6_TRACE_MAGIC;
		t[1] = 0U;
	}
	n = t[1];
	t[2U + (n % C6_TRACE_SLOTS)] = code;
	t[1] = n + 1U;
}

/*
 *  Task 4（真cold の無音の切り分け）: USB-Serial/JTAG と割込み経路の生レジスタを
 *  同じリセット越し領域（+0x40）へ写す。cold run が完走した時点（connect-timeout）
 *  で呼び、次の warm run の先頭で読む。番地の出典:
 *    USB_SERIAL_JTAG EP1_CONF 0x6000F004（bit1 SERIAL_IN_EP_DATA_FREE）、
 *    INT_RAW 0x6000F008、INT_ST 0x6000F00C、INT_ENA 0x6000F010、CONF0 0x6000F018
 *    （soc/esp32c6/register/soc/usb_serial_jtag_reg.h）
 *    PLIC_MX ENABLE 0x20001000 / TYPE 0x20001004 / PRI(17) 0x20001054
 *    INTMTX map of source 48 (USB_SERIAL_JTAG) = 0x60010000 + 48*4
 *    mstatus / mie（CSR）
 */
#define C6_TRACE_REGS_BASE	(C6_TRACE_BASE + 0x40U)
#define C6_TRACE_REGS_MAGIC	0xC6B00702U

void esp_wifi_adapter_c6_trace_regs(void);
void
esp_wifi_adapter_c6_trace_regs(void)
{
	volatile uint32_t	*r = (volatile uint32_t *) C6_TRACE_REGS_BASE;
	uint32_t			mstatus, mie;

	__asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
	__asm__ volatile("csrr %0, mie" : "=r"(mie));
	r[1]  = sil_rew_mem((void *) 0x6000F004U);	/* EP1_CONF */
	r[2]  = sil_rew_mem((void *) 0x6000F008U);	/* INT_RAW */
	r[3]  = sil_rew_mem((void *) 0x6000F00CU);	/* INT_ST */
	r[4]  = sil_rew_mem((void *) 0x6000F010U);	/* INT_ENA */
	r[5]  = sil_rew_mem((void *) 0x6000F018U);	/* CONF0 */
	r[6]  = sil_rew_mem((void *) 0x20001000U);	/* PLIC ENABLE */
	r[7]  = sil_rew_mem((void *) 0x20001004U);	/* PLIC TYPE */
	r[8]  = sil_rew_mem((void *) 0x20001054U);	/* PLIC PRI(17) */
	r[9]  = sil_rew_mem((void *) (0x60010000U + 48U * 4U));	/* INTMTX map src48 */
	r[10] = mstatus;
	r[11] = mie;
	r[0]  = C6_TRACE_REGS_MAGIC;
}

/*
 *  Task 4: 時刻つき USJ スナップショット（+0x80、最大 10 個 x 6 語）。
 *  [0]=hrt(us) [1]=EP1_CONF [2]=INT_RAW [3]=INT_ST [4]=INT_ENA [5]=PLIC ENABLE。
 *  cold run のどの時点で DATA_FREE が 0 に落ちたか（USB 列挙との前後）を見る。
 */
#define C6_USJ_SNAP_BASE	(C6_TRACE_BASE + 0x80U)
#define C6_USJ_SNAP_MAX		10U
#define C6_USJ_SNAP_MAGIC	0xC6B00703U

void esp_wifi_adapter_c6_usj_snap(uint32_t idx);
void
esp_wifi_adapter_c6_usj_snap(uint32_t idx)
{
	volatile uint32_t	*b = (volatile uint32_t *) C6_USJ_SNAP_BASE;
	volatile uint32_t	*e;

	if (idx >= C6_USJ_SNAP_MAX) {
		return;
	}
	if (b[0] != C6_USJ_SNAP_MAGIC) {
		uint32_t	i;
		for (i = 0; i < 1U + C6_USJ_SNAP_MAX * 6U; i++) {
			b[i] = 0U;
		}
		b[0] = C6_USJ_SNAP_MAGIC;
	}
	e = &b[1U + idx * 6U];
	e[0] = (uint32_t) esp_shim_time_us();
	e[1] = sil_rew_mem((void *) 0x6000F004U);
	e[2] = sil_rew_mem((void *) 0x6000F008U);
	e[3] = sil_rew_mem((void *) 0x6000F00CU);
	e[4] = sil_rew_mem((void *) 0x6000F010U);
	e[5] = sil_rew_mem((void *) 0x20001000U);
}

static void
c6_usj_snap_dump_prev(void)
{
	volatile uint32_t	*b = (volatile uint32_t *) C6_USJ_SNAP_BASE;
	uint32_t			i;

	if (b[0] != C6_USJ_SNAP_MAGIC) {
		syslog(LOG_NOTICE, "wifi_adapter(c6): prev-run usj snaps: none");
		return;
	}
	for (i = 0; i < C6_USJ_SNAP_MAX; i++) {
		volatile uint32_t	*e = &b[1U + i * 6U];
		if (e[0] == 0U) {
			continue;
		}
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): prev-run usj snap[%u] t=%uus ep1_conf=%08x int_raw=%08x"
			   " int_st=%08x",
			   (uint_t) i, (uint_t) e[0], (uint_t) e[1], (uint_t) e[2], (uint_t) e[3]);
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): prev-run usj snap[%u] int_ena=%08x plic_enable=%08x",
			   (uint_t) i, (uint_t) e[4], (uint_t) e[5]);
	}
	b[0] = 0U;
}

void esp_wifi_adapter_c6_trace_dump_prev(void);
void
esp_wifi_adapter_c6_trace_dump_prev(void)
{
	volatile uint32_t	*t = (volatile uint32_t *) C6_TRACE_BASE;
	volatile uint32_t	*r = (volatile uint32_t *) C6_TRACE_REGS_BASE;
	uint32_t			i;

	c6_usj_snap_dump_prev();
	if (r[0] == C6_TRACE_REGS_MAGIC) {
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): prev-run usj ep1_conf=%08x int_raw=%08x int_st=%08x"
			   " int_ena=%08x conf0=%08x",
			   (uint_t) r[1], (uint_t) r[2], (uint_t) r[3], (uint_t) r[4], (uint_t) r[5]);
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): prev-run plic enable=%08x type=%08x pri17=%08x"
			   " intmtx48=%08x",
			   (uint_t) r[6], (uint_t) r[7], (uint_t) r[8], (uint_t) r[9]);
		syslog(LOG_NOTICE, "wifi_adapter(c6): prev-run mstatus=%08x mie=%08x",
			   (uint_t) r[10], (uint_t) r[11]);
	}
	else {
		syslog(LOG_NOTICE, "wifi_adapter(c6): prev-run usj regs: none (magic=0x%08x)",
			   (uint_t) r[0]);
	}
	r[0] = 0U;
	if (t[0] != C6_TRACE_MAGIC) {
		syslog(LOG_NOTICE, "wifi_adapter(c6): prev-run trace: none (magic=0x%08x)",
			   (uint_t) t[0]);
	}
	else {
		syslog(LOG_NOTICE, "wifi_adapter(c6): prev-run trace: count=%u", (uint_t) t[1]);
		for (i = 0; i < C6_TRACE_SLOTS; i += 7U) {
			syslog(LOG_NOTICE,
				   "wifi_adapter(c6): prev-run trace[%u..]: %08x %08x %08x %08x",
				   (uint_t) i, (uint_t) t[2U + i], (uint_t) t[3U + i],
				   (uint_t) t[4U + i], (uint_t) t[5U + i]);
			syslog(LOG_NOTICE,
				   "wifi_adapter(c6): prev-run trace[%u..]: %08x %08x %08x",
				   (uint_t) (i + 4U), (uint_t) t[6U + i], (uint_t) t[7U + i],
				   (uint_t) t[8U + i]);
		}
	}
	/*  今回の run 用に初期化する。  */
	t[0] = C6_TRACE_MAGIC;
	t[1] = 0U;
	for (i = 0; i < C6_TRACE_SLOTS; i++) {
		t[2U + i] = 0U;
	}
}

#else /* A1_C6_BOOT_TRACE */
/*  既定（A1_C6_BOOT_TRACE 未定義）: 足跡・レジスタ写しは何もしない。  */
void esp_wifi_adapter_c6_trace(uint32_t code);
void
esp_wifi_adapter_c6_trace(uint32_t code)
{
	(void) code;
}
void esp_wifi_adapter_c6_trace_regs(void);
void
esp_wifi_adapter_c6_trace_regs(void)
{
}
void esp_wifi_adapter_c6_usj_snap(uint32_t idx);
void
esp_wifi_adapter_c6_usj_snap(uint32_t idx)
{
	(void) idx;
}
void esp_wifi_adapter_c6_trace_dump_prev(void);
void
esp_wifi_adapter_c6_trace_dump_prev(void)
{
}
#endif /* A1_C6_BOOT_TRACE */

/*
 *  ESP32-C6（段4 Task 3・2026-09-14）: modem クロック系レジスタの読み戻し。
 *  asp3 が Direct Boot で肩代わりしていた ICG init / regi2c master clock /
 *  LPCON |= 0x7（asp3-c6-inventory.md 2 節の (1)(3)(5)(a)）は、seam では
 *  bootloader が設定済みのはずで、**入れる前に現状を読む**（同 5 節、AC-4c）。
 *  ここでは値を syslog に出すだけで、これらのレジスタへは書かない
 *  （書くのは Task 4 が読み戻しを見てから決める）。番地の出典:
 *    MODEM_LPCON_CLK_CONF_REG          0x600AF018 (modem_lpcon_reg.h:140)  bits[2:0]
 *    MODEM_LPCON_WIFI_LP_CLK_CONF_REG  0x600AF00C (modem_lpcon_reg.h:92)
 *    MODEM_LPCON_CLK_CONF_POWER_ST_REG 0x600AF020 (modem_lpcon_reg.h:228)
 *    MODEM_SYSCON_CLK_CONF_POWER_ST_REG 0x600A980C (modem_syscon_reg.h:152)
 *    PMU_HP_ACTIVE_ICG_MODEM_REG       0x600B000C (pmu_reg.h:95) bits[31:30]
 */
/*
 *  Task 4: 真cold（COLD=1）の採取は USB-Serial/JTAG の列挙より前に出た行を
 *  取りこぼす（段3 E2）。初期化時の読み戻し（起動後 200ms 前後）を cold でも
 *  読めるように、最初の C6_CLK_SNAP_MAX 回分を保持し、アプリが後で
 *  esp_wifi_adapter_c6_dump_snapshots() で再表示する（値は読み戻し時点のもの。
 *  再表示時に読み直すのではない）。
 */
#define C6_CLK_SNAP_MAX		10

typedef struct {
	const char	*tag;
	uint32_t	lpcon_clk_conf;
	uint32_t	wifi_lp_clk;
	uint32_t	lpcon_pwr_st;
	uint32_t	syscon_pwr_st;
	uint32_t	pmu_icg_modem;
} C6_CLK_SNAP;

static C6_CLK_SNAP	c6_clk_snap[C6_CLK_SNAP_MAX];
static uint32_t		c6_clk_snap_n;		/* 呼ばれた総数（保持は先頭 MAX 件） */

static void
c6_modem_clk_readback(const char *tag)
{
	C6_CLK_SNAP	v;

	esp_wifi_adapter_c6_trace(0x20U + c6_clk_snap_n);	/* 0x20.. = 読み戻し n 回目 */
	v.tag            = tag;
	v.lpcon_clk_conf = sil_rew_mem((void *) 0x600AF018U);
	v.wifi_lp_clk    = sil_rew_mem((void *) 0x600AF00CU);
	v.lpcon_pwr_st   = sil_rew_mem((void *) 0x600AF020U);
	v.syscon_pwr_st  = sil_rew_mem((void *) 0x600A980CU);
	v.pmu_icg_modem  = sil_rew_mem((void *) 0x600B000CU);
	if (c6_clk_snap_n < C6_CLK_SNAP_MAX) {
		c6_clk_snap[c6_clk_snap_n] = v;
	}
	c6_clk_snap_n++;
	/*  syslog は format + 5 引数まで（TNUM_LOGPAR=6、vasyslog.c）。tag + 5 値
	 *  では最後が落ちるので 2 行に分ける。  */
	syslog(LOG_NOTICE,
		   "wifi_adapter(c6): %s lpcon_clk_conf=0x%08x wifi_lp_clk=0x%08x"
		   " lpcon_pwr_st=0x%08x",
		   tag, (uint_t) v.lpcon_clk_conf, (uint_t) v.wifi_lp_clk,
		   (uint_t) v.lpcon_pwr_st);
	syslog(LOG_NOTICE,
		   "wifi_adapter(c6): %s syscon_pwr_st=0x%08x pmu_icg_modem=0x%08x",
		   tag, (uint_t) v.syscon_pwr_st, (uint_t) v.pmu_icg_modem);
}

void esp_wifi_adapter_c6_dump_snapshots(void);
void
esp_wifi_adapter_c6_dump_snapshots(void)
{
	uint32_t	i;
	uint32_t	n = (c6_clk_snap_n < C6_CLK_SNAP_MAX) ? c6_clk_snap_n : C6_CLK_SNAP_MAX;

	syslog(LOG_NOTICE, "wifi_adapter(c6): snapshots kept=%u total=%u",
		   (uint_t) n, (uint_t) c6_clk_snap_n);
	for (i = 0; i < n; i++) {
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): snap[%u] %s lpcon_clk_conf=0x%08x"
			   " wifi_lp_clk=0x%08x pmu_icg_modem=0x%08x",
			   (uint_t) i, c6_clk_snap[i].tag, (uint_t) c6_clk_snap[i].lpcon_clk_conf,
			   (uint_t) c6_clk_snap[i].wifi_lp_clk, (uint_t) c6_clk_snap[i].pmu_icg_modem);
		syslog(LOG_NOTICE,
			   "wifi_adapter(c6): snap[%u] %s lpcon_pwr_st=0x%08x syscon_pwr_st=0x%08x",
			   (uint_t) i, c6_clk_snap[i].tag, (uint_t) c6_clk_snap[i].lpcon_pwr_st,
			   (uint_t) c6_clk_snap[i].syscon_pwr_st);
	}
}

/*
 *  ESP32-C6（段4 Task 5・2026-09-14）: APM（Access Permission Management）/
 *  TEE の読み戻しと、経路フィルタの解除。
 *
 *  なぜ要るか: scan が 0 AP（logs/task5-160-flash{1,2}.log。所要時間は S3 と
 *  同じ 2.4 秒、線 1 の割込みは毎回同じ回数 = RX が 1 つも来ない「deaf RX」）。
 *  asp3 の C6 では同じ症状の原因が APM だった（asp3/target/esp32c6_espidf/
 *  target_kernel_impl.c:99-160、実施87/88: HP_APM M1 の例外ラッチに
 *  master_id=4(MODEM)/mode=3(REE2)/INFO1=HP SRAM 番地が残り、解除で 0 AP ->
 *  14-23 AP）。esp-idf ではアプリ側の起動コード
 *  bootloader_support/src/bootloader_mem.c:19-37 bootloader_init_mem() が
 *  `#if !defined(BOOTLOADER_BUILD)` の中で apm_hal_enable_ctrl_filter_all(false)
 *  を呼ぶ = **bootloader は解除せず、アプリの cpu_start が解除する**。seam は
 *  その cpu_start をリンクしないので、C6 は POR 既定（フィルタ有効、HP CPU
 *  以外の master は REE2 = アクセス拒否）のまま modem の DMA が RAM に届かない。
 *  CPU は TEE なので CPU 例外は出ない（AC-4b の mcause=0 と矛盾しない）。
 *
 *  番地（esp-idf/components/soc/esp32c6/register/soc/{hp_apm,lp_apm0,lp_apm,
 *  tee}_reg.h、reg_base.h）:
 *    HP_APM_FUNC_CTRL_REG   0x600990C4   LP_APM0_FUNC_CTRL_REG 0x600998C4
 *    LP_APM_FUNC_CTRL_REG   0x600B38C4   TEE_M4_MODE_CTRL_REG  0x60098010（M4 = MODEM）
 *    HP_APM_Mn_STATUS_REG   0x600990C8 + 0x10*n（n=0..3）、+4 STATUS_CLR、
 *    +8 EXCEPTION_INFO0（region[15:0] mode[17:16] id[22:18]）、+0xc INFO1（番地）
 *  解除は IDF と同じ「3 コントローラの FUNC_CTRL を 0」だけ（hal/apm_hal.c:185-195
 *  apm_hal_enable_ctrl_filter_all(false)）。asp3 が加えて行う TEE 昇格
 *  （全 master を mode 0）は C6 の IDF アプリでは行わない
 *  （SOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL は C6 に無い）ので持ち込まない。
 *  cmake/a1_c6_stage1.cmake の A1_C6_APM_UNBLOCK（既定 ON）で -DA1_C6_APM_UNBLOCK=1、
 *  OFF が「外した対照」。
 */
#define C6_HP_APM_FUNC_CTRL		0x600990C4U
#define C6_LP_APM0_FUNC_CTRL	0x600998C4U
#define C6_LP_APM_FUNC_CTRL		0x600B38C4U
#define C6_TEE_M4_MODE_CTRL		0x60098010U
#define C6_HP_APM_M0_STATUS		0x600990C8U

void esp_wifi_adapter_c6_apm_readback(const char *tag);
void
esp_wifi_adapter_c6_apm_readback(const char *tag)
{
	uint32_t	n;

	syslog(LOG_NOTICE,
		   "wifi_adapter(c6): apm %s hp_func_ctrl=0x%08x lp_apm0_func_ctrl=0x%08x"
		   " lp_apm_func_ctrl=0x%08x tee_m4=0x%08x",
		   tag, (uint_t) sil_rew_mem((void *) C6_HP_APM_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C6_LP_APM0_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C6_LP_APM_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C6_TEE_M4_MODE_CTRL));
	for (n = 0U; n < 4U; n++) {
		uint32_t	base = C6_HP_APM_M0_STATUS + 0x10U * n;
		uint32_t	st   = sil_rew_mem((void *) base);

		/*  ラッチが立っている経路だけ出す（4 行 x 毎回は多い）。全 0 なら
		 *  「none」を 1 行出して沈黙と区別する。  */
		if (st != 0U) {
			syslog(LOG_NOTICE,
				   "wifi_adapter(c6): apm %s hp_apm m%u status=0x%08x info0=0x%08x"
				   " info1=0x%08x",
				   tag, (uint_t) n, (uint_t) st, (uint_t) sil_rew_mem((void *) (base + 8U)),
				   (uint_t) sil_rew_mem((void *) (base + 0xCU)));
		}
	}
	if (sil_rew_mem((void *) C6_HP_APM_M0_STATUS) == 0U
		&& sil_rew_mem((void *) (C6_HP_APM_M0_STATUS + 0x10U)) == 0U
		&& sil_rew_mem((void *) (C6_HP_APM_M0_STATUS + 0x20U)) == 0U
		&& sil_rew_mem((void *) (C6_HP_APM_M0_STATUS + 0x30U)) == 0U) {
		syslog(LOG_NOTICE, "wifi_adapter(c6): apm %s hp_apm exception latch: none", tag);
	}
}

#if defined(A1_C6_APM_UNBLOCK)
static void
c6_apm_unblock(void)
{
	uint32_t	n;

	esp_wifi_adapter_c6_apm_readback("before-unblock");
	sil_wrw_mem((void *) C6_HP_APM_FUNC_CTRL, 0U);
	sil_wrw_mem((void *) C6_LP_APM0_FUNC_CTRL, 0U);
	sil_wrw_mem((void *) C6_LP_APM_FUNC_CTRL, 0U);
	/*  例外ラッチをクリア（以降に新しい違反が出たら見えるように）。  */
	for (n = 0U; n < 4U; n++) {
		sil_wrw_mem((void *) (C6_HP_APM_M0_STATUS + 0x10U * n + 4U), 1U);
	}
	esp_wifi_adapter_c6_apm_readback("after-unblock");
}
#endif /* A1_C6_APM_UNBLOCK */

static void
phy_enable_wrapper(void)
{
	/*
	 *  asp3 :570-598 のうち (d) esp_phy_enable + phy_wifi_enable_set だけを
	 *  持ち込む（S3 と共通の中身）。(a) LPCON |= 0x7 は measure（Task 4）、
	 *  (b) 0x600af008/0x600af048 = 0x314 は asp3 自身が「検証のため」と書く
	 *  投機的書込みで skip、(c) RTC RAM 計数は診断で skip
	 *  （asp3-c6-inventory.md 2 節）。
	 */
	C6_DIAG_CLK_READBACK("phy_enable entry");
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1U);
	C6_DIAG_CLK_READBACK("phy_enable exit");
}
#elif !defined(TOPPERS_ESP32C5)	/* S3/LX6（C5 は esp_wifi_adapter_c5.inc） */
static void
phy_enable_wrapper(void)
{
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1U);
}
#endif /* TOPPERS_ESP32C6 */

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
	 * hal_init が MAC レディを永久に待ってハングする（JTAG_DEBUG.md 追記15/16）。
	 * チップ差（監査A2）：リセットレジスタ番地がS3と無印ESP32で異なる．
	 *  - ESP32-S3 : SYSCON_WIFI_RST_EN = 0x60026018
	 *  - 無印ESP32: DPORT_CORE_RST_EN_REG = 0x3ff000D0（DPORT_WIFIMAC_RST=bit2）
	 *  bit2・SET→CLEARパルスは同一．無印でS3番地を叩くとMACリセット不成立→
	 *  hal_initがMACレディ待ちでハング＝コンソール停止の直接原因．
	 */
#if defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* C5 も modem_clock_module_mac_reset（段4 Task 3） */
	/*
	 *  ESP32-C6（段4 Task 3・2026-09-14）。出典: asp3 esp/c6/wifi/esp_wifi_adapter.c
	 *  :613-616（wifi_reset_mac_wrapper）と :42-52 の注記。純正
	 *  esp_wifi/esp32c6/esp_adapter.c:304-307 と同じ modem_clock_module_mac_reset。
	 *  **periph_module_reset(PERIPH_WIFI_MODULE) を C6 で呼んではいけない**:
	 *  C6 の hal/clk_gate_ll.h の表は TIMG0/1・UHCI0・SYSTIMER の 4 種しか無く、
	 *  PERIPH_WIFI_MODULE は範囲外参照で GCC が到達不能と判断して ebreak を
	 *  生成する（asp3 の実機 JTAG 記録）。S3/LX6 の SYSCON/DPORT 直叩きも
	 *  C6 には存在しない番地なので行わない。
	 */
	modem_clock_module_mac_reset(PERIPH_WIFI_MODULE);
#else
#if defined(TOPPERS_ESP32_LX6)
	volatile uint32_t *wifi_rst = (volatile uint32_t *)0x3ff000D0U; /* DPORT_CORE_RST_EN_REG */
#else
	volatile uint32_t *wifi_rst = (volatile uint32_t *)0x60026018;
#endif
	*wifi_rst |= (1u << 2);
	*wifi_rst &= ~(1u << 2);
	periph_module_reset(PERIPH_WIFI_MODULE); /* 将来 legacy API 有効化時のため残置 */
#endif /* TOPPERS_ESP32C6 */
}

#if defined(TOPPERS_ESP32C6)
static void
wifi_clock_enable_wrapper(void)
{
	static bool_t	lpclk_selected = false;
	bool_t			first = !lpclk_selected;

	/*
	 *  ESP32-C6（段4 Task 3・2026-09-14）。出典: asp3 esp/c6/wifi/esp_wifi_adapter.c
	 *  :686-747（wifi_clock_enable_wrapper）。asp3 の 5 段のうち持ち込むのは
	 *    (2) 初回のみ modem_clock_deselect_all_module_lp_clock_source() +
	 *        modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, RC_SLOW, 0)
	 *        -- 純正 esp_system/port/soc/esp32c6/clk.c の esp_perip_clk_init()
	 *        相当。seam でもこれを呼ぶ者は居ない（app 側の起動コードは
	 *        リンクしない）ので port する。WIFIPWR クロック（LPCON CLK_CONF
	 *        bit0）はここで立つ（modem_clock.c:483）。
	 *    (4) wifi_module_enable()（S3 と共通）
	 *  の 2 つ。(1) esp_shim_modem_icg_init（PMU ICG code=2 + ICG bitmap）、
	 *  (3) regi2c master clock enable、(5) LPCON |= 0x7 は asp3 が Direct Boot
	 *  （bootloader 無し）で肩代わりしていたもので、seam では bootloader
	 *  （rtc_clk_init / bootloader_hardware_init）が行うはず。**読み戻して
	 *  から決める**（asp3-c6-inventory.md 5 節、AC-4c）ので、ここでは
	 *  入口と出口で値を syslog に出すだけにする（Task 4 で判定）。
	 */
	/*  段4 最終レビュー是正（2026-09-15）: 読み戻しは最初の呼出しの入口/出口の
	 *  1 対だけ既定で出す（AC-4c の根拠）。2 回目以降と after_lpclk_select は
	 *  A1_C6_WIFI_DIAG。  */
	if (first) {
		c6_modem_clk_readback("clock_enable entry");
	}
	else {
		C6_DIAG_CLK_READBACK("clock_enable entry");
	}
	if (!lpclk_selected) {
#if defined(A1_C6_APM_UNBLOCK)
		/*  段4 Task 5: modem を最初に有効化する前に APM の経路フィルタを解除
		 *  （上の c6_apm_unblock のコメント）。既定 ON、OFF が対照。  */
		c6_apm_unblock();
#endif
		modem_clock_deselect_all_module_lp_clock_source();
		modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,
										   MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0U);
		lpclk_selected = true;
		/*  Task 4: 初回焼込みで exit の lpcon_clk_conf bit0（WIFIPWR_EN）と
		 *  wifi_lp_clk（0x600AF00C）が 0 のままだったので、select 直後にも
		 *  読んで「select が書けていない」か「wifi_module_enable が消す」かを
		 *  分ける（README 4 節）。  */
		C6_DIAG_CLK_READBACK("clock_enable after_lpclk_select");
	}
	wifi_module_enable();
	if (first) {
		c6_modem_clk_readback("clock_enable exit");
	}
	else {
		C6_DIAG_CLK_READBACK("clock_enable exit");
	}
}
#elif !defined(TOPPERS_ESP32C5)	/* S3/LX6（C5 は esp_wifi_adapter_c5.inc） */
static void
wifi_clock_enable_wrapper(void)
{
	wifi_module_enable();
}
#endif /* TOPPERS_ESP32C6 */

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
#if defined(TOPPERS_ESP32C6) || defined(TOPPERS_ESP32C5)	/* C5 も LP_AON_STORE1 = 0x600B1004（reg_base.h:95 + 0x4） */
	/*  C6: LP_AON_STORE1_REG = DR_REG_LP_AON_BASE(0x600B1000)+0x4（lp_aon_reg.h:29）。
	 *  出典: asp3 esp/c6/wifi/esp_wifi_adapter.c:766-781。段4 Task 3。  */
	uint32_t cal = sil_rew_mem((void *)0x600B1004U);	/* LP_AON_STORE1 */
#else
	uint32_t cal = sil_rew_mem((void *)0x600080B8U);	/* RTC_CNTL_STORE1 */
#endif
	if (cal == 0U) {
		cal = (uint32_t)((1000000ULL << 13) / 150000U);
	}
	return(cal);
}
#endif /* !CONFIG_IDF_TARGET_ESP32 */

/*
 *		タイマ
 */
#ifdef M5_A4_HANGDIAG
/*
 *  2026-08-14: 「遠すぎる期限」を誰が仕掛けたのかを採る計装（既定 OFF）。
 *  `__builtin_return_address(0)` はここでは **blob 側の呼出し位置**になる
 *  （本関数が osi テーブルの入口そのものだから）。Xtensa windowed ABI では
 *  上位 2 ビットにウィンドウ幅が載るので、生値のまま出して解析側で落とす。
 */
extern void m5_log_now_u32(const char *msg, unsigned int v);
static void
hd_report_arm(const char *what, void *ptimer, uint32_t val, uint32_t thresh,
			  bool repeat, void *ra)
{
	static uint32_t	n;

	if ((val > thresh) && (n < 8U)) {
		n++;
		m5_log_now_u32(what, val);
		m5_log_now_u32("[HD]   ptimer=", (unsigned int)(uintptr_t) ptimer);
		m5_log_now_u32("[HD]   repeat=", repeat ? 1U : 0U);
		m5_log_now_u32("[HD]   blob 側 呼出し元(raw ra)=",
					   (unsigned int)(uintptr_t) ra);
	}
}
#endif

static void
timer_arm_wrapper(void *timer, uint32_t tmout, bool repeat)
{
#ifdef M5_A4_HANGDIAG
	hd_report_arm("[HD] timer_arm(ms) ms=", timer, tmout, 1000U, repeat,
				  __builtin_return_address(0));
#endif
	/*  2026-08-14: ここは `tmout * 1000U`（32bit）だった。blob が
	 *  「事実上無期限」として渡す 0xfffffffe ms が 0xfffff830 us へ折り返り、
	 *  タイマタスクの全系停止を引き起こしていた。ms 版へ委譲する。  */
	esp_shim_timer_arm_ms(timer, tmout, repeat);
}

static void
timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
#ifdef M5_A4_HANGDIAG
	hd_report_arm("[HD] timer_arm_us us=", ptimer, us, 1000000U, repeat,
				  __builtin_return_address(0));
#endif
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
 *  PCBへ書き込まれる破壊の一因。JTAG_DEBUG.md 追記32）。
 *  logtaskが整形するまで内容が生存する静的ローテーションバッファへ
 *  コピーしてから渡すことで、この寿命不整合を解消する。
 */
#define LOG_RINGBUF_SLOTS	4
#define LOG_RINGBUF_MSGLEN	128

static char		log_ringbuf[LOG_RINGBUF_SLOTS][LOG_RINGBUF_MSGLEN];
static uint32_t	log_ringbuf_idx;

static void
log_writev_wrapper(unsigned int level, const char *tag, const char *format,
				   va_list args)
{
	uint32_t	lock;
	uint32_t	slot;
	char		*buf;

	(void) level;
	(void) tag;
	lock = esp_shim_int_disable();
	slot = log_ringbuf_idx;
	log_ringbuf_idx = (log_ringbuf_idx + 1U) % LOG_RINGBUF_SLOTS;
	esp_shim_int_restore(lock);

	buf = log_ringbuf[slot];
	vsnprintf(buf, LOG_RINGBUF_MSGLEN, format, args);
	syslog(LOG_NOTICE, "%s", buf);
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

#if defined(TOPPERS_ESP32C6)
/*
 *  ESP32-C6（段4 Task 3・AC-3g）: v5.5.4 タグの wifi_os_adapter.h は C6 だけ
 *  _regdma_link_set_write_wait_content / _sleep_retention_find_link_by_id の
 *  2 フィールドを持ち sizeof は 0x1e8（S3 0x1e0、LX6 0x1e4。blob-supply-table.md
 *  7 節の nm -S 実測）。blob は _magic の位置でヘッダの版を検査するので、
 *  ここで固定する（asp3 は目視だった）。この 2 フィールドは asp3 と同じく
 *  **NULL のまま**（下の表に行を書かない）: light-sleep の retention 経路で
 *  しか呼ばれず、asp3 は NULL のまま scan/接続/DHCP/ping に到達している。
 *  本ポートは sleep へ入る経路を持たない。
 */
_Static_assert(sizeof(wifi_osi_funcs_t) == 0x1e8,
			   "wifi_osi_funcs_t must be 0x1e8 bytes on ESP32-C6 (v5.5.4 blob ABI)");
#endif

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
