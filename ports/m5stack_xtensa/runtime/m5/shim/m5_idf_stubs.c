/*
 *  M5GFX 用 ESP-IDF ミニスタブ群（M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  1-5 link probe で未定義だった ESP-IDF 関数のうち，SPI/GPIO/カーネル以外を
 *  まとめる：i2c_new/del_master_bus・ledc_*・nvs_*・esp_cache_msync・
 *  esp_efuse_get_pkg_ver・rtc_clk_cpu_freq_get_config・esp_log*。いずれも本物の
 *  ドライバ（esp_pm/割込みアロケータ依存）をリンクしないための最小実装。
 */

#include <stdint.h>
#include <stdarg.h>
#include <esp_err.h>
#include <driver/i2c_master.h>
#include <driver/ledc.h>
#include <nvs.h>
#include <esp_log.h>
#include <soc/rtc.h>
#include <soc/soc.h>
#include "m5_periph_clk.h"

#include "m5_stub_trace.h"			/* ★S2: M5_STUB_HIT（黙る no-op を作らない） */

#ifndef TOPPERS_S3_CPU_FREQ_MHZ
#define TOPPERS_S3_CPU_FREQ_MHZ	160
#endif

/*
 *  ------------------------------------------------------------------
 *  periph_rcc_enter/exit：M5GFX(Bus_SPI) の PERIPH_RCC_ATOMIC マクロが使う
 *  クロックゲート RMW 用クリティカルセクション（1-5 link probe で唯一必要な
 *  periph_ctrl 由来シンボル）。
 *  ------------------------------------------------------------------
 *  esp/wifi/hal_src/periph_ctrl.c 本体は hal_stub_include（libc スタブが C++ を
 *  壊す）に依存するため M5 では使えない。中身は「共有スピンロックの取得/解放」
 *  だが，M5 は単一コア・main_task 単一文脈でこれらが走るのは初期化時のみ・
 *  対象レジスタへ並行アクセスする ISR も無いため no-op で安全（＝実質競合なし）。
 */
/*  ★S2：この 2 本にだけ M5_STUB_HIT を付けていない。理由：これらは「デバイスへの
 *  操作」ではなく「共有スピンロックの取得/解放」であり、no-op でも失われる
 *  ドライバ手順が無い（上のコメントの根拠）。段階1 の 4 件（1-6/1-7/1-8/1-16）は
 *  いずれも**デバイス設定の一手順の欠落**で、この 2 本はその型に当たらない。
 *  加えて M5GFX の SPI 設定 RMW ごとに呼ばれるため、計装すると出力が増えるだけで
 *  診断価値が無い。 */
#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
void periph_rcc_enter(void) { }
void periph_rcc_exit(void)  { }
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ------------------------------------------------------------------
 *  I2C（新 API・handle のみ）
 *  ------------------------------------------------------------------
 *  M5GFX CoreS3 経路は i2c_new_master_bus でバスを確保するだけで，実際の
 *  トランザクションは register 直（I2C0/I2C1 構造体）で行う（1-5 実測：
 *  i2c_master_transmit/receive/add_device は未参照）。本スタブはバスの
 *  I2C ペリフェラルクロックを有効化し，ダミーハンドルを返す。
 */
static uint8_t	s_m5_i2c_bus_dummy[2];

/*  ★1-12：到達点マーカ（m5_display_smoke.c・即時出力／logtask 非経由）。
 *  `_light->init(0)`（AW9523B バックライト＝I2C）まで来たかの判定に使う。 */
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);

esp_err_t
i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
				   i2c_master_bus_handle_t *ret_bus_handle)
{
	int	port = 0;

	if (bus_config != NULL) {
		port = (int)bus_config->i2c_port;
	}
	m5_mark_u32("[MK] i2c_new_master_bus in  port=", 0x30U, (unsigned int) port);
	/*  register 直アクセスが効くよう I2C ペリフェラルクロックを有効化（SYSTEM 直）。 */
	if (port == 1) {
		REG_SET_BIT(M5_PERIP_CLK_EN_REG, M5_I2C_EXT1_CLK_EN);
		REG_CLR_BIT(M5_PERIP_RST_EN_REG, M5_I2C_EXT1_RST);
	}
	else {
		REG_SET_BIT(M5_PERIP_CLK_EN_REG, M5_I2C_EXT0_CLK_EN);
		REG_CLR_BIT(M5_PERIP_RST_EN_REG, M5_I2C_EXT0_RST);
	}
	if (ret_bus_handle != NULL) {
		int idx = (port == 1) ? 1 : 0;
		*ret_bus_handle = (i2c_master_bus_handle_t)&s_m5_i2c_bus_dummy[idx];
	}
	m5_mark_u32("[MK] i2c_new_master_bus out port=", 0x31U, (unsigned int) port);
	return(ESP_OK);
}

esp_err_t
i2c_del_master_bus(i2c_master_bus_handle_t bus_handle)
{
	(void) bus_handle;
	m5_mark_u32("[MK] i2c_del_master_bus     =", 0x32U, 0U);
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  LEDC（バックライト PWM）
 *  ------------------------------------------------------------------
 *  CoreS3 のバックライトは AW9523B(I2C) 経由なので LEDC は使わない。参照は
 *  Light_PWM.cpp（他ボード用・S3 では未実行経路）からのみ。no-op（ESP_OK）。
 *  design.md §4：初期は GPIO ON/OFF 代替で足りる。
 */
/*  LX6 では m5_lx6_ledc.c が本物を持つ（M5Stack Basic のバックライトは
 *  LEDC の PWM で点く。スタブのままだと画面が真っ暗になる）。 */
#if !defined(TOPPERS_ESP32_LX6)
esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf)
{ M5_STUB_HIT("ledc_timer_config"); (void) timer_conf; return(ESP_OK); }

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf)
{ M5_STUB_HIT("ledc_channel_config"); (void) ledc_conf; return(ESP_OK); }

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty)
{ M5_STUB_HIT("ledc_set_duty"); (void) speed_mode; (void) channel; (void) duty; return(ESP_OK); }

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel)
{ M5_STUB_HIT("ledc_update_duty"); (void) speed_mode; (void) channel; return(ESP_OK); }
#endif /* !defined(TOPPERS_ESP32_LX6) */

/*
 *  ------------------------------------------------------------------
 *  NVS（タッチ校正値等の保存）— 常に「未保存」を返し既定値で動かす
 *  ------------------------------------------------------------------
 */
#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
	M5_STUB_HIT("nvs_open");
	(void) name; (void) open_mode;
	if (out_handle != NULL) { *out_handle = 0; }
	return(ESP_ERR_NVS_NOT_FOUND);
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
	M5_STUB_HIT("nvs_get_u32");
	(void) handle; (void) key; (void) out_value;
	return(ESP_ERR_NVS_NOT_FOUND);
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
	M5_STUB_HIT("nvs_set_u32");
	(void) handle; (void) key; (void) value;
	return(ESP_OK);
}

void nvs_close(nvs_handle_t handle)
{ M5_STUB_HIT("nvs_close"); (void) handle; }
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ------------------------------------------------------------------
 *  その他の ESP-IDF ambient
 *  ------------------------------------------------------------------
 */

/*  DMA キャッシュ同期：S3 の M5GFX 現行実装は明示書戻しを要さない（1-4 M0 調査。
 *  そもそも P4 専用経路。CPU 書込み経路では no-op で安全）。 */
esp_err_t esp_cache_msync(void *addr, size_t size, int flags)
{ M5_STUB_HIT("esp_cache_msync"); (void) addr; (void) size; (void) flags; return(ESP_OK); }

/*  チップパッケージ版数：CoreS3 の autodetect 判定には未使用（0 で無害）。 */
uint32_t esp_efuse_get_pkg_ver(void)
{ M5_STUB_HIT("esp_efuse_get_pkg_ver"); return(0U); }

/*  CPU クロック設定取得：M5GFX は SPI クロック分周計算に freq_mhz を使う。
 *  プロジェクト既定（160MHz・PLL）を返す。 */
void rtc_clk_cpu_freq_get_config(rtc_cpu_freq_config_t *out_config)
{
	M5_STUB_HIT("rtc_clk_cpu_freq_get_config");
	if (out_config == NULL) { return; }
	out_config->source = SOC_CPU_CLK_SRC_PLL;
	out_config->source_freq_mhz = 480;					/* CPU PLL（S3） */
	out_config->div = 480U / TOPPERS_S3_CPU_FREQ_MHZ;
	out_config->freq_mhz = TOPPERS_S3_CPU_FREQ_MHZ;
}

/*
 *  ------------------------------------------------------------------
 *  ログ（esp_log v2 の可変長 API・esp_log_timestamp）
 *  ------------------------------------------------------------------
 *  従来は no-op だったが、それにより M5GFX の [Autodetect] 系ログ（パネル
 *  検出の唯一の内部証跡）が全滅していた。切り分けに使えるよう FMP3 の
 *  低レベルログ出力へ転送する。
 *
 *  ★なぜ syslog() マクロへ横流ししないか
 *    (1) FMP3 の syslog() は syslog_0..syslog_5 の**可変引数5個まで**しか無く、
 *        printf 形式の任意引数を取る esp_log をそのまま被せられない。
 *    (2) FMP3 の syslog はフォーマット文字列と %s 引数を**ポインタのまま**
 *        ログバッファへ積み、logtask が後で整形する（遅延整形）。ここで
 *        スタック上の一時バッファを渡すと、整形時には既に無効なポインタになる。
 *  ★なぜ vsnprintf を使わないか
 *    M5 構成は esp/shim/esp_shim_libc.c をリンクしておらず、ROM newlib にも
 *    vsnprintf の実体は無い（esp_shim_libc.c 冒頭コメント参照）。使うと
 *    undefined reference になる。
 *  → 採った方法：FMP3 自身の整形エンジンを直接使う。
 *      tt_syslog()   … va_list を SYSLOG レコード（logpar[0]=format,
 *                      logpar[1..5]=引数）へ詰める（library/vasyslog.c）
 *      syslog_printf() … そのレコードを putc コールバックで**即時**出力する
 *                      （library/log_output.c）
 *    出力先 putc は target_fput_log（arch/xtensa_gcc/esp32s3/chip_serial.c）で、
 *    これは syssvc/syslog.c 自身が低レベル出力に使うのと同じ関数＝アプリの
 *    syslog() 出力と同じ経路・同じ順序で出る。整形が即時なのでバッファ寿命
 *    問題は起きず、追加のスタックも使わない（SYSLOG レコードは 28B 程度）。
 *    引数が5個を超えた分は syslog_printf が '%' をそのまま出力するだけで、
 *    配列外参照はしない（i >= TNUM_LOGPAR-1 のガード）。
 *
 *  ★出力量（USJ を溢れさせない）
 *    本構成の実測：
 *      esp/config/esp32s3/sdkconfig.h  CONFIG_LOG_VERSION = 1
 *      m5/idf_compat/nuttx/config.h CONFIG_ESPRESSIF_LOG_LEVEL = 3(INFO)
 *        → CONFIG_LOG_MAXIMUM_LEVEL も 3 なので ESP_LOGD/ESP_LOGV は
 *          **コンパイル時に消える**（M5GFX.cpp:157 の i2c 全アドレス走査ログ
 *          ＝ESP_LOGV、:758 の read cmd ログ＝ESP_LOGD が該当）。実際に出るのは
 *          ESP_LOGI 以上＝[Autodetect] のボード判定結果など数行で、USJ は溢れない。
 *      CONFIG_LOG_COLORS 未定義 → LOG_COLOR_* は空文字列＝ANSI エスケープは出ない。
 *    下の M5_LOG_LEVEL は**実行時**の保険（1=ERROR … 5=VERBOSE、既定 3）。
 *    D/V を実際に出したい場合は CONFIG_ESPRESSIF_LOG_LEVEL 側も上げる必要がある
 *    （そちらがコンパイル時ゲートだから）。
 *
 *  ★タグの前置は LOG_VERSION で分岐する
 *    v1 では ESP_LOG_LEVEL マクロが format を LOG_FORMAT() で
 *    `"I (%u) %s: " <ユーザ書式> "\n"` に包み、可変引数の先頭2つが
 *    timestamp と tag になる＝タグは format 側に既に入っている。
 *    v2 では format がユーザ書式そのもので、タグ付けは esp_log() の責務。
 *    そこで v2 のときだけ "[tag] " を自前で前置する。
 */
#include <t_stddef.h>
#include <t_syslog.h>
#include <log_output.h>
#include <target_syssvc.h>			/* target_fput_log */

#ifndef M5_LOG_LEVEL
#define M5_LOG_LEVEL	3			/* ESP_LOG_INFO */
#endif

static void
m5_log_puts(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s++);
	}
}

#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
void esp_log(esp_log_config_t config, const char *tag, const char *format, ...)
{
	SYSLOG		logbuf;
	va_list		ap;
	const char *p;
	char		last;

	if (format == NULL) {
		return;
	}
	/*  レベル絞り込み（ESP_LOG_NONE=0 … ESP_LOG_VERBOSE=5）。 */
	if ((int) config.opts.log_level > (int) M5_LOG_LEVEL) {
		return;
	}

#if !defined(CONFIG_LOG_VERSION) || (CONFIG_LOG_VERSION >= 2)
	if (tag != NULL) {
		target_fput_log('[');
		m5_log_puts(tag);
		m5_log_puts("] ");
	}
#else
	(void) tag;					/* v1 は format 側にタグが埋め込まれている */
	(void) m5_log_puts;
#endif

	va_start(ap, format);
	tt_syslog(&logbuf, format, ap);
	va_end(ap);
	syslog_printf(format, &(logbuf.logpar[1]), target_fput_log);

	/*  ESP_LOG_VERSION=1 経路の書式は末尾に '\n' を含む。二重改行を避ける。 */
	last = '\0';
	for (p = format; *p != '\0'; p++) {
		last = *p;
	}
	if (last != '\n') {
		target_fput_log('\n');
	}
}

uint32_t esp_log_timestamp(void)
{ return(0U); }
#endif /* !M5_USE_ESP_SHIM */
