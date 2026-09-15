/*
 *  esp_shim_blobglue.c の ESP32-C5 分岐（計画 3 段4 Task 3、2026-09-16）
 *  ==========================================================================
 *  esp/shim/esp_shim_blobglue.c が既存の空行 1 本（6. の hmac スタブの直後、
 *  7. MAC アドレス読み出しの前）を `#include "esp_shim_blobglue_c5.inc"` に置き換えて
 *  読む。全体が #if defined(TOPPERS_ESP32C5)。置き方の理由は
 *  esp/shim/esp_wifi_adapter_c5.inc 冒頭（seam-c6-wifi golden の DWARF 感受性）。
 *
 *  共通ファイル側の同じ行数での書換え:
 *    - g_misc_nvs / mesh_sta_auth_expire_time / g_log_level のスタブ（LX6 用）の
 *      ガード `!S3 && !C6` -> `&& !C5`（C5 は g_misc_nvs = ROM ld
 *      esp32c5.rom.net80211.ld:87 = 0x4085ff7c、misc_nvs_* / mesh_sta_auth_expire_time /
 *      g_log_level は blob 内。blob-supply-table.md 2-1 節、4 節）
 *    - EFUSE_RD_MAC_SPI_SYS_0/1_REG の `#elif defined(TOPPERS_ESP32C6)` ->
 *      `|| defined(TOPPERS_ESP32C5)` と、値を esp_shim.h 末尾の
 *      ESP_SHIM_RISCV_EFUSE_BASE + 0x44/0x48 に（C6 0x600B0800 / C5 0x600B4800）
 *    - 11. C6 固有ブロック（esp_sleep_pd_config / esp_sleep_clock_config / putchar /
 *      rtc_clk_xtal_freq_get / vPortEnterCritical）は C6 のまま。C5 版は本ファイル
 *      （rtc_clk_xtal_freq_get の既定が 48 MHz で違い、他は同じ意味 = 出典を各所に）
 *
 *  【出典】asp3 esp/c5/wifi_v8/esp_shim_blobglue.c（g_misc_nvs extern、EFUSE 0x600B4844/48）、
 *  dev esp/shim/esp_shim_blobglue.c の 11. C6 ブロック（Low#1/Low#2 の判断）、
 *  esp-idf esp_hw_support/port/esp32c5/{rtc_clk.c:500-505, esp_clk_tree.c:107-139}。
 *  詳細は esp/shim/IMPORT_PROVENANCE_c5.md。
 */
#if defined(TOPPERS_ESP32C5)

#include <stdio.h>						/* putchar の宣言（hal_stub の stdio.h。EOF は無い） */
#include <errno.h>						/* extern int errno（hal_stub。実体は esp_shim_libc.c） */
#include "esp_sleep.h"
#include "hal/clk_tree_ll.h"
#include "freertos/FreeRTOS.h"
#include "soc/clk_tree_defs.h"

/*
 *  C5-a. esp_sleep_pd_config / esp_sleep_clock_config（C6 11-a と同じ判断）
 *  esp-idf esp_hw_support/modem_clock.c の modem_clock_module_enable/disable が
 *  呼ぶ（戻り値は捨てる）。Low#1（未実装）: sleep の電源ドメイン/クロック設定は
 *  保持も適用もしない。ESP_OK を返して「受理した体」にしない。
 */
esp_err_t
esp_sleep_pd_config(esp_sleep_pd_domain_t domain, esp_sleep_pd_option_t option)
{
	(void) domain; (void) option;
	return(ESP_ERR_NOT_SUPPORTED);	/* Low#1: sleep 未実装（設定を保持しない） */
}

esp_err_t
esp_sleep_clock_config(esp_sleep_clock_t clock, esp_sleep_clock_option_t option)
{
	(void) clock; (void) option;
	return(ESP_ERR_NOT_SUPPORTED);	/* Low#1: sleep 未実装（設定を保持しない） */
}

/*
 *  C5-b. putchar（C6 11-b と同じ判断）
 *  libpp.a の hal_debug.o / libnet80211.a が行末に putchar('\n') を呼ぶ
 *  （blob-supply-table.md 4 節、class a）。C5 の 13 本の ROM ld に putchar は無い
 *  （esp32c5.rom.newlib.ld にも無い）。Low#2（未実装）: 1 文字出力は行わず EOF を返す。
 */
int
putchar(int c)
{
	(void) c;
	return(-1);		/* EOF. Low#2: 出力していないことを戻り値で示す（C6 11-b と同じ） */
}

/*
 *  C5-c. rtc_clk_xtal_freq_get（libphy.a が参照）
 *  本体は esp-idf esp_hw_support/port/esp32c5/rtc_clk.c:500-505:
 *    xtal_freq_mhz = clk_ll_xtal_get_freq_mhz();   // PCR sysclk_conf.clk_xtal_freq [30:24]
 *    assert(xtal_freq_mhz == 48 || xtal_freq_mhz == 40);
 *  （hal/esp32c5/include/hal/clk_tree_ll.h:220-225。C6 の LP_AON_STORE4 ではなく
 *  PCR の HRO フィールドで、bootloader が渡す。段2 実測 sysclk_conf=0xb0030200 ->
 *  0x30 = 48）。rtc_clk.c 原本を丸ごとコンパイルすると PLL 較正経路（regi2c）まで
 *  引き込むので（asp3 は原本を採用、asp3-c5-inventory.md 1 節 #11）、本 1 関数だけ
 *  同じ意味で書く。40/48 以外なら syslog して **48**（M5Stamp-C5 の実読値）を返す
 *  （原本は assert。到達しないはずの経路で止めない = 値を記録して先へ）。
 */
soc_xtal_freq_t
rtc_clk_xtal_freq_get(void)
{
	uint32_t	xtal_freq_mhz = clk_ll_xtal_get_freq_mhz();

	if (xtal_freq_mhz != (uint32_t) SOC_XTAL_FREQ_48M && xtal_freq_mhz != (uint32_t) SOC_XTAL_FREQ_40M) {
		syslog(LOG_NOTICE, "blobglue(c5): PCR clk_xtal_freq=%u is not 40/48, assuming 48MHz",
			   (uint_t) xtal_freq_mhz);
		return(SOC_XTAL_FREQ_48M);
	}
	return((soc_xtal_freq_t) xtal_freq_mhz);
}

/*
 *  C5-d. vPortEnterCritical / vPortExitCritical（C6 11-d と同じ）
 *  esp_private/critical_section.h（OS_SPINLOCK==0）が esp_os_enter_critical{,_isr,_safe}()
 *  をこの名前へ展開する（宣言は esp/bt/stub/include/freertos/FreeRTOS.h の C6/C5 分岐）。
 *  写像先は S3/C6 と同じ esp_shim_bt_enter/exit_critical（mux は単一コアなので NULL）。
 */
void
vPortEnterCritical(void)
{
	esp_shim_bt_enter_critical(NULL);
}

void
vPortExitCritical(void)
{
	esp_shim_bt_exit_critical(NULL);
}

/*
 *  C5-e. esp_clk_tree_enable_src（C5 だけ。C6 には無い依存）
 *  C5 は SOC_CLOCK_TREE_MANAGEMENT_SUPPORTED=1（soc_caps.h:85）なので、esp-idf 原本の
 *  esp_hw_support/modem_clock.c:213-214（modem_clock_hal_init 内、ESP_ERROR_CHECK 付き）
 *  と esp_private/regi2c_ctrl.h:35-36（ANA_I2C_SRC_CLOCK_ENABLE）が
 *  esp_clk_tree_enable_src(SOC_MOD_CLK_MODEM_APB, enable) を呼ぶ
 *  （C5 の SOC_MOD_CLK_MODEM_APB = SOC_MOD_CLK_PLL_F160M、clk_tree_defs.h:88）。
 *  asp3 は esp_hw_support/port/esp32c5/esp_clk_tree.c と芋づるの 4 本
 *  （esp_clk_tree_common.c / clk_tree_hal.c / rtc_time.c / esp_clk.c）を丸ごと
 *  コンパイルした（esp_wifi_v8.cmake:842-869）。
 *  原本 esp_clk_tree.c:107-139 の esp_clk_tree_enable_src は
 *  `if (!esp_clk_tree_initialized || 範囲外) return ESP_OK;` で始まり、
 *  esp_clk_tree_initialized は esp_clk_tree_initialize()（同 :72-105。IDF アプリの
 *  起動コード esp_clk_init 系からしか呼ばれず、seam には呼び手が無い。asp3 も
 *  呼んでいない）だけが真にする。= **seam では原本も常に無動作で ESP_OK**。
 *  初期化済みの原本は参照カウントが 0->1 になるときに ref clock のゲート
 *  （PCR_PLL_DIV_CLK_EN_REG 0x60096128、pll_160m_clk_en = bit1、default 1 =
 *  pcr_reg.h:2053、clk_gate_ll.h:106-112 _clk_gate_ll_ref_160m_clk_en）を立てる。
 *  【dispatch B step 0、レビュー finding 5】enable && PLL_F160M（= MODEM_APB）のとき
 *  ビットを冪等に立てる（既に 1 なら書いても同じ値。初期化済みの原本の 0->1 と同じ
 *  結果）。0 だったら 1 度だけ syslog に残す（誰かが落としていた = 想定外の記録）。
 *  disable 側は参照カウントを持たないので落とさない（原本の未初期化経路と同じ
 *  = 無動作。Low#3: ゲート管理の縮小）。他の PLL_Fxx は ESP_OK のまま無動作。
 */
#define C5_PCR_PLL_DIV_CLK_EN		0x60096128U
#define C5_PCR_PLL_160M_CLK_EN_BIT	(1U << 1)

esp_err_t
esp_clk_tree_enable_src(soc_module_clk_t clk_src, bool enable)
{
	static bool_t	warned;
	uint32_t		v;

	if (enable && clk_src == SOC_MOD_CLK_PLL_F160M) {
		v = sil_rew_mem((void *) C5_PCR_PLL_DIV_CLK_EN);
		if ((v & C5_PCR_PLL_160M_CLK_EN_BIT) == 0U) {
			if (!warned) {
				warned = true;
				syslog(LOG_NOTICE, "blobglue(c5): PCR pll_160m_clk_en was 0, setting it (modem APB ref clock; Low#3)");
			}
			sil_wrw_mem((void *) C5_PCR_PLL_DIV_CLK_EN, v | C5_PCR_PLL_160M_CLK_EN_BIT);
		}
	}
	return(ESP_OK);		/* 原本 esp_clk_tree.c:109-111: 未初期化なら無動作で ESP_OK（seam では常にこの経路） */
}

/*
 *  C5-f. __errno（newlib reentrant の errno アクセサ）
 *  libpp.a が要求する log10（blob-supply-table.md 4 節 class c）を newlib の
 *  libm_nano.a（w_log10.c）から取ると、その定義域エラー経路が __errno() を呼ぶ
 *  （実測: 段4 Task 3 の初回リンクで `undefined reference to __errno`。floor しか
 *  引かない C6 では出ない）。本ポートの errno は esp_shim_libc.c の flat な
 *  `int errno;`（hal_stub の errno.h が extern 宣言。lwip も同じ実体を見る）なので、
 *  reentrant 版の口をその実体へ向ける（タスク単位ではない = errno.h 先頭の既知の制限）。
 */
int *
__errno(void)
{
	return(&errno);
}

#endif /* TOPPERS_ESP32C5 */
