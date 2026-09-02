/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  Wi-Fi blob（libnet80211.a／libpp.a／libcore.a／libphy.a）が直接
 *  参照する「グローバル変数」「osi経由でない裸のextern関数」の
 *  ASP3側実体をまとめたファイル（esp_wifi_adapter.cのwifi_osi_funcs_t
 *  経由の関数はesp_wifi_adapter.c側，libc相当はesp_shim_libc.c側）．
 *
 *  esp-hal-3rdpartyのNuttXポート（ref-esp32c3/nuttx）はBLE統合まで
 *  含む古い時点のblobを前提にしており，本リポジトリが取得した
 *  esp-hal-3rdparty@b90b183のblobが要求するシンボルの一部
 *  （g_misc_nvs・g_espnow_user_oui・mesh_sta_auth_expire_time・
 *  g_log_level等）はNuttX側に対応が無い＝ヘッダ宣言も無い．
 *  型・意味はesp-hal-3rdparty内の周辺API（esp_now_set_user_oui等）
 *  から妥当な推定で決めている．詳細・妥協点はdocs/wifi-shim.md参照．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <stdlib.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_err.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_private/esp_sleep_internal.h"

/*
 *  ------------------------------------------------------------------
 *  1. NVS（不揮発ストレージ）関連 — 無効化スタブ
 *  ------------------------------------------------------------------
 *
 *  NVSはこのポートでは未実装．
 *  g_misc_nvsはblob（ieee80211_*.o）がNULLチェック後にNVSハンドルとして
 *  使うポインタと推定（ヘッダ宣言が無いため型は要旨から妥当な範囲で
 *  決定）．NULL固定のため，blobのNVS依存経路（プロファイル保存・
 *  スキャン履歴の永続化等）は実行時に「NVS無し」として振る舞う想定．
 *  もし実際にはNULL非チェックで無条件デリファレンスする経路がある
 *  場合はクラッシュしうる＝実機検証時の既知リスク（未検証）．
 */
#ifndef TOPPERS_ESP32S3   /* S3はlibcore.a(misc_nvs.o)が本体を提供＝スタブ不要 */
/*  ★無印ESP32：g_misc_nvs を NULL にすると blob（cnx_sta_associated 等）が
 *  g_misc_nvs->field を「NULLチェック無し」で無条件デリファレンスして
 *  LoadProhibited(EXCCAUSE=28)になる（例：l32i a6,[g_misc_nvs+4] の後に beqz で
 *  field==0 を判定する経路＝ベースがNULLだと落ちるがゼロ実体なら安全にスキップ）。
 *  ゼロ化した実体バッファを指させ「空のNVS」として振る舞わせる。十分大きめに確保
 *  してblobが読む各オフセットが有効域に収まるようにする（NVS書込みは本ポートで
 *  無効＝バッファは読み取り0のまま）。 */
static uint32_t s_misc_nvs_store[64];	/* 256バイト・ゼロ化 */
void *g_misc_nvs = (void *) s_misc_nvs_store;

int
misc_nvs_init(void)
{
	/*  NVS未実装のため常に「初期化した体」で成功を返す（g_misc_nvsは
	 *  ゼロ化実体を指す静的初期化済み）。 */
	return(0);
}

void
misc_nvs_deinit(void)
{
	/*  no-op  */
}
#endif

/*
 *  ------------------------------------------------------------------
 *  2. ESP-NOW ユーザOUI — 未設定（全ゼロ）固定
 *  ------------------------------------------------------------------
 *
 *  esp_now_set_user_oui()/esp_now_get_user_oui()（esp_now.h）の
 *  バッキングストアと推定．3バイト固定（WIFI_OUI_LEN，
 *  esp_wifi_types_generic.hのvendor_oui[3]/wfa_oui[3]と同型）．
 *  ESP-NOWは本Wi-Fiスキャンデモのスコープ外のため，
 *  esp_now_set_user_oui()相当の設定APIは未実装＝常に全ゼロ．
 */
uint8_t g_espnow_user_oui[3] = { 0U, 0U, 0U };

/*
 *  ------------------------------------------------------------------
 *  3. メッシュ関連
 *  ------------------------------------------------------------------
 *
 *  ieee80211_sta.c（ieee80211_sta_new_state）が参照．ESP-MESH
 *  （libmesh.a）はリンク対象だが本デモでは有効化しない＝
 *  スキャン専用の通常STA遷移では「認証期限切れ猶予0」で影響しない
 *  よう0固定とする．
 */
#ifndef TOPPERS_ESP32S3   /* S3はlibmesh.a(mesh_parent.o)が提供 */
uint32_t mesh_sta_auth_expire_time = 0U;
#endif

/*
 *  ------------------------------------------------------------------
 *  4. ログレベル
 *  ------------------------------------------------------------------
 *
 *  ieee80211_debug.c（wifi_log）が参照する冗長度しきい値．ASP3の
 *  syslogは別途フィルタするため，blob側の冗長ログは最小（0=エラー
 *  のみ相当）にしておく．
 */
#ifndef TOPPERS_ESP32S3   /* S3はlibcore.aが提供 */
int g_log_level = 0;
#endif

/*
 *  ------------------------------------------------------------------
 *  5. ADC2較正リンク強制（no-op）
 *  ------------------------------------------------------------------
 *
 *  esp_hw_support/include/esp_private/adc_share_hw_ctrl.hのコメント
 *  どおり「ADC2較正コンストラクタをリンクさせるためだけの空関数」．
 *  本ビルドはADC較正ソース自体を取り込んでいないため，空実装のままで
 *  意味的に正しい（較正無し＝ADC2較正コンストラクタが存在しないため
 *  リンクすべき対象も無い）．
 */
void
adc2_cal_include(void)
{
	/*  no-op（意図通り．上記コメント参照）  */
}

/*
 *  ------------------------------------------------------------------
 *  6. eFuse鍵用途・HMAC — 未プロビジョニング固定
 *  ------------------------------------------------------------------
 *
 *  mbedtls psa_driver（esp_hmac_opaque／esp_ecdsa）がハードウェア
 *  鍵（eFuse焼き込み鍵）の有無を判定するために呼ぶ．本ビルドは
 *  efuseコンポーネント本体を取り込んでいない（esp_efuse_api.c等は
 *  bootloader_support等への依存が深いため対象外＝
 *  docs/wifi-shim.md参照）．eFuse鍵は焼いていない前提のため，
 *  「用途未設定（ESP_EFUSE_KEY_PURPOSE_USER=0）」を返せば呼び出し側は
 *  ハードウェア鍵無しとして正しくソフトウェア実装側にフォールバック
 *  する．esp_hmac_calculateは呼ばれないはずだが，念のため失敗を返す
 *  スタブとする（task指示どおり-1返し）．
 */
int
esp_efuse_get_key_purpose(int block)
{
	(void) block;
	return(0);	/* ESP_EFUSE_KEY_PURPOSE_USER相当 */
}

int
esp_hmac_calculate(int key_id, const void *message, size_t message_len,
					uint8_t *hmac)
{
	(void) key_id; (void) message; (void) message_len; (void) hmac;
	return(-1);	/* ESP_FAIL相当．eFuse鍵未プロビジョニングのため常に失敗 */
}

/*
 *  ------------------------------------------------------------------
 *  7. MAC アドレス読み出し（eFuseレジスタ直読み）
 *  ------------------------------------------------------------------
 *
 *  esp_hw_support/mac_addr.c本体（esp_read_mac）はesp_efuse本体・
 *  esp_efuse_table等チップ全体のブート基盤に依存が及ぶため採用せず
 *  （esp_wifi.cmake §6コメント参照），EFUSE_RD_MAC_SPI_SYS_{0,1}_REG
 *  （soc/efuse_reg.h）を直接読む簡易実装とする．バイト順は
 *  ESP-IDFの標準的なMAC efuseレイアウト（mac1の下位16bitが上位
 *  2バイト，mac0が下位4バイト，かつワード内バイト順が逆順）に従う．
 *  typeパラメータ（STA/AP/BT/ETH）は未区別＝全て同じベースMACを返す
 *  （esp_wifi_adapter.cのread_mac_wrapperはtype無視で呼んでいる）．
 *  シグネチャはesp_mac.h（esp_err_t esp_read_mac(uint8_t *mac,
 *  esp_mac_type_t type)）に一致させる（8bで本ファイルがesp_mac.hを
 *  #includeするようになったため．esp_wifi_adapter.c側は従来どおり
 *  独自のint版externで呼ぶ＝別TUのためリンクは名前のみで解決され
 *  問題ない）．
 */
/*  factory MAC の EFUSE 読み出しレジスタ．mac0=下位4B / mac1=上位2B のバイト順は
 *  S3/無印ESP32で完全一致，ベース番地のみ異なる（監査A1）：
 *   - ESP32-S3 : DR_REG_EFUSE_BASE=0x60007000 → RD_MAC_SPI_SYS_0/1 = 0x60007044/48
 *   - 無印ESP32: DR_REG_EFUSE_BASE=0x3ff5A000 → EFUSE_BLK0_RDATA1/2 = 0x3ff5A004/08
 *  誤MACだとWPA2 4-way/ARP/DHCPが破綻するためWi-Fi必須の修正．
 *  ★TOPPERS_ESP32_LX6ガード（kernel.h由来で確実．sdkconfig非依存）． */
#if defined(TOPPERS_ESP32_LX6)
#define EFUSE_RD_MAC_SPI_SYS_0_REG	0x3ff5A004U
#define EFUSE_RD_MAC_SPI_SYS_1_REG	0x3ff5A008U
#else
#define EFUSE_RD_MAC_SPI_SYS_0_REG	0x60007044U
#define EFUSE_RD_MAC_SPI_SYS_1_REG	0x60007048U
#endif

esp_err_t
esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
	uint32_t	mac0;
	uint32_t	mac1;

	(void) type;
	if (mac == NULL) {
		return(ESP_FAIL);
	}
	mac0 = sil_rew_mem((void *) EFUSE_RD_MAC_SPI_SYS_0_REG);
	mac1 = sil_rew_mem((void *) EFUSE_RD_MAC_SPI_SYS_1_REG);

	mac[0] = (uint8_t) (mac1 >> 8);
	mac[1] = (uint8_t) mac1;
	mac[2] = (uint8_t) (mac0 >> 24);
	mac[3] = (uint8_t) (mac0 >> 16);
	mac[4] = (uint8_t) (mac0 >> 8);
	mac[5] = (uint8_t) mac0;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  8. Wi-Fi/BTパワードメイン・PHY enable/disable／modem init-deinit／
 *     country info — esp_phy/src/phy_init.c 本実装へ置き換え
 *  ------------------------------------------------------------------
 *
 *  【2026-07-03更新】以前ここにあった簡易実装（esp_wifi_power_domain_
 *  on/off・esp_phy_enable/disable・esp_phy_modem_init/deinit・
 *  esp_phy_update_country_info）は，クロックゲートのみでlibphy.a内部
 *  の関数テーブル初期化（register_chipv7_phy）を一切呼んでいなかった
 *  ため，esp_wifi_scan_start()実行時にPHYの関数テーブル（set_chanfreq
 *  等）がNULLのまま呼び出され実機でクラッシュしていた．
 *
 *  esp_wifi.cmake §1cでesp_phy/src/phy_init.c（本体）・phy_common.c
 *  （補助関数）・esp32c3/phy_init_data.c（既定PHY初期化データ）を
 *  採用し，上記関数群はすべてそちらの実装（esp_wifi_bt_power_domain_
 *  on/off・esp_phy_enable/disable・esp_phy_modem_init/deinit・
 *  esp_phy_update_country_info）に置き換わったため，本ファイルの
 *  簡易実装は削除した（同名シンボルの重複定義を避けるため）．
 *  詳細・妥協点（PLL追従無効化・NVS較正未実装）はesp_wifi.cmake §1c
 *  およびdocs/wifi-shim.mdを参照．
 */

/*
 *  ------------------------------------------------------------------
 *  8a. NVS（PHY較正データ永続化）— 未実装スタブ
 *  ------------------------------------------------------------------
 *
 *  hal_stub/include/nvs.h参照．esp_phy/src/phy_init.cの較正データ
 *  永続化関数（`#ifndef __NuttX__`ガードのみでコンパイルされるが，
 *  本ビルドはCONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGEを定義しない
 *  ため実行経路には現れない＝毎回フル較正PHY_RF_CAL_FULL固定．
 *  esp_wifi.cmake §1c参照）が要求するシンボルを解決するためだけの
 *  スタブ．NVS「未初期化」として一貫して失敗を返す．
 *
 *  ★TOPPERS_ESP32_BT_BLUEDROID_CLASSIC(W3-④ BT Classic SPP)ビルドのみ：
 *  BlueDroidの btc_config→config.c(BT_OSI) は nvs_open/get_blob/set_blob/
 *  commit で bt_config.conf 名前空間を読み書きし，nvs_open失敗時に
 *  config_new/config_saveがNULL/err_code:0x2を返して esp_bluedroid_init が
 *  永久ハングする（"Call nvs_flash_init before initializing bluetooth"）．
 *  本ポートは実NVSコンポーネント（nvs_flash_init）を持たないため，
 *  最小の道として NVS を「揮発RAM・成功」応答にする（ボンド永続不要，
 *  BLE(W2) ble_store_config を PERSIST=0(RAM) にした流儀と同じ）．
 *  config_new は空configで始まり(get_blob→NOT_FOUND)，config_save は
 *  set_blob/commit成功で err_code:0 を返す＝bluedroid_init を通過する．
 *  ガードで非CLASSIC(Wi-Fi/BLE/S3)は従来どおり NOT_INITIALIZED を返す
 *  （S3/Wi-Fi非回帰）．
 */
esp_err_t
nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
	(void) name; (void) open_mode;
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	if (out_handle != NULL) {
		*out_handle = 1;	/* 非0（config.cのassert(fp!=0)を満たす） */
	}
	return(ESP_OK);
#else
	(void) out_handle;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
#endif
}

void
nvs_close(nvs_handle_t handle)
{
	(void) handle;
}

esp_err_t
nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
	(void) handle; (void) key; (void) value;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
			size_t *length)
{
	(void) handle; (void) key; (void) out_value; (void) length;
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	/* 揮発RAM実装：常に未保存＝空configで始める（bond永続不要の smoke） */
	return(ESP_ERR_NVS_NOT_FOUND);
#else
	return(ESP_ERR_NVS_NOT_INITIALIZED);
#endif
}

esp_err_t
nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
			size_t length)
{
	(void) handle; (void) key; (void) value; (void) length;
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	return(ESP_OK);		/* 破棄して成功（config_saveを通過させる） */
#else
	return(ESP_ERR_NVS_NOT_INITIALIZED);
#endif
}

esp_err_t
nvs_erase_all(nvs_handle_t handle)
{
	(void) handle;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_commit(nvs_handle_t handle)
{
	(void) handle;
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
	return(ESP_OK);
#else
	return(ESP_ERR_NVS_NOT_INITIALIZED);
#endif
}

/*
 *  ------------------------------------------------------------------
 *  8b. eFuse MACアドレス取得（esp_efuse_mac_get_default）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.cの較正データNVS保存関数（8a同様，本ビルド
 *  では実行経路に現れない）が参照．eFuse本体コンポーネント未採用
 *  （§7「7. MAC アドレス読み出し」コメント参照）のため，同じ
 *  EFUSE_RD_MAC_SPI_SYS_*レジスタ直読みのesp_read_mac()へ委譲する．
 */
esp_err_t
esp_efuse_mac_get_default(uint8_t *mac)
{
	/*  esp_read_mac()の戻り値は0=成功/-1=失敗＝ESP_OK/ESP_FAILと一致  */
	return((esp_err_t) esp_read_mac(mac, 0));
}

/*
 *  ------------------------------------------------------------------
 *  8c. ディープスリープPHYフック登録 — no-opスタブ
 *  ------------------------------------------------------------------
 *
 *  esp_phy_load_cal_and_init()末尾（`#ifndef __NuttX__` かつ
 *  `CONFIG_ESP_PHY_ENABLED && SOC_DEEP_SLEEP_SUPPORTED`＝本ビルドは
 *  両方成立するため実行される）がesp_deep_sleep_register_phy_hook()
 *  でPHYシャットダウン関数（phy_close_rf／phy_xpd_tsens）をディープ
 *  スリープ用フックとして登録する．本体（esp_hw_support/
 *  sleep_modes.c）はディープスリープ状態機械全体に依存が及ぶため
 *  不採用．ASP3はディープスリープへ入る経路を持たない（既存の
 *  esp_phy_modem_init/deinit移植時と同じ理由）ため，フックは受理
 *  するが登録内容を保持・呼び出しはしないno-opスタブとする．
 *  ESP_ERROR_CHECK()マクロがabort()しないよう常にESP_OKを返す．
 */
esp_err_t
esp_deep_sleep_register_phy_hook(esp_deep_sleep_cb_t new_dslp_cb)
{
	(void) new_dslp_cb;
	return(0);	/* ESP_OK相当 */
}

/*
 *  ------------------------------------------------------------------
 *  8d. ESP_ERROR_CHECK() マクロの実体（esp_err.h）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.c（esp_deep_sleep_register_phy_hook呼び出し）
 *  がESP_ERROR_CHECK()マクロ経由で参照する．失敗時は診断情報を
 *  syslogへ出力し，abort()（esp_shim_libc.c．syslog+無限ループ＝
 *  target_stddef.hのTOPPERS_assert_abort()と同じ停止方式）へ委譲する．
 *  ESP_ERROR_CHECK_WITHOUT_ABORT()用の_esp_error_check_failed_
 *  without_abort()は本ビルドで参照されないため未実装（到達不能）．
 */
void
_esp_error_check_failed(esp_err_t rc, const char *file, int line,
						const char *function, const char *expression)
{
	syslog(LOG_EMERG, "ESP_ERROR_CHECK failed: 0x%x at %s:%d (%s): %s",
		  (int_t) rc, file, line, function, expression);
	abort();
}

/*
 *  ------------------------------------------------------------------
 *  10. coexistence（未実装フィールドのno-opスタブ）
 *  ------------------------------------------------------------------
 *
 *  wifi_os_adapter.hの_coex_condition_set相当．libcoexist.aには
 *  対応する coex_condition_set 関数が存在しない（nm確認済み．
 *  coex_pti_get/coex_pti_set等は存在するが condition_set は無し＝
 *  このesp-hal-3rdpartyスナップショットでは未実装／将来予約
 *  フィールドと判断）．BLE非統合（本ターゲットはWi-Fi専用）のため
 *  実質呼ばれない想定のno-opスタブとする．
 */
void
coex_condition_set(uint32_t type, bool_t dissatisfy)
{
	(void) type; (void) dissatisfy;
}
