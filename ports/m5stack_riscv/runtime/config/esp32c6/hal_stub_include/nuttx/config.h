/*
 *  esp/config/esp32c6/sdkconfig.h（NuttX 向け生成物の verbatim コピー）が
 *  無条件に `#include <nuttx/config.h>` するため、これを解決するための
 *  最小スタブ。esp/config/esp32/hal_stub_include/nuttx/config.h（LX6/S3、
 *  Wi-Fi 用の値を持つ）と同じ役割・同じ配置規約。
 *
 *  段1（カーネル ELF がリンクできる、Wi-Fi 未着手）の時点で必要な値は
 *  実測（configure/build のエラーを1件ずつ潰した）で確定した最小集合のみ:
 *
 *  - CONFIG_ESPRESSIF_FLASH_FREQ_40M: sdkconfig.h:907-920 の
 *    CONFIG_ESPTOOLPY_FLASHFREQ_* 分岐が、20M/40M/80M のいずれも
 *    定義されないと `#error "SPI timing flash clock is invalid"` になる。
 *    esp32(LX6) 版スタブと同じ 40MHz（Direct Boot ボード既定）を選ぶ。
 *    段1では実機へ焼かないため値そのものに意味は無いが、#error 回避には
 *    いずれか1つの選択が要る。
 *
 *  他の CONFIG_ESPRESSIF_* 系 #ifdef 分岐（Wi-Fi/BLE/PM/efuse 等）は
 *  sdkconfig.h 側が #define しない場合の既定（未定義=無効）で問題ないことを
 *  実測済み（#error にならない）。Wi-Fi 移植（段4 以降）で追加の値が
 *  必要になったら、esp32 版に倣ってここへ追記する。
 *
 *  [段4 Task 2 追記]
 *  esp/config/esp32c6/sdkconfig.h:877-879 は CONFIG_LOG_DEFAULT_LEVEL 等を
 *  `#ifdef` 無しに無条件で CONFIG_ESPRESSIF_LOG_LEVEL へ束ねており、
 *  これが無いとログレベルを扱うすべての TU で `'CONFIG_ESPRESSIF_LOG_LEVEL'
 *  undeclared` になる（実測、wpa_libs 台本で162件）。値は esp32/esp32s3 版
 *  スタブと同じ 3（ESP_LOG_INFO 相当）。
 *  esp/config/esp32c6/sdkconfig.h:778 `#ifdef CONFIG_ESPRESSIF_WIFI` が
 *  CONFIG_ESP_WIFI_ENABLED 以下のバッファ/機能マクロ群（wpa_supplicant、
 *  esp_wifi ヘッダが参照）を丸ごとゲートしており、これを立てないと
 *  wpa_libs 台本のコンパイルで CONFIG_ESP_WIFI_* 未定義エラーになる
 *  （実測）。値は esp32(LX6)/esp32s3 版スタブ（esp/config/esp32/
 *  hal_stub_include/nuttx/config.h:34-80）と**同一**にする（これらは
 *  ESP-IDF Kconfig の既定値であってチップ固有ではなく、build_wpa_libs_
 *  espidf_esp32s3.sh の WPA_DEF（CONFIG_WPA3_SAE 等）と整合させる必要が
 *  あるため転記した）（コピー先だが値の出典は同じ ESP-IDF Kconfig 既定）。
 */
#ifndef TOPPERS_C6_HAL_STUB_NUTTX_CONFIG_H
#define TOPPERS_C6_HAL_STUB_NUTTX_CONFIG_H

/*  SPIフラッシュクロック（Direct Bootのボード既定＝40MHz）  */
#define CONFIG_ESPRESSIF_FLASH_FREQ_40M    1

/*  ログレベル（sdkconfig.h の CONFIG_LOG_DEFAULT_LEVEL 等が無条件に束ねる）。
 *  ESP_LOG_INFO(=3) 相当。esp32/esp32s3 版スタブと同値。  */
#define CONFIG_ESPRESSIF_LOG_LEVEL 3

/*
 *  Wi-Fi機能有効化。esp32c6/sdkconfig.h の CONFIG_ESPRESSIF_WIRELESS/
 *  CONFIG_ESPRESSIF_WIFI 配下の CONFIG_ESP_WIFI_* マクロ群（受信/送信
 *  バッファ数等）を有効化するゲート。個々の値は ESP-IDF Kconfig 既定値
 *  （esp32/hal_stub_include 版と同一）。
 */
#define CONFIG_ESPRESSIF_WIRELESS  1
#define CONFIG_ESPRESSIF_WIFI      1

#define CONFIG_ESPRESSIF_WIFI_STATIC_RX_BUFFER_NUM         10
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_RX_BUFFER_NUM        32
#define CONFIG_ESPRESSIF_WIFI_STATIC_TX_BUFFER_NUM         0
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_TX_BUFFER_NUM        32
/*  CONFIG_ESP_WIFI_TX_BUFFER_TYPE=1（動的）に対応する値  */
#define CONFIG_ESPRESSIF_WIFI_TX_BUFFER_TYPE               1
/*  CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF：既定は無効(0)＝静的管理バッファ */
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_RX_MGMT_BUFFER_TYPE  0
#define CONFIG_ESPRESSIF_WIFI_RX_MGMT_BUF_NUM_DEF          5
/*  A-MPDU送受信は既定で有効，BA winは6（ESP-IDF既定値）  */
#define CONFIG_ESPRESSIF_WIFI_AMPDU_TX_ENABLED             1
#define CONFIG_ESPRESSIF_WIFI_TX_BA_WIN                    6
#define CONFIG_ESPRESSIF_WIFI_AMPDU_RX_ENABLED             1
#define CONFIG_ESPRESSIF_WIFI_RX_BA_WIN                    6
/*
 *  WPA3拡張：build_wpa_libs_espidf_esp32c6.sh の WPA_DEF が
 *  CONFIG_WPA3_SAE を立てる方針と合わせ、SAE 本体のみ有効化し，
 *  SAE_PK／SAE_H2E／SoftAP SAE／OWE STA は未対応（0固定）とする
 *  （esp32/esp32s3 版と同じ方針）。
 */
#define CONFIG_ESPRESSIF_WIFI_ENABLE_WPA3_SAE              1
#define CONFIG_ESPRESSIF_WIFI_ENABLE_SAE_PK                0
#define CONFIG_ESPRESSIF_WIFI_ENABLE_SAE_H2E                0
#define CONFIG_ESPRESSIF_WIFI_SOFTAP_SAE_SUPPORT           0
#define CONFIG_ESPRESSIF_WIFI_ENABLE_WPA3_OWE_STA          0
#define CONFIG_ESPRESSIF_WIFI_STA_DISCONNECT_PM            1
/*  WAPI（CONFIG_WPA_WAPI_PSK）：esp32/esp32s3 版と同様 OFF 固定  */
#define CONFIG_WPA_WAPI_PSK                                0

#endif /* TOPPERS_C6_HAL_STUB_NUTTX_CONFIG_H */
