/*
 *  ターゲット依存モジュール（M5NanoC6 / ESP32-C6 用・FMP3）
 *
 *  出典: asp3_esp_idf asp3/target/esp32c6_espidf/target_kernel_impl.c（ASP3 版）。
 *  落としたもの: GPIO 診断マーク（GPIO/UART 直叩きの診断），R87 TEE/APM の実験，
 *  PMU/PLL の真cold 回避（TOPPERS_ESP32C6_COLD_*、Direct Boot 専用。seam では
 *  bootloader が済ませて渡す見込み。段2 で物理電源断 5 回で実測する）、
 *  QEMU セミホスティング（C6 に QEMU は無い）。
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include <sil.h>
#include "esp_rom_sys.h"
#ifdef TOPPERS_OMIT_TECS
#include "chip_serial.h"
#endif

extern void software_term_hook(void);

/*
 *  MWDT の無効化（TIMG0/1）
 */
static void
esp32c6_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTWPROTECT(timg_base),
				ESP32C6_TIMG_WDT_WKEY);         /* 書込み保護の解除 */
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTCONFIG0(timg_base), 0U);
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTWPROTECT(timg_base), 0U);
}

/*
 *  hardware_init_hook（start.S の .data 初期化より前）
 */
void
hardware_init_hook(void)
{
	/*
	 *  TIMG0 のクロック有効化・リセット解除（PCR_TIMERGROUP0_CONF_REG）。
	 *  クロックの止まったペリフェラルへのアクセスはバスストールになる。
	 */
	{
		volatile uint32_t *pcr_tg0 = (volatile uint32_t *)0x6009603CU;
		uint32_t v = *pcr_tg0;
		v |= 0x1U;
		v &= ~0x2U;
		*pcr_tg0 = v;
	}

	/*
	 *  ウォッチドッグの無効化（MWDT0/1・RTC WDT・super WDT）
	 *  super WDT の書込み保護キーは 0x50D83AA1（esp-idf lpwdt_ll.h の
	 *  LP_WDT_SWD_WKEY_VALUE。asp3 側で誤キーによる WDT リブートを踏んだ実績あり）。
	 *  段4 fix wave 1 (E)：esp32c6.h の ESP32C6_RTC_CNTL_SWD_WKEY マクロが
	 *  誤った値（C3からの類推 0x8F1D312A）を持っていたため、当時はこの
	 *  マクロを使わず正しい値をリテラルで直書きして回避していた。マクロを
	 *  訂正したので、ここもマクロ参照へ統一する（値は不変：0x50D83AA1）。
	 */
	esp32c6_disable_mwdt(ESP32C6_TIMG0_BASE);
	esp32c6_disable_mwdt(ESP32C6_TIMG1_BASE);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTWPROTECT, ESP32C6_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTWPROTECT, 0U);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_SWD_WPROTECT, ESP32C6_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C6_RTC_CNTL_SWD_CONF,
			ESP32C6_RTC_CNTL_SWD_AUTO_FEED_EN | (1U << 30));
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_SWD_WPROTECT, 0U);

	/*
	 *  ROM の esp_rom_delay_us 較正値を実クロック（CORE_CLK_MHZ）へ合わせる
	 */
	esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ);
}

/*
 *  software_init_hook（.data/.bss 初期化後・カーネル起動前）: コンソール起動
 */
void
software_init_hook(void)
{
#ifdef TOPPERS_OMIT_TECS
	sio_initialize(0);
	sio_opn_por(SIOPID_FPUT, 0);
#endif
}

/*
 *  ターゲット依存の初期化（マスタプロセッサ用）
 */
void
target_mprc_initialize(void)
{
	chip_mprc_initialize();
}

/*
 *  ターゲット依存の初期化
 */
void
target_initialize(PCB *p_my_pcb)
{
	chip_initialize(p_my_pcb);

	/*
	 *  ペリフェラル割込みソースを CPU 割込み線へ割り当てる
	 *  線 1-15 は Wi-Fi shim（段4）が動的に使う予約（asp3 と同じ退避）。
	 *   16: SYSTIMER_TARGET0 + FROM_CPU_0（タイマ割込みの強制用・多重マップ）
	 *   17: コンソール（USB Serial/JTAG または UART0）
	 *   18/20/21: FROM_CPU_1/2/3（テスト用 INTNO1/2/3、target_test.h）
	 *   19 は未使用（テストの INTNO_UNOPTED=INTNO1+1 のため空ける）
	 */
	esp32c6_intmtx_route(ESP32C6_INTSRC_SYSTIMER_TARGET0, 16U);
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_0, 16U);
#ifdef TOPPERS_ESP32C6_CONSOLE_USBJTAG
	esp32c6_intmtx_route(ESP32C6_INTSRC_USB_SERIAL_JTAG, 17U);
#else
	esp32c6_intmtx_route(ESP32C6_INTSRC_UART0, 17U);
#endif
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_1, 18U);
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_2, 20U);
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_3, 21U);
}

/*
 *  ターゲット依存の終了処理
 */
void
target_exit(void)
{
	software_term_hook();
	chip_terminate();
	while (1) ;
}

/*
 *  デフォルトの software_term_hook（weak 定義）
 */
__attribute__((weak))
void software_term_hook(void)
{
}
