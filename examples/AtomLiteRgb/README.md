# AtomLiteRgb

M5Stack ATOM Lite（ESP32-PICO-D4 / LX6）の本体 RGB LED（SK6812 3535、
データ G27）を点灯させる例題です。Tools > FMP3 Runtime で **WiFi** を選んで
建てます（`rgbLedWrite` はそのランタイムにだけリンクされています）。

## 板ガードとランタイムガード

`#if defined(ARDUINO_M5STACK_ATOM)` で囲ってあります。他の 6 板
（CoreS3 / M5StickS3 / M5AtomS3 Lite / M5Core / M5NanoC6 / M5Stamp-C5）でも
**コンパイルとリンクは通り**、setup() が `[AtomLiteRgb] this example targets
the M5AtomLite; nothing to do on this board` を 1 行出すだけです。

このマクロは上流の M5Stack core の `m5stack_atom` 行（`build.board=M5STACK_ATOM`、
ATOM Lite / Matrix / Echo 共通）をそのまま継いだものです（AtomS3 Lite のように
本パッケージが上書きして作ったものではありません）。

ATOM Lite には `Minimal` と `Bluetooth Classic (SPP)` もありますが、どちらも
`rgbLedWrite` をリンクしません。その構成で建てると
`#error AtomLiteRgb needs the WiFi runtime ...` で止まります（未定義参照の
一覧ではなく、メニュー名で止める）。

## 何をするか（ATOM Lite のみ、配線不要）

`loop()` 1000 tick（約 1 秒）ごとに 赤 -> 緑 -> 青 -> 消灯 を 2 周します。
書き込みのたびに

```
[LX6-RGB] write red
[LX6-RGB] tx_done=1
```

が出ます。`tx_done` はランタイム側
（`ports/m5stack_xtensa/runtime/arduino/arduino_rgb_led_lx6.c`）が出す
「RMT の送信が完了した回数」で、`[LX6-RGB] tx timeout` が出たらタイムアウト
です。**色そのものは目で見て確かめてください**（ログは送信の成否しか
言いません）。8 回書けば `tx_done=8` で終わり、その後は心拍だけになります。

心拍 `[AtomLiteRgb] heartbeat N` は 1000 tick ごとに出続けます。

## 仕組み

`rgbLedWrite()` は M5Stack core の同名関数の置き換えです。core の実装は
ESP-IDF の `esp_driver_rmt` + FreeRTOS に載っていて本ポートでは使えないため、
ヘッダのみの `hal/rmt_ll.h` と RMTMEM 直書きで RMT TX チャネル 0 を駆動し、
完了は割込みではなくポーリングで待ちます（`arduino_interrupt.cfg` は不変）。
色順は core の既定と同じ GRB です。

ESP32 (LX6) 版は S3 版（`arduino_rgb_led.c`、M5AtomS3 Lite 用）の写しで、
違い（`rmt_ll_tx_stop` が無い、group divider が無視される、SK6812 と WS2812B の
両方の規格に入るビットタイミング、番地と記号番号）は LX6 版のファイル先頭
コメントに列挙してあります。

## 実機での確認（2026-09-17 時点で未実施）

この例題を書いた機械には板が無く、ここまでの検証はコンパイルとリンクだけです。
実機では `tx_done=8` / `timeout` 無し、および赤 -> 緑 -> 青の目視を確かめて
ください（`docs/atomlite-port.md`）。
