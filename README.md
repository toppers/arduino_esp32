# Arduino on TOPPERS/FMP3 for M5Stack

M5Stack の 2 機種で、Arduino の `setup()` / `loop()` を
**TOPPERS/FMP3 SMP カーネルの上で**動かすための Arduino ボードパッケージです。

| ボード | チップ |
| --- | --- |
| M5Stack CoreS3 | ESP32-S3 / Xtensa LX7 |
| M5Stack Basic | ESP32 / Xtensa LX6 |

1 つのパッケージに両方が入り、`Tools > Board` で選びます。

FreeRTOS ではなく FMP3 がブート・割込み・スケジューラを所有し、Arduino の
スケッチは静的に構成された FMP3 タスクから呼ばれます。

## 導入

Arduino IDE の `File > Preferences > Additional boards manager URLs` へ次を追加し、
`Boards Manager` で `TOPPERS/FMP3 M5Stack boards` を検索して入れてください。

```text
https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
```

必要なツールチェーンは Boards Manager が自動で取得します。**スケッチのビルドに
CMake も Ninja も Python も要りません。** 導入手順と例題の詳細は
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

- 3 構成すべてが、Boards Manager 経由で入れたパッケージから
  **両ボードでビルドできること**（`scripts/verify_package.py` が
  2 ボード × 3 構成 × 例題を建てる）。CoreS3 単独については
  Windows・Linux x86_64・Apple Silicon macOS の 3 ホストで実測し、
  成果物が 3 ホストでバイト単位に一致することを確認済み
- CoreS3 実機で、M5Unified（LCD・touch、SMP カーネル上）と Wi-Fi 接続
  （Open / WPA2-PSK / WPA3-SAE → DHCP → DNS → TCP）
- M5Stack Basic 実機で、minimal / M5Unified（LCD、SMP）/ Wi-Fi スキャン と
  all-in-one。**touch・IMU・RTC はこの機種に無いので使えません**

## 制約

- **Bluetooth Classic は接続に認証を要求しません。** SPP サーバは
  `ESP_SPP_SEC_NONE` で起動するので、電波の届く範囲の誰でも、ペアリングを
  経ずに接続してデータを送受信できます。2026-09-02 に実機で確認しました——
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
- **Bluetooth は未対応です。** BT Classic は ESP32 にしかなく（S3 は BLE のみ）、
  どちらもこのパッケージには入っていません。

## リポジトリの構成

| パス | 役割 |
| --- | --- |
| `src/` | Arduino builder が再帰コンパイルする領域。`Arduino.h` だけに依存 |
| `ports/m5stack_xtensa/runtime/` | FMP3 の ESP32-S3 / CoreS3 固有ポート |
| `fmp_app/` | 開発ツリーでのみ使う FMP3 アプリケーション |
| `examples/` | 同梱例題 |
| `scripts/` | ビルド・パッケージング |
| `packaging/` | 配布物の定義と利用者向け手順 |
| `third_party/fmp3_core` | FMP3 本体（submodule） |

ソースからビルドする手順は [`BUILDING.md`](BUILDING.md) にあります。

## ライセンス

リポジトリ単一のライセンスではありません。各ファイルのヘッダと
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を正としてください。
