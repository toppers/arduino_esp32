#ifndef BT_CFG_H
#define BT_CFG_H

/*
 *  Bluetooth統合（Phase D-1〜W3）の静的構成（bt.cfgと一致させること）
 *
 *  bt.c本体が要求する動的プリミティブ（queue/semaphore/task）は
 *  esp/esp_shim.cの既存プール（ESP_SHIM_NUM_*．esp_shim_cfg.h）を
 *  そのまま使う（bt.cの実測要求：queue 1個・semaphore 2個・task 1個＝
 *  既存プールで十分）．
 *
 *  esp_timer_*（当初はBTコントローラのモデムスリープ用タイマとして
 *  導入したが，W3（非公開作業記録/20260714-esp32-classic-wifi-bt/）で
 *  BlueDroidの`osi_alarm`（bt/common/osi/alarm.c）が直接この上に乗る
 *  ことを確認した＝esp_timer_create/start_once/start_periodic/stop/
 *  delete/is_active/get_expiry_timeを本物のタイマとして要求する。
 *  実装はesp_shim.cのets_timer機構とは別に，本ファイル専用の小さな
 *  タイマタスク＋固定プールで提供する（poll型：deadline_us昇順走査＋
 *  セマフォ起床、bt/bt_shim.c参照）。
 *
 *  BT_TIMER_NUM：osi_alarm側のALARM_CBS_NUM(=UC_ALARM_MAX_NUM、
 *  bt_user_config.hのデフォルトは50)と同時に使われるプールなので、
 *  両者を揃える必要がある。W3のSPP MVPでは50個は過剰（RAM自体は
 *  小さいが）なので、対応するCONFIG_BT_ALARM_MAX_NUMも16へ絞り
 *  （esp/bt/classic/sdkconfig_override/sdkconfig.h参照）、両者を
 *  16で揃える。
 *
 *  2026-07-27 レビュー esp-2：BT_TIMER_NUM／BT_TIMER_TASK_PRI／
 *  BT_TIMER_STKSZ と bt_timer_task() の宣言は esp/shim/esp_shim_cfg.h の
 *  ESP_TIMER_NUM／ESP_TIMER_TASK_PRI／ESP_TIMER_STKSZ／esp_timer_shim_task()
 *  へ移した（実装は esp/shim/esp_timer_shim.c）。BT/BLE 構成での値
 *  （16／5／4096）は不変。 */

#endif /* BT_CFG_H */
