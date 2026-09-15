# GpioInterrupt

`pinMode` / `digitalWrite` / `digitalRead` と `attachInterrupt` を、配線なしで
確かめる例題です。1 本のピンを OUTPUT にし（入力バッファは有効のまま）、
スケッチ自身がパルスを作り、同じピンに attach した割込みの回数を数えます。

- 期待値: RISING 5 / FALLING 5 / CHANGE 10 / detach 後 0、runtime の
  `dispatch = call = 20`、`orphan = 0`。1 行の `[GPIO-INTR] VERDICT PASS ...` が出ます。
- ピン: M5NanoC6 G7（青 LED、パルスが見える）、CoreS3 G8（Grove B）、
  M5Stack Basic G16（Grove C）。M5StickS3 はピン未割当（リンクのみ、その旨を印字）。
- 構成: `attachInterrupt` を持つ構成（`M5Unified + Dual Core` / `WiFi` /
  `Bluetooth Classic (SPP)`）。`minimal` にはリンクできません。
- 制約: `delay()` / `Serial` は使えないので、ROM の `esp_rom_delay_us()` と
  loop() の呼出し回数で間隔を作ります。ONLOW / ONHIGH は自己駆動では
  測れないので対象外です。

2026-09-15 実測: CoreS3 / M5Stack Basic / M5NanoC6 の `WiFi` 構成で VERDICT PASS。
