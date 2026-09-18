# Arduino on TOPPERS/FMP3 for M5Stack

M5Stack の 8 機種で、Arduino の `setup()` / `loop()` を
**TOPPERS/FMP3 SMP カーネルの上で**動かすための Arduino ボードパッケージです。

| ボード | チップ |
| --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 |
| M5StickS3 | ESP32-S3 / Xtensa LX7 |
| M5AtomS3 Lite | ESP32-S3 / Xtensa LX7 |
| M5Stack Basic | ESP32 / Xtensa LX6 |
| M5Stack ATOM Lite | ESP32-PICO-D4 / Xtensa LX6 |
| M5NanoC6 | ESP32-C6 / RISC-V |
| M5Stamp-C5 | ESP32-C5 / RISC-V |
| M5Stamp-P4 | ESP32-P4 / RISC-V デュアルコア（Wi-Fi は C6 add-on 経由の hosted、下記） |

1 つのパッケージに 8 つとも入り、`Tools > Board` で選びます。M5NanoC6 ・
M5Stamp-C5・M5AtomS3 Lite は `Tools > FMP3 Runtime` に `Minimal` と `WiFi` の
2 構成しかありません（`M5Unified + Dual Core` と `Bluetooth Classic (SPP)` は
ありません。前 2 者は画面も BR/EDR も無いため、M5AtomS3 Lite は画面が無く
`M5Unified + Dual Core` の主要な例題（LCD へ描くもの）が実機で成立しないことを
確認したためです。下記「確認済みの範囲」参照）。M5Stack ATOM Lite は
`Minimal`・`WiFi`・`Bluetooth Classic (SPP)` の 3 構成で、`M5Unified + Dual Core`
だけありません（画面が無い。この 1 点だけは実機ではなく AtomS3 Lite からの類推です。
[`docs/atomlite-port.md`](docs/atomlite-port.md) 4 節）。M5Stamp-P4 は
`Minimal` と `WiFi` の 2 構成です（2 コア SMP で起動します。**ESP32-P4 自身に
無線はありません**——`WiFi` は SDIO でつないだ companion の ESP32-C6
（Stamp AddOn C6）へ RPC で渡す hosted Wi-Fi で、その add-on が要ります。
[`docs/p4-port.md`](docs/p4-port.md)）。

FreeRTOS ではなく FMP3 がブート・割込み・スケジューラを所有し、Arduino の
スケッチは静的に構成された FMP3 タスクから呼ばれます。

## 導入

Arduino IDE の `File > Preferences > Additional boards manager URLs` へ、
**次の 2 つとも**追加してください（この欄は複数書けます。置き換えないこと）。

```text
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
```

そのうえで `Boards Manager` から順に入れます。

1. `M5Stack` を検索して **3.3.9** を `Install`
2. `TOPPERS/FMP3 M5Stack boards` を検索して `Install`

**M5Stack core は自動では入りません。** このボードは Arduino の *core reference*
で M5Stack core のコンパイラ設定とコアソースを参照しており、Arduino には
「別の platform に依存する」という宣言の仕組みが無いためです。入っていないと
Verify の開始直後に次で止まります。

```text
Invalid FQBN: missing platform release m5stack:esp32 referenced by board ...
```

**3.3.9 以外は使えません**（同梱 ESP-IDF v5.5.4 の private な Wi-Fi ABI、
prebuilt archive、include 配置に依存しています）。

ツールチェーンと esptool は Boards Manager が自動で取得します。**スケッチの
ビルドに CMake も Ninja も Python も要りません。** 導入手順と例題の詳細は
[`packaging/README.release.md`](packaging/README.release.md) にあります。

## ランタイム構成（`Tools > FMP3 Runtime`）

ビルド時に 1 つ選びます。**どの構成でも普通のスケッチがそのままビルドできます。**

| 構成 | 内容 |
| --- | --- |
| `Minimal` | FMP3 起動、`setup()` / `loop()`、heartbeat |
| `M5Unified + Dual Core` | LCD・touch・RTC・PMIC・IMU。SMP（PRC1／PRC2）で起動 |
| `WiFi` | scan、Open / WPA2-PSK / WPA3-SAE 接続、DHCP、DNS、TCP |
| `Bluetooth Classic (SPP)` | SPP サーバ。**ESP32 の 2 板（M5Core・M5AtomLite）のみ**（ESP32-S3 に BR/EDR は無い。M5AtomLite ではリンクのみ確認） |

## 確認済みの範囲

- 各構成が、Boards Manager 経由で入れたパッケージから
  **対応するすべてのボードでビルドできること**
  （`python3 scripts/verify_package.py --list-builds` が導出する本数、
  2026-09-17 実測: CoreS3 17・M5StickS3 17・M5AtomS3 Lite 11・M5Core 22・
  M5AtomLite 16・M5NanoC6 11・M5Stamp-C5 11・M5Stamp-P4 3 の計 108 本。
  `Bluetooth Classic` は ESP32 の 2 板専用、M5NanoC6・M5Stamp-C5・M5AtomS3 Lite は
  minimal と wifi-connect のみ、M5Stamp-P4 は minimal のみ。**M5AtomLite を足した
  2026-09-17 の 105 本は Boards Manager 経由ではなく、導入済み platform に対する
  直接 compile で 96 PASS / 0 FAIL / 9 SKIP（生成例題）を確認**。同日の M5Stamp-P4 の
  3 本も同じ形で 3/3 リンク）。Xtensa 3 ボード分については
  Windows・Linux x86_64・Apple Silicon macOS の 3 ホストで実測し、
  成果物が 3 ホストでバイト単位に一致することを確認済み
  （**M5NanoC6 の成果物はホスト間バイト一致の対象外**: 3 ホストでの
  同一性は未計測です。driver 4（S5-8）でビルドパス依存は解消しましたが
  （同一ホスト内で build path を変えても `.bin` が一致することは実測済み）、
  cross-host は未検証のままです。下記「M5NanoC6 の既知の制限」参照）
- CoreS3 実機で、M5Unified（LCD・touch、SMP カーネル上）と Wi-Fi 接続
  （Open / WPA2-PSK / WPA3-SAE -> DHCP -> DNS -> TCP）
- M5Stack Basic 実機で、minimal / M5Unified（LCD、SMP）/ Wi-Fi スキャン と
  all-in-one。**touch・IMU・RTC はこの機種に無いので使えません**
- **M5AtomS3 Lite 実機**で、minimal（`Blink` warm 5/5・真cold 5/5）、
  `GpioInterrupt`（G7 自己駆動、`rising=5 falling=5 change=10 detached=0
  dispatch=call=20 orphan=0`）、本体 RGB LED（G35、`AtomS3LiteRgb` で
  `tx_done=8`。**点灯と色順（赤 -> 緑 -> 青）はユーザーが目視確認**）、
  Wi-Fi スキャン（16 AP）。
  **STA 接続は現状この AP に繋がりません**（`reason=17` で 3/3 切断。
  同じ stage を使う CoreS3 / StickS3 で同じ AP を試していないため、
  板固有か Xtensa 共通かは未確定です。[`docs/atoms3lite-port.md`](docs/atoms3lite-port.md) F-3）
- **M5Stack ATOM Lite 実機**（2026-09-17、板の無い機械で追加したものを別の PC で
  確認）で、minimal（`Blink` warm 5/5・真cold 5/5）、`GpioInterrupt`（G23、warm と
  真cold の両方で `VERDICT PASS`）、本体 RGB LED（SK6812 G27、`AtomLiteRgb` で
  warm・真cold とも `tx_done=8`。**点灯と色順（赤 -> 緑 -> 青）はユーザーが目視確認**）、
  Wi-Fi スキャン（14 AP）、
  **Bluetooth Classic**（`[BluetoothSPP] discoverable as M5Stack-SPP` まで。
  ペアリングは未実施）。**STA 接続はこの板でも未達**ですが、症状は AtomS3 Lite と
  違い `reason=201 (NO_AP_FOUND) rssi=-128`（AP を見つけられない）です。
  詳細と切り分けの次の一手は [`docs/atomlite-port.md`](docs/atomlite-port.md) 6-7 節。
- **M5Stamp-P4 実機**（2026-09-18、ESP32-P4 rev v1.3 + Stamp AddOn C6）で、
  minimal（`Blink` warm 5/5・真cold 5/5。**2 コア SMP** が `Processor 2 start.` と
  `[P4-CORE2] alive` で確認できます）、`GpioInterrupt`（G16 自己駆動、
  `rising=5 falling=5 change=10 detached=0 dispatch=call=20 orphan=0`）、
  **hosted Wi-Fi のスキャン**（14〜16 AP）。この板の無線は P4 自身ではなく
  **SDIO で繋いだ ESP32-C6（Stamp AddOn）**が担い、`[WiFiHosted] companion INIT
  chip_id=0x0d` が相手と話せている一次証拠です。
  **STA 接続 -> DHCP -> DNS -> TCP** も通ります（warm 3/3・真cold 3/3、
  各回 `connected and DHCP completed` と `TCP received=255`。DHCP は 8〜9 秒）。実機でしか出なかった欠陥——SDK の bootloader が仕掛けた
  ウォッチドッグによる起動ループ、`[WiFiScan]` の印字漏れ、PRC2 の 1 kHz ログ、
  採取台本 5 件——は [`docs/p4-port.md`](docs/p4-port.md) 2-8 節に、
  DHCP が通らなかった原因 2 件（cfg のオブジェクト ID がフォールバック値で
  固まっていた／`host_by_name` の戻り値の向きが逆）は同 2-9 節にあります。
  どちらも**戻り値を見ても捕まらない**型でした。コンソールの行頭欠落
  （2-8 節で未解決としたもの）は原因が確定して直っています——
  `fmp3_core` の `sio_irdy_snd()` が `sio_snd_chr()` の戻り値を捨てていた
  という**全チップ共有**の欠陥で、詳細は同 2-10 節。
- M5StickS3 実機で、minimal（`Blink`）、Wi-Fi スキャン（13 AP を検出）、
  M5Unified（`board_M5StickS3` を検出、240x135 の LCD・IMU・PMIC）。
  当初この機種だけ M5Unified が動かなかった経緯と原因は
  [`docs/m5sticks3-m5unified.md`](docs/m5sticks3-m5unified.md)
- **M5NanoC6（ESP32-C6）は `minimal` / `wificonnect` の 2 構成で配布物に
  収録されています。** `Minimal`・`WiFi` の 2 つが `Tools > FMP3 Runtime` に
  出ます。実機で確認済みなのは、stock M5Stack bootloader のまま
  minimal（`Blink`）が起動すること（warm 5/5、真cold 9/10）と、
  Wi-Fi STA -> DHCP -> DNS -> TCP がユーザーの実 AP（WPA2/WPA3 混在。**接続は 9/9 とも
  WPA3-SAE**）に対して通ること（真cold 3/4、1 回は無音採取で成否判定不能）、および `WiFi` 構成で
  `pinMode` / `digitalRead` の読み戻し、`attachInterrupt` の自己駆動試験、RGB LED への
  RMT 送信完了が通ること（例題 `NanoC6Gpio`、warm 4 + 真cold 1。LED の色は赤 -> 緑 -> 青を目視確認）
  です。**Open AP と WPA2-PSK 単独の AP は用意できず未実測（混在 AP では WPA3-SAE が選ばれた）、
  BLE は未着手、M5Unified 相当の profile はありません**（下記「M5NanoC6 の既知の制限」）。判断と到達点は
  [`docs/c6-port.md`](docs/c6-port.md)
- **M5Stamp-C5（ESP32-C5）は `minimal` / `wificonnect` の 2 構成で配布物に
  収録されています**（2026-09-16、段5）。`Minimal`・`WiFi` の 2 つが
  `Tools > FMP3 Runtime` に出ます。実機で確認済みなのは、stock M5Stack
  bootloader（**@0x2000**）のまま minimal（`Blink`）が起動すること
  （warm 5/5・真cold 5/5、CPU 240 MHz）と、Wi-Fi の scan が 2.4 GHz と
  5 GHz の両方を拾うこと、STA -> DHCP -> DNS -> TCP がユーザーの実 AP に
  対して warm 3/3・真cold 3/3 で通ること、`WiFi` 構成で `pinMode` の
  読み戻しと `attachInterrupt` の自己駆動試験（例題 `GpioInterrupt`、G1）が
  PASS することです。判断 A0-A12 と段ごとの到達点は
  [`docs/c5-port.md`](docs/c5-port.md)（下記「M5Stamp-C5 の既知の制限」も
  読んでください）

## M5Stamp-P4 の既知の制限（2026-09-17）

- **`Tools > FMP3 Runtime` は `Minimal` だけです。** P4 自身に無線が無く、Wi-Fi は
  Stamp AddOn C6 経由の hosted 方式（SDIO + esp-hosted）になりますが、その Arduino
  向けの層はまだありません（`docs/p4-port.md` 4 節、次の段）。画面も無いので
  `M5Unified + Dual Core` もありません。**GPIO API（`pinMode` など）もまだありません**
  （他の RISC-V 板と同じく `WiFi` 構成側に付ける予定）。
- **2 コア（SMP）で起動します。** スケッチは PRC1 で、PRC2 では runtime 側の小さな
  タスクが 1 秒ごとに `[P4-CORE2] alive N` を出します（core1 が動いている証拠。
  消し方は今のところありません）。
- **CPU は 360 MHz 固定**（bootloader が残す 90 MHz から seam が昇圧）。
- **bootloader は flash の 0x2000**（M5Stamp-C5 と同じ）。**`Tools > ChipVariant` は
  ありません**（上流 core にはある。stage が `esp32p4_es`＝rev v3 未満の silicon 用 SDK で
  建っているため、この板は `esp32p4_es` に固定してあります。rev v3 以降の P4 では
  動かない可能性があります）。
- **core 3.3.8 の欠陥の回避は 3.3.9 で外しました**: 3.3.8 では上流の
  `m5stack_stamp_p4` 板が空のスケッチでも `esp32-hal-spi.c` のコンパイルで落ちた
  ため（`BOARD_SDMMC_POWER_CHANNEL` 未定義）、本板は
  `-DBOARD_SDMMC_POWER_CHANNEL=4` を足して回避していました。3.3.9 は上流で直って
  おり（`cores/` から当該識別子が消え、`arduino-cli compile -b
  m5stack:esp32:m5stack_stamp_p4` が空スケッチを通ることを実測）、値 4 は Tab5 の
  LDO チャネルで本板のものとは限らないため、定義を外して上流の値を継ぐようにしました。
- **Wi-Fi は companion 頼みです**: `WiFi` 構成は SDIO で繋いだ
  **Stamp AddOn C6**（ESP32-C6）へ RPC を投げる hosted 実装で、add-on が無い
  StampP4 単体では上がりません。実機で確認した範囲（scan / STA -> DHCP ->
  DNS -> TCP）は上記「確認済みの範囲」。

## M5Stamp-C5 の既知の制限（2026-09-16）

- **on-board の RGB LED はありません。** `rgbLedWrite` は M5Stamp-C5 の
  ランタイムに入っていません（板に WS2812 系の LED が無いため）。同梱例題
  `NanoC6Gpio` は M5Stamp-C5 では板ガードにより「この例題は M5NanoC6 向け」の
  1 行を出して何もしません。M5Stamp-C5 の GPIO を実際に動かして見せるのは
  例題 `GpioInterrupt` のほうです（試験ピンは **G1**。青 LED の G28 は BOOT
  ピンなので試験には使いません）。
- **GPIO API は `WiFi` 構成にだけあり、`Minimal` にはありません。**
  `pinMode` / `digitalWrite` / `digitalRead` / `attachInterrupt` は
  `WiFi`（wificonnect）ランタイムにだけリンクされます（M5NanoC6 と同じ
  切り分け）。`attachInterrupt` は `pinMode` を呼ばないので、先に `pinMode`
  してください。`delay()` / `Serial` が使えないのは他の構成と同じです。
- **CPU は 240 MHz 固定です**（`Minimal` / `WiFi` とも）。
- **bootloader は flash の 0x2000 に置きます**（M5NanoC6 の 0x0 とは違います）。
  **asp3_esp_idf の Direct Boot 像を焼いたことのある板では、そのままでは
  起動しません**: flash 0x0 に Direct Boot の magic が残っていると ROM が
  それを見てしまい、0x2000 の bootloader へ進みません。その場合は
  `esptool erase-region 0x0 0x2000` で先頭 8 KB を消してから書き込んで
  ください（Arduino IDE から焼くだけの利用者には関係ありませんが、同じ板で
  両方を試す場合に踏みます）。
- **実測できた認証方式は WPA3-SAE だけです。** 試験に使えた AP は
  WPA2/WPA3 混在の 1 台で、実際に張られた接続は 8/8 とも WPA3-SAE
  （`authmode=6`）でした。**WPA2-PSK 単独の AP と Open AP は未実測**です。
- **scan が返す AP は最大 20 件です。** アダプタの記録上限
  （`TOPPERS_WIFI_MAX_RECORDS` = 20）で、実測では 5 回とも 20 件に張り付いて
  いました。**「20 件見えた」は「近所に 20 台ある」ではありません**（同じ板・
  同じ場所で上限の無い計測をすると 19-29 台でした）。
- **5 GHz での接続は未実測です。** scan には 5 GHz の ch（36/44/48/52/116）が
  出ますが、実際に張れた接続は 8/8 とも ch 10（2.4 GHz）でした。
- **BLE と 802.15.4（Zigbee / Thread）はありません。**
- **M5Unified 相当の profile はありません**（LCD が無いため）。
- **成果物のホスト間（Windows／Linux／macOS）バイト一致は未計測です**
  （M5NanoC6 と同じ。下記「M5NanoC6 の既知の制限」の同項目を参照）。
- **`random()` を呼ぶスケッチの ROM `rand()` ハザード**も M5NanoC6 と同じです
  （下記）。

## M5NanoC6 の既知の制限（2026-09-15）

- **IDE の Verify 後に出る size 表示の分母**は、M5NanoC6 では ld の実際の RAM
  上限（`upload.maximum_data_size=452112`）に上書きしてあります。継承元の
  FreeRTOS 前提の値（327,680）のままだと実際より大きい使用率に見えたため
  （段5 判断 S5-1）。他の 3 ボードは変えていません。
- **`random()` を呼ぶスケッチは ROM の `rand()` を使い、未初期化ポインタを
  辿る既知のハザードがあります。** wifi-connect 構成で `random()`/`rand()` を
  呼ぶと、現状の 4 例題では到達しない ROM リンカスクリプトの経路
  （`syscall_table_ptr` 未初期化）に踏み込む可能性があります。自分のスケッチで
  乱数が要る場合は使う前に注意してください。
- **`WiFiConnect` は、接続前に一度切断イベント（`NO_AP_FOUND` 等）を受けると
  自分からは再接続しません。** これは Xtensa の 3 ボードと同じ製品挙動です
  （adapter のコードは共通）。有界の再試行は現状スケッチ側の責務です。
- **GPIO API は `WiFi` 構成にだけあり、`Minimal` にはありません。** M5NanoC6 では
  `pinMode` / `digitalWrite` / `digitalRead` / `attachInterrupt` / `rgbLedWrite` が
  `WiFi`（wificonnect）ランタイムにリンクされていて、同梱例題 `NanoC6Gpio` と
  `GpioInterrupt` で実機確認済みです（G7 を自分で駆動する自己駆動試験: RISING 5 / FALLING 5 / CHANGE 10 /
  detach 後 0、warm 4 回 + 真cold 1 回すべて `VERDICT PASS`。段6）。`pinMode` が受ける
  mode は `INPUT` / `INPUT_PULLUP` / `INPUT_PULLDOWN` / `OUTPUT` の 4 つで、それ以外は
  何も書かずにログを出します。USB の G12 / G13 は拒否します。`attachInterrupt` は
  `pinMode` を呼ばないので、先に `pinMode` してください。on-board RGB LED（G20、
  WS2812 系）は `rgbLedWrite` が RMT で駆動し、赤 -> 緑 -> 青 -> 消灯の色サイクルを
  実機で目視確認しています。**G19（電源イネーブル）を HIGH にしないと光りません**
  （送信完了のログは G19 に関係なく出るので、光らないときはまず G19 を疑って
  ください。例題は `NANOC6_RGB_POWER_ENABLE 1` で G19 を HIGH にします）。
  `delay()` / `Serial` が使えないのは他の構成と同じです。
- **成果物のホスト間（Windows／Linux／macOS）バイト一致は未計測です。**
  driver 4（S5-8）で「同じホスト内で build path を変えても `.bin` が一致する」
  ことは実測済みですが、cross-host の一致はまだ確認していません。
- **M5Unified 相当の profile はありません。** LCD・touch・IMU 等を使う
  `M5Unified + Dual Core` 構成は M5NanoC6 には提供していません。

## M5Unified をスケッチから使うとき

`<M5Unified.h>` ではなく **`<ToppersFMP3_M5Unified.h>`** を include してください。
プリビルトランタイムは M5GFX を `ARDUINO` 未定義でビルドしており（定義すると
M5GFX が本移植の持たない Arduino-ESP32 の SPI HAL 経路に切り替わるため）、
スケッチ側は arduino-cli が必ず `-DARDUINO` を付けるので、素の
`<M5Unified.h>` ではレイアウトが 4 バイトずれて `M5.` の値が壊れます。
このヘッダが両者を揃えます。誤って先に `<M5Unified.h>` を読んだ場合は
`static_assert` でビルドが止まります（詳細は
[`docs/m5unified-sketch-abi.md`](docs/m5unified-sketch-abi.md)）。

## 制約

- **Bluetooth Classic は接続に認証を要求しません。** SPP サーバは
  `ESP_SPP_SEC_NONE` で起動するので、電波の届く範囲の誰でも、ペアリングを
  経ずに接続してデータを送受信できます。2026-09-02 に実機で確認しました--
  PC 側のボンドを消した状態から接続でき、ボンドは作られず、デバイス側にも
  ペアリングの確認は一度も出ませんでした（SSP の "Just Works"）。
  コードには数値比較の自動承認と、レガシーペアリング用の固定 PIN `1234` も
  ありますが、この経路ではどちらも通りません。**外に出したくないものを
  このリンクに載せないでください。** ペアリング方針をスケッチ側から
  選べるようにするのは別の設計課題で、まだ手を付けていません。
- **Arduino / FreeRTOS API は完全互換ではありません。** 各構成と例題で実際に
  使用したサブセットのみ対応しています。未実装の API は無条件に成功を返すの
  ではなく、リンクエラーになるか失敗を返します。
  M5Stack core の Arduino API 本体（`core.a`）はリンクしません。**`Serial` と
  `delay()` は使えません。** どちらも FreeRTOS を呼ぶためです。ログは
  `target_fput_log()` へ書いてください（同梱例題はすべてそうしています）。
  複数ファイルのスケッチと、独自の `.cpp` を持つライブラリはリンクできます。
  `pinMode` / `digitalWrite` / `digitalRead` と `attachInterrupt` は、4 ボードとも
  `minimal` 以外の構成（`M5Unified + Dual Core` / `WiFi` / `Bluetooth Classic (SPP)`）
  で使えます（Xtensa 3 ボードの `pinMode` 群は 2026-09-15 に追加。`WiFi` 構成で
  `GPIO` が未定義になる問題も同日に修正）。同梱例題 `GpioInterrupt`（自己駆動の
  割込み試験: RISING 5 / FALLING 5 / CHANGE 10 / detach 後 0）を CoreS3（G8）・
  M5Stack Basic（G16）・M5StickS3（G9）・M5NanoC6（G7）・M5Stamp-C5（G1）の
  `WiFi` 構成で 5 板とも実機確認済み（M5AtomS3 Lite は G7、M5Stack ATOM Lite は
  G23 で、いずれも実機確認済み）。`pinMode` が受ける mode は `INPUT` / `INPUT_PULLUP` /
  `INPUT_PULLDOWN` / `OUTPUT` の 4 つ。拒否するピン: CoreS3 / M5StickS3 は USB の
  G19 / G20、flash の G26-32、存在しない G22-25、M5Stack Basic は UART0 の G1 / G3、
  flash の G6-11、GPIO でないパッド（24, 28-31）、M5NanoC6 は USB の G12 / G13 と
  flash の G24-30、M5Stamp-C5 は USB の G13 / G14 と内蔵 flash の MSPI パッド
  G15-22（GPIO は G0-G28 まで）。M5Stack Basic の入力専用 G34-39 は `OUTPUT` と pull 付き mode を
  拒否し（ログを出す）、RTC 系パッド（G0/2/4/12-15/25-27/32/33）の pull は
  RTC_IO レジスタで設定します（Grove Port B の G26 で pull-up / pull-down の
  読み戻しを実機確認済み）。`attachInterrupt` は `pinMode` を呼ばないので
  先に `pinMode` してください。`delay()` / `Serial` は引き続きありません。
- M5Stack Basic の `M5Unified + Dual Core` 構成で、ローカルの `char` 配列を持つ
  関数を含むスケッチが `_exit` / `_kill` / `_getpid` 未定義でリンクに落ちる問題は
  2026-09-15 に修正しました（SDK の `-fstack-protector` が `__stack_chk_fail` ->
  `_exit` を参照する経路。実機でカナリア破壊 -> `*** stack smashing detected ***`
  -> `libc: _kill(sig=6)` で停止することまで確認）。
- **Intel Mac には対応していません。** ビルドに必要なリンクドライバをホストごとに
  同梱していますが、`x86_64-apple-darwin` 向けは含まれていません。
- FMP3 の `dly_tsk` の `RELTIM` はこのポートではマイクロ秒で、FreeRTOS API の
  tick とは単位が異なります。
- M5Stack Arduino core は **3.3.9 固定**、`M5GFX` は **0.2.29**、`M5Unified` は
  **0.2.22** 固定です（上げたときの測定は [`docs/version-bumps.md`](docs/version-bumps.md)）。
- **Bluetooth は Classic (SPP) だけで、M5Core 限定です。** BR/EDR 無線は
  ESP32 にしかなく、ESP32-S3 は BLE のみですが、BLE はどのボードにも
  入っていません。

## リポジトリの構成

| パス | 役割 |
| --- | --- |
| `src/` | Arduino builder が再帰コンパイルする領域。`Arduino.h` だけに依存 |
| `ports/m5stack_xtensa/runtime/` | FMP3 の Xtensa ポート（ESP32-S3 / LX7 と ESP32 / LX6。板ごとの差は `arduino/arduino_rgb_led*.c` のようにチップ単位） |
| `ports/m5stack_riscv/runtime/` | FMP3 の RISC-V ポート（ESP32-C6・ESP32-C5・ESP32-P4。チップ分岐で 1 ポート） |
| `fmp_app/` | 開発ツリーでのみ使う FMP3 アプリケーション |
| `examples/` | 同梱例題 |
| `scripts/` | ビルド・パッケージング |
| `packaging/` | 配布物の定義と利用者向け手順 |
| `third_party/fmp3_core` | FMP3 本体（submodule） |

ソースからビルドする手順は [`BUILDING.md`](BUILDING.md) にあります。
ESP32-C6（M5NanoC6）の判断と到達点は [`docs/c6-port.md`](docs/c6-port.md)、
ESP32-C5（M5Stamp-C5）は [`docs/c5-port.md`](docs/c5-port.md)、
M5AtomS3 Lite は [`docs/atoms3lite-port.md`](docs/atoms3lite-port.md)、
M5Stack ATOM Lite は [`docs/atomlite-port.md`](docs/atomlite-port.md)、
ESP32-P4（M5Stamp-P4）は [`docs/p4-port.md`](docs/p4-port.md)。
固定している依存（M5Stack core・M5GFX・M5Unified）を上げたときに何を測ったかは
[`docs/version-bumps.md`](docs/version-bumps.md)。

## ライセンス

リポジトリ単一のライセンスではありません。各ファイルのヘッダと
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を正としてください。
