# ToppersFMP3-M5CoreS3

M5Stack の 2 機種向けに、ArduinoスケッチとTOPPERS/FMP3を統合する
Arduinoボードパッケージです。

| ボード | チップ | `Tools > Board` |
| --- | --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 | `M5CoreS3 (TOPPERS/FMP3)` |
| M5Stack Basic | ESP32 / Xtensa LX6 | `M5Core (TOPPERS/FMP3)` |

1つのパッケージに両方入っています（パッケージ名がCoreS3なのは、
名前を変えると既存のスケッチと例題の場所が動くためです）。

Arduinoの`setup()`／`loop()`は、FreeRTOSではなくTOPPERS/FMP3 SMPカーネルの
タスクとして動きます。ブート、割込み、スケジューラはFMP3が所有します。

## 必要な環境

- Arduino IDE 2.x
- **M5Stack Arduino core 3.3.8**（先に入れておく必要があります。次章参照）
- M5Unified 0.2.20 ／ M5GFX 0.2.27（`M5Unified` profileを使う場合のみ）

**CMake、Ninja、Pythonは要りません。** スケッチのビルドに使うのは
M5Stack coreに同梱のツールチェーンとesptoolだけで、FMP3側は事前にビルド済みの
形で同梱されています。

## インストール

### 1. M5Stack Arduino core 3.3.8

**これは自動では入りません。** 本ボードはArduinoの
*core reference*でM5Stack coreのコンパイラ設定とコアソースを参照しており、
Arduinoの仕組みには「別のplatformに依存する」という宣言が無いためです。
入っていないとVerifyの開始直後に次のエラーで止まります。

```text
Invalid FQBN: missing platform release m5stack:esp32 referenced by board ...
```

`File > Preferences > Additional boards manager URLs`へM5Stackの
ボードマネージャURLを追加し、`Tools > Board > Boards Manager`から
`M5Stack` **3.3.8** を入れてください。

> **3.3.8以外のバージョンは使えません。** 同梱ESP-IDF v5.5.4のprivate Wi-Fi ABI、
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

削除も`Boards Manager`の`Remove`で行えます。

### 3. M5Unifiedライブラリ（`M5Unified` profileのみ）

`Tools > Manage Libraries`から`M5Unified` **0.2.20** と`M5GFX` **0.2.27**
を入れます。

> **同梱のFMP3ランタイムはこのバージョンのソースに対してビルドされています。**
> ライブラリだけ更新するとヘッダと事前ビルド済みオブジェクトが食い違い、
> 未定義シンボルやリンクエラーになります。他のprofileでは不要です。

ライブラリ本体はボードパッケージに同梱されているので、
`Add .ZIP Library`は必要ありません。

## ボードとprofileの選択

```text
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5CoreS3 (TOPPERS/FMP3)
Tools > Board > M5Stack Arduino with TOPPERS/FMP3 > M5Core (TOPPERS/FMP3)
```

**M5Stack Basicにはtouch・IMU・RTCがありません。** `M5Unified` profileの
例題は動きますが、これらは無効として報告されます。バックライトはLEDCの
PWM（GPIO32）で点きます。

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
`ToppersFMP3-M5CoreS3`として現れます。ライブラリはボードパッケージに
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

## Blink（Minimal）

TOPPERS/FMP3のArduino task上で1秒ごとに状態を反転する最小exampleです。

```text
Tools > FMP3 Runtime > Minimal
File > Examples > ... > ToppersFMP3-M5CoreS3 > Blink
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
File > Examples > ... > ToppersFMP3-M5CoreS3 > M5Unified
```

Verify／Upload後、Serial Monitorで`M5.begin and initial LCD draw PASS`、
60秒後の`60-second M5Unified integration PASS`を確認します。LCDには生存時間が
表示され、画面を触るとtouch座標と描画が更新されます。

このprofileではSpeaker／Micを除外しています。
CJKフォントは同梱していません（フォントは`ToppersFMP3_M5Fonts.h`のIDで
選択でき、アプリが実際に使ったものだけがリンクされます）。

## Wi-Fi scan

SSIDとパスワードは不要です。

```text
Tools > FMP3 Runtime > WiFi
File > Examples > ... > ToppersFMP3-M5CoreS3 > WiFiScan
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
File > Examples > ... > ToppersFMP3-M5CoreS3 > WiFiConnect
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

- 3つのprofile全てが、ボードマネージャ経由で入れたパッケージからビルドできる
  （**Windows・Linux（x86_64）・Apple Silicon macOS の3ホストで実測**）。各profileは
  同梱exampleと素の`Blink`の両方で確認しています。**7件の成果物は3ホストで
  バイト単位に一致します**
- CoreS3実機で、Minimal profileのUpload、FMP3 3.4.0起動、Arduino task、
  `setup()`、1秒heartbeat
- M5Unified（LCD／touch、SMPカーネル上）と、`WiFi` profileでのscanおよび接続の実機動作
- Wi-Fi connectはAndroidテザリングでオープンAP、WPA2-PSK、WPA3-SAEに接続し、
  DHCP、DNS、TCP受信まで

- **LinuxからのUpload**（Ubuntu → `/dev/ttyACM0`、`M5Unified + Dual Core`）。
  ユーザが`dialout`グループに属している必要があります

未確認: **macOS**でのVerifyとUpload、OTA書き込み、
WPA2-PSK／WPA3-SAEの追加アクセスポイントでの互換性。

> **Intel Macには対応していません（0.3.0・0.4.0）。** ビルドに必要なリンクドライバは
> ホストごとに凍結して同梱しますが、これまでのリリースに入っているのは
> Windows・Linux（x86_64）・**Apple Silicon の macOS** の3種です。
> Intel Mac（`x86_64-apple-darwin`）向けは、生成に使っていたCIランナーが
> 割り当てられなくなったため入っていません。Intel Macではボードを入れても
> ビルドできません。使えるランナーを確認しだい追加します。

## 制約

- Arduino／FreeRTOS APIは完全互換ではありません。各profileとexampleで
  実際に使用したサブセットのみ対応しています。
- FMP3の`dly_tsk`のRELTIMはこのportではマイクロ秒です。FreeRTOS APIのtickとは
  単位が異なります。

## ライセンス

本パッケージは複数のcomponentを扱うため、単一のライセンスが全体へ
適用されるとは限りません。各ファイルのライセンスヘッダと
`THIRD_PARTY_NOTICES.md`を確認してください。
