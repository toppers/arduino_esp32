# Windows 側テスト（`scripts/Test-*.ps1`）

PowerShell のテスト群は Windows でしか走らない。Linux/macOS の CI と
`scripts/verify_package.py` は**これらを一切走らせていない**ので、ここが唯一の
実行経路になる。

実施したら `runs/` に記録を残すこと（書式は下の「記録の書き方」）。

**2026-09-02 の初回実施では、当時の 15 本のうち通ったのは 2 本だけだった。**
残りは環境ではなくリポジトリ側の欠落で止まっており、その大半をその run の中で
直した（現在 PASS 9・FAIL 3・未実行 1）。何がどう壊れていて何を直したかは
`runs/2026-09-02-first-windows-run.md` に出力ごと書いてある。走らせる前に
それを読むこと。残っている FAIL の理由もそこにある。

## 走らせる前に

```powershell
pwsh -File scripts\Test-BaselineEnvironment.ps1   # 道具と木の存在確認。最初にこれ
```

`pwsh`（PowerShell 7）が無い機械では `powershell.exe -NoProfile -ExecutionPolicy
Bypass -File ...` でよい。2026-09-02 の run は全本これで走らせた。

`Test-BaselineEnvironment.ps1` は `-SourceTree`（既定はこのリポジトリ）を取り、
submodule と vendored runtime の存在も見る。外部の `toppers/fmp3_esp_idf`
チェックアウトはもう要らない。

ステージを作り直す場合（チップごとに 1 回ずつ）:

```powershell
pwsh -File scripts\New-Fmp3PrebuiltStages.ps1 -Chip esp32s3
pwsh -File scripts\New-Fmp3PrebuiltStages.ps1 -Chip esp32
```

`Test-ArduinoLibrary.ps1`（したがって `Test-Regression.ps1`）は TOPPERS/FMP3
プラットフォームが入っていることを前提にする。`examples/LibraryInfo` は kernel の
log port に書くので、stock の M5Stack FQBN では link できない:

```powershell
python scripts\install_platform.py --prebuilt-stage-root build\prebuilt
```

既定は `Documents\Arduino`。別の場所に入れたなら両スクリプトに `-Sketchbook` で渡す。

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
| `Test-Regression.ps1` | ホスト | CoreS3 / M5Core | 上記ホスト側をまとめて回す（`-Chip`） |
| `Test-StagePlatform.ps1` | ホスト | **3 ボード** | `install_platform.py` が組んだプラットフォームからスケッチを建てる。出荷経路 |
| `Test-ArduinoReleasePackage.ps1` | 実機 | CoreS3 | Release ZIP を隔離環境で生成・導入・コンパイル |
| `Test-Hardware.ps1` | 実機 | CoreS3 | API プローブを焼いてシリアル判定 |
| `Test-M5UnifiedHardware.ps1` | 実機 | CoreS3 | M5Unified 例題を焼いてシリアル判定 |
| `Test-DualCoreHardware.ps1` | 実機 | CoreS3 | DualCore 例題で両プロセッサを判定 |
| `Test-Touch.ps1` | 実機 | CoreS3 | touch イメージを焼いて実操作を判定 |

## ★対象外（既知の穴）

初回実施の時点では 15 本すべて CoreS3 前提だった。いまはホスト側 7 本と
`Test-Hardware.ps1` が `-Chip esp32s3|esp32` を取り、`Test-StagePlatform.ps1`
が 3 ボード分を建てる。閉じた穴の経緯は `backlog.md`（B-1 / B-2 / B-3 / B-4）
にある。残っているのは次:

- **実機は CoreS3 と M5Core だけ、しかも項目が偏っている**
  - `Test-M5UnifiedHardware.ps1` / `Test-DualCoreHardware.ps1` は legacy ZIP の
    成果物を焼くので **CoreS3 専用のまま**（legacy は CoreS3 専用と決めた:
    `backlog.md` の A-2）
  - `Test-Touch.ps1` は CoreS3 のみ。M5Core と M5StickS3 にタッチパネルは無い
  - **M5StickS3 は 1 本も焼いていない**。ビルドは通るが実機確認が無い
  - **`Test-StagePlatform.ps1` は焼かない**。3 ボード分のイメージを建てて
    静的に検証するだけ
- **`bt-classic` の `BluetoothSPP` を実機で焼く**確認。例題のビルドは
  `Test-StagePlatform.ps1` が M5Core 向けにやるが、焼いて SPP を通す確認は
  Linux 側の `tools/bt/spp_echo_test.py` だけ
- **M5StickS3 の `m5-unified`**。動かないことが
  `docs/m5sticks3-m5unified.md` に記録されており、`Test-StagePlatform.ps1` の
  matrix にも入れていない（入れれば偽を主張することになる）。直ったら足すこと

Linux 側で通っている `scripts/verify_package.py`（3 ボード分を建てる）と
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
