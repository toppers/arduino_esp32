/*
 *  seam-C5: CPU クロックを 80MHz -> 240MHz へ昇圧する（A1_C5_CPU_FREQ_MHZ=240）
 *  ==========================================================================
 *  ESP32-C5 統合 計画 3 段4 Task 1（2026-09-16）。判定基準:
 *  .steering/20260915-c5-plan/stage4/AC.md AC-1。型は esp/boot/seam_c6_clk.c
 *  （C6 段4 Task 1、80->160MHz）。値の正本は esp-idf v5.5.4 の C5 ヘッダ/ソース
 *  （下記）と asp3 esp32c5 の実機記録（asp3/target/esp32c5_espidf/
 *  target_kernel_impl.c:300-345, 800-845 = esp32c5_r32_cpu_clock_switch。asp3 は
 *  Direct Boot なので BBPLL 較正とソース切替まで自前で行うが、seam では
 *  bootloader が済ませているので本ファイルは分周器 1 本しか書かない）。
 *
 *  --------------------------------------------------------------------------
 *  なぜ分周だけでよいか（PLAN.md D5 / F1）
 *  --------------------------------------------------------------------------
 *  段2 の実機読み戻し（.steering/20260915-c5-plan/stage2/README.md）で、seam の
 *  実 ESP-IDF 2nd-stage bootloader は起動直後の PCR を
 *    PCR_SYSCLK_CONF   = 0xb0030200  (SOC_CLK_SEL[17:16]=3=PLL_F240M,
 *                                     CLK_XTAL_FREQ[30:24]=0x30=48)
 *    PCR_CPU_FREQ_CONF = 0x00000002  (CPU_DIV_NUM[7:0]=2 = /3)
 *  にして渡す（= 240/3 = 80MHz。soc/esp32c5/include/soc/soc.h:136
 *  CPU_CLK_FREQ_MHZ_BTLD=80、rev v1.0 では PLL_F240M 経路）。ソースが既に
 *  PLL_F240M なので **CPU_DIV_NUM を 2 から 0 に書き換えるだけ**で 240MHz に
 *  なる。ソース切替（PLL_F160M <-> PLL_F240M）は rev <= 1.01 の ICG erratum
 *  IDF-11064（esp_hw_support/port/esp32c5/rtc_clk.c:304-305, 475）に触れるので
 *  行わない。160MHz は作らない（F1）。
 *
 *  --------------------------------------------------------------------------
 *  レジスタ、式、手順の一次資料（ヘッダを読んで確かめた値）
 *  --------------------------------------------------------------------------
 *  esp-idf/components/soc/esp32c5/register/soc/pcr_reg.h
 *    :1904 PCR_SYSCLK_CONF_REG   (base+0x110)  :1927-1930 PCR_SOC_CLK_SEL [17:16]
 *          0=XTAL 1=RC_FAST 2=PLL_F160M 3=PLL_F240M（hal/esp32c5/include/hal/
 *          clk_tree_ll.h clk_ll_cpu_set_src）、:1937 PCR_CLK_XTAL_FREQ [30:24]
 *    :1970 PCR_CPU_FREQ_CONF_REG (base+0x118)  :1976-1979 PCR_CPU_DIV_NUM [7:0]
 *          "(PCR_CPU_DIV_NUM + 1) = divider"（clk_tree_ll.h:359-369
 *          clk_ll_cpu_set_divider）。**C6 の CPU_HS_DIV_NUM[15:8] とは別の
 *          フィールド**（C5 には HS/LS の区別が無い）。
 *    :1982 PCR_AHB_FREQ_CONF_REG (base+0x11c)  :1991-1994 PCR_AHB_DIV_NUM [7:0]
 *          bootloader は 5 (=/6 = 40MHz)。240MHz でも IDF は 6 固定
 *          （rtc_clk.c:217-222 "let f_ahb = 40MHz"、制約 cpu_div <= ahb_div かつ
 *          ahb_div % cpu_div == 0 は cpu_div=1 で満たす）ので **触らない**。
 *    :2305 PCR_BUS_CLK_UPDATE_REG (base+0x144) bit0 PCR_BUS_CLOCK_UPDATE
 *          R/W/WTC。"Configures whether or not to update configurations for
 *          CPU_CLK division, AHB_CLK division and HP_ROOT_CLK clock source
 *          selection ... automatically cleared when configurations have been
 *          updated"。**C5 では分周を書いただけでは反映されず、このパルスが要る**
 *          （clk_tree_ll.h:301-306 clk_ll_bus_update()。C6 には無い手順）。
 *  esp-idf/components/esp_hw_support/port/esp32c5/rtc_clk.c:207-227
 *    rtc_clk_cpu_freq_to_pll_240_mhz(): clk_ll_cpu_set_divider(240/f) ->
 *    clk_ll_ahb_set_divider(6) -> clk_ll_cpu_set_src(PLL_F240M) ->
 *    clk_ll_bus_update() -> esp_rom_set_cpu_ticks_per_us(f)。本ファイルは
 *    このうち「分周 -> bus_update -> ticks_per_us」だけを同じ順序で行う
 *    （ahb/src は bootloader の値のまま = 書かない）。
 *  esp_rom_set_cpu_ticks_per_us: esp32c5.rom.api.ld:57 で ROM の
 *    ets_update_cpu_frequency（esp32c5.rom.ld:32、0x40000044）へ PROVIDE。
 *    fmp3/target/m5stampc5_gcc/target.cmake が全ビルドで両 ld をリンクする。
 *  bus_update の待ち: IDF は無限待ち（clk_ll_bus_update の while）。asp3 は
 *    上限 2,000,000 回（target_kernel_impl.c:837-842）。本ファイルも上限を
 *    設け、超えたら rc=BUSUPD_TIMEOUT で戻る（ticks_per_us は更新しない）。
 *
 *  結果の受け渡し先が .bss ではなく .data である理由は seam_c6_clk.c 冒頭と
 *  同じ（start.S の .bss クリアが hardware_init_hook の後に走る）。
 */
#include "seam_c5_clk.h"
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include "esp32c5.h"
#include "esp_rom_sys.h"

seam_c5_clk_result_t g_seam_c5_clk_result __attribute__((section(".data"))) = {
	0U, 0U, 0U, 0U, 0U, 0U, 0U, SEAM_C5_CLK_RC_NOT_RUN
};

#define PCR_SYSCLK_CONF_SEL_MASK     ((uint32_t) (3U << 16))   /* SOC_CLK_SEL[17:16] */
#define PCR_SYSCLK_CONF_SEL_PLL240M  ((uint32_t) (3U << 16))
#define PCR_CPU_FREQ_DIV_MASK        ((uint32_t) 0xFFU)        /* CPU_DIV_NUM[7:0] */
#define PCR_CPU_FREQ_240MHZ_VALUE    0U                        /* divider 1 -> field 0 */
#define PCR_BUS_CLK_UPDATE_REG       (ESP32C5_PCR_BASE + 0x144)
#define PCR_BUS_CLK_UPDATE_BIT       ((uint32_t) 1U)
#define SEAM_C5_BUSUPD_WAIT_MAX      2000000U                  /* asp3 と同じ上限 */

void
seam_c5_clk_set(void)
{
	uint32_t sysclk_conf, cpu_freq_conf, ahb_freq_conf;
	uint32_t written, readback, i;

	sysclk_conf   = sil_rew_mem((const uint32_t *) ESP32C5_PCR_SYSCLK_CONF);
	cpu_freq_conf = sil_rew_mem((const uint32_t *) ESP32C5_PCR_CPU_FREQ_CONF);
	ahb_freq_conf = sil_rew_mem((const uint32_t *) ESP32C5_PCR_AHB_FREQ_CONF);
	g_seam_c5_clk_result.sysclk_conf_before   = sysclk_conf;
	g_seam_c5_clk_result.cpu_freq_conf_before = cpu_freq_conf;
	g_seam_c5_clk_result.ahb_freq_conf_before = ahb_freq_conf;
	g_seam_c5_clk_result.sysclk_conf_after    = sysclk_conf;
	g_seam_c5_clk_result.cpu_freq_conf_after  = cpu_freq_conf;
	g_seam_c5_clk_result.ahb_freq_conf_after  = ahb_freq_conf;

	if ((sysclk_conf & PCR_SYSCLK_CONF_SEL_MASK) != PCR_SYSCLK_CONF_SEL_PLL240M) {
		/*
		 *  前提（bootloader が PLL_F240M を残す）が崩れている。ソース切替は
		 *  IDF-11064 に触れる（冒頭）ので、書かずに戻る。
		 */
		g_seam_c5_clk_result.rc = SEAM_C5_CLK_RC_SKIP_NOT_PLL240;
		return;
	}

	written = (cpu_freq_conf & ~PCR_CPU_FREQ_DIV_MASK) | PCR_CPU_FREQ_240MHZ_VALUE;
	sil_wrw_mem((uint32_t *) ESP32C5_PCR_CPU_FREQ_CONF, written);

	/*
	 *  分周の反映（clk_ll_bus_update 相当）。自己クリアを待つ。
	 */
	sil_wrw_mem((uint32_t *) PCR_BUS_CLK_UPDATE_REG, PCR_BUS_CLK_UPDATE_BIT);
	for (i = 0U; i < SEAM_C5_BUSUPD_WAIT_MAX; i++) {
		if ((sil_rew_mem((const uint32_t *) PCR_BUS_CLK_UPDATE_REG) & PCR_BUS_CLK_UPDATE_BIT) == 0U) {
			break;
		}
	}
	g_seam_c5_clk_result.busupd_wait = i;

	readback = sil_rew_mem((const uint32_t *) ESP32C5_PCR_CPU_FREQ_CONF);
	g_seam_c5_clk_result.sysclk_conf_after   = sil_rew_mem((const uint32_t *) ESP32C5_PCR_SYSCLK_CONF);
	g_seam_c5_clk_result.cpu_freq_conf_after = readback;
	g_seam_c5_clk_result.ahb_freq_conf_after = sil_rew_mem((const uint32_t *) ESP32C5_PCR_AHB_FREQ_CONF);

	if (i >= SEAM_C5_BUSUPD_WAIT_MAX) {
		g_seam_c5_clk_result.rc = SEAM_C5_CLK_RC_BUSUPD_TIMEOUT;
		return;
	}
	if ((readback & PCR_CPU_FREQ_DIV_MASK) != PCR_CPU_FREQ_240MHZ_VALUE) {
		g_seam_c5_clk_result.rc = SEAM_C5_CLK_RC_VERIFY_NG;
		return;
	}

	/*
	 *  ROM の g_ticks_per_us を実クロックへ合わせる（rtc_clk.c:226 と同じ手順）。
	 *  target_kernel_impl.c の hardware_init_hook も CORE_CLK_MHZ(240) で同じ
	 *  呼出しを行うので二重になるが無害（ROM 側は格納するだけ）。
	 */
	esp_rom_set_cpu_ticks_per_us(240U);
	g_seam_c5_clk_result.rc = SEAM_C5_CLK_RC_OK;
}

/*
 *  報告タスク（esp/boot/seam_c5_clk.cfg）。fmp_app の tick タスク（優先度 10）
 *  より低い優先度で起動され、tick #0 の dly_tsk で CPU が空いたときに 1 回だけ
 *  syslog して終わる。fmp_app.c/.cfg を触らずに 240 の証跡を出すための形
 *  （80 の像を 1 バイトも変えないため。ATT_INI は syslog_initialize より前に
 *  走ってログが消えるので使わない）。syslog は書式 + 引数 5 個までなので 2 行。
 */
void
seam_c5_clk_report_task(intptr_t exinf)
{
	(void) exinf;
	syslog(LOG_NOTICE,
		   "pcr240 before sysclk_conf=0x%08x cpu_freq_conf=0x%08x ahb_freq_conf=0x%08x"
		   " rc=%d busupd_wait=%u",
		   (uint_t) g_seam_c5_clk_result.sysclk_conf_before,
		   (uint_t) g_seam_c5_clk_result.cpu_freq_conf_before,
		   (uint_t) g_seam_c5_clk_result.ahb_freq_conf_before,
		   (int_t) g_seam_c5_clk_result.rc,
		   (uint_t) g_seam_c5_clk_result.busupd_wait);
	syslog(LOG_NOTICE,
		   "pcr240 after  sysclk_conf=0x%08x cpu_freq_conf=0x%08x ahb_freq_conf=0x%08x"
		   " core_clk_mhz=%d",
		   (uint_t) g_seam_c5_clk_result.sysclk_conf_after,
		   (uint_t) g_seam_c5_clk_result.cpu_freq_conf_after,
		   (uint_t) g_seam_c5_clk_result.ahb_freq_conf_after,
		   (int_t) CORE_CLK_MHZ);
}
