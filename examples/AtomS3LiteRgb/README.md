# AtomS3LiteRgb

M5AtomS3 Lite（ESP32-S3）の本体 RGB LED（WS2812B-2020、データ G35）を
点灯させる例題です。Tools > FMP3 Runtime で **WiFi** を選んで建てます
（`rgbLedWrite` はそのランタイムにだけリンクされています）。

## 板ガード

`#if defined(ARDUINO_M5STACK_ATOMS3LITE)` で囲ってあります。他の 5 板
（CoreS3 / M5StickS3 / M5Core / M5NanoC6 / M5Stamp-C5）でも**コンパイルと
リンクは通り**、setup() が `[AtomS3LiteRgb] this example targets the
M5AtomS3 Lite; nothing to do on this board` を 1 行出すだけです。

このマクロは上流の M5Stack core には無く、本パッケージが
`install_platform.py` の `BOARD_BUILD_OVERRIDES` で
`m5atoms3lite_fmp3.build.board=M5STACK_ATOMS3LITE` と上書きして作って
います（派生元の `m5stack_atoms3` は LCD 付きで RGB LED が無い別物なので、
継承した `ARDUINO_M5STACK_ATOMS3` では両者を区別できません）。

## 何をするか（AtomS3 Lite のみ、配線不要）

`loop()` 1000 tick（約 1 秒）ごとに 赤 -> 緑 -> 青 -> 消灯 を 2 周します。
書き込みのたびに

```
[S3-RGB] write red
[S3-RGB] tx_done=1
```

が出ます。`tx_done` はランタイム側
（`ports/m5stack_xtensa/runtime/arduino/arduino_rgb_led.c`）が出す
「RMT の送信が完了した回数」で、`[S3-RGB] tx timeout` が出たらタイムアウト
です。**色そのものは目で見て確かめてください**（ログは送信の成否しか
言いません）。

心拍 `[AtomS3LiteRgb] heartbeat N` は 1000 tick ごとに出続けます。

## 仕組み

`rgbLedWrite()` は M5Stack core の同名関数の置き換えです。core の実装は
ESP-IDF の `esp_driver_rmt` + FreeRTOS に載っていて本ポートでは使えないため、
ヘッダのみの `hal/rmt_ll.h` と RMTMEM 直書きで RMT TX チャネル 0 を駆動し、
完了は割込みではなくポーリングで待ちます（`arduino_interrupt.cfg` は不変）。
色順は core の既定と同じ GRB です。

同じ役割の ESP32-C6 版は `ports/m5stack_riscv/runtime/arduino/arduino_rgb_led.c`
（M5NanoC6 用、例題は `examples/NanoC6Gpio`）で、2 つの違いは S3 版の
ファイル先頭コメントに列挙してあります。
