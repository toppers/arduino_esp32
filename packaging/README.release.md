# ToppersFMP3-M5Stack

M5Stack の 8 機種向けに、ArduinoスケッチとTOPPERS/FMP3を統合する
Arduinoボードパッケージです。

| ボード | チップ | `Tools > Board` |
| --- | --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 | `M5CoreS3 (TOPPERS/FMP3)` |
| M5StickS3 | ESP32-S3 / Xtensa LX7 | `M5StickS3 (TOPPERS/FMP3)` |
| M5AtomS3 Lite | ESP32-S3 / Xtensa LX7 | `M5AtomS3Lite (TOPPERS/FMP3)` |
| M5Stack Basic | ESP32 / Xtensa LX6 | `M5Core (TOPPERS/FMP3)` |
| M5Stack ATOM Lite | ESP32-PICO-D4 / Xtensa LX6 | `M5AtomLite (TOPPERS/FMP3)` |
| M5NanoC6 | ESP32-C6 / RISC-V | `M5NanoC6 (TOPPERS/FMP3)` |
| M5Stamp-C5 | ESP32-C5 / RISC-V | `M5StampC5 (TOPPERS/FMP3)` |
| M5Stamp-P4 | ESP32-P4 / RISC-V デュアルコア | `M5StampP4 (TOPPERS/FMP3)`（`WiFi` は AddOn C6 が要ります） |

1つのパッケージに8つとも入っています。M5NanoC6・M5Stamp-C5・M5AtomS3 Liteは
`Tools > FMP3 Runtime`に`Minimal`と`WiFi`の2つしかありません
（`M5Unified + Dual Core`は画面を持つCoreS3・M5StickS3・M5Stack Basicのみ、
`Bluetooth Classic (SPP)`はESP32の2機種＝M5Stack BasicとATOM Liteのみ）。
M5AtomS3 Liteで実機確認したのは`Blink`・`GpioInterrupt`・本体RGB LED・
Wi-Fiスキャンで、**STA接続はWPA3移行モード（WPA2/WPA3混在）のAPに対して
できません**（`reason=17`。下の「制約」参照。詳細はdocs/atoms3lite-port.md）。
M5Stack ATOM Liteは`Minimal`・`WiFi`・`Bluetooth Classic (SPP)`の3つが出ます。
実機で確認したのは`Blink`・`GpioInterrupt`（G23）・本体RGB LED（SK6812、G27。
例題は`AtomLiteRgb`）・Wi-Fiスキャン・**Wi-Fi STA接続**（warm 3/3・真cold 3/3。
WPA2/WPA3混在のAP、ch=10）・Bluetooth Classic（`discoverable`まで。
ペアリングは未実施）です。STA接続は初回（板の無い機械で追加した直後）に
`NO_AP_FOUND`で失敗しましたが、別の機械・別のAPでは繋がりました
（詳細はdocs/atomlite-port.md 8節）。
M5Stamp-P4は`Minimal`と`WiFi`の2つです。2コア SMP で起動し、PRC2 が
`[P4-CORE2] alive N`を出します。**ESP32-P4自身に無線はありません**——`WiFi`は
SDIOでつないだcompanionの**ESP32-C6（Stamp AddOn C6）**へRPCで渡すhosted方式で、
**そのadd-onが無いと上がりません**。実機ではスキャンと
STA接続->DHCP->DNS->TCPまで通っています（詳細は docs/p4-port.md）。

Arduinoの`setup()`／`loop()`は、FreeRTOSではなくTOPPERS/FMP3 SMPカーネルの
タスクとして動きます。ブート、割込み、スケジューラはFMP3が所有します。

## 必要な環境

- Arduino IDE 2.x
- **M5Stack Arduino core 3.3.9**（先に入れておく必要があります。次章参照）
- M5Unified 0.2.22 ／ M5GFX 0.2.29（`M5Unified` profileを使う場合のみ）

**CMake、Ninja、Pythonは要りません。** スケッチのビルドに使うのは
M5Stack coreに同梱のツールチェーンとesptoolだけで、FMP3側は事前にビルド済みの
形で同梱されています。

## インストール

### 1. M5Stack Arduino core 3.3.9

**これは自動では入りません。** 本ボードはArduinoの
*core reference*でM5Stack coreのコンパイラ設定とコアソースを参照しており、
Arduinoの仕組みには「別のplatformに依存する」という宣言が無いためです。
入っていないとVerifyの開始直後に次のエラーで止まります。

```text
Invalid FQBN: missing platform release m5stack:esp32 referenced by board ...
```

`File > Preferences > Additional boards manager URLs`へ次のURLを追加します。

```text
https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
```

続いて`Tools > Board > Boards Manager`で`M5Stack`を検索し、**3.3.9**を選んで
`Install`してください。

> **この欄は複数のURLを書けます。** 次章のTOPPERS/FMP3のURLと両方必要なので、
> **置き換えずに追加**してください（改行区切り、またはカンマ区切り）。
> どちらか一方だけだと、そのボードがBoards Managerに出てきません。

> **3.3.9以外のバージョンは使えません。** 同梱ESP-IDF v5.5.4のprivate Wi-Fi ABI、
> archive、include配置に依存しています。

### 2. TOPPERS/FMP3ボード

`File > Preferences > Additional boards manager URLs`へ次のURLを追加し、
`Boards Manager`で`TOPPERS/FMP3 M5Stack boards`を検索して入れます。

```text
https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
```

このURLは版が上がっても変わりません。追加は最初の一度だけです。

必要なツールチェーン（`esp-x32`、`esptool_py`、SDK、FMP3リンクドライバ）は
ボードマネージャが自動で取得します。ZIPの手動追加やスクリプトの実行は不要です。

**M5NanoC6とM5Stamp-C5を使う場合も追加のツールは不要です。** RISC-Vツールチェーン
（`esp-rv32`）と各チップのSDK（M5NanoC6は`esp32c6-libs`、M5Stamp-C5は`esp32c5-libs`）は、
前章で入れたM5Stack Arduino core 3.3.9に既に同梱されています。この2枚のために
別のインデックスやツールを追加する必要はありません。

削除も`Boards Manager`の`Remove`で行えます。

### 3. M5Unifiedライブラリ（`M5Unified` profileのみ）

`Tools > Manage Libraries`から`M5Unified` **0.2.22** と`M5GFX` **0.2.29**
を入れます。

> **同梱のFMP3ランタイムはこのバージョンのソースに対してビルドされています。**
> ライブラリだけ更新するとヘッダと事前ビルド済みオブジェクトが食い違い、
> 未定義シンボルやリンクエラーになります。他のprofileでは不要です。

ライブラリ本体はボードパッケージに同梱されているので、
`Add .ZIP Library`は必要ありません。

### 更新がうまくいかないとき

**更新に失敗しても、古い内容がそのまま残ります。** そして失敗はそのときではなく、
**次にビルドしたときのリンクエラー**として現れます。

```text
fatal error: ToppersFMP3_M5Unified.h: No such file or directory
undefined reference to `...'
```

新しい版が要求するヘッダやシンボルが、残っている古い版に無いためです。**症状が
「パッケージが壊れている」「例題が悪い」ように見えます**が、原因は更新が
適用されていないことです。同じ症状は 0.4.0 への更新で実際に起きています。

順に確認してください。

**1. スケッチブックに開発用のplatformが残っていないか**

これがあるあいだ、**`toppers:esp32`はBoards Managerから一切管理できません。**
`Install`・`Remove`・検索が、そのパッケージが存在しないかのように振る舞います
（更新したつもりで何も起きません）。ソースからビルドしたことがある場合だけ
該当します。

```text
Windows  %USERPROFILE%\Documents\Arduino\hardware\toppers
macOS    ~/Documents/Arduino/hardware/toppers
Linux    ~/Arduino/hardware/toppers
```

このフォルダを削除してからIDEを再起動してください。

**2. スケッチブックに同じライブラリが残っていないか**

`ToppersFMP3-M5Stack`をスケッチブックへ手で入れたことがあると、そちらが
**同梱版より優先され**ます。古ければ上のエラーになります。

```text
Windows  %USERPROFILE%\Documents\Arduino\libraries\ToppersFMP3-M5Stack
macOS    ~/Documents/Arduino/libraries/ToppersFMP3-M5Stack
Linux    ~/Arduino/libraries/ToppersFMP3-M5Stack
```

**0.4.1までの名前`ToppersFMP3-M5CoreS3`も確認してください。** 3機種に対応しているのに
名前がCoreS3のままだったので改名しました。**名前が違うので古いフォルダは同梱版を
隠しません**が、同じ`ToppersFMP3_*.h`を提供するライブラリが2つ見える状態になり、
どちらが使われるかは選べません。古いフォルダは削除してください。

**3. ボードパッケージを入れ直す**

`Boards Manager`で`Remove`してから入れ直します。それでも直らないときは、
フォルダを消してからIDEを再起動し、`Boards Manager`で入れます。

```text
Windows  %LOCALAPPDATA%\Arduino15\packages\toppers
macOS    ~/Library/Arduino15/packages/toppers
Linux    ~/.arduino15/packages/toppers
```

`packages\toppers`の下にはこのボードパッケージとリンクドライバしか入らないので、
消しても他のボードには影響しません。

> **どの版が実際に使われているかは`arduino-cli board details`で確かめられます。**
>
> ```sh
> arduino-cli board details -b toppers:esp32:m5cores3_fmp3
> ```
>
> `Board version`が入れたはずの版と違っていれば、更新は適用されていません。

## ボードとprofileの選択

```text
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5CoreS3 (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5StickS3 (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5AtomS3Lite (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5Core (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5AtomLite (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5NanoC6 (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5StampC5 (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5StampP4 (TOPPERS/FMP3)
```

**M5Stack Basicにはtouch・IMU・RTCがありません。** `M5Unified` profileの
例題は動きますが、これらは無効として報告されます。バックライトはLEDCの
PWM（GPIO32）で点きます。

**M5StickS3にはtouchがありません**（LCDは240x135で、IMUとPMICは使えます）。
`Bluetooth Classic (SPP)`はESP32の2機種（M5Core・M5AtomLite）だけの構成です。
ESP32-S3にBR/EDR無線が無いため、S3の3機種では選択肢に出ません。

**M5AtomLite（ATOM Lite）で選べるFMP3 Runtimeは`Minimal`・`WiFi`・
`Bluetooth Classic (SPP)`です。** LCDが無いため`M5Unified + Dual Core`は
ありません。本体RGB LED（SK6812、G27）は`WiFi`構成の`rgbLedWrite()`で
点きます（例題`AtomLiteRgb`。`Minimal`／`Bluetooth Classic`ではリンクされず、
例題は`#error`で止まります）。**Wi-Fi STA接続は実機で確認済み**です（上記）。

**M5NanoC6とM5Stamp-C5で選べるFMP3 Runtimeは`Minimal`と`WiFi`だけです。**
LCDが無いため`M5Unified + Dual Core`は無く、BR/EDR無線が無いため
`Bluetooth Classic (SPP)`もありません。

**M5Stamp-P4で選べるFMP3 Runtimeは`Minimal`と`WiFi`です**（どちらも2コア SMP）。
`pinMode`／`digitalWrite`／`digitalRead`／`attachInterrupt`は`WiFi`構成にあります
（例題`GpioInterrupt`、試験ピンG16）。**ESP32-P4自身に無線はありません**——`WiFi`は
SDIOでつないだcompanionの**ESP32-C6（Stamp AddOn C6）**へRPCで渡すhosted方式で、
**そのadd-onが無いと`WiFi`構成は上がりません**。`Tools > ChipVariant`はこの板には
ありません（stage が`esp32p4_es`＝rev v3未満のsilicon用SDKで建っているため固定）。
bootloaderはflashの0x2000に置きます。

**M5Stamp-C5にはon-boardのRGB LEDがありません。** 例題`NanoC6Gpio`は
M5Stamp-C5では1行ログを出すだけの no-op です。M5Stamp-C5のGPIOを動かして
見せるのは例題`GpioInterrupt`（試験ピンG1）のほうです。
**bootloaderはflashの0x2000に置きます**（M5NanoC6の0x0とは違います）。
asp3_esp_idfのDirect Boot像を焼いたことのある板は、flash 0x0に残っている
magicのせいでそのままでは起動しません（先頭8 KBを消してください）。

`Tools > FMP3 Runtime`でランタイム構成を選びます。一度に選べるのは1つで、
選んだ構成がスケッチと一緒にリンクされます。**どの構成でも普通のスケッチが
そのままビルドできます**（下表の example は、その構成を実際に動かして見せる
ものであって、必須の書き方ではありません）。

| FMP3 Runtime | 内容 | 動かして見せる example |
| --- | --- | --- |
| `Minimal` | FMP3起動、`setup()`／`loop()`、heartbeat | `Blink`、`Fmp3Minimal` |
| `M5Unified + Dual Core` | LCD、touch、RTC、PMIC、IMU。SMP（PRC1／PRC2）で起動 | `M5Unified` |
| `WiFi` | scan（資格情報不要）、Open／WPA接続、DHCP、DNS、TCP | `WiFiScan`、`WiFiConnect` |
| `Bluetooth Classic (SPP)` | SPPサーバ。**M5Coreのみ**（ESP32-S3にBR/EDRは無い） | `BluetoothSPP` |

> **Bluetooth Classicは接続に認証を要求しません。** SPPサーバは
> `ESP_SPP_SEC_NONE`で起動するので、電波の届く範囲の誰でも、ペアリングを
> 経ずに接続してデータを送受信できます（2026-09-02実機確認: PC側のボンドを
> 消した状態から接続でき、ボンドは作られず、確認も一度も出なかった＝
> SSPの"Just Works"）。コードには数値比較の自動承認とレガシー用の固定PIN
> `1234`もありますが、この経路ではどちらも通りません。試すぶんには
> 問題ありませんが、**外に出したくないものをこのリンクに載せないでください。**

exampleは`File > Examples`の、選択中のボード向けセクションに
`ToppersFMP3-M5Stack`として現れます。ライブラリはボードパッケージに
同梱されているため、これらは**このボードを選んでいるときにだけ**現れます。

`Blink`はFMP3ブリッジ経由の最小スケッチで、1秒周期の反転を行います。
どのprofileでもそのままビルドできるので、導入直後の動作確認に使えます。

`Fmp3Minimal`は`Blink`よりさらに小さく、`setup()`／`loop()`が呼ばれていることを
数えるだけのスケッチです。

`M5Unified`の冒頭には`phase5_`で始まる変数の宣言がありますが、これは
**このポートの自己診断が読む計装**であって、スケッチに必要なものではありません。
ランタイム側が弱シンボルで定義しているので、書かなければ自己診断が
「未実施」を報告するだけで、動作は変わりません。

ボードを入れた直後にメニュー項目が見えない場合は
`Tools > Reload Board Data`を実行してください。

## サイズ表示について

Verify後に表示されるFlash／RAM使用量は、FMP3のセクション構成に合わせた
実際の値です。集計するセクション名をFMP3の構成
（`.iram_boot`／`.flash_text`／`.flash_rodata`／`.kernel_bss`など）に
合わせてあります。

3MBのアプリケーションパーティションに対する実測値:

| FMP3 Runtime | Flash | RAM |
| --- | --- | --- |
| `Minimal` | 30,112 | 19,036 |
| `M5Unified + Dual Core` | 269,104 | 48,900 |
| `WiFi`（`WiFiConnect`） | 513,900 | 242,880 |
| `WiFi`（`WiFiScan`） | 518,724 | 244,624 |
| `Bluetooth Classic (SPP)` | 571,056 | 230,300 |

**M5NanoC6とM5Stamp-C5ではRAM使用量の分母（IDEのVerify後に出る「N%」の
計算に使う値）を上書きしています。** 継承元のFreeRTOS前提の値（327,680）は
実際のRAM上限と合わず、使用率の意味がずれるためです。M5NanoC6は ld の上限
`upload.maximum_data_size=452112`、M5Stamp-C5は**320,928**（こちらは継承値より
小さい = 実際は継承値が示すより余裕が少ない）に上書きしました（Xtensaの3機種は
変えていません）。表示されるバイト数そのものは変わりません、100%に対する意味が
変わります。

## Blink（Minimal）

TOPPERS/FMP3のArduino task上で1秒ごとに状態を反転する最小exampleです。

```text
Tools > FMP3 Runtime > Minimal
File > Examples > ... > ToppersFMP3-M5Stack > Blink
```

Verify／Upload後、Serial Monitorで`[Blink] ON`と`[Blink] OFF`が交互に
表示されることを確認します。CoreS3の`LED_BUILTIN`はRGB仮想ピンで追加driverが
必要なため、serial logをportableなindicatorとして使用します。

Serial MonitorではFMP3起動banner、`[Arduino] setup complete`、
約1秒周期の`[Arduino] loop heartbeat`も確認できます。

## デュアルコアについて

`M5Unified + Dual Core` profileのカーネルは**SMP（PRC1／PRC2）**で起動します。
Arduinoの`setup()`／`loop()`はPRC1で動きます。

> **現時点ではスケッチからPRC2へタスクを置く手段がありません。** profileごとの
> FMP3構成（cfg）は同梱時に固定されており、タスクは静的に生成されるためです。
> PRC2はカーネルとしては動いていますが、同梱構成では空いています。
> スケッチからPRC2を使えるようにするのは今後の課題です。

## M5Unified

M5StackのM5GFX／M5UnifiedライブラリをFMP3互換層で再コンパイルし、CoreS3のLCD、
touch、IMU、RTC、AXP2101をArduinoスケッチから使用するexampleです。

```text
Tools > FMP3 Runtime > M5Unified + Dual Core
File > Examples > ... > ToppersFMP3-M5Stack > M5Unified
```

Verify／Upload後、Serial Monitorで`M5.begin and initial LCD draw PASS`、
60秒後の`60-second M5Unified integration PASS`を確認します。LCDには生存時間が
表示され、画面を触るとtouch座標と描画が更新されます。

このprofileではSpeaker／Micを除外しています。
CJKフォントは同梱していません（フォントは`ToppersFMP3_M5Fonts.h`のIDで
選択でき、アプリが実際に使ったものだけがリンクされます）。

## StackChanBasic

同じ`M5Unified`profileで動く、改造して遊ぶための入門exampleです。

```text
Tools > FMP3 Runtime > M5Unified + Dual Core
File > Examples > ... > ToppersFMP3-M5Stack > StackChanBasic
```

LCDに図形だけで顔を描き、ときどきまばたきし、画面をタッチすると笑い、
15秒さわらないと眠って`zzz`を出します。Serial Monitorには
`StackChanBasic started`、`Expression: NORMAL`、`Blink`、`Touch detected`、
`Expression: HAPPY`、`Expression: SLEEPY`が出ます。

顔の色・大きさ・眠るまでの時間は先頭の定数だけで変えられます。顔の各部は
**画面の短い辺に対する割合（`..._PCT`）**で書いてあるので、3機種すべてで
同じ見た目になります（240x135のM5StickS3でもはみ出しません）。

`M5.begin()`ではなく`toppers_m5_begin()`を呼ぶ点だけ`M5Unified`example と
同じ約束です（このportはGDMAを実装しておらず、panelのDMAチャネルを
落とすのがadapter側だからです）。

> **このexampleは音を鳴らしません。** profileがSpeakerを除外しているため、
> `playHappySound()`はログを出すだけの置き換え可能な関数になっています。

> **タッチのないボードでは眠ったままになります。** 起こす操作がタッチだけなので、
> M5Stack BasicとM5StickS3では15秒後に`Sleepy`になったあと戻りません。表示と
> まばたきの確認には使えます。ボタンで起こすようにするのが最初の改造として
> ちょうどよく、`updateTouch()`の隣に同じ形の関数を足すだけです。

## Wi-Fi scan

SSIDとパスワードは不要です。

```text
Tools > FMP3 Runtime > WiFi
File > Examples > ... > ToppersFMP3-M5Stack > WiFiScan
```

Verify／Upload後、Serial Monitorで`[WiFiScan] found`、SSID、RSSI、channel、
`[WiFiScan] done`を確認します。周辺SSIDはログへ表示されますが、ソース、
Release asset、試験記録には保存しません。

実装しているのは`WiFi.scanNetworks()`、`SSID()`、`RSSI()`、`channel()`、
`encryptionType()`、`scanDelete()`のscan用サブセットです。Arduino標準WiFi APIとの
完全互換ではありません。

## Wi-Fi connect

```text
Tools > FMP3 Runtime > WiFi
File > Examples > ... > ToppersFMP3-M5Stack > WiFiConnect
```

`WiFiConnect.ino`の`WIFI_SSID`を設定します。オープンAPでは`WIFI_PASSWORD`を
空にし、WPA2／WPA3パスワード認証では8～63文字のパスフレーズを設定してください。
1～7文字または64文字以上は安全に拒否します。

> **資格情報はスケッチへ直接記述するため、公開前に必ず削除してください。**

Serial MonitorでDHCP、DNS、TCPの`[WiFiConnect]`ログを確認します。SSIDが空なら
接続処理を開始せず、設定を促すメッセージだけを表示します。

Wi-Fi初期化中は次の低レベルマーカーを順に表示します。途中で停止した場合は、
最後に表示された行を報告してください。

```text
[WiFiConnect] init: shim begin
[WiFiConnect] init: esp_wifi_init begin
[WiFiConnect] init: esp_wifi_init OK
[WiFiConnect] begin: set_config begin
[WiFiConnect] begin: set_config OK
[WiFiConnect] init: esp_wifi_start begin
[WiFiConnect] init: esp_wifi_start OK
[WiFiConnect] init: tcpip begin
[WiFiConnect] init: tcpip OK
[WiFiConnect] begin: connect request begin
[WiFiConnect] begin: connect request accepted
```

このprofileでは初期化時のバーストに対応するため、FMP3 syslogバッファを
標準の32件から128件へ拡張しています。段階マーカーとexampleの結果表示は
log taskへ統一し、1行ごとにflushするため、複数taskの1文字出力による混在を
避けています。

FMP3 shimでは、参照移植と同じく`set_config`を`esp_wifi_start`より前に実行します。
Wi-Fi blobの整形済みログは128件分の永続バッファへ保存してからsyslogへ渡すため、
log taskが読む前の一時バッファ再利用による重複・文字化けを防ぎます。

## 確認済みの範囲

- 各ボードで選べるprofile全てが、ボードマネージャ経由で入れたパッケージから
  ビルドできる（ボード x profile x example の本数はソースリポジトリの
  `python3 scripts/verify_package.py --list-builds` が導出します。2026-09-16
  実測: 5ボード68本、すべてPASS）。Xtensa 3ボード分については
  **Windows・Linux（x86_64）・Apple Silicon macOS の3ホストで実測**しており、
  各profileは同梱exampleと素の`Blink`の両方で確認しています。**7件の成果物は
  3ホストでバイト単位に一致します**（RISC-Vの2ボードは対象外、下記）
- CoreS3実機で、Minimal profileのUpload、FMP3 3.4.0起動、Arduino task、
  `setup()`、1秒heartbeat
- M5Unified（LCD／touch、SMPカーネル上）と、`WiFi` profileでのscanおよび接続の実機動作
- Wi-Fi connectはAndroidテザリングでオープンAP、WPA2-PSK、WPA3-SAEに接続し、
  DHCP、DNS、TCP受信まで

- **LinuxからのUpload**（Ubuntu -> `/dev/ttyACM0`、`M5Unified + Dual Core`）。
  ユーザが`dialout`グループに属している必要があります
- **M5NanoC6実機**で、stock M5Stack bootloaderのままMinimal（`Blink`）の起動
  （warm 5/5、真cold 9/10）と、Wi-Fi STA -> DHCP -> DNS -> TCPの実機確認
  （真cold 3/4、1回は無音採取で成否判定不能）。**接続に実際に使われた認証方式は
  9/9ともWPA3-SAE**（APはWPA2/WPA3混在の1台で、`authmode=6`）。**WPA2-PSK単独と
  Open APはAPが用意できず未実測**、BLEは未着手です。ホスト間のバイト単位一致
  （上記「7件の成果物」）は**M5NanoC6の成果物には拡張していません**:
  3ホストでの同一性は未計測です。driver 4（S5-8）でビルドパス依存は
  解消しましたが（同一ホスト内でbuild pathを変えても`.bin`が一致することは
  実測済み）、cross-hostは未検証のままです。判断と到達点は
  ソースリポジトリの`docs/c6-port.md`（`docs/`はリリースパッケージには
  同梱しません）
- **M5Stamp-C5実機**で、stock M5Stack bootloader（**@0x2000**）のまま
  Minimal（`Blink`）の起動（warm 5/5、真cold 5/5、CPU 240 MHz）と、
  Wi-Fi STA -> DHCP -> DNS -> TCP（warm 3/3、真cold 3/3）、scanが2.4 GHzと
  5 GHzの両方を拾うこと、`WiFi`構成での`pinMode`読み戻しと`attachInterrupt`の
  自己駆動試験（例題`GpioInterrupt`、G1）。**接続に使われた認証方式は8/8とも
  WPA3-SAE**で、**WPA2-PSK単独・Open AP・5 GHzでの接続は未実測**です。
  scanが返すAPは**最大20件**（アダプタの記録上限）なので、20件は「近所に20台」
  ではありません。**RGB LEDはありません**（例題`NanoC6Gpio`はno-op）。
  BLEと802.15.4は未着手。ホスト間のバイト単位一致はM5NanoC6と同じく未計測です。
  判断と到達点はソースリポジトリの`docs/c5-port.md`

- **M5Stamp-P4実機**（ESP32-P4 rev v1.3 + Stamp AddOn C6）で、Minimal（`Blink`
  warm 5/5・真cold 5/5。**2コアSMP**が`Processor 2 start.`と`[P4-CORE2] alive`で
  見えます）、`attachInterrupt`の自己駆動試験（例題`GpioInterrupt`、G16）、
  **hosted Wi-Fi**のスキャン（14〜16 AP）とSTA接続 -> DHCP -> DNS -> TCP
  （warm 3/3・真cold 3/3、DHCPは8〜9秒）。無線を担うのはP4ではなく**SDIOで繋いだ
  ESP32-C6**で、`[WiFiHosted] companion INIT chip_id=0x0d`が相手と話せている
  一次証拠です。判断と到達点はソースリポジトリの`docs/p4-port.md`
- **M5Stack ATOM Lite実機**で、Minimal（`Blink` warm 5/5・真cold 5/5）、
  `GpioInterrupt`（G23、warm・真coldとも`VERDICT PASS`）、本体RGB LED
  （SK6812 G27、`AtomLiteRgb`で`tx_done=8`。**点灯と色順はユーザーが目視確認**）、
  Wi-Fiスキャン（14 AP）、**Wi-Fi STA接続**（warm 3/3・真cold 3/3。目的のSSIDを
  スキャンで見つけ`begin()`が0を返す。APはWPA2/WPA3混在・ch=10・rssi -62〜-71）、
  **Bluetooth Classic**（`discoverable`まで。ペアリングは未実施）。
  判断と到達点はソースリポジトリの`docs/atomlite-port.md`

未確認: **macOS**でのVerifyとUpload、
WPA2-PSK／WPA3-SAEの追加アクセスポイントでの互換性。
（**OTA書き込みは未確認ではなく未対応です。** 下記「制約」。）

> **Intel Macには対応していません（0.3.0・0.4.0）。** ビルドに必要なリンクドライバは
> ホストごとに凍結して同梱しますが、これまでのリリースに入っているのは
> Windows・Linux（x86_64）・**Apple Silicon の macOS** の3種です。
> Intel Mac（`x86_64-apple-darwin`）向けは、生成に使っていたCIランナーが
> 割り当てられなくなったため入っていません。Intel Macではボードを入れても
> ビルドできません。使えるランナーを確認しだい追加します。

## 制約

- **ESP32-S3の3板は、WPA3移行モード（WPA2/WPA3混在）のAPにSTA接続できません。**
  `reason=17`（`IE_IN_4WAY_DIFFERS`）で4-way handshakeの最終段だけが落ちます
  （scan・認証・アソシエーションは通ります）。原因はSDKのWi-Fi blob側にあり、
  **このポートでは回避できません**——APがbeaconに載せるRSNXEをESP32-S3のblobが
  supplicantへ渡さず（`esp_wifi_sta_get_rsnxe()`がNULLを返すことを実機で確認）、
  APはEAPOL-Key msg 3にRSNXEを載せてくるため、supplicantの一致検査
  （設定で外す口はありません）が落とします。**ESP32（LX6）の2板は同じAPに
  繋がります**ので、板固有でもこのポート固有でもなく、チップのblobの差です。
  WPA2専用のAPならS3でも繋がる見込みですが未実測です。
- **M5Stack Arduino coreのランタイムはリンクされません。** FMP3がカーネルなので、
  core自身のランタイムもFreeRTOSも像に入りません。帰結として**`Serial`・`delay()`・
  `millis()`・`micros()`・`Wire`・`SPI`は使えません**。`<Wire.h>`や`<SPI.h>`を
  includeするライブラリ、`Serial`や`delay()`を呼ぶライブラリは、**どの
  `Tools > FMP3 Runtime`を選んでもリンクできません**（例: `M5-RoverC`は`<Wire.h>`を
  引くので使えません）。リンク時に`i2cInit`／`xQueueCreateMutex`／`delay`などが
  未定義になった場合は、リンクドライバが`--- why ---`として理由を説明します。
  **代わりに各ランタイムのAPIを使ってください**——`M5Unified + Dual Core`なら
  `M5.Ex_I2C`／`M5.In_I2C`が`Wire`の代わりになり、`loop()`は周期的に呼ばれるので
  `delay()`は要りません。同梱例題はすべてこの流儀です。
- **OTA書き込み（`Tools`のネットワークポート経由）は対応していません。**
  受け手が無いためで、試せば動くかもしれないという類のものではありません。
  OTAは端末側で待ち受けているArduinoOTA応答器にPC側から押し込む仕組みですが、
  本ポートは待ち受けソケットもmDNSも持たず（Wi-Fiの通信APIは1回分の
  クライアント要求まで）、フラッシュを書き換える`esp_ota_*`も入っていません。
  core側の`ArduinoOTA`も、上記のとおりcoreのランタイムをリンクしないので
  使えません。パーティション表は`ota_0`／`ota_1`／`otadata`を持っていますが、
  **入れ物があるだけで、受け取って書き換える側がありません。**
- Arduino／FreeRTOS APIは完全互換ではありません。各profileとexampleで
  実際に使用したサブセットのみ対応しています。
- FMP3の`dly_tsk`のRELTIMはこのportではマイクロ秒です。FreeRTOS APIのtickとは
  単位が異なります。

## ライセンス

本パッケージは複数のcomponentを扱うため、単一のライセンスが全体へ
適用されるとは限りません。各ファイルのライセンスヘッダと
`THIRD_PARTY_NOTICES.md`を確認してください。
