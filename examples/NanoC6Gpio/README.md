# NanoC6Gpio

M5NanoC6（ESP32-C6）向けの GPIO API、attachInterrupt の自己駆動試験、
RGB LED の例題です。Tools > FMP3 Runtime で **WiFi** を選んで建てます
（`pinMode` / `digitalWrite` / `digitalRead` / `rgbLedWrite` はその
ランタイムにだけリンクされています）。

## 板ガード

`#if defined(ARDUINO_M5STACK_NANO_C6)` で囲ってあります。他の 5 板
（CoreS3 / M5StickS3 / M5AtomS3 Lite / M5Core / M5Stamp-C5）でも
**コンパイルとリンクは通ります**が、setup() が
`[NanoC6Gpio] this example targets the M5NanoC6; nothing to do on this
board` を 1 行出すだけで、何もしません。

ガードを外すと落ちるのは `rgbLedWrite` です（`pinMode` 群は Xtensa にも
C5 にもあるが、RGB ドライバは C6 と ESP32-S3 にしか無い）。当初この節は
「Xtensa には `pinMode` 等がまだ無い」と書いていましたが、それは C6 計画
段6 時点の話で、その後 Xtensa へ `pinMode` 群が載ったので訂正しました
（2026-09-17）。

## 何を試すか（NanoC6 のみ、配線不要）

1. **G7（青 LED）の読み戻し**: `pinMode(7, OUTPUT)` の後 HIGH / LOW を
   書いて `digitalRead` で読み返します。この移植の `OUTPUT` は
   M5Stack core と同じく入力バッファも有効にするので、自分の駆動レベルが
   読めます。`[C6-GPIO] readback ok` が出れば合格です。
2. **attachInterrupt の自己駆動試験（G7）**: 同じピンに ISR を付け、
   自分で LOW -> HIGH -> LOW を 5 回作って発火回数を数えます。
   RISING 5 / FALLING 5 / CHANGE 10 / detach 後 0（負対照）を期待し、
   ランタイムのカウンタ `ard_intr_n_dispatch` = `ard_intr_n_call` = 20、
   `ard_intr_n_orphan` = 0 も見ます。ONLOW / ONHIGH は測りません
   （自己駆動でレベル型を付けると解除できず再入し続けるため）。
   結果は 1 行の `[C6-INTR] VERDICT PASS ...` または `VERDICT FAIL ...`
   です。試験後 G7 は LOW（LED 消灯）、ISR は外した状態にします。
3. **RGB LED（G20、WS2812）**: 起動後 1000 loop tick（約 1 秒）ごとに
   赤 -> 緑 -> 青 -> 消灯 を 2 周（8 回書込み）します。書込みごとに
   ランタイムが `[C6-RGB] tx_done=N` を出します（N は RMT の送信完了を
   確認できた書込みの累計）。`[C6-RGB] tx timeout` が出たら RMT が
   完了を報告していません。
   電源ゲート G19 は `#define NANOC6_RGB_POWER_ENABLE 1` で HIGH に
   します（M5 の資料に基づく。G19 が本当に必要かは未確認で、0 にして
   点くかどうかで確かめる前提のスイッチです）。
4. **heartbeat**: 1000 tick ごとに `[NanoC6Gpio] heartbeat N`。

## 見るところ

USB Serial/JTAG のログで、次の行を `grep` で数えられます。

| 行 | 期待 |
| --- | --- |
| `[C6-GPIO] readback ok` | 1 回 |
| `[C6-INTR] phase=RISING got=5 want=5` | 1 回（FALLING / CHANGE / DETACHED も同様） |
| `[C6-INTR] VERDICT PASS ...` | 1 回 |
| `[C6-RGB] tx_done=` | 8 回（1..8） |
| `[NanoC6Gpio] heartbeat` | 毎秒 |

`[C6-GPIO] pinMode: refused pin` / `unsupported mode` はランタイムが
設定を拒否したときの行で、この例題では出ないはずです。

## 実測（2026-09-15、段6 Task 3、M5NanoC6 実機）

warm 4 回（うち 1 回は JTAG probe 付き）+ 真cold 1 回の 5 run すべてで、
`[C6-GPIO] readback ok` 1、`[C6-INTR] VERDICT PASS rising=5 falling=5 change=10
detached=0 dispatch=20 call=20 orphan=0 acre=2` 1、`[C6-RGB] tx_done` 8（1..8）、
`tx timeout` 0、`## Unexpected` 0、heartbeat 38-39 / 40 秒でした。
**LED の色は目視で確認済み**（赤 -> 緑 -> 青 -> 消灯。`tx_done` は RMT の
送信完了で、光ったことの証拠ではないため、別途ユーザーが目で確かめた）。

G19（`NANOC6_RGB_POWER_ENABLE`）は 1 と 0 の両方で焼き、ログのカウントは
全項目で同一でした（`[C6-RGB] power pin G19 HIGH` / `... left alone` の行だけが
違う）が、**目視では 0 のとき光らない**。G19 HIGH は必須で、既定の 1 のまま
使ってください。

## 制約

- 例題は `target_fput_log` で 1 文字ずつ出します（`Serial` / `delay()` /
  `millis()` は使えません。M5Stack core はリンクされていません）。
  パルスの間隔は ROM の `esp_rom_delay_us` です。
- `attachInterrupt` は `pinMode` を呼びません。先に `pinMode` してから
  `attachInterrupt` してください（この例題はその順序です）。
