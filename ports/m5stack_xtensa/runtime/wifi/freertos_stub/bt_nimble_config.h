/*
 *  NimBLE（Phase BT-2）用のCONFIG_BT_*補完ヘッダ（force-include）
 *
 *  hal/nuttx/esp32s3/include/sdkconfig.hではNimBLE/コントローラ設定
 *  （CONFIG_BT_NIMBLE_* / CONFIG_BT_CONTROLLER_ENABLED / CONFIG_BT_CTRL_*_EFF）
 *  一式が`#ifdef CONFIG_ESPRESSIF_BLE`ブロックにある．ところが同
 *  sdkconfig.hはCONFIG_ESPRESSIF_WIFIを常時定義しているため，
 *  CONFIG_ESPRESSIF_BLEを立てると
 *    `#if defined(ESPRESSIF_BLE) && defined(ESPRESSIF_WIFI)`
 *  によりCONFIG_ESP_COEX_SW_COEXIST_ENABLE=1→CONFIG_SW_COEXIST_ENABLE=1
 *  となり，bt.cのcoex経路（COEX_SCHM_CALLBACK_TYPE_BT等）が有効化されて
 *  しまう（Phase BT-1はcoex OFFで検証済み＝挙動を変えたくない）．
 *
 *  そこでCONFIG_ESPRESSIF_BLEは立てず，NimBLEに必要なCONFIG_BT_*だけを
 *  本ヘッダで直接定義する（sdkconfig.hの当該ブロックからの写しで，
 *  coex/WiFiは一切含めない）．sdkconfig.hは各NimBLEソースの
 *  `#include "sdkconfig.h"`（syscfg/syscfg.h→esp_nimble_cfg.h経由）で
 *  通常の-I探索により見つかるため，本ヘッダはsdkconfig.hより後に
 *  force-includeする（esp_nimble_cfg.hがCONFIG_BT_NIMBLE_*を読むため）．
 *  本ヘッダ自体は#define集合のみでC構文を含まないため，OFLAGSが全
 *  ソース（.S含む）に一様適用されてもアセンブリを壊さない．
 *
 *  姉妹プロジェクトC3（asp3_esp_idf）のPhase D-2a実装（docs/bt-shim.md）
 *  からの移植．値はESP-IDF Kconfig既定のチップ非依存な写しであり，
 *  S3固有の変更は無い（C3/S3で共有のsdkconfig.hブリッジ構造も同一）．
 *
 *  CONFIG_BT_ENABLEDはesp/build_ble_incflags.txtの-Dで既に定義済みの
 *  ため含めない．
 */
#ifndef TOPPERS_BT_NIMBLE_CONFIG_H
#define TOPPERS_BT_NIMBLE_CONFIG_H

/*
 *  npl_os_freertos.cはESP_IDF_VERSION / ESP_IDF_VERSION_VALを#ifで
 *  使うが同ファイルはesp_idf_version.hをincludeしない（ESP-IDFでは
 *  暗黙include前提）．force-include本ヘッダで供給する．
 */
#include <esp_idf_version.h>

#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_CONTROLLER_ENABLED 1
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL 1
#define CONFIG_BT_NIMBLE_LOG_LEVEL_INFO 1
/*  注（BT-5切り分けで判明）：本値を0(DEBUG)にしてもNimBLEのDEBUGログは
 *  出ない。MODLOGはESP_LOG_LEVEL_LOCAL経由で、コンパイル時ゲート
 *  LOG_LOCAL_LEVEL=CONFIG_LOG_MAXIMUM_LEVEL=CONFIG_ESPRESSIF_LOG_LEVEL=3
 *  (INFO、hal_stub/include/nuttx/config.h固定)がDEBUGを落とすため。  */
#define CONFIG_BT_NIMBLE_LOG_LEVEL 1
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3
#define CONFIG_BT_NIMBLE_MAX_BONDS 3
#define CONFIG_BT_NIMBLE_MAX_CCCDS 8
#define CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM 0
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 1
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 1
#define CONFIG_BT_NIMBLE_ROLE_BROADCASTER 1
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 1
#define CONFIG_BT_NIMBLE_SECURITY_ENABLE 1
#define CONFIG_BT_NIMBLE_SM_LEGACY 1
#define CONFIG_BT_NIMBLE_SM_SC 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION 1
#define CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME "nimble"
#define CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN 31
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 256
#define CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE 0x0
#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT 12
#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE 256
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT 24
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE 320
#define CONFIG_BT_NIMBLE_ACL_BUF_COUNT 24
#define CONFIG_BT_NIMBLE_ACL_BUF_SIZE 255
#define CONFIG_BT_NIMBLE_HCI_EVT_BUF_SIZE 70
#define CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT 30
#define CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT 8
#define CONFIG_BT_NIMBLE_GATT_MAX_PROCS 4
/*
 *  ★BT-4最終検証で発見：本定義が無いと
 *  esp_nimble_cfg.hがMYNEWT_VAL_BLE_GATTS=(0)にフォールバックし，
 *  GATT/ATTサーバ実装（ble_gatts.c・ble_svc_gap.c・ble_svc_gatt.c等，
 *  いずれも`#if MYNEWT_VAL(BLE_GATTS)`ガード）が丸ごと空にコンパイル
 *  される。結果：接続は成立してもcentralのATT要求（MTU交換・サービス
 *  発見）に一切応答できず，bluez側はATTトランザクションタイムアウト
 *  （30秒）後に切断する。IDF Kconfig既定はBT_NIMBLE_GATT_SERVER=y／
 *  BT_NIMBLE_GAP_SERVICE=y（stock bleprphも同値）。
 */
#define CONFIG_BT_NIMBLE_GATT_SERVER 1
#define CONFIG_BT_NIMBLE_GAP_SERVICE 1
/*  IDF Kconfig既定：BT_NIMBLE_SVC_GAP_CAR_CHAR_NOT_SUPP（CAR特性なし）→-1  */
#define CONFIG_BT_NIMBLE_SVC_GAP_CENT_ADDR_RESOLUTION (-1)
/*  IDF Kconfig既定：BT_NIMBLE_ATT_MAX_PREP_ENTRIES=64（prepare write用） */
#define CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES 64
#define CONFIG_BT_NIMBLE_RPA_TIMEOUT 900
#define CONFIG_BT_NIMBLE_HS_STOP_TIMEOUT_MS 2000
/*  ★S3固有の既知バグ回避：ble_gap_slave_adv_reattempt()が
 *  ble_gap_adv_start()をadv_params=NULLで呼ぶ設計だが、ble_adv_reattempt.retryは
 *  「前回のreattemptが成功した後」にしか1にならないため、切断後最初のreattemptでは
 *  retry=0のままble_gap_adv_validate()のNULLチェック(adv_params==NULL→EINVAL=3)に
 *  必ず引っかかり、advertisingが再開できず完全に沈黙する（"Adv reattempt failed; rc=3"）。
 *  無効化することで、reason=0x3E(Connection Failed to be Established)時もアプリの
 *  gap_event_cb()へ通常のBLE_GAP_EVENT_DISCONNECTが届き、start_advertising()による
 *  正常な再アドバタイズ経路（reason=0x08等で既に動作確認済み）を使えるようにする。 */
#define CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT 0
#define CONFIG_BT_NIMBLE_MAX_CONN_REATTEMPT 3
#define CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_2M_PHY 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY 1
#define CONFIG_BT_NIMBLE_MAX_PERIODIC_SYNCS 0
#define CONFIG_BT_NIMBLE_WHITELIST_SIZE 12
#define CONFIG_BT_NIMBLE_USE_ESP_TIMER 1
#define CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE 1
/*
 *  以下2つは上記ブロックには無いがesp_nimble_cfg.hが
 *  MYNEWT_VAL_BLE_TRANSPORT_EVT_(DISCARDABLE_)COUNTの元として参照する．
 *  HCI event buffer数（HI/LO）に合わせる．
 */
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT 30
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT 8
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE 255
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 70
#define CONFIG_BT_NIMBLE_L2CAP_COC_SDU_BUFF_COUNT 1
#define CONFIG_BT_NIMBLE_EATT_CHAN_NUM 0
/*
 *  transport.cのPOOL_ACL_COUNTは（LL=非native／HS=nativeの本構成では）
 *  BLE_TRANSPORT_ACL_FROM_LL_COUNTに等しい．ble_transport_deinitは
 *  pool_aclを無条件参照するため0だと未定義参照でコンパイル不能．
 *  ACLバッファ数（CONFIG_BT_NIMBLE_ACL_BUF_COUNT=24）に合わせて24とする．
 */
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT 24

#define CONFIG_BT_BLE_RPA_TIMEOUT 900
#define CONFIG_BT_BLE_42_FEATURES_SUPPORTED 1
#define CONFIG_BT_BLE_42_DTM_TEST_EN 1
#define CONFIG_BT_BLE_42_ADV_EN 1
#define CONFIG_BT_BLE_42_SCAN_EN 1
#define CONFIG_BT_BLE_VENDOR_HCI_EN 1
#define CONFIG_BT_CTRL_MODE_EFF 1
#define CONFIG_BT_CTRL_BLE_MAX_ACT 6
#define CONFIG_BT_CTRL_BLE_MAX_ACT_EFF 6
#define CONFIG_BT_CTRL_BLE_STATIC_ACL_TX_BUF_NB 0
#define CONFIG_BT_CTRL_PINNED_TO_CORE 0
#define CONFIG_BT_CTRL_HCI_MODE_VHCI 1
#define CONFIG_BT_CTRL_HCI_TL 1
#define CONFIG_BT_CTRL_ADV_DUP_FILT_MAX 30
#define CONFIG_BT_BLE_CCA_MODE_NONE 1
#define CONFIG_BT_BLE_CCA_MODE 0
#define CONFIG_BT_CTRL_HW_CCA_VAL 75
#define CONFIG_BT_CTRL_HW_CCA_EFF 0
#define CONFIG_BT_CTRL_CE_LENGTH_TYPE_ORIG 1
#define CONFIG_BT_CTRL_CE_LENGTH_TYPE_EFF 0
#define CONFIG_BT_CTRL_TX_ANTENNA_INDEX_0 1
#define CONFIG_BT_CTRL_TX_ANTENNA_INDEX_EFF 0
#define CONFIG_BT_CTRL_RX_ANTENNA_INDEX_0 1
#define CONFIG_BT_CTRL_RX_ANTENNA_INDEX_EFF 0
#define CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_P9 1
#define CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_EFF 11
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_FLOW_CTRL_SUPP 1
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_FLOW_CTRL_NUM 100
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_DISCARD_THRSHOLD 20
#define CONFIG_BT_CTRL_BLE_SCAN_DUPL 1
#define CONFIG_BT_CTRL_SCAN_DUPL_TYPE_DEVICE 1
#define CONFIG_BT_CTRL_SCAN_DUPL_TYPE 0
#define CONFIG_BT_CTRL_SCAN_DUPL_CACHE_SIZE 100
#define CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD 0
#define CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_DIS 1
#define CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_EFF 0
#define CONFIG_BT_CTRL_HCI_TL_EFF 1
#define CONFIG_BT_CTRL_CHAN_ASS_EN 1
#define CONFIG_BT_CTRL_LE_PING_EN 1
#define CONFIG_BT_CTRL_DTM_ENABLE 1
#define CONFIG_BT_CTRL_BLE_MASTER 1
#define CONFIG_BT_CTRL_BLE_SCAN 1
#define CONFIG_BT_CTRL_BLE_SECURITY_ENABLE 1
#define CONFIG_BT_CTRL_BLE_ADV 1
#define CONFIG_BT_ALARM_MAX_NUM 50
#define CONFIG_MAC_BB_PD 0
/*  modem sleep無効（Phase BT-1と同じ．CONFIG_PM非使用相当）  */
#define CONFIG_BT_CTRL_SLEEP_MODE_EFF 0
#define CONFIG_BT_CTRL_SLEEP_CLOCK_EFF 0

/*
 *  hal/nuttx/esp32s3/include/sdkconfig.hが既にCONFIG_IDF_TARGET_ESP32S3=1
 *  を無条件定義している（esp32c3側sdkconfig.hも同様にCONFIG_IDF_TARGET_
 *  ESP32C3=1を定義．確認済み）．esp_nimble_cfg.hのNIMBLE_CFG_CONTROLLER
 *  判定（`#if CONFIG_IDF_TARGET_ESP32||ESP32C3||ESP32S3`→upstream npl経路．
 *  C3のPhase D-2aで検証済み）は，本ヘッダで何もしなくてもsdkconfig.h側の
 *  定義だけで正しく成立する．
 *
 *  ★S3固有の追加定義：esp_nimble_cfg.hはCONFIG_IDF_TARGET_ESP32（原型
 *  ESP32のみ）の場合だけMYNEWT_VAL_BLE_HS_PVCY/HOST_BASED_PRIVACYを強制
 *  (1)にし，C3/S3を含むそれ以外はCONFIG_BT_NIMBLE_HS_PVCY等（本ヘッダで
 *  未供給だと0）にフォールバックする．ところがble_gap.c
 *  （ble_gap_read_local_irk等）・ble_sm.c・ble_store.cはble_hs_pvcy_our_irk/
 *  ble_hs_pvcy_resolve_with_irk/ble_store_read_local_irkを`#if MYNEWT_VAL
 *  (BLE_HS_PVCY)`の外側から無条件に呼ぶため，0のままだと実装（ble_hs_pvcy.c
 *  全体が`#if MYNEWT_VAL(BLE_HS_PVCY)`で丸ごとガードされている）が消え，
 *  リンクエラー（undefined reference）になる．CONFIG_BT_NIMBLE_HS_PVCY=1で
 *  基本実装（非HOST_BASED_PRIVACY版）を有効化する（HOST_BASED_PRIVACYは
 *  さらに別のRPA解決変種で，sync到達には不要のため0のまま）．
 */
#define CONFIG_BT_NIMBLE_HS_PVCY 1

#endif /* TOPPERS_BT_NIMBLE_CONFIG_H */
