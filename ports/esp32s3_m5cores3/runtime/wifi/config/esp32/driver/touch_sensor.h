/*
 *  無印ESP32(LX6) の静電タッチセンサ driver の最小スタブ（本ポートには無い）
 *
 *  M5Unified.cpp は CONFIG_IDF_TARGET_ESP32 のとき
 *  `__has_include(<driver/touch_sens.h>)` → `<driver/touch_sensor.h>` の順で
 *  探し、どちらも無いと touch_pad_* を未宣言のまま使ってコンパイルに失敗する。
 *  このヘッダが在れば旧 API 側の分岐が選ばれる。
 *
 *  用途はボード判別の補助（タッチチャネルを読んで機種を絞る）だけで、
 *  M5Stack Basic のボタンは GPIO の物理スイッチである。touch_pad_init() が
 *  失敗を返すと呼び出し元は false を返し、判別は他の手段へ落ちる。
 *  ★本ポートはタッチセンサを持たないので「読めない」と答えるのが正しい。
 *  実装を入れるときはここを消して実物の driver を通すこと。
 */

#ifndef TOPPERS_STUB_DRIVER_TOUCH_SENSOR_H
#define TOPPERS_STUB_DRIVER_TOUCH_SENSOR_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int touch_pad_t;

/*  esp-idf の driver/touch_sensor_common.h と同じ値（10bit しきい値の最大）。 */
#define TOUCH_PAD_THRESHOLD_MAX  (0)

static inline esp_err_t touch_pad_init(void) { return(ESP_FAIL); }
static inline esp_err_t touch_pad_deinit(void) { return(ESP_FAIL); }
static inline esp_err_t touch_pad_config(touch_pad_t pad, uint16_t threshold)
{ (void) pad; (void) threshold; return(ESP_FAIL); }
static inline esp_err_t touch_pad_read(touch_pad_t pad, uint16_t *value)
{ (void) pad; if (value != 0) { *value = 0; } return(ESP_FAIL); }

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_STUB_DRIVER_TOUCH_SENSOR_H */
