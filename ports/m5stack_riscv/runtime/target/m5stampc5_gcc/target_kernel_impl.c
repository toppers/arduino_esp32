/*
 *  ターゲット依存モジュール（M5Stamp-C5 / ESP32-C5 用/FMP3）
 *
 *  出典: fmp3/target/m5nanoc6_gcc/target_kernel_impl.c（C6 段1-4）を C5 の値へ。
 *  差分: (1) PCR_TIMERGROUP0_CONF は C5 では +0x54（C6 は +0x3C の直値）。
 *  esp32c5.h のマクロ経由に統一。(2) 割込みソース番号は esp32c5.h（C5 の
 *  interrupts.h 実値）。(3) テスト用 INTNO1/2/3 は割込みマトリクスを使わず
 *  P4 と同じ software raise（CLIC エッジ線の IP）にするため FROM_CPU_1..3 の
 *  route は行わない（target_test.h 参照）。
 *  asp3 C5 の target_kernel_impl.c（1,794 行）からは何も持ち込まない: seam では
 *  bootloader が BBPLL 480 MHz 較正/CPU 80 MHz/PMA を済ませる見込み
 *  （INVESTIGATION.md B 表 PMA/PMP 行。**机上**。段2 で実測）なので、asp3 の
 *  r32/r34/r35/r41/pmu_init/APM 解除は載せない（APM は段4 の shim 側）。
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
esp32c5_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(timg_base),
				ESP32C5_TIMG_WDT_WKEY);         /* 書込み保護の解除 */
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTCONFIG0(timg_base), 0U);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(timg_base), 0U);
}

/*
 *  hardware_init_hook（start.S の .data 初期化より前）
 */
void
hardware_init_hook(void)
{
	/*
	 *  TIMG0 のクロック有効化/リセット解除（PCR_TIMERGROUP0_CONF_REG = +0x54。
	 *  C6 の +0x3C とは違う。register/soc/pcr_reg.h をコンパイルして確認済み）。
	 *  クロックの止まったペリフェラルへのアクセスはバスストールになる。
	 */
	sil_orw((void *)ESP32C5_PCR_TIMERGROUP0_CONF, ESP32C5_PCR_TG0_CLK_EN);
	sil_clrw((void *)ESP32C5_PCR_TIMERGROUP0_CONF, ESP32C5_PCR_TG0_RST_EN);

	/*
	 *  ウォッチドッグの無効化（MWDT0/1/RTC WDT/super WDT）
	 *  super WDT の書込み保護キーは 0x50D83AA1（hal/esp32c5/include/hal/
	 *  lpwdt_ll.h:30 LP_WDT_SWD_WKEY_VALUE。asp3 も C5 で誤キー 0x8F1D312A による
	 *  8 秒周期の SUPER_WDT リセットを踏んで訂正した: 実施33）。
	 *  seam の bootloader は CONFIG_BOOTLOADER_WDT_ENABLE=n（段2 で作る）だが、
	 *  stock bootloader（arduino）は WDT 有効で渡すので FMP3 側で止める型は
	 *  C6 と同じ。
	 */
	esp32c5_disable_mwdt(ESP32C5_TIMG0_BASE);
	esp32c5_disable_mwdt(ESP32C5_TIMG1_BASE);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT, ESP32C5_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT, 0U);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT, ESP32C5_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C5_RTC_CNTL_SWD_CONF,
			ESP32C5_RTC_CNTL_SWD_AUTO_FEED_EN | ESP32C5_RTC_CNTL_SWD_DISABLE);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT, 0U);

	/*
	 *  ROM の esp_rom_delay_us 較正値を実クロック（CORE_CLK_MHZ）へ合わせる
	 *  （esp32c5.rom.api.ld が解決）
	 */
	esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ);
}

/*
 *  software_init_hook（.data/.bss 初期化後/カーネル起動前）: コンソール起動
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
	 *  ペリフェラル割込みソースを CLIC 線へ割り当てる（MAP 値 = CLIC 線番号）。
	 *  CLIC 線 0-15 は内部線（使わない）。外部線 16-47:
	 *   16: SYSTIMER_TARGET0 + FROM_CPU_0（タイマ割込みの強制用/多重マップ。
	 *       asp3 が C5 実機で同じ多重マップを使用）
	 *   17: コンソール（USB Serial/JTAG または UART0）
	 *   18/20/21: INTNO1/2/3（target_test.h。software raise のエッジ線で、
	 *       割込みマトリクスのソースは割り付けない = P4 と同じ）
	 *   19: 未使用（テストの INTNO_UNOPTED = INTNO1+1 のため空ける）
	 *   22: IDF の C 実装が切断先に使う番号（6 + 16。ROM 版は未確認）。念のため空ける
	 *   23-47: 段4 の Wi-Fi shim（esp_shim_intr_clic）用
	 */
	esp32c5_intmtx_route(ESP32C5_INTSRC_SYSTIMER_TARGET0, 16U);
	esp32c5_intmtx_route(ESP32C5_INTSRC_FROM_CPU_0, 16U);
#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG
	esp32c5_intmtx_route(ESP32C5_INTSRC_USB_SERIAL_JTAG, 17U);
#else
	esp32c5_intmtx_route(ESP32C5_INTSRC_UART0, 17U);
#endif
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
