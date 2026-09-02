# Windows 側テスト（`scripts/Test-*.ps1`）

PowerShell のテスト群は Windows でしか走らない。Linux/macOS の CI と
`scripts/verify_package.py` は**これらを一切走らせていない**ので、ここが唯一の
実行経路になる。

実施したら `runs/` に記録を残すこと（書式は下の「記録の書き方」）。
**2026-09-02 時点で、コミットされた実施記録は 1 件も無い。**

## 走らせる前に

```powershell
pwsh -File scripts\Test-BaselineEnvironment.ps1   # 道具と木の存在確認。最初にこれ
```

ステージを作り直す場合（チップごとに 1 回ずつ）:

```powershell
pwsh -File scripts\New-Fmp3PrebuiltStages.ps1 -Chip esp32s3
pwsh -File scripts\New-Fmp3PrebuiltStages.ps1 -Chip esp32
```

## 一覧（2026-09-02 実測）

| スクリプト | 実機 | 対象ボード | 内容 |
| --- | --- | --- | --- |
| `Test-BaselineEnvironment.ps1` | – | – | 道具と source tree の存在確認 |
| `Test-ArduinoLibrary.ps1` | ホスト | CoreS3 | ライブラリ認識スケッチのコンパイル |
| `Test-SketchBridge.ps1` | ホスト | CoreS3 | `setup()`/`loop()` → FMP3 タスク橋の検査 |
| `Test-M5Unified.ps1` | ホスト | CoreS3 | M5Unified イメージのビルドと検証 |
| `Test-M5UnifiedLink.ps1` | ホスト | CoreS3 | Arduino 生成 .o の FMP3 イメージへのリンク |
| `Test-Smp.ps1` | ホスト | CoreS3 | デュアルコアイメージの静的検証 |
| `Test-WiFiScan.ps1` | ホスト | CoreS3 | 資格情報不要 Wi-Fi スキャンの静的検証 |
| `Test-PartitionTable.ps1` | ホスト | – | ドライバのパーティション表変換を `gen_esp32part` と突合 |
| `Test-RecipeOverride.ps1` | ホスト | – | FreeRTOS を混ぜずに ELF を包めることの実証 |
| `Test-Regression.ps1` | ホスト | CoreS3 | 上記ホスト側をまとめて回す |
| `Test-ArduinoReleasePackage.ps1` | 実機 | CoreS3 | Release ZIP を隔離環境で生成・導入・コンパイル |
| `Test-Hardware.ps1` | 実機 | CoreS3 | API プローブを焼いてシリアル判定 |
| `Test-M5UnifiedHardware.ps1` | 実機 | CoreS3 | M5Unified 例題を焼いてシリアル判定 |
| `Test-DualCoreHardware.ps1` | 実機 | CoreS3 | DualCore 例題で両プロセッサを判定 |
| `Test-Touch.ps1` | 実機 | CoreS3 | touch イメージを焼いて実操作を判定 |

## ★対象外（既知の穴）

**15 本すべて CoreS3(ESP32-S3) 前提**で、次はどれも検証していない:

- **M5Core (ESP32/LX6) ボード** — minimal / m5-unified / wifi-connect / all-in-one
- **`bt-classic` profile と `BluetoothSPP` 例題** — M5Core 専用。
  Linux 側では `tools/bt/spp_echo_test.py` で実機確認済み
- **2 ボードが 1 パッケージに入った状態** — `Test-ArduinoReleasePackage.ps1` は
  CoreS3 の FQBN しか建てない

Linux 側で通っている `scripts/verify_package.py`（26 ビルド・2 ボード）と
役割が重なる部分もあるが、**Windows のホスト経路そのもの**（PowerShell の
ステージビルド、Windows の arduino-cli、gen_esp32part）はここでしか通らない。

## 記録の書き方

`runs/YYYY-MM-DD-<短い名前>.md` に、最低限これだけ:

- 日付、Windows と PowerShell のバージョン、arduino-cli のバージョン
- どのコミットで走らせたか（`git rev-parse HEAD`）
- 実機を使ったものは**ボードの実体**（CoreS3 か M5Core か、シリアル番号か MAC）
- スクリプトごとの PASS/FAIL と、FAIL なら出力そのもの（要約ではなく）
- 走らせなかったものと、その理由

失敗を「たぶん環境のせい」で片付けないこと。原因が分からないまま閉じるなら、
分からないと書く。
