/*
 *  M5（CoreS3）ビルド専用の nuttx/config.h スタブ．
 *
 *  esp/config/esp32s3/sdkconfig.h:532 が無条件で <nuttx/config.h> を include する
 *  （ESP-IDF/NuttX 両対応の vendored 設定であるため）．Wi-Fi/BT の golden ビルドは
 *  esp/config/esp32/hal_stub_include/nuttx/config.h（Wi-Fi 用 CONFIG_ESPRESSIF_* を
 *  多数定義し，かつ libc スタブ time.h/string.h/pthread 等を同梱する）でこれを満たす．
 *
 *  しかし M5 は C++（M5GFX）翻訳単位であり，hal_stub_include の libc スタブヘッダが
 *  実 newlib の time.h/string.h/pthread.h 等を shadow して C++ 標準ライブラリを壊す
 *  （time_t 二重定義・pthread_mutexattr_t 未宣言などのカスケード．1-5 実測）．
 *  そこで M5 では hal_stub_include を include パスから外し，sdkconfig.h が必要とする
 *  この nuttx/config.h だけを最小限に供給する．
 *
 *  M5 は Wi-Fi/BT を使わないので CONFIG_ESPRESSIF_WIRELESS/WIFI は定義しない
 *  （sdkconfig.h の該当ブロックは #ifdef で丸ごとスキップされる）．sdkconfig.h には
 *  #error ガードが無いことを確認済み（未定義シンボルは 0 相当で無害）．
 */
#ifndef TOPPERS_M5_IDF_COMPAT_NUTTX_CONFIG_H
#define TOPPERS_M5_IDF_COMPAT_NUTTX_CONFIG_H

/*  CPU クロック（本プロジェクト既定＝160MHz．sdkconfig.h が
 *  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 導出に使う．CONFIG_ESP32S3_DEFAULT_CPU_FREQ_160 は
 *  sdkconfig.h:1107 が自前で #define するのでここでは定義しない＝再定義警告回避）．  */
#define CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ 160

/*  ログレベル（sdkconfig.h:946-948 が CONFIG_LOG_DEFAULT_LEVEL/MAXIMUM_LEVEL/
 *  BOOTLOADER_LOG_LEVEL へ転写する．未定義だと esp_log のレベル比較で
 *  「CONFIG_ESPRESSIF_LOG_LEVEL was not declared」となる）．ESP_LOG_INFO=3 相当．
 *  hal_stub_include/nuttx/config.h と同値．  */
#define CONFIG_ESPRESSIF_LOG_LEVEL 3

#endif /* TOPPERS_M5_IDF_COMPAT_NUTTX_CONFIG_H */
