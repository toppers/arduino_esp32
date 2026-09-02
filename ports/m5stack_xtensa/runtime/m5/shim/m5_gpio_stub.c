/*
 *  M5GFX 用 GPIO ミニスタブ（レジスタ直・M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  M5GFX（common.cpp / Bus_SPI.cpp）が DC/CS/RST 等の制御に使う ESP-IDF
 *  gpio_* API のうち，1-5 link probe で未定義だったもの：gpio_set_level /
 *  gpio_get_level / gpio_set_direction / gpio_reset_pin / gpio_set_pull_mode /
 *  gpio_iomux_out。本物の esp_driver_gpio（割込みアロケータ・esp_pm 依存）を
 *  リンクせず，hal/gpio_ll.h のインライン（レジスタ直）を薄くラップする。
 *  （gpio_matrix_out / esp_rom_gpio_connect_* は S3 ROM=rom.api.ld で解決。）
 */

#include <stdint.h>
#include <driver/gpio.h>
#include <hal/gpio_ll.h>

#define M5_GPIO_HW	GPIO_LL_GET_HW(0)

/*
 *  ★1-10 診断計装（一時）：OD 制御を入れると起動 ~5 秒でコンソールが死ぬ回帰を
 *  A-B-A で確認した（OD 修正だけ外すと完走）。どの呼出しで死ぬかを特定するため，
 *  gpio_set_direction / gpio_reset_pin の呼出しを pin と mode 付きで記録する。
 *  M5_GPIO_TRACE を未定義にすれば完全に消える（既定=有効／原因特定後に外す）。
 */
/* #define M5_GPIO_TRACE */	/* 原因特定済みにつき無効化（出力量削減） */
#ifdef M5_GPIO_TRACE
extern void m5_log_u32(const char *msg, unsigned int v);
/*  pin と mode を 1 語に詰める：上位16bit=mode，下位16bit=pin。 */
#define M5_TRACE(tag, pin, mode) \
	m5_log_u32((tag), ((unsigned int)(mode) << 16) | ((unsigned int)(pin) & 0xFFFFU))
#else
#define M5_TRACE(tag, pin, mode)	((void) 0)
#endif

/*
 *  ★1-13：呼出し回数カウンタ（周期ハンドラが毎秒ダンプ）。
 *  lgfx の I2C/SPI ビットバングはここを大量に叩くので、毎秒増え続けるなら
 *  「GPIO を叩くループを回り続けている」と分かる（＝単一箇所での固着ではない）。
 *  出力はしない（ホットパス）。
 */
extern volatile unsigned int m5_ctr[];
#define M5_CTR_GPIO		7

/*
 *  ==================================================================
 *  ★1-15：ピン付き到達点マーカと「観測ピン保護」
 *  ==================================================================
 *  【ckpt】m5_ckpt_set は出力しない到達点マーカで、周期ハンドラが
 *    `[HB] ckpt id<<16|seq` として毎秒 1 行だけ再出力する。ここに**ピン番号を
 *    載せた id** を打つと、begin() 内で止まっても「最後に触れた GPIO はどのピンか」
 *    が分かる。id 帯は既存（0x10/0x11/0x20/0x21/0x30-0x32/0x40-0x42/0x50/0x51）と
 *    衝突しないよう 0x1000 以上を使う。
 *
 *  【観測ピン保護（★これは診断であると同時に現行バグへの防御でもある）】
 *    本アプリは m5_route_u0txd_portc()（m5_display_smoke.c:69-89）で
 *    **GPIO17/18 に U0TXD を複製**し、実機ログはそこから外部 UART で採っている。
 *    ところが本ファイルの gpio_set_direction は E-1 修正で
 *      gpio_ll_matrix_out_default()  ＝ func_out_sel ← SIG_GPIO_OUT_IDX
 *      gpio_ll_func_sel(PIN_FUNC_GPIO)
 *    を行うようになっており、gpio_reset_pin は gpio_ll_output_disable() を行う。
 *    **これらが GPIO17 か 18 に対して呼ばれた瞬間、観測チャネルは切断される**
 *    （＝以後どこで何が起きても「ログが途絶した」としか見えなくなる）。
 *    M5GFX の S3 autodetect 経路で 17/18 を使うのは AtomS3 の分岐
 *    （M5GFX.cpp:2040-2044）で、CoreS3 が先に確定すれば通らない――が、
 *    それは**読んで得た推測**であって実測ではない。推測に観測を賭けないため、
 *    17/18 への破壊的操作は無条件に阻止し、回数だけ数えて可視化する。
 *    ★機能への影響：本構成で 17/18 を機能目的に使うコードは存在しない
 *      （I2C=11,12／SPI=35,36,37／CS=3／SD_CS=4）。よって阻止しても失うものは無い。
 *      逆に m5_gpio_console_guard が 0 でなければ「M5GFX が観測ピンを触った」＝
 *      run G/H/J/K/L/M の途絶を説明し得る**新しい事実**が 1 個得られる。
 */
extern void m5_ckpt_set_quiet(unsigned int id);

volatile unsigned int	m5_gpio_console_guard;	/* 観測ピンへの破壊的操作を阻止した回数 */

#define M5_CON_PIN_A	17
#define M5_CON_PIN_B	18

static inline int
m5_is_console_pin(gpio_num_t gpio_num)
{
	return((int)gpio_num == M5_CON_PIN_A || (int)gpio_num == M5_CON_PIN_B);
}

/*  ckpt id 帯（下位 8bit にピン番号を載せる）。 */
#define M5_CKPT_SETDIR	0x1000U
#define M5_CKPT_SETLVL	0x1100U
#define M5_CKPT_GETLVL	0x1200U
#define M5_CKPT_RESET	0x1300U
#define M5_CKPT_PULL	0x1400U
#define M5_CKPT_IOMUX	0x1500U

esp_err_t
gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
	m5_ctr[M5_CTR_GPIO]++;
	m5_ckpt_set_quiet(M5_CKPT_SETLVL | (unsigned int)gpio_num);
	if (m5_is_console_pin(gpio_num)) {
		m5_gpio_console_guard++;		/* 観測ピンは駆動させない */
		return(ESP_OK);
	}
	gpio_ll_set_level(M5_GPIO_HW, (uint32_t)gpio_num, level);
	return(ESP_OK);
}

int
gpio_get_level(gpio_num_t gpio_num)
{
	m5_ctr[M5_CTR_GPIO]++;
	m5_ckpt_set_quiet(M5_CKPT_GETLVL | (unsigned int)gpio_num);
	return(gpio_ll_get_level(M5_GPIO_HW, (uint32_t)gpio_num));
}

esp_err_t
gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
	M5_TRACE("[TRACE-GPIO] set_direction 入口 (hi16=mode,lo16=pin)=", gpio_num, mode);
	m5_ckpt_set_quiet(M5_CKPT_SETDIR | (unsigned int)gpio_num);
	if (m5_is_console_pin(gpio_num)) {
		/*  ★観測チャネル（U0TXD 複製）を切らない。上の節の理由による。 */
		m5_gpio_console_guard++;
		return(ESP_OK);
	}
	if (mode & GPIO_MODE_DEF_INPUT) {
		gpio_ll_input_enable(M5_GPIO_HW, (uint32_t)gpio_num);
	}
	else {
		gpio_ll_input_disable(M5_GPIO_HW, (uint32_t)gpio_num);
	}
	if (mode & GPIO_MODE_DEF_OUTPUT) {
		/*
		 *  ★1-11（E-1 修正・現行バグ）：実 IDF の gpio_output_enable と手順を揃える。
		 *
		 *  実 ESP-IDF の gpio_set_direction は OUTPUT 時に gpio_output_enable を呼び、
		 *  その中で 3 手順を行う（esp-idf/components/esp_driver_gpio/src/gpio.c:222-229）:
		 *    (1) gpio_ll_matrix_out_default  … func_out_sel ← SIG_GPIO_OUT_IDX
		 *                                      （＝周辺器出力をパッドから切離し、
		 *                                        GPIO_OUT レジスタで駆動できる状態にする）
		 *    (2) gpio_ll_output_enable       … OE を立てる
		 *    (3) gpio_ll_func_sel(PIN_FUNC_GPIO) … IO_MUX を GPIO へ
		 *                                      （さもないと OE を周辺器しか制御できない）
		 *  本スタブは長らく (2) しか行っていなかった。
		 *
		 *  【これが引き起こしていた現行バグ】
		 *    lgfx の I2C バスクリア（common.cpp:1244-1279 の i2c_stop）は
		 *      gpio_set_direction(scl, GPIO_MODE_OUTPUT_OD) → gpio_set_level(...) を 9 回
		 *    で STOP コンディションをビットバングする。ところが直前の
		 *    lgfx::i2c::set_pin（common.cpp:1170-1199）が func_out_sel を
		 *    I2CEXT*_SCL_OUT/SDA_OUT へ向けたままなので、(1) が無いと
		 *    **パッドは I2C ペリフェラルに駆動され続け、gpio_set_level が電気的に
		 *    何の効果も持たない**＝バスクリアが丸ごと no-op だった。
		 *
		 *  【なぜ SPI ルーティングを壊さないか（呼出し元を全て確認済み）】
		 *    (1) は func_out_sel を既定へ戻すので、「配線した後に set_direction を
		 *    呼ぶ」経路があると SPI 信号が切れる。本構成の OUTPUT 系呼出しは全て
		 *    **set_direction が先・配線が後**であることを確認した:
		 *      - m5_spi_wire_out（本 shim）      : set_direction → connect_out_signal(FSPI*)
		 *                                         ＝実 IDF spi_common.c:663-674 と同順
		 *      - m5_spi_wire_out 経由の CS 配線  : 同上（spi_bus_add_device）
		 *      - lgfx i2c::set_pin               : set_direction → connect_out_signal(I2C*)
		 *      - lgfx i2c_stop                   : set_direction → gpio_set_level（★これを直す）
		 *      - app の DIAG-LOOP (B)            : set_direction → connect_out_signal(SIG_GPIO_OUT)
		 *    lgfx の Bus_SPI は gpio_set_direction を一切呼ばず pinMode/gpio_lo/hi のみ
		 *    （pinMode 自身が func_out_sel←SIG_GPIO_OUT_IDX を行う＝同じ意味論）。
		 *    したがって「配線後に set_direction」経路は存在せず、SPI は壊れない。
		 */
		gpio_ll_matrix_out_default(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_output_enable(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_func_sel(M5_GPIO_HW, (uint8_t)gpio_num, PIN_FUNC_GPIO);
	}
	else {
		/*  ★非 OUTPUT 側は意図的に実 IDF より控えめにしてある。
		 *  実 IDF の gpio_output_disable は output_disable に加えて
		 *  set_output_enable_ctrl(false,false) と func_sel(PIN_FUNC_GPIO) も行うが、
		 *  本構成でこの枝を通るのは（E-2 修正後は）lgfx の一部経路のみで、
		 *  IO_MUX を触る副作用の実害が検証できていない。必要になるまで足さない
		 *  （「IDF がやらない一手順を足して回帰を疑われた」轍を踏まないため、
		 *   逆に「IDF がやる手順を省いている」ことをここに明記しておく）。 */
		gpio_ll_output_disable(M5_GPIO_HW, (uint32_t)gpio_num);
	}
	/*
	 *  ★**open-drain を必ず解除すること。**
	 *
	 *  実 ESP-IDF の gpio_set_direction は必ず
	 *    if (mode & GPIO_MODE_DEF_OD) gpio_od_enable(); else gpio_od_disable();
	 *  を行う（esp-idf/components/esp_driver_gpio/src/gpio.c）。この一手順を欠くと，
	 *  直前に lgfx の pinMode が立てた open-drain が残留する
	 *  （lgfx は非 output モードで GPIO.pin[n].pad_driver=1 を立て復元しない。
	 *   M5GFX/src/lgfx/v1/platforms/esp32/common.cpp:460-462）。
	 *
	 *  M5GFX の CoreS3 autodetect は SPI 初期化「前」に GPIO35/36/37 を pull-up probe
	 *  するため，SCLK(36)/MOSI(37) が OD のままパネル ID 読取へ入り，High 側が内部弱
	 *  プルアップ頼みで実効クロック/データがパネルへ届かなくなる。
 */
	if (mode & GPIO_MODE_DEF_OD) {
		M5_TRACE("[TRACE-GPIO]   od_ENABLE 直前 pin=", gpio_num, 0);
		gpio_ll_od_enable(M5_GPIO_HW, (uint32_t)gpio_num);
	}
	else {
		M5_TRACE("[TRACE-GPIO]   od_DISABLE 直前 pin=", gpio_num, 0);
		gpio_ll_od_disable(M5_GPIO_HW, (uint32_t)gpio_num);
	}
	M5_TRACE("[TRACE-GPIO] set_direction 出口 pin=", gpio_num, 0);
	return(ESP_OK);
}

esp_err_t
gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
	m5_ckpt_set_quiet(M5_CKPT_PULL | (unsigned int)gpio_num);
	if (m5_is_console_pin(gpio_num)) {
		m5_gpio_console_guard++;
		return(ESP_OK);
	}
	switch (pull) {
	case GPIO_PULLUP_ONLY:
		gpio_ll_pullup_en(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_pulldown_dis(M5_GPIO_HW, (uint32_t)gpio_num);
		break;
	case GPIO_PULLDOWN_ONLY:
		gpio_ll_pullup_dis(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_pulldown_en(M5_GPIO_HW, (uint32_t)gpio_num);
		break;
	case GPIO_PULLUP_PULLDOWN:
		gpio_ll_pullup_en(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_pulldown_en(M5_GPIO_HW, (uint32_t)gpio_num);
		break;
	case GPIO_FLOATING:
	default:
		gpio_ll_pullup_dis(M5_GPIO_HW, (uint32_t)gpio_num);
		gpio_ll_pulldown_dis(M5_GPIO_HW, (uint32_t)gpio_num);
		break;
	}
	return(ESP_OK);
}

esp_err_t
gpio_reset_pin(gpio_num_t gpio_num)
{
	/*  出力を切り，入力＋プルアップの既定状態へ（IDF gpio_reset_pin 相当の要点）。 */
	M5_TRACE("[TRACE-GPIO] reset_pin 入口 pin=", gpio_num, 0);
	m5_ckpt_set_quiet(M5_CKPT_RESET | (unsigned int)gpio_num);
	if (m5_is_console_pin(gpio_num)) {
		m5_gpio_console_guard++;
		return(ESP_OK);
	}
	gpio_ll_output_disable(M5_GPIO_HW, (uint32_t)gpio_num);
	gpio_ll_func_sel(M5_GPIO_HW, (uint8_t)gpio_num, PIN_FUNC_GPIO);
	gpio_ll_input_enable(M5_GPIO_HW, (uint32_t)gpio_num);
	gpio_ll_pullup_en(M5_GPIO_HW, (uint32_t)gpio_num);
	gpio_ll_pulldown_dis(M5_GPIO_HW, (uint32_t)gpio_num);
	/*
	 *  ★1-10：open-drain の解除（B-1 と同型の残留を防ぐ defensive）。
	 *
	 *  ★事実の明記：**実 ESP-IDF の gpio_reset_pin は OD を解除しない**
	 *  （esp-idf/components/esp_driver_gpio/src/gpio.c の gpio_reset_pin には
	 *   gpio_od_disable 相当の呼出しが無いことを実ソースで確認済み。intr_disable /
	 *   pullup_en / pulldown_dis / input_disable / output_disable / func_sel のみ）。
	 *  したがってこの一行は「IDF 仕様への追従」ではなく **意図的な逸脱**である。
	 *
	 *  それでも入れる理由：本関数は「ピンを安全な既定へ戻す」ヘルパとして使われており，
	 *  lgfx の pinMode が立てた pad_driver=1 を持ち越すと B-1（1-10 で実機確定）と同型の
	 *  「OD 残留で信号がパッドに出ない」不具合を再発させ得るため。副作用は
	 *  「IDF より OD を落とす方向」のみで，OD を要求する用途（I2C 等）は
	 *  gpio_set_direction(GPIO_MODE_*_OD) 側で明示的に再設定されるので実害がない。
	 */
	gpio_ll_od_disable(M5_GPIO_HW, (uint32_t)gpio_num);
	return(ESP_OK);
}

/*
 *  gpio_iomux_out：指定ピンの IO_MUX 機能選択を func にし，GPIO 出力を有効化。
 *  M5GFX(Bus_SPI) が SPI ピンを IO_MUX 直結する高速経路で使う。
 */
void
gpio_iomux_out(uint8_t gpio_num, int func, bool oen_inv)
{
	(void) oen_inv;
	m5_ckpt_set_quiet(M5_CKPT_IOMUX | (unsigned int)gpio_num);
	if (m5_is_console_pin((gpio_num_t)gpio_num)) {
		m5_gpio_console_guard++;
		return;
	}
	gpio_ll_func_sel(M5_GPIO_HW, gpio_num, (uint32_t)func);
	gpio_ll_output_enable(M5_GPIO_HW, (uint32_t)gpio_num);
}
