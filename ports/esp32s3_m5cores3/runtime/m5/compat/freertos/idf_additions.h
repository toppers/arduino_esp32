/*
 *  ESP-IDF の `freertos/idf_additions.h` の**互換版**（本ポート用）
 *
 *  【なぜ本物を使わないか】ESP-IDF の `idf_additions.h` は
 *  `freertos/stream_buffer.h` を include し、そこから **IDF の FreeRTOS カーネル本体**を
 *  引き込む。本ポートは FreeRTOS を持たず `m5/compat/freertos/` の互換層で置き換えて
 *  いるので、**両者を同時に include すると衝突する**。
 *
 *  【実測で確定した必要集合】ESP-IDF の i2s ドライバ（`esp_driver_i2s`）が
 *  `idf_additions.h` から実際に使うのは **5 本だけ**である
 *  （`grep` で確定。2026-07-25）:
 *
 *    xQueueCreateWithCaps / vQueueDeleteWithCaps
 *    xSemaphoreCreateBinaryWithCaps / xSemaphoreCreateMutexWithCaps / vSemaphoreDeleteWithCaps
 *
 *  ⇒ その 5 本は `queue.h` / `semphr.h` に置いた。本ヘッダはそれらを束ねるだけ。
 *
 *  ★**このヘッダを「IDF の idf_additions.h と等価」と思ってはいけない。**
 *  IDF 版はタスク生成系（`xTaskCreatePinnedToCoreWithCaps` 等）やストリームバッファも
 *  持つ。ここに無いものが必要になったら、**そのとき実測して足す**こと。
 */
#ifndef M5_COMPAT_FREERTOS_IDF_ADDITIONS_H
#define M5_COMPAT_FREERTOS_IDF_ADDITIONS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#endif /* M5_COMPAT_FREERTOS_IDF_ADDITIONS_H */
