/*
 *  M5Unified 用 ESP-IDF ミニスタブ群（M5 ビルド専用・段階2 / S2）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  ★このファイルに入れてよいもの／入れてはいけないもの（S2 で確立した規律）
 *  ============================================================================
 *
 *  【入れてよい】`M5.begin()` を実際に呼ぶアプリで **実リンク時に実際に
 *  未定義になった**シンボルだけ。
 *
 *  【入れてはいけない】まだ未定義になっていないシンボル
 *  （nvs_set_i32 / sleep・wakeup / gpio_wakeup / pthread など）。
 *  ★理由：**未定義のまま置くと、それを呼ぶコードが到達可能になった瞬間に
 *    リンクが失敗する＝fail-closed で必ず気付ける。**先回りしてスタブを置くと、
 *    その瞬間から「黙って失敗する機能」に変わる。
 *
 *  【全スタブ共通の義務】M5_STUB_HIT()（m5/shim/m5_stub_trace.h）を必ず通す。
 *  初回呼び出しで即時 1 行（m5_putc_now_ext＝**有界**・logtask 非経由）が出る。
 *  「呼ばれたのに何の痕跡も残らないスタブ」は本ファイルに存在しない。
 *  ★本ファイル内の即時出力は全て m5_putc_now_ext を使うこと（target_fput_log 直は
 *    A1_CONSOLE_USJ=OFF で無界スピン。m5_stub_trace.h:55-67・A-2 是正）。
 *
 *  【返り値の方針】機能が無いものは **失敗を返す**のが既定
 *  （ESP_ERR_NOT_SUPPORTED / ESP_ERR_NVS_NOT_FOUND / -1）。ESP_OK を返すのは
 *  「本当に何もしなくても上位の意味が保たれる」場合だけで、各所に理由を書く。
 *  本ファイルには ESP_OK を返すスタブは **1 本も無い**。
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>

#include <esp_err.h>
#include <nvs.h>
#include <driver/rmt_common.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>

#include "m5_stub_trace.h"

/*
 *  ============================================================================
 *  NVS（IMU オフセットの読み出し）
 *  ============================================================================
 *  IMU_Class.cpp:351-366 `loadOffsetFromNVS()`。既存の nvs_open スタブ
 *  （m5_idf_stubs.c）が ESP_ERR_NVS_NOT_FOUND を返すので、実行時はここへ
 *  到達しない（リンク上の参照だけが残る）。到達した場合も「保存値なし」＝
 *  M5Unified 側が既定オフセットのまま動く、が正しい意味なので **失敗を返す**。
 *  ★nvs_set_i32 は S2 時点で **未定義のまま**（保存経路 saveOffsetToNVS が
 *    到達可能でないため）。上のヘッダコメントの方針どおり先回りしない。
 */
esp_err_t
nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *out_value)
{
	M5_STUB_HIT("nvs_get_i32");
	(void) handle; (void) key;
	if (out_value != NULL) {
		*out_value = 0;		/* 呼ばれた場合でも未初期化値を返さない */
	}
	return(ESP_ERR_NVS_NOT_FOUND);
}

/*
 *  ============================================================================
 *  RMT（RGB LED strip 駆動）— 全て失敗を返す
 *  ============================================================================
 *  M5Unified の `LedBus_RMT::init()`（utility/led/LED_Strip_Class.cpp:120-160）は
 *  **`rmt_new_tx_channel()` が ESP_OK を返さなければ、他の rmt_* を一切呼ばずに
 *  `false` を返して LED strip 機能を無効化する**（実ソース確認済み）。したがって
 *  rmt_new_tx_channel を失敗させるのが最小かつ安全な断ち方である。
 *
 *  ★CoreS3 には RGB LED が無い（`_pin_table_other0` にエントリ無し＝rgb_led=-1、
 *     §4）ので、そもそも init() は pin_data<0 で先に抜ける。
 *  ★残り 10 本も **失敗を返す**（ESP_OK にしない）。上記の早期 return により
 *    到達しないはずだが、到達したら「RMT ドライバが無い」ことが上位へ伝わる
 *    べきであり、成功を装うと LED を書いたつもりで黙って何も光らない状態になる。
 *  ★将来 RMT を持つ M5 ボード（AtomS3 等）へ広げるときは、ここを本物の
 *    ドライバへ差し替えること。M5_STUB_HIT のログが「まだスタブだ」と告げる。
 */
esp_err_t
rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan)
{
	M5_STUB_HIT("rmt_new_tx_channel");
	(void) config;
	if (ret_chan != NULL) {
		*ret_chan = NULL;
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_del_channel(rmt_channel_handle_t channel)
{
	M5_STUB_HIT("rmt_del_channel");
	(void) channel;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_enable(rmt_channel_handle_t channel)
{
	M5_STUB_HIT("rmt_enable");
	(void) channel;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_disable(rmt_channel_handle_t channel)
{
	M5_STUB_HIT("rmt_disable");
	(void) channel;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_transmit(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t encoder,
			 const void *payload, size_t payload_bytes,
			 const rmt_transmit_config_t *config)
{
	M5_STUB_HIT("rmt_transmit");
	(void) tx_channel; (void) encoder; (void) payload;
	(void) payload_bytes; (void) config;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_tx_wait_all_done(rmt_channel_handle_t tx_channel, int timeout_ms)
{
	M5_STUB_HIT("rmt_tx_wait_all_done");
	(void) tx_channel; (void) timeout_ms;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *config,
					  rmt_encoder_handle_t *ret_encoder)
{
	M5_STUB_HIT("rmt_new_bytes_encoder");
	(void) config;
	if (ret_encoder != NULL) {
		*ret_encoder = NULL;
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config,
					 rmt_encoder_handle_t *ret_encoder)
{
	M5_STUB_HIT("rmt_new_copy_encoder");
	(void) config;
	if (ret_encoder != NULL) {
		*ret_encoder = NULL;
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_del_encoder(rmt_encoder_handle_t encoder)
{
	M5_STUB_HIT("rmt_del_encoder");
	(void) encoder;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
rmt_encoder_reset(rmt_encoder_handle_t encoder)
{
	M5_STUB_HIT("rmt_encoder_reset");
	(void) encoder;
	return(ESP_ERR_NOT_SUPPORTED);
}

/*  エンコーダ用メモリ確保（本来は DMA 可能領域）。NULL を返すと
 *  `rmt_new_led_strip_encoder()` が ESP_FAIL を返す＝失敗が上位へ伝わる。 */
void *
rmt_alloc_encoder_mem(size_t size)
{
	M5_STUB_HIT("rmt_alloc_encoder_mem");
	(void) size;
	return(NULL);
}

/*
 *  ============================================================================
 *  ADC oneshot ＋ cali（バッテリ電圧の生読み）— 全て失敗を返す
 *  ============================================================================
 *  ★S1 の申し送りの訂正：事前調査は legacy API（adc1_get_raw / esp_adc_cal_*）を
 *    前提にしていたが、実際にコンパイルされる枝は **新 oneshot API** である
 *    （Power_Class.cpp:1336 `#if __has_include (<esp_adc/adc_oneshot.h>)`）。
 *
 *  M5Unified の `Power_Class::_getBatteryAdcRaw()`（Power_Class.cpp:1337-1344）は
 *    static adc_oneshot_unit_handle_t adc_handle;
 *    if (adc_handle == nullptr) {
 *      adc_oneshot_new_unit(&init_config, &adc_handle);
 *      if (adc_handle == nullptr) { return 0; }      ← ここで抜ける
 *  という形なので、**`adc_oneshot_new_unit` がハンドルを NULL のままにすれば
 *  以降の adc_* を一切呼ばずに 0（＝電圧不明）を返して終わる**。これが最小の
 *  断ち方であり、S1 §5-2 の「電圧不明を返させる」に合致する。
 *
 *  ★CoreS3 のバッテリ電圧は AXP2101（I2C・PMIC）経由で読むので、この ADC 経路は
 *    CoreS3 では実行されない見込み（PMIC 非搭載ボード用）。リンク上の参照だけが
 *    残る。実機で M5_STUB_HIT のログが出たら「CoreS3 が PMIC 経路に乗っていない」
 *    ことの証拠になる＝この計装自体が診断価値を持つ。
 *
 *  ★out 引数を必ず 0 で埋める理由：Power_Class.cpp:1367 は `int raw, volt;` と
 *    **未初期化**で宣言してから adc_oneshot_read/adc_cali_raw_to_voltage に渡す。
 *    上記の早期 return で到達しないはずだが、万一到達したときスタックのゴミが
 *    そのまま「電圧」になるのは最悪（もっともらしい嘘の値）なので 0 を書く。
 */
esp_err_t
adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config,
					 adc_oneshot_unit_handle_t *ret_unit)
{
	M5_STUB_HIT("adc_oneshot_new_unit");
	(void) init_config;
	if (ret_unit != NULL) {
		*ret_unit = NULL;		/* ★M5 側の早期 return を成立させる */
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

/*
 *  ★2026-08-21 追加：M5Unified 0.2.20 の `_getBatteryAdcRaw()`
 *  （Power_Class.cpp:1799）が `adc_oneshot_del_unit()` を呼ぶようになり、
 *  スタブが無いとリンクが `undefined reference` で失敗する
 *  （0.2.18 では参照されていなかった）。
 *  `adc_oneshot_new_unit` がハンドルを NULL のままにするので、実行時に
 *  ここへ来るのは M5 側が NULL ハンドルで解放を試みた場合だけである。
 */
esp_err_t
adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle)
{
	M5_STUB_HIT("adc_oneshot_del_unit");
	(void) handle;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel,
						   const adc_oneshot_chan_cfg_t *config)
{
	M5_STUB_HIT("adc_oneshot_config_channel");
	(void) handle; (void) channel; (void) config;
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
	M5_STUB_HIT("adc_oneshot_read");
	(void) handle; (void) chan;
	if (out_raw != NULL) {
		*out_raw = 0;
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config,
									 adc_cali_handle_t *ret_handle)
{
	M5_STUB_HIT("adc_cali_create_scheme_curve_fitting");
	(void) config;
	if (ret_handle != NULL) {
		*ret_handle = NULL;		/* M5 側は cali==nullptr なら raw を返す枝を持つ */
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

esp_err_t
adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage)
{
	M5_STUB_HIT("adc_cali_raw_to_voltage");
	(void) handle; (void) raw;
	if (voltage != NULL) {
		*voltage = 0;
	}
	return(ESP_ERR_NOT_SUPPORTED);
}

/*
 *  ============================================================================
 *  settimeofday（RTC → システム時刻の反映）— 失敗を返す
 *  ============================================================================
 *  `RTC_Class::setSystemTimeFromRtc()`（utility/RTC_Class.cpp:181）が呼ぶ。
 *  本ポートには POSIX のシステム壁時計が無い（FMP3 は相対時刻のみ）。
 *  **0（成功）を返すと「システム時刻が RTC に合った」という嘘になる**ので -1 を返す。
 *  ★M5Unified 側は返り値を見ていない（実ソース確認済み）ので、上位の挙動は
 *    どちらでも変わらない。それでも -1 にするのは、後で誰かが返り値を見たときに
 *    正しい判断ができるようにするため。
 *  ★errno は設定しない：errno は `*__errno()`＝newlib の _impure_ptr 経由で、
 *    本構成ではこれが未初期化（S0 で printf が LoadProhibited で落ちた原因そのもの。
 *    m5_libcxx_glue.c の自前 printf のコメント参照）。触ると落ちる。
 */
int
settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	M5_STUB_HIT("settimeofday");
	(void) tv; (void) tz;
	return(-1);
}

/*
 *  ============================================================================
 *  esp_restart — ★スタブではなく「実装」
 *  ============================================================================
 *  ここに置く理由：これは no-op スタブではなく**本物のリセット**だから。
 *  「機能が無いのに成功を装う」類ではないので、先回り禁止の原則（本ファイル冒頭）
 *  には抵触しない。逆に、うっかり no-op を足されると
 *  **powerOff() が黙って戻り、上位（M5Unified の呼び出し元）が電源が切れた前提で
 *  進んで暴走する**。先に正しい実装を置いて塞ぐ。
 *
 *  【実装】RTC_CNTL_OPTIONS0_REG の SW_SYS_RST（bit31、WO）を立ててシステム全体を
 *  ソフトリセットする。番地・ビット位置はマクロ名からの推測ではなく
 *  esp-idf submodule のヘッダ実物で確認した：
 *      components/soc/esp32s3/register/soc/rtc_cntl_reg.h:22  RTC_CNTL_OPTIONS0_REG
 *                                                             = DR_REG_RTCCNTL_BASE + 0x0
 *      同 :25  RTC_CNTL_SW_SYS_RST = BIT(31)   （:23 のコメントに "WO bitpos:[31]"）
 *
 *  【黙って戻らないこと】リセットが効かなかった場合に備えて末尾で無限ループする。
 *  esp_restart は __attribute__((noreturn)) 宣言なので、戻ること自体が契約違反。
 *  ★ここでログを先に出すのは必須：出力が出ないままリセットすると、実機では
 *    「勝手に再起動した」としか見えず原因の特定に丸一日かかる（段階1 の教訓）。
 *
 *  ★出力は **有界** の m5_putc_now_ext を使うこと。target_fput_log 直は
 *    A1_CONSOLE_USJ=OFF 構成で上限の無いスピンであり、UART0 の TX が掃けない
 *    状況では **リセット発行へ到達する前に putc 内で永久ハングする**。
 *    このスタブの存在理由（powerOff() の暴走防止＝確実にリセットを撃つこと）が、
 *    まさにそれが要る日に裏切られる。文字は落ちても m5_putc_now_ext は必ず戻る
 *    ので、リセットには到達する。
 */
void
esp_restart(void)
{
	static const char	msg[] = "[STUB] esp_restart(): RTC_CNTL SW_SYS_RST を発行する\n";
	const char			*p;

	M5_STUB_HIT("esp_restart");
	for (p = msg; *p != '\0'; p++) {
		m5_putc_now_ext(*p);
	}

	REG_WRITE(RTC_CNTL_OPTIONS0_REG,
			  REG_READ(RTC_CNTL_OPTIONS0_REG) | RTC_CNTL_SW_SYS_RST);

	/*  ここへ来たらリセットが効いていない。黙って戻らず止まる（noreturn 契約）。 */
	for (;;) {
	}
}
