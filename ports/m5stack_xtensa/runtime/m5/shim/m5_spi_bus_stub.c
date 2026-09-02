/*
 *  M5GFX 用 SPI バス確保ミニスタブ（design.md §4・M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ------------------------------------------------------------------
 *  役割（確定した最小面）
 *  ------------------------------------------------------------------
 *  M5GFX の SPI 転送はレジスタ直（common.cpp / Bus_SPI.cpp）で，本物の
 *  gpspi ドライバ（esp_pm/esp_cache/sleep_retention 依存）は不要。common.cpp
 *  の spi::init が使う ESP-IDF SPI API は「バス確保・ピン配線・DMAチャネル
 *  割当」だけ：spi_bus_initialize / spi_bus_add_device / spi_device_acquire_bus
 *  / spi_device_release_bus / spi_bus_free / spi_bus_remove_device。本スタブは
 *  この 6 個を提供する。
 *
 *  実装（design.md §4 の項目対応）:
 *    1. GPSPI2(FSPI) のモジュールクロック有効化＋リセット解除
 *       → SYSTEM_PERIP_CLK_EN0/RST_EN0 レジスタ直（M5_SPI2_CLK_EN/RST）。
 *         ★これがクロックレジスタを実際に変化させる＝positive control の観測対象。
 *       ※既存 periph_ctrl.c（periph_module_enable/reset）の再利用は評価したが不採用：
 *         periph_ctrl.c は platform/os.h 等を esp/config/esp32/hal_stub_include から
 *         引く。hal_stub_include は libc スタブ（time.h/string.h/pthread）を同梱し，
 *         同一 M5 ビルドの C++(M5GFX) TU の標準ライブラリを壊すため include パスに
 *         **載せられない**。M5GFX が実際に要するのは periph_rcc_enter/exit の
 *         2 関数のみで，これは m5_idf_stubs.c が自前提供する。
 *         →レジスタ直の方が依存が閉じて安全。
 *    2. GPIO マトリクスで MOSI/SCLK/(MISO) を要求ピンへ配線（esp_rom_gpio_*，ROM）。
 *    3. GDMA 割当は初期実装では行わない（design.md 推奨＝DMA 無し CPU 書込みで開始，
 *       DMA 化は 1-5 以降）。dma_channel 引数は受け取るだけ。
 *    4. acquire/release_bus は参照カウントのみ（M5GFX 単独使用前提）。
 */

#include <stdint.h>
#include <string.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <soc/soc.h>
#include "m5_periph_clk.h"
#include <soc/spi_reg.h>
#include <soc/gpio_sig_map.h>
#include <soc/io_mux_reg.h>			/* PIN_FUNC_GPIO */
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <hal/gpio_ll.h>			/* gpio_ll_func_sel（IO_MUX 機能選択） */

#define M5_SPI_GPIO_HW	GPIO_LL_GET_HW(0)

/*  ★1-12：到達点マーカ（m5_display_smoke.c・即時出力／logtask 非経由）。
 *  kernel.h を本 TU へ持ち込まないためプロトタイプのみをローカル宣言する。 */
extern void m5_mark(const char *tag, unsigned int id);
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);

/*  M5 は CoreS3 の GPSPI2(FSPI) 一系統のみ。デバイスハンドルはダミー実体を返す
 *  （M5GFX は handle の非 NULL 性しか見ず，転送はレジスタ直で行う）。 */
static uint8_t		s_m5_spi_dev_dummy;
static int			s_m5_spi_bus_refcnt;
static unsigned int	s_m5_spi_bus_init_calls;	/* ★1-12：呼出し回数（何回目か） */

/*  FSPI ピン配線（GPIO マトリクス）。pin<0 は未使用としてスキップ。 */
static void
m5_spi_wire_out(int pin, uint32_t out_sig)
{
	if (pin < 0) {
		return;
	}
	gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT_OUTPUT);
	esp_rom_gpio_connect_out_signal((uint32_t)pin, out_sig, false, false);
	/*  ★1-8（getBoard()=0 続き）：IO_MUX の機能選択を GPIO マトリクスへ向ける。
	 *  esp_rom_gpio_connect_out_signal は GPIO マトリクス（func_out_sel_cfg）と
	 *  GPIO_ENABLE しか触らず，IO_MUX の MCU_SEL（＝ピンをどの機能へ結ぶか）は
	 *  変えない。実 ESP-IDF の spi_common.c bus_init_io は各 SPI ピンについて
	 *  connect_*_signal の後に gpio_func_sel(pin, FUNC_GPIO) を必ず呼び MCU_SEL を
	 *  GPIO へ設定する（esp-idf .../gpspi/spi_common.c:661/674/705）。本スタブは
	 *  これを欠いていた。SCLK/MOSI は M5GFX の pinMode（MCU_SEL=GPIO を設定する）を
	 *  一度も通らない（pinMode は DC/CS/RST 用途のみ）ため，ブート既定の MCU_SEL の
	 *  ままだと FSPICLK/FSPID 信号がパッドへ届かず，パネルにクロックが出ない可能性が
	 *  ある。→ 実ドライバに合わせて明示設定する。 */
	gpio_ll_func_sel(M5_SPI_GPIO_HW, (uint8_t)pin, PIN_FUNC_GPIO);
}

static void
m5_spi_wire_in(int pin, uint32_t in_sig)
{
	if (pin < 0) {
		return;
	}
	/*
	 *  ★1-11（E-2 修正）：入力ピンは「入力を有効化する」だけにし、出力イネーブルは
	 *  触らない。実 ESP-IDF の spi_common.c bus_init_io は MISO が出力不要なとき
	 *  `gpio_input_enable(pin)` のみを呼び、gpio_set_direction は使わない
	 *  （esp-idf/components/esp_driver_spi/src/gpspi/spi_common.c:663-674）。
	 *
	 *  従来は gpio_set_direction(pin, GPIO_MODE_INPUT) を使っており、これは
	 *  **出力イネーブルを落とす**副作用があった。CoreS3 では MISO=GPIO35 が
	 *  パネルの D/C と兼用で、lgfx の Bus_SPI::init() が pinMode(35, output) した
	 *  直後に本関数が OE を落としてしまう。現状は後続の cs_control/dc_control が
	 *  結果的に復活させるので表面化していないが、依存すべきでない挙動＝潜在バグ。
	 *  実 IDF と同型（入力有効化のみ）に直す。
	 */
	gpio_ll_input_enable(M5_SPI_GPIO_HW, (uint32_t)pin);
	esp_rom_gpio_connect_in_signal((uint32_t)pin, in_sig, false);
	/*  ★1-8：入力ピンも IO_MUX を GPIO マトリクスへ（理由は m5_spi_wire_out 参照）。 */
	gpio_ll_func_sel(M5_SPI_GPIO_HW, (uint8_t)pin, PIN_FUNC_GPIO);
}

/*
 *  ============================================================================
 *  ★★ここから下の 6 本は `A1_M5_SDSPI=ON` では**コンパイルしない**
 *  ============================================================================
 *  【なぜ】microSD を ESP-IDF の `sdmmc`/`sdspi_host` で使うには、
 *  ★**`spi_bus_add_device()` が本物でなければならない**（`sdspi_host_init_device()` が呼ぶ）。
 *  libsdspi.a 側の `spi_bus_initialize` は `--gc-sections` で捨てられ、
 *  ★**このスタブが勝ってしまう**。
 *  ⇒ ★SD 構成では**スタブを外して IDF の `spi_master` に SPI2 を任せる**。
 *
 *  ★★危険を明記する: **M5GFX も `spi_bus_initialize()` を呼ぶ**
 *  （`lgfx/v1/platforms/esp32/common.cpp` の `spi::init`）。
 *  ⇒ SD 構成では **LCD も IDF の実 SPI 初期化を通る**ことになる。
 *  ★**表示が壊れる可能性がある**。だから
 *   ・★**既定（`A1_M5_SDSPI` 未定義）では従来どおりスタブを使う**（基準を動かさない）
 *   ・★SD 構成を焼いたら**まず表示の非退行を確かめる**
 *     （`camstride_cb=PASS_MIN_AT_640`・`[BEGIN] ->` 18・例外 0・自然終了）
 *  ★「SD が読めた」より先に「表示が生きている」を見ること。
 */
#if !defined(M5_SDSPI)
esp_err_t
spi_bus_initialize(spi_host_device_t host_id, const spi_bus_config_t *bus_config,
				   spi_dma_chan_t dma_chan)
{
	(void) dma_chan;		/* 初期実装は DMA 無し（design.md §4-3） */

	/*  ★1-12：入口ブラケット。Panel_Device::init() 経路の `_bus->init()` から
	 *  2 回目が呼ばれる（1 回目は M5GFX の autodetect 用 bus）。何回目かを出す。 */
	s_m5_spi_bus_init_calls++;
	m5_mark_u32("[MK] spi_bus_initialize in  #", 0x10U, s_m5_spi_bus_init_calls);

#if defined(TOPPERS_ESP32_LX6)
	/*
	 *  無印ESP32(LX6) 版。S3 との違いは 3 つで、いずれもチップの構造差である。
	 *
	 *  (1) ホストを固定できない。M5GFX は CoreS3 では SPI2 を頼むが、
	 *      M5Stack Basic の LCD は SCK=18/MOSI=23/MISO=19 ＝ VSPI 既定ピンで、
	 *      autodetect は SPI3_HOST を渡してくる。よって host_id で選ぶ。
	 *  (2) クロック／リセットは DPORT の SPI2/SPI3 それぞれのビット。
	 *  (3) SPI_CLK_GATE_REG に相当するレジスタが**無い**（classic の spi_reg.h に
	 *      その定義は存在しない）。S3 で無限ハングを避けるために要る
	 *      マスタクロックゲートは classic には無く、DPORT のクロック許可だけで
	 *      転送が動く。
	 *
	 *  GPIO マトリクスの信号番号は SPI2=HSPI 群 / SPI3=VSPI 群
	 *  （soc/esp32/include/soc/gpio_sig_map.h）。
	 */
	{
		uint32_t clk_en, rst_bit, clk_out, d_out, d_in, q_in;

		if (host_id == SPI3_HOST) {
			clk_en = DPORT_SPI3_CLK_EN; rst_bit = DPORT_SPI3_RST;
			clk_out = VSPICLK_OUT_IDX;  d_out = VSPID_OUT_IDX;
			d_in    = VSPID_IN_IDX;     q_in  = VSPIQ_IN_IDX;
		}
		else {
			clk_en = DPORT_SPI2_CLK_EN; rst_bit = DPORT_SPI2_RST;
			clk_out = HSPICLK_OUT_IDX;  d_out = HSPID_OUT_IDX;
			d_in    = HSPID_IN_IDX;     q_in  = HSPIQ_IN_IDX;
		}

		REG_SET_BIT(M5_PERIP_CLK_EN_REG, clk_en);
		REG_SET_BIT(M5_PERIP_RST_EN_REG, rst_bit);	/* reset assert */
		REG_CLR_BIT(M5_PERIP_RST_EN_REG, rst_bit);	/* reset deassert */

		if (bus_config != NULL) {
			m5_spi_wire_out(bus_config->mosi_io_num, d_out);
			/*  3線(SIO)読取で D 線から入力を取るため、S3 と同じく in も張る。 */
			if (bus_config->mosi_io_num >= 0) {
				esp_rom_gpio_connect_in_signal(
					(uint32_t) bus_config->mosi_io_num, d_in, false);
			}
			m5_spi_wire_out(bus_config->sclk_io_num, clk_out);
			m5_spi_wire_in(bus_config->miso_io_num,  q_in);
		}
	}
#else /* defined(TOPPERS_ESP32_LX6) */
	/*  項目1：GPSPI2 クロック有効化＋リセット解除（SYSTEM レジスタ直）。
	 *  ★positive control の観測点：この 2 行で M5_PERIP_CLK_EN_REG の
	 *    M5_SPI2_CLK_EN ビットが 0→1 に変化する。 */
	REG_SET_BIT(M5_PERIP_CLK_EN_REG, M5_SPI2_CLK_EN);
	REG_SET_BIT(M5_PERIP_RST_EN_REG, M5_SPI2_RST);		/* reset assert */
	REG_CLR_BIT(M5_PERIP_RST_EN_REG, M5_SPI2_RST);		/* reset deassert */

	/*  項目1b（★1-6 追加・begin() 内 Bus_SPI::wait() 無限ハングの根本修正）：
	 *  GPSPI2 の「機能マスタクロックゲート」を有効化する（SPI_CLK_GATE_REG）。
	 *
	 *  ★M5_SPI2_CLK_EN（上の SYSTEM レジスタ）は「SPI レジスタを読み書き
	 *    できる」バスクロックにすぎず，実際に転送を駆動する内部マスタクロックは
	 *    別物である。ESP-IDF の実 spi_master は spi_bus_initialize 内で
	 *    spi_ll_enable_clock（clk_en=1）＋ spi_hal_init→spi_ll_master_init
	 *    （mst_clk_active=1, mst_clk_sel=1）を実行してこのゲートを開く
	 *    （esp-idf/components/esp_driver_spi/src/gpspi/spi_master.c:339,341 →
	 *     components/hal/spi_hal.c:18 → hal/esp32s3/include/hal/spi_ll.h:189-210）。
	 *    本スタブはこれを欠いていたため，M5GFX が転送を開始（cmd.usr=1）しても
	 *    機能クロックが動かず SPI_USR ビットが永久に落ちず，Bus_SPI::wait()
	 *    （Bus_SPI.cpp:345 `while(*spi_cmd_reg & SPI_USR)`）が無限ループした。
	 *    上の M5_SPI2_RST リセットは clk_gate を 0 に戻すので，リセット
	 *    解除の「後」に立てる必要がある。
	 *    - SPI_CLK_EN         : clk gate 有効（レジスタクロックゲート）
	 *    - SPI_MST_CLK_ACTIVE : マスタモジュールクロック投入（★これが無いと無限ハング）
	 *    - SPI_MST_CLK_SEL=1  : マスタクロック源＝PLL_CLK_80M。M5GFX の
	 *                           FreqToClockDiv は getApbFrequency()（S3=80MHz 固定）
	 *                           を分周基準にするので 80MHz 源と整合する。
	 *  ★観測点（positive control）：この書込み後 SPI_CLK_GATE_REG(2) は
	 *    SPI_CLK_EN|SPI_MST_CLK_ACTIVE|SPI_MST_CLK_SEL = 0x7 を読み返すはず。
	 *  レジスタ index=2 は GPSPI2（M5GFX の spi_port = spi_host+1 = 2 と一致。
	 *  REG_SPI_BASE(2)=DR_REG_SPI2_BASE=0x60024000）。 */
	REG_SET_BIT(SPI_CLK_GATE_REG(2), SPI_CLK_EN | SPI_MST_CLK_ACTIVE | SPI_MST_CLK_SEL);

	/*  項目2：GPIO マトリクス配線（MOSI=FSPID_out/in, SCLK=FSPICLK_out, MISO=FSPIQ_in）。 */
	if (bus_config != NULL) {
		m5_spi_wire_out(bus_config->mosi_io_num, FSPID_OUT_IDX);
		/*  ★1-7（getBoard()=0 根本修正）：MOSI(FSPID) には out に加えて in も配線する。
		 *  M5GFX の CoreS3 は spi_3wire=true。3線(SIO)モードのパネル ID 読取
		 *  （_read_panel_id → Bus_SPI::beginRead が SPI_SIO を立てる, Bus_SPI.cpp:1033）は
		 *  MISO(FSPIQ=GPIO35, DC と共有) ではなく D 線(FSPID=GPIO37) から入力をサンプルする
		 *  （S3 TRM: SPI_SIO=3線半二重, MOSI/MISO が D 線を共用）。実 ESP-IDF の
		 *  spi_common.c bus_init_io は GPIO マトリクス経路で MOSI に spid_out と spid_in の
		 *  両方を connect する（esp-idf .../spi_common.c: connect_out_signal(spid_out)+
		 *  connect_in_signal(spid_in)）。本スタブは in 配線を欠いており，SIO 読取が
		 *  未接続入力（定数/フロート）を読むため _read_panel_id が 0xE3 と一致せず，
		 *  autodetect が CoreS3 を検出できず getBoard()=0 に落ちていた（1-7 実機で確定：
		 *  I2C 両経路健全・電源投入ブロック到達を AW9523 読み返しで実測，残る失敗は本読取経路）。 */
		if (bus_config->mosi_io_num >= 0) {
			esp_rom_gpio_connect_in_signal((uint32_t)bus_config->mosi_io_num,
										   FSPID_IN_IDX, false);
		}
		m5_spi_wire_out(bus_config->sclk_io_num, FSPICLK_OUT_IDX);
		m5_spi_wire_in(bus_config->miso_io_num,  FSPIQ_IN_IDX);
	}
	(void) host_id;		/* CoreS3 は SPI2 固定 */
#endif /* defined(TOPPERS_ESP32_LX6) */

	m5_mark_u32("[MK] spi_bus_initialize out #", 0x11U, s_m5_spi_bus_init_calls);
	return(ESP_OK);
}

esp_err_t
spi_bus_add_device(spi_host_device_t host_id,
				   const spi_device_interface_config_t *dev_config,
				   spi_device_handle_t *handle)
{
	/*  CS ピン配線（devcfg.spics_io_num）。信号番号は spi_bus_initialize と
	 *  同じ理由でチップとホストで変わる。 */
	if (dev_config != NULL && dev_config->spics_io_num >= 0) {
#if defined(TOPPERS_ESP32_LX6)
		m5_spi_wire_out(dev_config->spics_io_num,
						(host_id == SPI3_HOST) ? VSPICS0_OUT_IDX
											   : HSPICS0_OUT_IDX);
#else
		(void) host_id;
		m5_spi_wire_out(dev_config->spics_io_num, FSPICS0_OUT_IDX);
#endif
	}
	else {
		(void) host_id;
	}
	if (handle != NULL) {
		*handle = (spi_device_handle_t)&s_m5_spi_dev_dummy;
	}
	return(ESP_OK);
}

esp_err_t
spi_device_acquire_bus(spi_device_handle_t device, TickType_t wait)
{
	(void) device; (void) wait;
	s_m5_spi_bus_refcnt++;		/* 参照カウントのみ（単独使用前提） */
	return(ESP_OK);
}

void
spi_device_release_bus(spi_device_handle_t dev)
{
	(void) dev;
	if (s_m5_spi_bus_refcnt > 0) {
		s_m5_spi_bus_refcnt--;
	}
}

esp_err_t
spi_bus_remove_device(spi_device_handle_t device)
{
	(void) device;
	return(ESP_OK);
}

esp_err_t
spi_bus_free(spi_host_device_t host_id)
{
	(void) host_id;
	return(ESP_OK);
}

#endif /* !M5_SDSPI */