/*
 *  ⚠ [孤児 / CMake 未対応] classic 全廃(2026-07-20)で本オーバーライドの consumer（BT Classic
 *     SPP=W3 の seam ビルド）は削除済み。W3 バリアントの CMake preset 化は統合レビュー
 *     B-2/P-2（方針決定待ち）。復元は branch archive/trb-classic-cfg 参照。
 */
/*
 *  BT Classic(BR/EDR)/BlueDroid統合（W3、非公開作業記録/20260714-esp32-classic-wifi-bt/）
 *  専用の sdkconfig.h オーバーライド。
 *
 *  無印ESP32(LX6)の共有 sdkconfig.h（hal/nuttx/esp32/include/sdkconfig.h）は
 *  W1(Wi-Fi)/W2(BLE, NimBLE)が既に CONFIG_BTDM_CTRL_MODE_BLE_ONLY 前提で
 *  使っているため、直接書き換えるとW2(BLEコントローラ)側の
 *  patch_apply()（bt.c）の分岐が変わってしまい非回帰を壊す。
 *
 *  そのため、本ファイルを -I で本体sdkconfig.hより「前」に置き、
 *  #include_next で本体を読み込んだ後に BT Classic 専用の値だけを
 *  #undef/#define で上書きする（W1/W2のビルドはこのディレクトリを
 *  -I に含めないため影響を受けない＝W2/W1非回帰）。
 *
 *  上書き内容：
 *   - CONFIG_BTDM_CTRL_MODE_BLE_ONLY を外し CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY
 *     を立てる。bt.c(controller/esp32) の patch_apply() が
 *     #ifndef CONFIG_BTDM_CTRL_MODE_BLE_ONLY で config_bt_funcs_reset()
 *     （BR/EDR用パッチ適用）を行うため、BLE_ONLYのままだとBR/EDRパッチが
 *     当たらず、コントローラenable自体は成功してもBR/EDR機能が死んでいる
 *     状態になる（レビューが指摘したCLASSIC_BT単独運用の前提）。
 *   - CONFIG_BT_CLASSIC_ENABLED / CONFIG_BT_SPP_ENABLED を立てる
 *     （bluedroid_user_config.h経由でbt_target.hのCLASSIC_BT_INCLUDED /
 *     RFCOMM_INCLUDED等を有効化。BLE/A2DP/HFP/HID/OBEX/PBAP等は未定義
 *     のままデフォルトFALSEにとどめ、レビュー方針
 *     「ESP_BT_MODE_CLASSIC_BT単独＋SPP MVP」に沿って不要スタックを削る）。
 */
#ifndef TOPPERS_BT_CLASSIC_SDKCONFIG_OVERRIDE_H
#define TOPPERS_BT_CLASSIC_SDKCONFIG_OVERRIDE_H

#include_next "sdkconfig.h"

#undef CONFIG_BTDM_CTRL_MODE_BLE_ONLY
#ifndef CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY
#define CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY 1
#endif

/*
 *  [W3-④ 真因] CONFIG_BT_CONTROLLER_ENABLED：BlueDroidのHCIトランスポート
 *  選択を決める最重要フラグ。common/include/bt_user_config.h が
 *    #ifdef CONFIG_BT_CONTROLLER_ENABLED → UC_BT_CONTROLLER_INCLUDED=TRUE
 *  とし、bt_target.h が BT_CONTROLLER_INCLUDED=TRUE を導く。これが立って
 *  いないと、host/bluedroid/api/esp_bluedroid_hci.c の
 *  hci_host_register_callback() が #else 側（外部HCIドライバ
 *  s_hci_driver_ops 経由）にコンパイルされ、s_hci_driver_ops は未登録
 *  （NULL）なので register が -1 を返す。→ hci_hal_h4.c hal_open() が false
 *  → hci_layer.c hci_start_up() 失敗 → bte_main.c「Start HCI Host Layer
 *  Failure」→ BTU_StartUp スキップ → esp_bluedroid_init の INIT future が
 *  永久未発火でハング（実機WROVERで確認、JTAG逆アセンブルで確定）。
 *  本フラグを立てると #if(BT_CONTROLLER_INCLUDED==TRUE) 側＝内蔵
 *  コントローラの esp_vhci_host_register_callback を直接呼ぶ正規経路になる。
 *  （旧セッションが疑った vhci_env_p ゼロ化は誤りで、stable blobでは
 *   vhci_env_p は非NULLのまま。真因はこのビルド構成漏れ。）
 */
#ifndef CONFIG_BT_CONTROLLER_ENABLED
#define CONFIG_BT_CONTROLLER_ENABLED 1
#endif

#ifndef CONFIG_BT_CLASSIC_ENABLED
#define CONFIG_BT_CLASSIC_ENABLED 1
#endif

#ifndef CONFIG_BT_SPP_ENABLED
#define CONFIG_BT_SPP_ENABLED 1
#endif

/*
 *  [2026-07-15 W3継続セッション追記] CONFIG_BT_SMP_ENABLE：BlueDroidリンク
 *  駆動棚卸しで発見。bt_target.hのSMP_INCLUDED（UC_BT_SMP_ENABLE由来）は
 *  一見BLEのSecurity Manager Protocol専用に見えるが、実際は
 *  btm_int.h(tBTM_SEC_CB)のpairing_state/pairing_bda/p_rmt_name_callback/
 *  pairing_disabled/connect_only_paired等、**Classic BTのペアリング状態
 *  管理フィールドもSMP_INCLUDED配下に同居**している（BlueDroidがBLE SMと
 *  Classic SSPの状態管理を同じ構造体ブロックで共有する設計のため）。
 *  CLASSIC_BT_INCLUDEDのみ立てCONFIG_BT_SMP_ENABLEを立てないと、
 *  btm_sec.c等のペアリング処理コードが構造体メンバ不足でコンパイル不能
 *  になる（実測確認：stack/btm/btm_sec.cで'tBTM_CB' has no member named
 *  'pairing_state'等）。SPP MVP（レビュー方針＝SSP Just Worksでペアリング
 *  面最小化）でも、ペアリング状態機械そのものは動かす必要があるため
 *  本フラグは必須と判断。 */
#ifndef CONFIG_BT_SMP_ENABLE
#define CONFIG_BT_SMP_ENABLE 1
#endif

/*
 *  osi_alarm(bt/common/osi/alarm.c)のALARM_CBS_NUM(=UC_ALARM_MAX_NUM)既定は
 *  50。bt/bt_cfg.hのBT_TIMER_NUM(esp_timer_*の固定プール、osi_alarmが
 *  直接乗る)と必ず揃える必要がある。SPP MVPでは50個は過剰なため16へ絞る
 *  （bt_cfg.hのBT_TIMER_NUM=16と同期させること）。
 */
#ifndef CONFIG_BT_ALARM_MAX_NUM
#define CONFIG_BT_ALARM_MAX_NUM 16
#endif

/*
 *  ★BR/EDR の接続数。同梱 sdkconfig は BLE 専用構成なので、BR/EDR 側の
 *    本数がすべて 0 のまま来る（wifi/config/esp32/sdkconfig.h の
 *    CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN_EFF ほか）。モードだけ BR_EDR へ
 *    倒しても本数は 0 のままで、esp_bt_controller_init() が
 *    ESP_ERR_INVALID_ARG(258) を返す。
 *
 *    2026-09-02、M5Stack Basic 実機で実測。コントローラ
 *    (libbtdm_app.a) は cfg.mode の bit1 が立っているとき
 *    cfg.bt_max_acl_conn を 1..7 で検査する（逆アセンブルで確認：
 *    `l8ui a8,a2,22; addi a8,a8,-1; bgeui a8,7,<err>`）。0 は 0-1=255 と
 *    なって範囲外に落ちる。
 *
 *    ACL 2 本は ESP-IDF の既定。SPP サーバは 1 本で足りるが、接続が
 *    切り替わる間に 2 本並ぶ瞬間があるため既定に合わせる。
 */
#undef CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN_EFF
#define CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN_EFF 2

/*
 *  SCO/eSCO（音声）は使わない。ESP-IDF も SCO 無効時は 0。
 */
#undef CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN_EFF
#define CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN_EFF 0

/*
 *  暗号鍵長の下限。ESP-IDF の既定は 7 バイトで、範囲は 7..16。0 のまま
 *  だと BIAS 攻撃対策の下限が無い状態になる。
 */
#undef CONFIG_BTDM_CTRL_BR_EDR_MIN_ENC_KEY_SZ_DFT_EFF
#define CONFIG_BTDM_CTRL_BR_EDR_MIN_ENC_KEY_SZ_DFT_EFF 7

#endif /* TOPPERS_BT_CLASSIC_SDKCONFIG_OVERRIDE_H */
