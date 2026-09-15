/*
 *  esp_wifi_adapter.c の ESP32-C5 分岐（計画 3 段4 Task 3、2026-09-16）
 *  ==========================================================================
 *  本ファイルは esp/shim/esp_wifi_adapter.c が **既存の空行 1 本**（extern 宣言群の
 *  直後、PERIPH_WIFI_MODULE の定義の前）を `#include "esp_wifi_adapter_c5.inc"` に
 *  置き換えて読む。全体が #if defined(TOPPERS_ESP32C5) なので S3/LX6/C6 では空。
 *
 *  【なぜ C6 分岐の隣に書かないか（.steering/20260915-c5-plan/stage4/asp3-c5-inventory.md 4 節）】
 *  esp_wifi_adapter.c は seam-c6-wifi（golden）がコンパイルし、C6 の seam 像は
 *  app_desc の app_elf_sha256 に ELF 全体の sha256（DWARF 込み）を持つ。C6 の行の
 *  前に 1 行でも足すと .debug_line がずれて golden が動く（実測: 空行 1 本で DIFF）。
 *  空行 1 本を #include に置き換える形と、EOF への追記、#if 条件の同じ行数での
 *  書換えだけが golden を動かさない（実測 MATCH）。C5 固有の関数はここに置き、
 *  共通ファイル側は次の #if 条件だけを書き換えた:
 *    - `#if defined(TOPPERS_ESP32C6)`（PERIPH_WIFI_MODULE を #define しない）
 *      -> `|| defined(TOPPERS_ESP32C5)`（C5 も soc/periph_defs.h の enum）
 *    - set_intr/clear_intr の S3 既定 `#else` -> `#elif !defined(TOPPERS_ESP32C5)`
 *      （C5 は本ファイルの定義を使う）
 *    - set_isr/ints_on/ints_off（関数の頭が共通）: 本ファイルが C5 版を定義した後、
 *      同名を関数形式マクロで別名（c5_unused_*）へ逃がす。共通側の定義は
 *      「別名の未使用 static」になり、S3 の本体は `#elif !defined(TOPPERS_ESP32C5)`
 *      で空にする（未使用警告は本ファイル末尾の参照配列で抑える）。テーブル
 *      （g_wifi_osi_funcs）の `._set_isr = set_isr_wrapper` は括弧無しの識別子なので
 *      関数形式マクロは展開されず、本ファイルの C5 版に束縛される。
 *    - wifi_reset_mac / slowclk_cal_get の C6 分岐 -> `|| defined(TOPPERS_ESP32C5)`
 *      （modem_clock_module_mac_reset / LP_AON_STORE1 0x600B1004 は C5 でも同じ）
 *    - phy_enable / wifi_clock_enable の `#if defined(TOPPERS_ESP32C6)` ... `#else`:
 *      `#else` -> `#elif !defined(TOPPERS_ESP32C5)`（C5 は本ファイルの定義）
 *
 *  【出典】asp3 esp/c5/wifi_v8/esp_wifi_adapter.c（set_intr :150-200、ints_on/off :210-240、
 *  phy_enable :648-680、wifi_clock_enable :731-825）、asp3 target_kernel_impl.c
 *  :1020-1125（APM/TEE、実施42/43）、dev の C6 分岐（読み戻し・APM の型）。
 *  番地はすべて esp-idf v5.5.4 の C5 ヘッダで確認した（各定義の右に出典）。
 *  詳細は esp/shim/IMPORT_PROVENANCE_c5.md。
 */
#if defined(TOPPERS_ESP32C5)

#include "soc/periph_defs.h"					/* PERIPH_WIFI_MODULE（enum） */
#include "esp_private/esp_modem_clock.h"		/* modem_clock_*（esp-idf 原本を C5 でコンパイル） */
#include "esp_shim_intr_c5.h"					/* 線の写像と route/set_isr/ints_on/off */

#ifndef PHY_MODEM_WIFI
#define PHY_MODEM_WIFI      1
#endif

/*
 *  wifi_osi_funcs_t の ABI（v5.5.4 タグ、blob-supply-table.md 5 節）: C6 と同じ
 *  `_regdma_link_set_write_wait_content` / `_sleep_retention_find_link_by_id` の
 *  2 フィールド増で 0x1e8（asp3 C5 ELF の nm -S と同値）。_wifi_disable_ac_ax は無い。
 */
_Static_assert(sizeof(wifi_osi_funcs_t) == 0x1e8, "wifi_osi_funcs_t must be 0x1e8 bytes on ESP32-C5 (v5.5.4 tag blob ABI)");

/*
 *  繰り返し出る診断行は cmake/a1_c5_stage1.cmake の A1_C5_WIFI_DIAG（既定 OFF）の下。
 *  既定で残すのは 1 回きりの検出器（APM 読み戻し、最初の wifi_clock_enable の
 *  入口/出口 1 対、失敗時の行）だけ（C6 段4 fix wave 2 と同じ方針）。
 */
#if defined(A1_C5_WIFI_DIAG)
#define C5_DIAG_SYSLOG(...)			syslog(__VA_ARGS__)
#define C5_DIAG_CLK_READBACK(tag)	c5_modem_clk_readback(tag)
#else /* A1_C5_WIFI_DIAG */
#define C5_DIAG_SYSLOG(...)			((void) 0)
#define C5_DIAG_CLK_READBACK(tag)	((void) 0)
#endif /* A1_C5_WIFI_DIAG */

/*
 *  ---- 割込み（blob の _set_intr/_clear_intr/_set_isr/_ints_on/_ints_off）----
 *  写像とレジスタ操作は esp/shim/esp_shim_intr_c5.c。
 */
static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	C5_DIAG_SYSLOG(LOG_NOTICE, "wifi_adapter(c5): set_intr src=%d intno=%d prio=%d -> line %d",
				   (int_t) intr_source, (int_t) intr_num, (int_t) intr_prio,
				   (int_t) ESP_SHIM_C5_WIFI_LINE(intr_num));
	/*  範囲外は esp_shim_c5_wifi_route が syslog 済み。blob は戻り値を持たない。  */
	(void) esp_shim_c5_wifi_route((int) intr_source, (int) intr_num);
	(void) cpu_no;
	(void) intr_prio;
}

static void
clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
	(void) esp_shim_c5_wifi_unroute((int) intr_source, (int) intr_num);
}

static void
set_isr_wrapper(int32_t n, void *f, void *arg)
{
	int	ercd = esp_shim_c5_wifi_set_isr(n, f, arg);

	if (f != NULL && ercd == (int) E_OK) {
		C5_DIAG_SYSLOG(LOG_NOTICE, "wifi_adapter(c5): ena_int line=%d (intno %d) -> 0",
					   (int_t) ESP_SHIM_C5_WIFI_LINE(n), (int_t) n);
	}
}

static void
ints_on_wrapper(uint32_t mask)
{
	esp_shim_c5_wifi_ints_on(mask);
}

static void
ints_off_wrapper(uint32_t mask)
{
	esp_shim_c5_wifi_ints_off(mask);
}

/*
 *  共通側の同名定義（S3/LX6/C6 用の頭を持つ 3 関数）を別名へ逃がす（冒頭参照）。
 *  以降の `set_isr_wrapper(int32_t n, void *f, void *arg)` という**定義**は
 *  マクロ展開で c5_unused_set_isr_wrapper(...) になる。テーブルの識別子は
 *  展開されない。
 *
 *  【dispatch B step 0、レビュー finding 1】この関数形式マクロは「定義」と「括弧つきの
 *  呼出し」を区別できないので、将来 esp_wifi_adapter.c の 90 行目以降に
 *  `ints_off_wrapper(mask);` のような呼出しが足されると C5 だけ黙って空の
 *  c5_unused_* へ束縛される。それを**コンパイル時エラー**にするため、別名 3 本の
 *  プロトタイプに __attribute__((error)) を付ける: GCC は attribute error 付きの関数への
 *  **呼出し**（最適化で消えなかったもの）をエラーにするが、定義そのものと、下の
 *  参照配列（アドレス取得。呼出しではない）は許す。空の static 関数は IPA
 *  （inline / pure-const 解析）で呼出しごと消えて error が発火しない（実測: 素の
 *  error でも noinline でも stray call が rc=0 で通った）ので noipa を付ける。呼出しが要るなら
 *  esp_shim_c5_wifi_{set_isr,ints_on,ints_off}() を直接呼ぶこと。positive control:
 *  logs/step0/finding1-positive-control.txt（stray call -> error）。
 */
#define set_isr_wrapper(n, f, arg)		c5_unused_set_isr_wrapper(n, f, arg)
#define ints_on_wrapper(mask)			c5_unused_ints_on_wrapper(mask)
#define ints_off_wrapper(mask)			c5_unused_ints_off_wrapper(mask)
#define C5_UNUSED_CALL_ERROR \
	__attribute__((noipa, error("ESP32-C5: this shared wrapper is replaced by esp_wifi_adapter_c5.inc; call esp_shim_c5_wifi_* instead")))
static void c5_unused_set_isr_wrapper(int32_t n, void *f, void *arg) C5_UNUSED_CALL_ERROR;
static void c5_unused_ints_on_wrapper(uint32_t mask) C5_UNUSED_CALL_ERROR;
static void c5_unused_ints_off_wrapper(uint32_t mask) C5_UNUSED_CALL_ERROR;
/*  未使用 static 3 本の -Wunused-function を抑える（配列自体は gc-sections で落ちる）  */
static void (* const c5_unused_keep[]) (void) __attribute__((unused)) = {
	(void (*)(void)) c5_unused_set_isr_wrapper,
	(void (*)(void)) c5_unused_ints_on_wrapper,
	(void (*)(void)) c5_unused_ints_off_wrapper,
};

/*
 *  ---- modem クロック系レジスタの読み戻し（C6 の c6_modem_clk_readback の C5 版）----
 *  asp3 C5 が Direct Boot で肩代わりしていた ICG init / regi2c / LPCON |= 0x7
 *  （asp3-c5-inventory.md 1 節 #6/#7/#8）は seam では bootloader が設定済みのはずで、
 *  **入れる前に読む**（AC-4c）。ここでは読むだけで書かない。番地（C5）:
 *    MODEM_LPCON_CLK_CONF_REG            0x600AF018 (modem_lpcon_reg.h:140)  bits[2:0]
 *    MODEM_LPCON_WIFI_LP_CLK_CONF_REG    0x600AF00C (modem_lpcon_reg.h:86)
 *    MODEM_LPCON_CLK_CONF_POWER_ST_REG   0x600AF020 (modem_lpcon_reg.h:198)
 *    MODEM_SYSCON_CLK_CONF_POWER_ST_REG  0x600A9C0C (modem_syscon_reg.h:260。**C6 は 0x600A980C**)
 *    PMU_HP_ACTIVE_ICG_MODEM_REG         0x600B000C (pmu_reg.h:102) bits[31:30]
 */
#define C5_MODEM_LPCON_CLK_CONF			0x600AF018U
#define C5_MODEM_LPCON_WIFI_LP_CLK_CONF	0x600AF00CU
#define C5_MODEM_LPCON_CLK_CONF_PWR_ST	0x600AF020U
#define C5_MODEM_SYSCON_CLK_CONF_PWR_ST	0x600A9C0CU
#define C5_PMU_HP_ACTIVE_ICG_MODEM		0x600B000CU

static void
c5_modem_clk_readback(const char *tag)
{
	/*  syslog は書式 + 引数 5 個まで（TNUM_LOGPAR=6）。tag + 5 値は 2 行に分ける。  */
	syslog(LOG_NOTICE,
		   "wifi_adapter(c5): %s lpcon_clk_conf=0x%08x wifi_lp_clk=0x%08x lpcon_pwr_st=0x%08x",
		   tag, (uint_t) sil_rew_mem((void *) C5_MODEM_LPCON_CLK_CONF),
		   (uint_t) sil_rew_mem((void *) C5_MODEM_LPCON_WIFI_LP_CLK_CONF),
		   (uint_t) sil_rew_mem((void *) C5_MODEM_LPCON_CLK_CONF_PWR_ST));
	syslog(LOG_NOTICE,
		   "wifi_adapter(c5): %s syscon_pwr_st=0x%08x pmu_icg_modem=0x%08x",
		   tag, (uint_t) sil_rew_mem((void *) C5_MODEM_SYSCON_CLK_CONF_PWR_ST),
		   (uint_t) sil_rew_mem((void *) C5_PMU_HP_ACTIVE_ICG_MODEM));
}

/*
 *  ---- APM（Access Permission Management）/ TEE（PLAN.md D9、F5）----
 *  esp-idf ではアプリ側の起動コード bootloader_support/src/bootloader_mem.c:20-38
 *  bootloader_init_mem() が `#if !defined(BOOTLOADER_BUILD)` の中で
 *    (1) apm_hal_enable_ctrl_filter_all(false)  = 4 controller の FUNC_CTRL を 0
 *        （hal/apm_hal.c:185-195: HP_APM / LP_APM0 / LP_APM / CPU_APM。C5 は
 *          SOC_APM_CPU_APM_SUPPORTED=1 なので C6 の 3 つに CPU_APM が足される）
 *    (2) apm_hal_set_master_sec_mode_all(APM_SEC_MODE_TEE) = TEE の master を
 *        mode 0（hal/apm_hal.c:57-75、hal/esp32c5/include/hal/apm_ll.h:46-49:
 *        TEE_M0_MODE_CTRL_REG + 4*id。C5 は SOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL=1。
 *        IDF は APM_MASTER_MASK_ALL=0x03CE017F（割当済み 15 ID、apm_defs.h:45）だけを
 *        回すが、本ポートは ID 0..31 の 32 本すべてに 0 を書く（レジスタは全部
 *        存在し、未割当 ID への mode 0 は無害 = IDF の上位集合。dispatch B レビュー
 *        finding 4）。IDF は master 1(LPCORE) について LP_TEE_M0_MODE_CTRL_REG
 *        (0x600B3400) も書く（apm_hal.c:63-66、SOC_APM_SUPPORT_LP_TEE_CTRL）が、
 *        本ポートは書かない = LP コアを使わない（レビュー finding 6）。
 *        「IDF 相当」から外れるのはこの 2 点）
 *  を行う = bootloader は解除せず、seam はこの cpu_start をリンクしないので
 *  POR 既定（フィルタ有効、HP CPU 以外の master は REE2）のまま modem の DMA が
 *  HP SRAM へ届かない（C6 段4 Task 5: scan 0 AP、HP_APM M1 status=1
 *  info0=0x00130001。asp3 C5 実施42 も同じ像 = master 4(MODEM)/mode 3(REE2)）。
 *  asp3 C5 実施43: (1) だけでも (2) の master 4 だけでも、それぞれ単独で足りる
 *  （AND ゲート）。恒久実装は IDF と同じ両方。
 *  番地（esp-idf/components/soc/esp32c5/register/soc/{reg_base,hp_apm_reg,lp_apm0_reg,
 *  lp_apm_reg,cpu_apm_reg,tee_reg}.h）:
 *    HP_APM_FUNC_CTRL_REG   0x600990C4   LP_APM0_FUNC_CTRL_REG 0x600998C4
 *    LP_APM_FUNC_CTRL_REG   0x600B38C4   CPU_APM_FUNC_CTRL_REG 0x6009A0C4
 *    TEE_M0_MODE_CTRL_REG   0x60098000（+4*id、id 0..31。M4 = MODEM）
 *    HP_APM_Mn_STATUS_REG   0x600990C8 + 0x10*n（n=0..4）、+4 STATUS_CLR、
 *    +8 EXCEPTION_INFO0（region[15:0] mode[17:16] id[22:18]）、+0xC INFO1（番地）
 *    CPU_APM_Mn_STATUS_REG  0x6009A0C8 + 0x10*n（n=0..1）、同じ並び
 *  cmake の A1_C5_APM_UNBLOCK（既定 ON）の下に A1_C5_APM_FUNC_CTRL（(1)、既定 ON）と
 *  A1_C5_APM_TEE（(2)、既定 ON）。dispatch B の対照で最小集合を決める（AC-5b）。
 */
#define C5_HP_APM_FUNC_CTRL		0x600990C4U
#define C5_LP_APM0_FUNC_CTRL	0x600998C4U
#define C5_LP_APM_FUNC_CTRL		0x600B38C4U
#define C5_CPU_APM_FUNC_CTRL	0x6009A0C4U
#define C5_TEE_M0_MODE_CTRL		0x60098000U
#define C5_TEE_MASTER_COUNT		32U
#define C5_TEE_MASTER_MODEM		4U
#define C5_HP_APM_M0_STATUS		0x600990C8U
#define C5_HP_APM_PATHS			5U
#define C5_CPU_APM_M0_STATUS	0x6009A0C8U
#define C5_CPU_APM_PATHS		2U

static void
c5_apm_latch_dump(const char *tag, const char *name, uint32_t m0_status, uint32_t paths)
{
	uint32_t	n, any = 0U;

	for (n = 0U; n < paths; n++) {
		uint32_t	base = m0_status + 0x10U * n;
		uint32_t	st   = sil_rew_mem((void *) base);

		if (st != 0U) {
			any = 1U;
			syslog(LOG_NOTICE,
				   "wifi_adapter(c5): apm %s %s m%u status=0x%08x info0=0x%08x",
				   tag, name, (uint_t) n, (uint_t) st, (uint_t) sil_rew_mem((void *) (base + 8U)));
			syslog(LOG_NOTICE,
				   "wifi_adapter(c5): apm %s %s m%u info1=0x%08x",
				   tag, name, (uint_t) n, (uint_t) sil_rew_mem((void *) (base + 0xCU)));
		}
	}
	if (any == 0U) {
		syslog(LOG_NOTICE, "wifi_adapter(c5): apm %s %s exception latch: none", tag, name);
	}
}

void esp_wifi_adapter_c5_apm_readback(const char *tag);
void
esp_wifi_adapter_c5_apm_readback(const char *tag)
{
	syslog(LOG_NOTICE,
		   "wifi_adapter(c5): apm %s hp_func_ctrl=0x%08x lp_apm0_func_ctrl=0x%08x"
		   " lp_apm_func_ctrl=0x%08x cpu_apm_func_ctrl=0x%08x",
		   tag, (uint_t) sil_rew_mem((void *) C5_HP_APM_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C5_LP_APM0_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C5_LP_APM_FUNC_CTRL),
		   (uint_t) sil_rew_mem((void *) C5_CPU_APM_FUNC_CTRL));
	syslog(LOG_NOTICE,
		   "wifi_adapter(c5): apm %s tee_m0(hpcore)=0x%08x tee_m4(modem)=0x%08x",
		   tag, (uint_t) sil_rew_mem((void *) C5_TEE_M0_MODE_CTRL),
		   (uint_t) sil_rew_mem((void *) (C5_TEE_M0_MODE_CTRL + 4U * C5_TEE_MASTER_MODEM)));
	c5_apm_latch_dump(tag, "hp_apm", C5_HP_APM_M0_STATUS, C5_HP_APM_PATHS);
	c5_apm_latch_dump(tag, "cpu_apm", C5_CPU_APM_M0_STATUS, C5_CPU_APM_PATHS);
}

#if defined(A1_C5_APM_UNBLOCK)
static void
c5_apm_unblock(void)
{
	uint32_t	n;

	esp_wifi_adapter_c5_apm_readback("before-unblock");
#if defined(A1_C5_APM_FUNC_CTRL)
	/*  (1) 4 controller の経路フィルタを解除（apm_ll_*_enable_ctrl_filter_all(false)）  */
	sil_wrw_mem((void *) C5_HP_APM_FUNC_CTRL, 0U);
	sil_wrw_mem((void *) C5_LP_APM0_FUNC_CTRL, 0U);
	sil_wrw_mem((void *) C5_LP_APM_FUNC_CTRL, 0U);
	sil_wrw_mem((void *) C5_CPU_APM_FUNC_CTRL, 0U);
#endif /* A1_C5_APM_FUNC_CTRL */
#if defined(A1_C5_APM_TEE)
	/*  (2) ID 0..31 の全 master を TEE（mode 0）へ（IDF の 15 ID の上位集合、上のコメント）  */
	for (n = 0U; n < C5_TEE_MASTER_COUNT; n++) {
		sil_wrw_mem((void *) (C5_TEE_M0_MODE_CTRL + 4U * n), 0U);
	}
#endif /* A1_C5_APM_TEE */
	/*  例外ラッチをクリア（以降に新しい違反が出たら見えるように）  */
	for (n = 0U; n < C5_HP_APM_PATHS; n++) {
		sil_wrw_mem((void *) (C5_HP_APM_M0_STATUS + 0x10U * n + 4U), 1U);
	}
	for (n = 0U; n < C5_CPU_APM_PATHS; n++) {
		sil_wrw_mem((void *) (C5_CPU_APM_M0_STATUS + 0x10U * n + 4U), 1U);
	}
	esp_wifi_adapter_c5_apm_readback("after-unblock");
}
#endif /* A1_C5_APM_UNBLOCK */

/*
 *  ---- PHY / modem クロック（osi の _phy_enable / _wifi_clock_enable）----
 *  asp3 C5 の (a) LPCON |= 0x7、(b) ICG init は入れない（読み戻しで決める = C6 と同じ）。
 */
static void
phy_enable_wrapper(void)
{
	C5_DIAG_CLK_READBACK("phy_enable entry");
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1U);
	C5_DIAG_CLK_READBACK("phy_enable exit");
}

static void
wifi_clock_enable_wrapper(void)
{
	static bool_t	lpclk_selected = false;
	bool_t			first = !lpclk_selected;

	/*  読み戻しは最初の呼出しの入口/出口の 1 対だけ既定で出す（AC-4c）。  */
	if (first) {
		c5_modem_clk_readback("clock_enable entry");
	}
	else {
		C5_DIAG_CLK_READBACK("clock_enable entry");
	}
	if (!lpclk_selected) {
#if defined(A1_C5_APM_UNBLOCK)
		/*  modem を最初に有効化する前に APM/TEE を解除（上のコメント）。  */
		c5_apm_unblock();
#endif
		/*  esp_system/port/soc/esp32c5/clk.c の esp_perip_clk_init() 相当
		 *  （seam ではこれを呼ぶ者が居ない）。WIFIPWR クロック（LPCON CLK_CONF bit0）
		 *  はここで立つ（modem_clock.c）。C6 と同じ。  */
		modem_clock_deselect_all_module_lp_clock_source();
		modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,
										   MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0U);
		lpclk_selected = true;
		C5_DIAG_CLK_READBACK("clock_enable after_lpclk_select");
	}
	wifi_module_enable();
	if (first) {
		c5_modem_clk_readback("clock_enable exit");
	}
	else {
		C5_DIAG_CLK_READBACK("clock_enable exit");
	}
}

/*
 *  ---- 診断の出口（esp/app/wifi_sta.c の C5 分岐が呼ぶ）----
 */
void esp_wifi_adapter_c5_diag_dump(void);
void
esp_wifi_adapter_c5_diag_dump(void)
{
	esp_shim_c5_wifi_diag_dump();
}

#endif /* TOPPERS_ESP32C5 */
