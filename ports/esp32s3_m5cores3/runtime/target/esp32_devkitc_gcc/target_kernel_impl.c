/*
 *  ターゲット依存モジュール（無印ESP32(Xtensa LX6) QEMU PoC用）
 *
 *  [2026-07-13新規] esp32s3_devkitc_gcc/target_kernel_impl.cを雛形に作成。
 *
 *  本PoCのスコープ（tasklist.md T1「vector table初期化＋UARTポーリング
 *  出力のみ」）：
 *  ・hardware_init_hook()：banner文字列をUART0へポーリング出力後、既定
 *    （`TOPPERS_LX6_PROCEED_TO_KERNEL`未定義）では無限ループで停止する
 *    （T1のdone条件：tick/dispatch/xthal_window_spill_nwに一切依存せず
 *    common/のブート経路のみを検証）。
 *  ・[2026-07-13追記] `TOPPERS_LX6_PROCEED_TO_KERNEL`をビルド時定義
 *    （EXTRA_OFLAGS経由）すると停止せずにそのまま戻り、start.Sが
 *    software_init_hook/sta_kerへ進む。window-spill自前実装
 *    （chip_window_spill.S、design.md §7）の動的検証
 *    （step1/lx6_dispatch_probe/）に使う。T1の既定ビルドは非回帰
 *    （マクロ未定義時の動作は変更前と完全に同一）。
 *  ・target_initialize()／target_mprc_initialize()：TNUM_PRCID==1既定の
 *    ためtarget_mprc_initialize()は空。
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include "target_timer.h"		/* TOPPERS_ESP32_CPU_FREQ_MHZ */
#include <sil.h>

/*
 *  起動時のハードウェア初期化処理
 *
 *  start.S から，BSS/DATA の初期化に先立って呼び出される。
 *
 *  既定ではbanner出力後にここで停止する（T1、sta_ker到達前）。
 *  `TOPPERS_LX6_PROCEED_TO_KERNEL`定義時は停止せずカーネル起動へ進む。
 */
void
hardware_init_hook(void)
{
	static const char banner[] =
		"\r\n"
		"TOPPERS/FMP3 ESP32(Xtensa LX6) QEMU PoC: hardware_init_hook reached\r\n"
#ifdef TOPPERS_LX6_PROCEED_TO_KERNEL
		"proceeding to sta_ker (dispatch/window-spill dynamic test)\r\n";
#else /* TOPPERS_LX6_PROCEED_TO_KERNEL */
		"vector table init + UART0 polling output only (T1, halting here)\r\n";
#endif /* TOPPERS_LX6_PROCEED_TO_KERNEL */
	const char *p;

	for (p = banner; *p != '\0'; p++) {
		target_fput_log(*p);
	}

#ifndef TOPPERS_LX6_PROCEED_TO_KERNEL
	while (1) {
	}
#endif /* TOPPERS_LX6_PROCEED_TO_KERNEL */
}

/*
 *  ets_delay_us()：ROM版を置き換える（無印ESP32 / Xtensa LX6）
 *  ------------------------------------------------------------------
 *  2026-08-13（BL-H-5 LX6 の I2C 停止の根本修正。
 *   記録 `非公開作業記録/20260813-lx6-i2c-hang/README.md`）
 *
 *  【何が壊れていたか（実測）】
 *  本ポートの LX6 flash-XIP リンカ（`esp32_xip_m5_wifi.ld` ほか）は DRAM を
 *  0x3FFB0000〜0x3FFF8000 と宣言する。この上端側 0x3FFE0000〜 は **ROM が
 *  自分のデータに使う SRAM1** であり、ESP-IDF は丸ごと予約している
 *  （ld 冒頭コメントの「ROM予約へ意図的にはみ出す設計」がこれ）。
 *  その中に ROM の `g_ticks_per_us_pro`（**0x3ffe01e0**、
 *  `esp-idf/components/esp_rom/esp32/ld/esp32.rom.ld:1368`）が在り、
 *  ROM の `ets_delay_us()`（0x40008534、`esp_rom_delay_us()` の実体）は
 *  「要求 us × この語」を CCOUNT で待つ。
 *  ⇒ **この語にどのアプリ変数／blob 変数が載るかはビルドごとに変わり、
 *     そこへ書込みが起きた瞬間にすべての `esp_rom_delay_us()` が壊れる。**
 *  実測（2026-08-13、M5Stack Core Basic）: `A1_ARDUINO_T*` を ON にした
 *  `seam-lx6-m5-wifi` では 0x3ffe01e0 が `.bss.M5`（M5Unified の大域 `M5`）
 *  の中に落ち、値が 0xa0(=160) から **0x3ffe00ac**（M5 自身を指すポインタ）
 *  へ書き潰された。以後 `delayMicroseconds(2)` は
 *  2 × 0x3ffe00ac = 0x7ffc0158 サイクル ≒ **13.4 秒**を回るため、
 *  lgfx の I2C バス開放（`i2c_stop()` が 41 回呼ぶ）で main_task が
 *  事実上停止した。golden 構成でも 0x3ffe01e0 は
 *  `seam-lx6-wifi`→libnet80211 の `g_cnxMgr`、
 *  `seam-lx6-m5-wifi`→`netif_esp32s3.o`、
 *  `seam-lx6-m5-factory`→SD バッファ `m5_sd_b1` の中に在る
 *  （`seam-lx6-smp` だけ .bss が 0x3FFE0000 に届かない）。
 *
 *  【なぜこの直し方か】
 *  DRAM を 0x3FFE0000 で切る（＝重なりを無くす）と 55KB 足りずリンクできず、
 *  ROM 予約域に穴を開ける分割も `esp_shim` のヒープ(96KB)が入らない。
 *  ⇒ **ROM の DRAM 状態に依存するのをやめる**のが最小の直し方である。
 *  `esp32.rom.ld` の当該行は `PROVIDE(ets_delay_us = 0x40008534)` なので、
 *  本 TU が強い定義を置けば **すべての参照（アプリ・シム・Wi-Fi/BT blob）が
 *  こちらへ解決される**（`esp_rom_delay_us` も同じ実体を指す）。
 *  刻み幅は ROM 実装と同じ 1000us で、掛け算の 32bit 溢れを避ける。
 *  周波数は本ファイルが自分で持つ（`.data` ＝ DRAM 下端側に置かれるので
 *  ROM 予約域と重ならない）。値の更新点は `ets_update_cpu_frequency_rom()`
 *  の呼出しと 1 対 1 に対応させる（下の `esp32_cpu_clock_init_pll`）。
 *
 *  【残る危険（本修正の射程外・記録として残す）】
 *  ROM 関数が**内部で**参照する他の ROM データは依然としてアプリ .bss と
 *  重なっている。本修正が外したのは「ets_delay_us が読む 1 語」だけである。
 */
static uint32_t	esp32_ticks_per_us = (uint32_t) TOPPERS_ESP32_CPU_FREQ_MHZ;

static inline uint32_t
esp32_rd_ccount(void)
{
	uint32_t	r;

	__asm__ __volatile__ ("rsr %0, ccount" : "=a"(r));
	return(r);
}

void	ets_delay_us(uint32_t us);

void
ets_delay_us(uint32_t us)
{
	uint32_t	start = esp32_rd_ccount();
	uint32_t	chunk;
	uint32_t	ticks;

	while (us != 0u) {
		chunk = (us > 1000u) ? 1000u : us;
		us -= chunk;
		ticks = chunk * esp32_ticks_per_us;
		while ((esp32_rd_ccount() - start) < ticks) {
		}
		start += ticks;
	}
}

/*
 *  CPUクロックのPLL昇圧（無印ESP32 / Xtensa LX6）
 *
 *  本ポートはDirect Bootのため2nd-stageブートローダ/esp_clk_initを持たず、
 *  ROMがCPU=XTAL 40MHz直結のまま起動する（S3はbootloaderが160MHzへ昇圧
 *  済みで来るのと対照的）。ここでBBPLLを立ち上げCPUクロック源をPLLへ
 *  切替え、TOPPERS_ESP32_CPU_FREQ_MHZ（既定240）まで引き上げる。
 *
 *  実装はESP-IDF v5.5 components/esp_hw_support/port/esp32/rtc_clk.c の
 *  rtc_clk_cpu_freq_set_config（現在XTAL→PLL遷移）を、静的レジスタ
 *  アドレスとROM regi2c関数（chip_serial.c流の埋め込み方式）で再現した
 *  もの。一次情報：
 *   - clk_ll_bbpll_enable / clk_ll_bbpll_set_config / clk_ll_cpu_set_src /
 *     clk_ll_cpu_set_freq_mhz_from_pll：
 *     ~/tools/esp-idf/components/hal/esp32/include/hal/clk_tree_ll.h
 *   - REGI2C_WRITE(I2C_BBPLL, reg, data) = rom_i2c_writeReg(0x66, 4, reg,
 *     data)（block=I2C_BBPLL=0x66, host_id=I2C_BBPLL_HOSTID=4。
 *     soc/regi2c_bbpll.h）。regi2cブロック有効化は ANA_CONFIG_REG(0x6000e044)
 *     の I2C_BBPLL_M(bit17) クリア。
 *   - レジスタアドレス（reg_base.h + 各reg.h のオフセット）：
 *     RTC_CNTL_OPTIONS0=0x3ff48000, RTC_CNTL_CLK_CONF=0x3ff48070,
 *     RTC_CNTL(=DBIAS)=0x3ff4807c, DPORT_CPU_PER_CONF=0x3ff0003c,
 *     SYSCON_PLL_TICK_CONF=0x3ff66008, UART0_CLKDIV=0x3ff40014。
 *   - ROMシンボル（esp32.rom.ld）：rom_i2c_writeReg=0x400041a4,
 *     ets_delay_us=0x40008534, ets_update_cpu_frequency_rom=0x40008550。
 *
 *  classic ESP32とS3のクロック体系差で対処が必要だった点：
 *   - PLL構成：240MHzはBBPLL 480M、80/160MHzはBBPLL 320M（S3は全て480M）。
 *   - CPUPERIOD_SEL（DPORT_CPU_PER_CONF[1:0]）0/1/2 → 80/160/240MHz。
 *     クロック源選択は RTC_CNTL_SOC_CLK_SEL[28:27]=1(PLL)（S3の
 *     SYSTEM_SYSCLK_CONF とは別レジスタ体系）。
 *   - APB：PLL選択時どれも80MHz。だがclassicはブート時APB=XTAL 40MHzのため
 *     切替でAPBが倍増し、UART0ボーレート分周（APB由来）が2倍ずれてコンソール
 *     が化ける。切替後にUART0_CLKDIVを×2再設定して115200を維持する
 *     （S3はブートローダ由来でAPB=80MHz据置きのためこの再設定が不要だった）。
 *
 *  切替タイミング：software_init_hook（BSS/DATA初期化後・sta_ker/tick開始前）。
 *  カーネルの時刻換算(TCYC_PER_HRT)が最初のtickから終始一貫するよう、
 *  HRT/tickが走り出す前に一度だけ実行する（tick開始後に切替えると、その
 *  瞬間までのCCOUNTは旧クロックで進むのにTCYC_PER_HRT=240で割ってしまい
 *  時刻がずれる）。ESP-IDF自身もbootloaderがapp実行前にPLL昇圧する順序で
 *  あり整合する。TOPPERS_ESP32_CPU_FREQ_MHZ==40 のときは何もしない
 *  （XTALフォールバック）。
 */
#if TOPPERS_ESP32_CPU_FREQ_MHZ != 40

#define ESP32_RTC_CNTL_OPTIONS0_REG    0x3ff48000u
#define ESP32_RTC_CNTL_CLK_CONF_REG    0x3ff48070u
#define ESP32_RTC_CNTL_REG             0x3ff4807cu  /* DIG_DBIAS_WAK[13:11] */
#define ESP32_DPORT_CPU_PER_CONF_REG   0x3ff0003cu
#define ESP32_SYSCON_PLL_TICK_CONF_REG 0x3ff66008u
#define ESP32_ANA_CONFIG_REG           0x6000e044u
#define ESP32_UART0_CLKDIV_REG         0x3ff40014u
#define ESP32_UART0_STATUS_REG         0x3ff4001cu  /* [23:16]TXFIFO_CNT [27:24]ST_UTX_OUT */

#define ESP32_I2C_BBPLL_BLOCK   0x66  /* I2C_BBPLL */
#define ESP32_I2C_BBPLL_HOSTID  4     /* I2C_BBPLL_HOSTID（S3の1とは別値） */

extern void rom_i2c_writeReg(int block, int host_id, int reg_add, int data);
/*  ets_delay_us は本ファイルが定義する（上のブロック参照。ROM 版は使わない）。 */
extern void ets_update_cpu_frequency_rom(uint32_t mhz);

/*
 * UART0のボーレート分周をAPB=80MHz向けへ×2再整合する。
 * ブート時 APB=XTAL 40MHz、PLL切替後 APB=80MHz。UART0のSCLKはAPB
 * （UART_TICK_REF_ALWAYS_ON=1が既定）なので、分周比(整数[19:0]+小数[23:20]/16)
 * を2倍しないとボーレートが2倍（115200→230400）になり化ける。
 */
/*
 * UART0のTX FIFOが空・送信FSMがアイドルになるまで待つ。
 * hardware_init_hookのバナー出力バイトはTX FIFOにまだ在庫が残った状態で
 * software_init_hookへ進む。その在庫を吐き切る前にAPBクロックを40→80MHzへ
 * 切替えると、在庫バイトが新ボーレートで送出され化ける（実機WROVER-KITで
 * バナーのみ化け・切替後の出力は正常、という非対称で確認）。切替前に
 * ここで完全にドレインする。UART_STATUS_REG[23:16]=TXFIFO_CNT,
 * [27:24]=ST_UTX_OUT(0=アイドル)。ハング回避のため有限リトライ。
 */
static void
esp32_uart0_tx_drain(void)
{
	int timeout;
	uint32_t v;

	for (timeout = 1000000; timeout > 0; timeout--) {
		v = sil_rew_mem((const uint32_t *) ESP32_UART0_STATUS_REG);
		if (((v >> 16) & 0xFFu) == 0u && ((v >> 24) & 0xFu) == 0u) {
			break;
		}
	}
}

static void __attribute__((unused))	/*  構成によっては未使用  */
esp32_uart0_rescale_for_apb80(void)
{
	uint32_t v = sil_rew_mem((const uint32_t *) ESP32_UART0_CLKDIV_REG);
	uint32_t total16 = ((v & 0x000FFFFFu) << 4) | ((v >> 20) & 0xFu);

	total16 *= 2u;  /* APB 40MHz → 80MHz */
	sil_wrw_mem((uint32_t *) ESP32_UART0_CLKDIV_REG,
	            ((total16 >> 4) & 0x000FFFFFu) | ((total16 & 0xFu) << 20));
}

static void
esp32_cpu_clock_init_pll(void)
{
	uint32_t v;

	/*
	 * バナー等の在庫TXバイトを吐き切ってからAPBを切替える（化け防止）。
	 */
	esp32_uart0_tx_drain();

	/*
	 * 切替前の各遅延（ets_delay_us）はCPU=40MHzで動くため、ROMの
	 * g_ticks_per_usを40に合わせておく（さもないと遅延が短くなりPLLロック
	 * 待ちが不足しうる）。
	 * 2026-08-13: ets_delay_us は本ファイルの実装（esp32_ticks_per_us）を
	 * 使うようになったので、ROM 側と**同じ値**をこちらにも入れる。
	 * ROM 側の更新も残す（ROM 内部から呼ばれる ROM 版のため）。
	 */
	esp32_ticks_per_us = 40u;
	ets_update_cpu_frequency_rom(40u);

	/*
	 * BBPLL再構成の前にCPUクロック源をXTALへ退避する（起動元非依存化）。
	 * seam-S3ではbootloaderがCPUを既にPLL上(80MHz, BBPLL 320M)で残すため、
	 * PLL上のままBBPLLを320M→480Mへ再構成するとCPUクロックがglitchして
	 * ハングする（実機で確認：seam-S3でboostを回すとsta_ker後に無音停止）。
	 * 一旦XTAL(40MHz)へ逃がしてからBBPLLを再構成しPLLへ復帰すれば、起動元が
	 * XTAL(Direct Boot)でもPLL(seam-S3)でも安全。Direct BootはCPUが元々XTAL
	 * (SOC_CLK_SEL=0)のため本書込みはno-op（非回帰）。
	 * RTC_CNTL_SOC_CLK_SEL[28:27]=0(XTAL)。この間コンソール出力はしない
	 * （XTAL中はAPB=40MHzで分周がずれるが、昇圧完了後にPLL(APB=80MHz)へ戻る）。
	 */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_CLK_CONF_REG);
	v &= ~(0x3u << 27);
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_CLK_CONF_REG, v);

	/* 1. 内部I2Cバス電源投入（clk_ll_i2c_pu）：BIAS_I2C_FORCE_PD(18)解除 */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_OPTIONS0_REG);
	v &= ~(1u << 18);
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_OPTIONS0_REG, v);

	/* 2. BBPLL電源投入（clk_ll_bbpll_enable）：
	 *    BB_I2C_FORCE_PD(6)/BBPLL_FORCE_PD(10)/BBPLL_I2C_FORCE_PD(8)解除 */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_OPTIONS0_REG);
	v &= ~((1u << 6) | (1u << 10) | (1u << 8));
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_OPTIONS0_REG, v);

	/* regi2c BBPLLブロック有効化（ANA_CONFIG I2C_BBPLL_M=bit17クリア） */
	sil_andw((void *) ESP32_ANA_CONFIG_REG, ~(1u << 17));

	/* clk_ll_bbpll_enable内のリセット設定REGI2C群 */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  0, 0x18); /* IR_CAL_DELAY */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  1, 0x20); /* IR_CAL_EXT_CAP */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  4, 0x9a); /* OC_ENB_FCAL */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID, 10, 0x00); /* OC_ENB_VCON */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID, 12, 0x00); /* BBADC_CAL_7_0 */

#if TOPPERS_ESP32_CPU_FREQ_MHZ == 240
	/* 3. 電圧引き上げ（rtc_clk_bbpll_configure先頭、480M時のみ）：
	 *    DIG_DBIAS_WAK = DIG_DBIAS_240M(=RTC_CNTL_DBIAS_1V25=7) */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_REG);
	v = (v & ~(0x7u << 11)) | (7u << 11);
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_REG, v);
	ets_delay_us(3u);   /* SOC_DELAY_PLL_DBIAS_RAISE */

	/* 4. BBPLL 480M設定（clk_ll_bbpll_set_config, xtal=40MHz）：
	 *    div_ref=0 div7_0=28 div10_8=0 lref=0 dcur=6 bw=3 */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID, 11, 0xc3);        /* ENDIV5 480M */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  9, 0x74);        /* BBADC_DSMP 480M */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  2, 0x00);        /* OC_LREF=(lref<<7)|(div10_8<<4)|div_ref */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  3, 28);          /* OC_DIV_7_0 */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  5, (3u<<6)|6u);  /* OC_DCUR=(bw<<6)|dcur */
#else
	/* 4. BBPLL 320M設定（80/160MHz, xtal=40MHz）：
	 *    div_ref=0 div7_0=32 div10_8=0 lref=0 dcur=6 bw=3 */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID, 11, 0x43);        /* ENDIV5 320M */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  9, 0x84);        /* BBADC_DSMP 320M */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  2, 0x00);        /* OC_LREF */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  3, 32);          /* OC_DIV_7_0 */
	rom_i2c_writeReg(ESP32_I2C_BBPLL_BLOCK, ESP32_I2C_BBPLL_HOSTID,  5, (3u<<6)|6u);  /* OC_DCUR */
#endif
	ets_delay_us(160u);  /* SOC_DELAY_PLL_ENABLE（安全側に大きい方=32K時160us） */

	/* 5. CPUクロック源をPLLへ（rtc_clk_cpu_freq_to_pll_mhz） */
#if TOPPERS_ESP32_CPU_FREQ_MHZ != 240
	/* 80/160MHz: DIG_DBIAS_WAK = DIG_DBIAS_80M_160M(=RTC_CNTL_DBIAS_1V10=4) */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_REG);
	v = (v & ~(0x7u << 11)) | (4u << 11);
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_REG, v);
#endif

	/* CPUPERIOD_SEL：80→0, 160→1, 240→2 */
	sil_wrw_mem((uint32_t *) ESP32_DPORT_CPU_PER_CONF_REG,
	            (TOPPERS_ESP32_CPU_FREQ_MHZ == 80)  ? 0u :
	            (TOPPERS_ESP32_CPU_FREQ_MHZ == 160) ? 1u : 2u);

	/* REF_TICK分周（PLL選択時 APB=80MHz → 80-1） */
	sil_wrw_mem((uint32_t *) ESP32_SYSCON_PLL_TICK_CONF_REG, 80u - 1u);

	/* RTC_CNTL_SOC_CLK_SEL[28:27]=1(PLL)：実際のクロック源切替 */
	v = sil_rew_mem((const uint32_t *) ESP32_RTC_CNTL_CLK_CONF_REG);
	v = (v & ~(0x3u << 27)) | (1u << 27);
	sil_wrw_mem((uint32_t *) ESP32_RTC_CNTL_CLK_CONF_REG, v);

	/* ROMのg_ticks_per_usを新CPU周波数へ（以降のets_delay_us精度）。
	 * 2026-08-13: 本ファイルの ets_delay_us が使う値も同じ点で更新する。 */
	esp32_ticks_per_us = (uint32_t) TOPPERS_ESP32_CPU_FREQ_MHZ;
	ets_update_cpu_frequency_rom((uint32_t) TOPPERS_ESP32_CPU_FREQ_MHZ);
	ets_delay_us(30u);

	/* 6. UART0ボーレート分周をAPB=80MHzへ再整合（コンソール化け防止）。
	 *    Direct Boot：ブート時APB=40MHz→昇圧後80MHzのため×2再設定が必要。
	 *    seam-S3：bootloaderが既にAPB=80MHzでUART0を設定済み（昇圧はXTAL退避を
	 *    挟むが最終的にPLL/APB=80MHzへ戻るだけでUART設定は不変）。ここで×2すると
	 *    逆に化けるためskipする。 */
#ifndef TOPPERS_BOOT_SEAM
	esp32_uart0_rescale_for_apb80();
#endif
}

#endif /* TOPPERS_ESP32_CPU_FREQ_MHZ != 40 */

/*
 *  ソフトウェア環境（ライブラリ等）の初期化処理
 *
 *  start.SからBSS/DATA初期化の後、sta_ker呼び出しの前に、マスタコア
 *  （コア0）のみ呼ばれる（スレーブはstart.Sのスレーブ分岐でスキップ）。
 *  ここでAPP_CPU（コア1）を起動し、両コアがsta_kerのbarrier_sync(1)で
 *  ランデブできるようにする（barrier_sync(1)はsta_ker冒頭にあり全コアの
 *  到達を待つため、それより前にコア1を起動する必要がある。design.md §5）。
 */
void
software_init_hook(void)
{
	/*
	 * CPUクロックをPLLへ昇圧（既定240MHz。TOPPERS_ESP32_CPU_FREQ_MHZ==40
	 * のときは何もしない=XTAL 40MHzフォールバック）。tick/HRTが走り出す前の
	 * この地点で一度だけ行う（詳細はesp32_cpu_clock_init_pllのコメント）。
	 *
	 * ※S3では早期のesp_bbpll_enable_480m呼出しがクラッシュしwifi_scan.c
	 *   （カーネル起動後）へ移した経緯があった（JTAG_DEBUG.md追記14）が、
	 *   これはS3のWiFi BBクロック文脈固有の事情。classic ESP32のCPUクロック
	 *   昇圧はESP-IDFのbootloaderがapp実行前に行う順序と同じで、この早期
	 *   地点が正しい（時刻換算TCYC_PER_HRTを最初のtickから一貫させるため
	 *   にもtick前が必須）。
	 */
	/*
	 * CPUクロックをPLLへ昇圧（既定240MHz）。TCYC_PER_HRT=240 と一致させるため
	 * seam-S3（実bootloader経由）でも昇圧する。ただしseam-S3ではbootloaderが
	 * CPUを既にPLL上(80MHz)で残すため、esp32_cpu_clock_init_pll内で
	 * 「BBPLL再構成の前にXTALへ退避」して起動元(XTAL/PLL)に依らず安全化し、
	 * UART0分周×2再設定は（bootloaderが既にAPB=80MHzでUART設定済みのため）
	 * seam-S3ではskipする。詳細は各関数のコメント。
	 */
#if defined(TOPPERS_BOOT_SEAM) && defined(SEAM_S3_BOOT_MARKERS)
	{ const char *m = "[SIH:enter]"; for (; *m; m++) target_fput_log(*m); }
#endif
#if TOPPERS_ESP32_CPU_FREQ_MHZ != 40
	esp32_cpu_clock_init_pll();
#endif
#if defined(TOPPERS_BOOT_SEAM) && defined(SEAM_S3_BOOT_MARKERS)
	{ const char *m = "[SIH:clk]"; for (; *m; m++) target_fput_log(*m); }
#endif
	target_mprc_initialize();
#if defined(TOPPERS_BOOT_SEAM) && defined(SEAM_S3_BOOT_MARKERS)
	{ const char *m = "[SIH:mprc->sta_ker]"; for (; *m; m++) target_fput_log(*m); }
#endif
}

/*
 *  マスタプロセッサ依存の初期化：APP_CPU（コア1）の起動
 *
 *  無印ESP32(classic / Xtensa LX6)のAPP_CPU起動はS3と**レジスタ体系が別**。
 *  S3はSYSTEM_CORE_1_CONTROL_0/1（0x600C0000系、runstall/clkgate/reset/
 *  bootaddrが2レジスタに集約）だが、classicは**DPORT系**で4レジスタに分離：
 *    DPORT_APPCPU_CTRL_A (0x3FF0002C bit0 = RESETTING、既定1)
 *    DPORT_APPCPU_CTRL_B (0x3FF00030 bit0 = CLKGATE_EN)
 *    DPORT_APPCPU_CTRL_C (0x3FF00034 bit0 = RUNSTALL)
 *    DPORT_APPCPU_CTRL_D (0x3FF00038 [31:0] = BOOT_ADDR)
 *  加えてRTC_CNTLのソフトストールを解除する（classicアドレス）：
 *    RTC_CNTL_OPTIONS0    (0x3FF48000 [1:0]   = SW_STALL_APPCPU_C0)
 *    RTC_CNTL_SW_CPU_STALL(0x3FF480AC [25:20] = SW_STALL_APPCPU_C1)
 *
 *  起動手順・レジスタ順序はESP-IDF v5.5に厳密に一致させる（一次情報）：
 *    ~/tools/esp-idf/components/esp_hw_support/cpu.c    esp_cpu_unstall()
 *    ~/tools/esp-idf/components/hal/esp32/include/hal/cpu_utility_ll.h
 *        cpu_utility_ll_unstall_cpu() / _enable_clock_and_reset_app_cpu()
 *    ~/tools/esp-idf/components/esp_system/port/cpu_start.c  (呼び出し順)
 *    ~/tools/esp-idf/components/soc/esp32/register/soc/{dport_reg.h,
 *        rtc_cntl_reg.h,reg_base.h}（レジスタアドレス・ビット）
 *  手順：(1)RTCソフトストール解除→(2)クロックゲート有効化＋runstall解除＋
 *  reset パルス→(3)ブートアドレス公開。APP_CPUのROMはreset解除後に
 *  CTRL_D(=boot addr)を読み、そこ（_start＝start.Sスレーブ経路）へ飛ぶ。
 *  共有カーネルデータは内蔵SRAMでコヒーレント。TNUM_PRCID==1では何もしない。
 *
 *  ※classicのCTRL_D等のDPORTアクセスはここでは**書きのみ**、かつAPP_CPU
 *    起動前＝シングルコア文脈のため、DPORT読みエラッタ（chip_dport.h）は
 *    非該当。
 *  2026-07-17追記(F-10)：上記「書きのみ」は陳腐化。レビュー指摘#6対応で
 *    CTRL_B（DPORT_APPCPU_CTRL_B_REG）に対する読み出し（`sil_rew_mem`、
 *    冪等化のためのCLKGATE_EN済みチェック。OpenOCDが先にAPP_CPUを起こして
 *    いる場合にJTAGブレークポイントを壊す無条件リセットパルスを避ける目的）
 *    が追加されている（下記コード参照）。ただしこの読みも依然
 *    **APP_CPU起動（リセット解除・ブートアドレス公開）前**＝シングルコア
 *    文脈で行われるため、「DPORT読みエラッタが非該当」という結論自体は
 *    変わらない（両コアが同時にDPORTへアクセスしうる状況ではない）。
 *    「書きのみ」という記述だけが実態と食い違っていたので訂正する。
 */
#if TNUM_PRCID >= 2

/* DPORT APP_CPU 制御レジスタ（ESP-IDF soc/dport_reg.h、base=0x3FF00000） */
#define DPORT_APPCPU_CTRL_A_REG       ((void *) 0x3FF0002CU)  /* bit0=RESETTING */
#define DPORT_APPCPU_CTRL_B_REG       ((void *) 0x3FF00030U)  /* bit0=CLKGATE_EN */
#define DPORT_APPCPU_CTRL_C_REG       ((void *) 0x3FF00034U)  /* bit0=RUNSTALL */
#define DPORT_APPCPU_CTRL_D_REG       ((void *) 0x3FF00038U)  /* [31:0]=BOOT_ADDR */
#define DPORT_APPCPU_RESETTING        0x00000001U
#define DPORT_APPCPU_CLKGATE_EN       0x00000001U
#define DPORT_APPCPU_RUNSTALL         0x00000001U

/* RTC_CNTL ソフトストール（ESP-IDF soc/rtc_cntl_reg.h、base=0x3FF48000） */
#define RTC_CNTL_OPTIONS0_REG         ((void *) 0x3FF48000U)
#define RTC_CNTL_SW_CPU_STALL_REG     ((void *) 0x3FF480ACU)
#define RTC_SW_STALL_APPCPU_C0_M      0x00000003U  /* OPTIONS0 [1:0] */
#define RTC_SW_STALL_APPCPU_C1_M      0x03F00000U  /* SW_CPU_STALL [25:20] */

/*
 *  APP_CPU flashキャッシュ有効化（ESP-IDF v5.5 components/esp_system/port/
 *  cpu_start.c start_other_core()より、`#if CONFIG_IDF_TARGET_ESP32`
 *  ブロックのみ：Cache_Flush(1); Cache_Read_Enable(1);
 *  を esp_cpu_unstall(1)（=RTCソフトストール解除）より前に呼ぶ。
 *
 *  [2026-07-16 seam-S3(LX6) W0追記] Direct BootではAPP_CPUのflashキャッシュが
 *  POR既定状態のまま（本関数呼出し前に他コードが触らない）で動作していたため
 *  この呼出しは省略されていたが、seam-S3（実ESP-IDFブートローダ経由）では
 *  ブートローダがPRO_CPU用にキャッシュ/MMUを再設定する過程でAPP_CPU側の
 *  キャッシュ状態が無効なまま残る（ESP-IDF自身がAPP_CPU起動直前に明示的に
 *  Cache_Read_Enable(1)を呼ぶ理由と同じ）。欠落すると、APP_CPU起動直後に
 *  両コアがflash命令フェッチを試み、PRO_CPU側のsta_ker()呼出し(entry命令)が
 *  フェッチ化け（IllegalInstruction、PCが命令境界からずれる）を起こす形で
 *  実機確認（smp_sample、seam×PRC_NUM=2）。Direct Bootでも呼出し自体は
 *  無害（ESP-IDF実装と同一のROM関数・同一引数）なので無条件で呼ぶ。
 */
typedef void (*rom_cache_cpuarg_t)(int);
#define ROM_Cache_Flush_1        ((rom_cache_cpuarg_t) 0x40009a14U)
#define ROM_Cache_Read_Enable_1  ((rom_cache_cpuarg_t) 0x40009a84U)

void
target_mprc_initialize(void)
{
	extern void _start(void);

	/* 0. APP_CPU flashキャッシュ有効化（上記コメント参照、ESP-IDF start_other_core()順） */
	ROM_Cache_Flush_1(1);
	ROM_Cache_Read_Enable_1(1);

	/* 1. RTCソフトストール解除（APPCPUのC0/C1フィールドをクリア） */
	sil_andw(RTC_CNTL_OPTIONS0_REG, ~RTC_SW_STALL_APPCPU_C0_M);
	sil_andw(RTC_CNTL_SW_CPU_STALL_REG, ~RTC_SW_STALL_APPCPU_C1_M);

	/*
	 * 2. クロックゲート有効化→runstall解除→resetパルス（set→clear）
	 *    （cpu_utility_ll_enable_clock_and_reset_app_cpu と同順）。
	 *
	 * ［レビュー指摘#6］ESP-IDFのcpu_utility_ll_enable_clock_and_reset_app_cpu()
	 * （esp-idf/components/hal/esp32/include/hal/cpu_utility_ll.h）は
	 * このブロック全体を`if (!CLKGATE_EN)`でガードしている（OpenOCDが
	 * 先にAPP_CPUのクロック有効化・リセット解除済みの場合、無条件で
	 * リセットパルスを打つとbreakpointが消えJTAGデバッグを阻害するため。
	 * cpu_start.cのコメント参照）。ESP32-S3版target_kernel_impl.cと対称に
	 * 同じガードを追加する。通常のコールドブート（CLKGATE_EN未設定）では
	 * 従来通りブロック全体が実行される。
	 */
	if ((sil_rew_mem((const uint32_t *) DPORT_APPCPU_CTRL_B_REG)
			& DPORT_APPCPU_CLKGATE_EN) == 0U) {
		sil_orw(DPORT_APPCPU_CTRL_B_REG, DPORT_APPCPU_CLKGATE_EN);
		sil_andw(DPORT_APPCPU_CTRL_C_REG, ~DPORT_APPCPU_RUNSTALL);
		sil_orw(DPORT_APPCPU_CTRL_A_REG, DPORT_APPCPU_RESETTING);
		sil_andw(DPORT_APPCPU_CTRL_A_REG, ~DPORT_APPCPU_RESETTING);
	}

	/* 3. ブートアドレス公開：_start（start.Sスレーブ経路へ合流） */
	Asm("memw" ::: "memory");
	sil_wrw_mem(DPORT_APPCPU_CTRL_D_REG, (uint32_t)(uintptr_t) &_start);
	Asm("memw" ::: "memory");
}

#else /* TNUM_PRCID >= 2 */

void
target_mprc_initialize(void)
{
}

#endif /* TNUM_PRCID >= 2 */

/*
 *  ターゲット依存部 初期化処理
 */
void
target_initialize(PCB *p_my_pcb)
{
	/*
	 *  コア依存部の初期化
	 */
	core_initialize(p_my_pcb);
}

/*
 *  ターゲット依存部 終了処理
 */
void
target_exit(void)
{
	extern void	software_term_hook(void);
	void (*volatile fp)(void) = software_term_hook;

	if (fp != 0) {
		(*fp)();
	}

	/*
	 *  コア依存部の終了処理
	 */
	core_terminate();

#ifdef TOPPERS_QEMU
	/*
	 *  QEMU上ではXtensaセミホスティングのSYS_exit（simcall）でQEMU自体を
	 *  即座に終了させる（テスト高速化：タイムアウト待ちを不要にする）。
	 *  QEMUは -semihosting 付きで起動する。実機ではsimcallは非対応のため
	 *  この分岐はビルドフラグTOPPERS_QEMUでのみ有効化し、実機ビルドでは
	 *  下のループにフォールバックする。
	 *  Xtensa simcall ABI：a2=SYS_exit(1), a3=終了コード。
	 */
	{
		register uint32_t a2 __asm__("a2") = 1U;	/* SYS_exit */
		register uint32_t a3 __asm__("a3") = 0U;	/* 終了コード0 */
		__asm__ __volatile__("simcall" : "+r"(a2) : "r"(a3) : "memory");
	}
#endif /* TOPPERS_QEMU */

	while (1) {
	}
}

/*
 *  エラー発生時の処理
 */
void
Error_Handler(void)
{
	while (1) {
	}
}

/*
 *  デフォルトの software_term_hook（弱い定義）
 */
__attribute__((weak)) void
software_term_hook(void)
{
}
