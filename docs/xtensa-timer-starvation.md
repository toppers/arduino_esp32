# Wi-Fi 切断後に全タスクが 296 秒止まる（Xtensa・2026-09-20 修正）

## 症状

`WiFi` 構成のスケッチで STA が切れたあと、**`loop()` が呼ばれなくなり
コンソールも沈黙する**。M5CoreS3 実機で 120 秒採取して `tick=3` で停止。
CPU は動いている（JTAG で PC は進む）のに、runtime の `loop_calls` は
進まない。

**永久ではない。296 秒で自分で復帰する**（360 秒採取で `tick=3` の次が
`tick=4`、合計 64 ティック＝止まっていたのは 296 秒）。採取窓がそれより
短いと「ハングした」ように見える。

## 根本原因

2 つの欠陥の合わせ技で、**どちらもタイマの表現の問題**である。

1. **`esp_wifi_adapter.c` の `timer_arm_wrapper()` が ms→us を 32bit で計算
   していた**（`tmout * 1000U`）。Wi-Fi blob は association 時に
   `_timer_arm(ptimer, 0xfffffffe ms, false)`＝約 49.7 日を「事実上発火させない」
   番人として渡す。32bit で 1000 倍すると **0xfffff830 us ＝ 約 4,294.97 秒**へ
   折り返り、49.7 日のタイマが 71.6 分のタイマに化ける。
2. **`esp_shim.c` の `esp_shim_timer_task()` が待ち時間を頭打ちにせず、
   `twai_sem()` の戻り値も捨てていた**。FMP3 は `tmout > TMAX_RELTIM`
   （4,000,000,000 us ≒ 4,000 秒）を **E_PAR で即座に弾く**
   （`VALID_TMOUT`、`fmp3_core/kernel/check.h:92`）。

⇒ 切断で短周期タイマが全部 disarm されると、**残った唯一の期限が
「4,294.97 秒後」**になる。これは `TMAX_RELTIM` を 294.97 秒ぶん超えるので、
タイマタスク（優先度 **2**）は `twai_sem` が E_PAR で即戻るたびに回り続け、
LOGTASK(3)・NET_TSK(4)・ARDUINO_TASK(10) を**全部飢餓させる**。
期限までの残りが 4,000 秒を切った瞬間に合法な待ちになり、系は動き出す
——これが「296 秒で復帰」の正体である。

## 修正

- `timer_arm_wrapper()` は `esp_shim_timer_arm_ms()`（64bit）へ委譲する。
- `SHIM_TIMER.period_us` を `uint64_t` へ広げ、`esp_shim_timer_arm_us64()` を
  内部実装にする。
- タイマタスクは待ちを `TMAX_RELTIM` で頭打ちにし、`twai_sem()` の戻り値を
  見て、E_OK / E_TMOUT 以外なら数えて申告し **1ms 寝る**（この経路が
  100% CPU を占めないことを構造的に保証する）。

**これは dev リポジトリで 2026-08-14 に特定・修正済みの欠陥**
（`.steering/20260814-wifi-disconnect-hang/`）で、arduino 側は
**RISC-V のコピーにだけ修正が入り、Xtensa のコピーに入っていなかった**。
本修正はその移植である。

## 実測

| | 修正前 | 修正後 |
|---|---|---|
| 120 秒採取のティック数 | **3**（以後沈黙） | **117**（欠番なし） |
| `esp_shim: timer_task twai_sem` エラー | －（計器が無かった） | **0 回** |
| `reason=17` の切断 | 起きる | 起きる（別件・S3 の既知の制限） |

配布バイト列への影響（X-check、修正前の 7 ステージを基準）:

```
esp32s3/wifi-connect: DIFF (2 files)  objs/esp_shim.o  objs/esp_wifi_adapter.o
esp32/wifi-connect:   DIFF (2 files)  objs/esp_shim.o  objs/esp_wifi_adapter.o
esp32/bt-classic:     DIFF (1 files)  objs/esp_shim.o
esp32s3/minimal, esp32s3/m5-unified, esp32/minimal, esp32/m5-unified: MATCH
```

**触った 2 ファイル以外は 1 バイトも動いていない。**

### 罠（次にやる人へ）

最初の比較で `bt-classic` が MATCH と出たが、これは**建て直していなかった
だけ**だった（`bt-classic` は `build_prebuilt_stages.py` の既定 profile に
入っていない）。`esp_shim.o` を含むステージが「変更したのに一致」したら、
まず自分がそれを建て直したかを疑うこと。

## 残っている制限（本修正では直らない）

ESP32-S3 が WPA3 移行モードの AP に `reason=17` で繋がらないことは**別の
原因**（SDK の blob が RSNXE を supplicant へ渡さない）で、このポートでは
直せない。README「制約」と `docs/atoms3lite-port.md` F-3 を参照。
本修正で変わるのは「繋がらなかったあと、スケッチが止まらなくなる」ことである。
