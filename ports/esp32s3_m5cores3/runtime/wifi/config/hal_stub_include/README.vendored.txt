このディレクトリは TOPPERS/ASP3 の esp-idf ターゲット用 hal_stub include を
esp32_s3 リポジトリへ取り込んだもの（2026-07-16、段階4締め）。

出自: asp3_esp_idf/asp3/target/esp32c3_espidf/hal_stub/include
目的: LX6 の ESP-IDF-only ビルド(build_*_espidf_esp32.sh)を外部 asp3_esp_idf
      checkout からも自己完結させる（$IDF/asp3 参照の撤去）。
内容: nuttx/config.h(vendored sdkconfig.hが#includeするスタブ)、platform/os.h
      (OSシムヘッダ)、libc/sys スタブ、esp_netif.h/nvs*.h/driver/gpio.h の最小スタブ。
※ これは TOPPERS-ASP3 由来であり esp-hal-3rdparty ではない。ESP-IDF が等価を
   供給しない config/libc シム層のため vendor した。

★2026-07-17追記(F-10・命名の紛らわしさ)：親ディレクトリ名は`esp/config/esp32`
（LX6/無印ESP32を指す名前）だが、本`hal_stub_include`はS3のビルドスクリプト
（例：`esp/boot/build_lwip_lib_espidf_esp32s3.sh`）からも参照されており、
実際にはLX6専用ではなくチップ間で共有されている。ディレクトリ名だけを見て
「S3には無関係」と誤解しないこと（改名は本追記の範囲外・低優先の整備事項）。
