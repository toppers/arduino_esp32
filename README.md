# Arduino on TOPPERS/FMP3 for M5Stack

M5Stack の 4 機種で、Arduino の `setup()` / `loop()` を
**TOPPERS/FMP3 SMP カーネルの上で**動かすための Arduino ボードパッケージです。

| ボード | チップ |
| --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 |
| M5StickS3 | ESP32-S3 / Xtensa LX7 |
| M5Stack Basic | ESP32 / Xtensa LX6 |
| M5NanoC6 | ESP32-C6 / RISC-V |

1 つのパッケージに 4 つとも入り、`Tools > Board` で選びます。M5NanoC6 だけは
`Tools > FMP3 Runtime` に `Minimal` と `WiFi` の 2 構成しかありません
（`M5Unified + Dual Core` と `Bluetooth Classic (SPP)` はありません。下記
「確認済みの範囲」参照）。

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

1. `M5Stack` を検索して **3.3.8** を `Install`
2. `TOPPERS/FMP3 M5Stack boards` を検索して `Install`

**M5Stack core は自動では入りません。** このボードは Arduino の *core reference*
で M5Stack core のコンパイラ設定とコアソースを参照しており、Arduino には
「別の platform に依存する」という宣言の仕組みが無いためです。入っていないと
Verify の開始直後に次で止まります。

```text
Invalid FQBN: missing platform release m5stack:esp32 referenced by board ...
```

**3.3.8 以外は使えません**（同梱 ESP-IDF v5.5.4 の private な Wi-Fi ABI、
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
| `Bluetooth Classic (SPP)` | SPP サーバ。**M5Core のみ**（ESP32-S3 に BR/EDR は無い） |

## 確認済みの範囲

- 各構成が、Boards Manager 経由で入れたパッケージから
  **対応するすべてのボードでビルドできること**
  （`python3 scripts/verify_package.py --list-builds` が導出する本数、
  2026-09-15 実測: CoreS3 13・M5StickS3 13・M5Core 17・M5NanoC6 8 の
  計 51 本。`Bluetooth Classic` は M5Core 専用、M5NanoC6 は minimal と
  wifi-connect のみ）。Xtensa 3 ボード分については
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
- M5StickS3 実機で、minimal（`Blink`）、Wi-Fi スキャン（13 AP を検出）、
  M5Unified（`board_M5StickS3` を検出、240x135 の LCD・IMU・PMIC）。
  当初この機種だけ M5Unified が動かなかった経緯と原因は
  [`docs/m5sticks3-m5unified.md`](docs/m5sticks3-m5unified.md)
- **M5NanoC6（ESP32-C6）は `minimal` / `wificonnect` の 2 構成で配布物に
  収録されています。** `Minimal`・`WiFi` の 2 つが `Tools > FMP3 Runtime` に
  出ます。実機で確認済みなのは、stock M5Stack bootloader のまま
  minimal（`Blink`）が起動すること（warm 5/5、真cold 9/10）と、
  Wi-Fi STA（**WPA2-PSK のみ**）-> DHCP -> DNS -> TCP がユーザーの実 AP に対して
  通ること（真cold 3/4、1 回は無音採取で成否判定不能）です。**Open AP・
  WPA3-SAE は AP が用意できず未実測、BLE は未着手、M5Unified 相当の
  profile はありません**（下記「M5NanoC6 の既知の制限」）。判断と到達点は
  [`docs/c6-port.md`](docs/c6-port.md)

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
- **`attachInterrupt` はリンクできるところまでで、実機での動作確認はまだです**
  （段6 の候補）。
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
- **Intel Mac には対応していません。** ビルドに必要なリンクドライバをホストごとに
  同梱していますが、`x86_64-apple-darwin` 向けは含まれていません。
- FMP3 の `dly_tsk` の `RELTIM` はこのポートではマイクロ秒で、FreeRTOS API の
  tick とは単位が異なります。
- M5Stack Arduino core は **3.3.8 固定**です。
- **Bluetooth は Classic (SPP) だけで、M5Core 限定です。** BR/EDR 無線は
  ESP32 にしかなく、ESP32-S3 は BLE のみですが、BLE はどのボードにも
  入っていません。

## リポジトリの構成

| パス | 役割 |
| --- | --- |
| `src/` | Arduino builder が再帰コンパイルする領域。`Arduino.h` だけに依存 |
| `ports/m5stack_xtensa/runtime/` | FMP3 の Xtensa ポート（ESP32-S3 / LX7 と ESP32 / LX6） |
| `fmp_app/` | 開発ツリーでのみ使う FMP3 アプリケーション |
| `examples/` | 同梱例題 |
| `scripts/` | ビルド・パッケージング |
| `packaging/` | 配布物の定義と利用者向け手順 |
| `third_party/fmp3_core` | FMP3 本体（submodule） |

ソースからビルドする手順は [`BUILDING.md`](BUILDING.md) にあります。
ESP32-C6（M5NanoC6）の判断と到達点は [`docs/c6-port.md`](docs/c6-port.md)。

## ライセンス

リポジトリ単一のライセンスではありません。各ファイルのヘッダと
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を正としてください。
