# Arduino on TOPPERS/FMP3 for M5Stack

M5Stack の 8 機種で、Arduino の `setup()` / `loop()` を
**TOPPERS/FMP3 SMP カーネルの上で**動かすための Arduino ボードパッケージです。

1 つのパッケージに 8 つとも入り、`Tools > Board` で選びます。

| ボード | チップ | 選べる FMP3 Runtime |
| --- | --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 | `Minimal` / `M5Unified + Dual Core` / `WiFi` |
| M5StickS3 | ESP32-S3 / Xtensa LX7 | `Minimal` / `M5Unified + Dual Core` / `WiFi` |
| M5AtomS3 Lite | ESP32-S3 / Xtensa LX7 | `Minimal` / `WiFi` |
| M5Stack Basic | ESP32 / Xtensa LX6 | `Minimal` / `M5Unified + Dual Core` / `WiFi` / `Bluetooth Classic (SPP)` |
| M5Stack ATOM Lite | ESP32-PICO-D4 / Xtensa LX6 | `Minimal` / `WiFi` / `Bluetooth Classic (SPP)` |
| M5NanoC6 | ESP32-C6 / RISC-V | `Minimal` / `WiFi` |
| M5Stamp-C5 | ESP32-C5 / RISC-V | `Minimal` / `WiFi` |
| M5Stamp-P4 | ESP32-P4 / RISC-V デュアルコア | `Minimal` / `WiFi`（hosted。下の表を参照） |

各 Runtime が何を提供し、どのボードで選べるのかは次のとおりです。ビルド時に
1 つ選びます。**どの構成でも普通のスケッチがそのままビルドできます。**

| FMP3 Runtime | 内容 | 選べるボードと、選べない理由 |
| --- | --- | --- |
| `Minimal` | FMP3 起動、`setup()` / `loop()`、heartbeat | 全 8 ボード |
| `M5Unified + Dual Core` | LCD・touch・RTC・PMIC・IMU。SMP（PRC1／PRC2）で起動 | 画面のある 3 ボード（CoreS3・M5StickS3・M5Stack Basic）のみ。M5AtomS3 Lite は画面が無く、主要な例題（LCD へ描くもの）が実機で成立しないことを確認したため出していません。M5Stack ATOM Lite も画面が無く同様ですが、この 1 点だけは実機ではなく AtomS3 Lite からの類推です（[`docs/atomlite-port.md`](docs/atomlite-port.md) 4 節） |
| `WiFi` | scan、Open / WPA2-PSK / WPA3-SAE 接続、DHCP、DNS、TCP | 全 8 ボード。ただし **ESP32-P4 自身に無線はありません**——M5Stamp-P4 の `WiFi` は SDIO でつないだ companion の ESP32-C6（Stamp AddOn C6）へ RPC で渡す hosted Wi-Fi で、その add-on が要ります（[`docs/p4-port.md`](docs/p4-port.md)） |
| `Bluetooth Classic (SPP)` | SPP サーバ | ESP32 の 2 板（M5Stack Basic・M5Stack ATOM Lite）のみ。ESP32-S3／C6／C5／P4 に BR/EDR はありません（M5Stack ATOM Lite ではリンクのみ確認） |

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

## 確認済みの範囲

- **全構成が、対応するすべてのボードでビルドできること。** 本数の正本は
  `python3 scripts/verify_package.py --list-builds` の出力（2026-09-20 時点で
  8 ボード計 124 本）。板ごとに選べる構成は冒頭の表のとおりです
- **Xtensa 3 ボードの成果物が、Windows・Linux x86_64・Apple Silicon macOS の
  3 ホストでバイト単位に一致すること。** RISC-V 3 ボード（M5NanoC6・M5Stamp-C5・
  M5Stamp-P4）はホスト間一致の対象外です（同一ホスト内で build path を変えても
  `.bin` が一致することは実測済み。cross-host は未計測）
- 例題 [`Fmp3Sample1`](examples/Fmp3Sample1) が **6 板・全ポートの実機**で動くこと
  （M5Stack ATOM Lite・CoreS3・M5AtomS3 Lite・M5Stamp-P4・M5NanoC6・M5Stamp-C5）。
  1 秒周期・60 秒採取で周期通知 60〜61 回、**アラームはどの板でも 1 回**、
  `unexpected=0`。M5Stamp-P4 は同じ採取で `core2_alive=60 core2_full=60
  prc2_start=1` も出ます

ボードごとの実機到達点:

| ボード | 実機で確認できていること |
| --- | --- |
| M5Stack CoreS3 | `M5Unified + Dual Core`（LCD・touch、SMP カーネル上）、Wi-Fi STA（Open / WPA2-PSK / WPA3-SAE）-> DHCP -> DNS -> TCP |
| M5StickS3 | `Minimal`（`Blink`）、Wi-Fi スキャン（13 AP）、`M5Unified + Dual Core`（`board_M5StickS3` を検出、240x135 LCD・IMU・PMIC）。[`docs/m5sticks3-m5unified.md`](docs/m5sticks3-m5unified.md) |
| M5AtomS3 Lite | `Minimal`（`Blink` warm 5/5・真cold 5/5）、`GpioInterrupt`（G7 自己駆動）、本体 RGB LED（G35、色順も目視確認）、Wi-Fi スキャン（16 AP）。**STA は WPA3 移行モードの AP に繋がりません**（下記「既知の制限」） |
| M5Stack Basic | `Minimal`、`M5Unified + Dual Core`（LCD、SMP）、Wi-Fi スキャン。**touch・IMU・RTC はこの機種に無いので使えません** |
| M5Stack ATOM Lite | `Minimal`（`Blink` warm 5/5・真cold 5/5）、`GpioInterrupt`（G23、warm・真cold とも PASS）、本体 RGB LED（SK6812 G27、色順も目視確認）、Wi-Fi スキャン（14 AP）と STA 接続（warm 3/3・真cold 3/3）、`Bluetooth Classic`（`discoverable as M5Stack-SPP` まで。ペアリングは未実施）。[`docs/atomlite-port.md`](docs/atomlite-port.md) |
| M5NanoC6 | `Minimal`（`Blink` warm 5/5・真cold 9/10）、Wi-Fi STA -> DHCP -> DNS -> TCP（真cold 3/4、1 回は無音採取で判定不能。接続は 9/9 とも WPA3-SAE）、`pinMode` / `digitalRead` / `attachInterrupt` / RGB LED（例題 `NanoC6Gpio`）。**Open AP と WPA2-PSK 単独 AP は未実測、BLE は未着手**。[`docs/c6-port.md`](docs/c6-port.md) |
| M5Stamp-C5 | `Minimal`（`Blink` warm 5/5・真cold 5/5、CPU 240 MHz）、Wi-Fi scan（2.4 GHz と 5 GHz の両方）、STA -> DHCP -> DNS -> TCP（warm 3/3・真cold 3/3）、`pinMode` と `attachInterrupt`（例題 `GpioInterrupt`、G1）。[`docs/c5-port.md`](docs/c5-port.md) |
| M5Stamp-P4 | `Minimal`（`Blink` warm 5/5・真cold 5/5。**2 コア SMP** が `Processor 2 start.` と `[P4-CORE2] alive` で確認できます）、`GpioInterrupt`（G16 自己駆動）、hosted Wi-Fi のスキャン（14〜16 AP、`[WiFiHosted] companion INIT chip_id=0x0d`）、STA -> DHCP -> DNS -> TCP（warm 3/3・真cold 3/3、DHCP は 8〜9 秒）。[`docs/p4-port.md`](docs/p4-port.md) |

いずれも各ボードの「既知の制限」の節とあわせて読んでください。

## M5Stamp-P4 の既知の制限

- **`Tools > FMP3 Runtime` は `Minimal` と `WiFi` の 2 つです。** 画面が無いので
  `M5Unified + Dual Core` はありません。**GPIO API（`pinMode` など）は他の RISC-V 板と
  同じく `WiFi` 構成にだけあります。**
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

- **ESP32-S3 の 3 板は WPA3 移行モード（WPA2/WPA3 混在）の AP に STA 接続できません。**
  `reason=17`（`IE_IN_4WAY_DIFFERS`）で 4-way handshake の最後に切断されます。
  scan・認証・アソシエーションはすべて通り、**鍵交換の最終段だけ**が落ちます。

  原因は SDK の Wi-Fi blob 側にあり、**このポートで回避できません**（2026-09-19 に
  supplicant の診断ビルドで特定）。AP が beacon に載せる RSNXE を、ESP32-S3 の
  blob が supplicant へ渡しません（`esp_wifi_sta_get_rsnxe()` が実在の BSSID に
  対して NULL を返すことを実機で確認）。一方 AP は EAPOL-Key msg 3 に RSNXE を
  載せてくるため、supplicant の「Beacon と EAPOL-Key の RSNXE は一致すべき」検査
  （`wpa.c` の無条件判定。設定で外す口はありません）が不一致で落とします。

  **ESP32（LX6）の 2 板は同じ AP に繋がります**——同じ supplicant ソース・同じ
  スケッチで、`set AP RSNXE` が LX6 では `f4 01 20`、S3 では空でした。
  ⇒ これは板固有でもこのポート固有でもなく、**チップの blob の差**です。

  **WPA2 専用の AP なら S3 でも繋がる見込み**です（RSNXE を出さない AP なら
  両側とも無しで一致するため）。ただし本ポートでは未実測です。
  切り分けの全経過は [`docs/atoms3lite-port.md`](docs/atoms3lite-port.md) F-3。
- **M5Stack Arduino core のランタイムはリンクされません。** FMP3 がカーネルなので、
  core 自身のランタイムも FreeRTOS も像に入りません。帰結として、**`Serial`・
  `delay()`・`millis()`・`micros()`・`Wire`・`SPI` は使えません**。
  `<Wire.h>` や `<SPI.h>` を include するライブラリ、`Serial` や `delay()` を呼ぶ
  ライブラリは、**どの `Tools > FMP3 Runtime` を選んでもリンクできません**
  （例: `M5-RoverC` は `<Wire.h>` を引くので使えません）。
  リンク時に `i2cInit` / `xQueueCreateMutex` / `delay` などが未定義になった場合、
  リンクドライバがその旨を `--- why ---` として説明します。
  **代わりに各ランタイムの API を使ってください**——`M5Unified + Dual Core` なら
  `M5.Ex_I2C` / `M5.In_I2C` が `Wire` の代わりになり、`loop()` は周期的に
  呼ばれるので `delay()` は要りません。同梱例題はすべてこの流儀で書いてあります。
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
- **OTA 書き込みは対応していません。** ボードは M5Stack core から
  `upload.tool.network=esp_ota` を継いでいますが、**受け手がありません**——
  OTA は端末側で待ち受ける ArduinoOTA 応答器へ PC 側から押し込む仕組みで、
  本ポートには待ち受けソケットも mDNS も無く（`ToppersFMP3WiFiClass` の通信 API は
  `tcpRequest()`＝1 回分のクライアント要求まで）、フラッシュを書き換える
  `esp_ota_*` / `libapp_update.a` も stage に入っていません。core の
  `ArduinoOTA` も、この節の「core のランタイムはリンクされません」のとおり
  使えません。パーティション表は core のものを継いで `otadata` / `app0(ota_0)` /
  `app1(ota_1)` を持っていますが、**入れ物があるだけです。** 対応するには
  待ち受けと書き込みの両方を足す機能追加が要ります。
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

同梱例題のうち [`examples/Fmp3Sample1`](examples/Fmp3Sample1) は、TOPPERS/FMP3 の
`sample1` に相当する縮約版です（`Minimal` 構成専用）。優先度の違う 3 タスク・
`slp_tsk`/`wup_tsk`・周期通知・アラーム通知・`chg_pri` を、スケッチから
`ToppersFMP3_Kernel.h` の薄いラッパ経由で動かします。本家が 31 タスクを cfg で
静的に宣言し、シリアルのコマンドで動くのに対し、こちらはタスクを実行時に作り、
周期通知が進行を進めます（理由は例題の README）。

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
