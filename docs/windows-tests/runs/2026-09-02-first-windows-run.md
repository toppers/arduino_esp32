# 2026-09-02 Windows 実施記録（初回）

`docs/windows-tests/README.md` が「コミットされた実施記録は 1 件も無い」と書いた
状態の解消。**15 本のうち通ったのは 2 本**で、残りは環境ではなくリポジトリ側の
欠落で止まった。原因は下に、出力そのまま添えて書く。

## 環境

| 項目 | 値 |
| --- | --- |
| OS | Windows 11 Home 10.0.26200 |
| PowerShell | Windows PowerShell 5.1.26100.9168（`pwsh` は無い。README は `pwsh -File` と書いているが、全スクリプトを `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` で実行した） |
| arduino-cli | 1.5.0 (dd407d42d, 2026-05-19) — `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe` |
| CMake / Ninja | 4.1.1 / 1.12.1 |
| M5Stack core | m5stack:esp32 **3.3.8**（この run のために新規導入。esp-idf v5.5.4 735507283d） |
| M5GFX / M5Unified | **0.2.27 / 0.2.20**（pin 版。導入前は 0.2.22 / 0.2.17。旧版は `Documents\Arduino\lib-backup-20260902\` に退避） |
| commit | `e485cc25e689f48dac88504ea4924446db512628`（+ 本 run 中の修正 2 件、下記） |
| submodule | `third_party/fmp3_core` = `685b36a98175e3093c0931b713729b9030774e16`（未 init だったので init した） |

`M5GFX@0.2.27` は素直に入らない: `M5Unified@0.2.20` の依存解決が M5GFX を 0.2.28 に
上げてしまうので、`lib install M5GFX@0.2.27 --no-deps` を後から当てる必要がある。

## ボードの実体

| ポート | チップ | 識別子 | 実体 |
| --- | --- | --- | --- |
| COM24 | ESP32-S3 (`VID_303A&PID_1001`, ネイティブUSB) | MAC `44:1B:F6:E2:73:AC` | M5Stack CoreS3 |
| COM31 | ESP32 / LX6 (CH9102 `VID_1A86&PID_55D4`, 変換器シリアル `5B21222745`) | MAC `5C:01:3B:0C:CA:44` | 無印 M5Core |

両方とも電源制御可能な Hub に接続、給電あり、両ポート列挙済み。
**実機テストは1本も実行しなかったが、それはボードの都合ではない**（下記）。

## ステージ生成（README の「走らせる前に」）

| コマンド | 結果 |
| --- | --- |
| `New-Fmp3PrebuiltStages.ps1 -Chip esp32s3` | **PASS** — minimal 47 obj / m5-unified 138 obj / wifi-connect 74 obj |
| `New-Fmp3PrebuiltStages.ps1 -Chip esp32` | 初回 **FAIL** → 下記 ★1 を直して **PASS** — minimal 48 obj / m5-unified 139 obj / wifi-connect 75 obj |

esp32(LX6) 側が通ったことで、直前コミット e485cc2 が「未検証」と書いた
`-Chip` 追従（コンパイラ名・ツールチェーンファイル・`-DA1_CHIP`）も実機ツール
チェーンで検証済みになった。

## 一覧の結果

| スクリプト | 結果 | 備考 |
| --- | --- | --- |
| `Test-BaselineEnvironment.ps1` | **PASS** | `-Fmp3Repository` を `-SourceTree`（既定=このリポジトリ）に置き換え、submodule と vendored runtime の存在確認を追加。git 失敗で失敗サマリを出さずに死ぬ件も直した |
| `Test-ArduinoLibrary.ps1` | 初回 **FAIL** → ★2 を直して **PASS** | `libraryInfo()` = `42006354 T`、ELF SHA-256 `6D16AB5B80C6FB20823C385A8F1B45E34F70E7842F5F38657BC609831A5F622F` |
| `Test-SketchBridge.ps1` | 初回 **FAIL** → ★3 を直して **PASS** | profile `minimal` |
| `Test-M5Unified.ps1` | 初回 **FAIL** → ★3 を直して **PASS** | profile `m5-unified` |
| `Test-M5UnifiedLink.ps1` | 初回 **FAIL** → ★3 を直して **PASS** | profile `m5-unified` |
| `Test-Smp.ps1` | 初回 **FAIL** → ★3・★12 を直して **PASS** | profile `m5-unified`、`TNUM_PRCID=2` |
| `Test-WiFiScan.ps1` | 初回 **FAIL** → ★3・★4・★12 を直して **PASS** | profile `wifi-connect`、clean ビルドからも確認 |
| `Test-PartitionTable.ps1` | **PASS** | 47 CSV byte identical、malformed 7 種すべて両実装で reject 一致 |
| `Test-RecipeOverride.ps1` | **FAIL** | ★14。★3 は解消したが、単体で建つアプリが無い（★13） |
| `Test-Regression.ps1` | **FAIL** | 修正後に通しで再実行: `Arduino library PASS 215.4s` → 2 本目 `recipe override FAIL 33.3s` で abort（★14）。初回は 1 本目（★2）で止まっていた |
| `Test-ArduinoReleasePackage.ps1` | 初回 **FAIL** → ★2・★5・★6・★8・★9 を直して **PASS** | ZIP SHA-256 `CCAE66401CE961982C70A7AD9D3FA0B62A07FC6716C4A33AB6C9C22FFC1DE8E4` |
| `Test-Hardware.ps1` | **FAIL** | ★15。★3 の解消で入力が揃い、初めて実行できた。焼き込みは成功し、実機が自分で否定判定を出している |
| `Test-M5UnifiedHardware.ps1` | 初回 **FAIL** → ★10 を直して **PASS** | 3 回連続 `EXIT=0` |
| `Test-DualCoreHardware.ps1` | 初回 **FAIL** → ★10・★11 を直して **PASS** | 3 回連続 `EXIT=0` |
| `Test-Touch.ps1` | **未実行** | 入力 `build\arduino-phase5-m5unified\M5Unified.ino.bin` が ★3 で生成されない |

実機 5 本はいずれも**上流のホスト側テストが焼く .bin を作れない**ため未実行。
ボード・ケーブル・給電・ポートはすべて揃っていた。

## 分かったこと

### ★1 `Resolve-ArduinoEsp32S3Sdk.ps1` が chip を取らない（この run で修正済み）

`New-Fmp3PrebuiltStages.ps1 -Chip esp32` が configure で止まった:

```
CMake Error at CMakeLists.txt:156 (message):
  Required file was not found:
  C:/Users/honda/AppData/Local/Arduino15/packages/m5stack/tools/esp32s3-libs/3.3.8/ld/esp32.rom.ld
```

esp32 を指定したのに `esp32s3-libs` を見ている。resolver が `esp32s3-libs`・
`xtensa\esp32s3\...\core-isa.h`・`esp32s3.peripherals.ld`・`esp32s3.rom.ld` を
すべてハードコードしていて、`-Chip` を受け取る口が無かった。
`scripts/arduino_sdk.py` は同じものを `chip` 引数で持っている（Linux 側はこれを
使うので露出しなかった）。e485cc2 が直したツールチェーン名の件と同じ族で、
resolver だけ取り残されていた。

resolver に `-Chip`（`esp32s3`/`esp32`、既定 `esp32s3`）を追加し、
`New-Fmp3PrebuiltStages.ps1` から渡すようにした。他の呼び出し元
（`Install-ArduinoIdeIntegration.ps1` / `Invoke-PortableFmp3Recipe.ps1`）は
渡さないので既定のまま挙動不変。

### ★2 `examples/LibraryInfo` は stock の M5Stack FQBN では link できない

`Test-ArduinoLibrary.ps1` と `Test-ArduinoReleasePackage.ps1` の LibraryInfo
コンパイル段:

```
LibraryInfo.ino:19:(.text._ZN12_GLOBAL__N_13logEPKc+0xc): undefined reference to `target_fput_log'
collect2.exe: error: ld returned 1 exit status
```

両方とも `--fqbn m5stack:esp32:m5stack_cores3`（stock の M5Stack プラットフォーム）
で建てている。だが LibraryInfo.ino は自分のコメントで
「This port does not bring the M5Stack core's Serial with it … The kernel's own
log port is what every other example writes to, so this one does too」と宣言して
`target_fput_log` を呼ぶ。FMP3 ランタイムが link されない stock 経路では、
この例題は原理的に解決できない。

Linux 側の `scripts/verify_package.py` は LibraryInfo を `toppers:esp32` で
建てている（`PACKAGE = "toppers:esp32"`）。つまり**例題が kernel log port へ
移った時に、PowerShell 側 2 本の FQBN が追従しなかった**。

`Test-ArduinoReleasePackage.ps1` はこの 1 段だけが stock FQBN で、後続の
Fmp3Minimal / Blink / DualCore / M5Unified / WiFi はすべて
`toppers:esp32:m5cores3_fmp3` で建てている。stock のままにする理由が
（ライブラリとして認識されることの確認以外に）見当たらない。

### ★3 legacy CMake 経路（`Build-SeamS3M5.ps1`）はどの呼び出し元からも通らない

`Test-RecipeOverride` / `Test-SketchBridge` / `Test-M5Unified` /
`Test-M5UnifiedLink` / `Test-Smp` の 5 本が同じ場所で落ちる:

```
Build-SeamS3M5.ps1 : Cannot bind argument to parameter 'Path' because it is an empty string.
    + FullyQualifiedErrorId : ParameterArgumentValidationErrorEmptyStringNotAllowed,Build-SeamS3M5.ps1
```

`Build-SeamS3M5.ps1` の `-Fmp3Repository` は既定が `''` で、`Assert-Path` の
`[Parameter(Mandatory)][string]$Path` が空文字を拒否する。つまり実質必須。
ところが中間の `Invoke-FmpImageRecipe.ps1` と `Invoke-SketchLinkRecipe.ps1` は
`-Fmp3Repository` を受け取る口も渡す口も持たない。`Test-WiFiScan.ps1` だけが
パラメータを持っている。

`git log -S` によると空既定は初回コミット `c0c47d4`(2026-08-31) から。
**この 5 本はこのリポジトリの歴史上一度も通っていない**と読める。

### ★4 legacy 経路が要求する patch がリポジトリに無い

`-Fmp3Repository` を渡せる `Test-WiFiScan.ps1` に、公開 `toppers/fmp3_esp_idf`
（`fdd89f8` 公開スナップショット 2026-08-18）を clone して渡すと:

```
esp32_s3へ patches\esp32_s3-windows-host-tools.patch を適用してください。
```

`Build-SeamS3M5.ps1` は参照リポジトリの `CMakeLists.txt` に
`A1_ESPTOOL_EXECUTABLE` があることを要求するが、公開スナップショットには無く
（`grep -c` = 0）、要求される `patches/` ディレクトリはこのリポジトリに存在しない。
**したがって legacy 経路は、外部リポジトリを用意しても通らない。**

`Test-BaselineEnvironment.ps1` の `-Fmp3Repository` も同じ外部チェックアウトを
指す引数で、この機械には無かった。この run では arduino_esp32 自身を渡して
git 状態表示だけ通した（値の意味は本来と違う）。なお同スクリプトは
`git rev-parse` が失敗すると `$ErrorActionPreference='Stop'` で
NativeCommandError を投げ、**失敗サマリを表示せずに死ぬ**（exit 1 ではあるが、
何が FAIL だったか出ない）。

### ★5 `Install-ArduinoIdeIntegration.ps1` が `-PrebuiltStageRoot` 無しで動かない（この run で修正済み）

`Test-ArduinoReleasePackage.ps1` はこのインストーラを `-PrebuiltStageRoot` 無しで
呼ぶ（ZIP は prebuilt stage ではなく runtime ソース一式を同梱する legacy 経路の
配布物なので、それが正しい呼び方）。しかし:

```
Install-ArduinoIdeIntegration.ps1 : Cannot bind argument to parameter 'Path' because it is an empty string.
```

all-in-one メニューの有無を見る `Test-Path (Join-Path $PrebuiltStageRoot 'all-in-one')`
が、空既定を守っていなかった。`Join-Path` は空 `-Path` を受け付けない。
stage root が無ければ stage も無いので、空チェックを足した。

### ★6 `Invoke-PortableFmp3Recipe.ps1` に link-all-objects 修正が入っていない

★2・★5 を回避した probe（LibraryInfo を `toppers:esp32:m5cores3_fmp3` で建てる）
まで進めると、次はこうなる:

```
xip/objs/LibraryInfo.ino.o:(.literal._ZN12_GLOBAL__N_16reportEv+0x18):
  undefined reference to `toppers::fmp3::m5cores3::libraryInfo()'
```

`libraryInfo()` は `src/ToppersFMP3_M5CoreS3.cpp` にある。だが
`Invoke-PortableFmp3Recipe.ps1` は FMP3 link に渡す object を手で選んでいる:
スケッチの `.cpp.o` と `ArduinoSketchBridge.cpp.o`、それに wifi-connect のときだけ
`ToppersFMP3_WiFi.cpp.o`。`ToppersFMP3_M5CoreS3.cpp.o` はどこにも入らない。

`scripts/fmp3_link.py` は `collect_arduino_objects` で
「スケッチの全 TU + manifest が名指しする library object を force-link + 残りの
library object は archive にして on-demand」という形になっている
（aa9eb04「link-all-objects」の系列）。**PowerShell 側の legacy recipe だけが
その修正を受けていない。** これは配布 ZIP に同梱される recipe なので、テストの
不備ではなく製品側の欠落。

## ★6・★9 の CMake 変更に対する回帰確認

★6 で `ports/m5stack_xtensa/runtime/CMakeLists.txt` と
`cmake/xip_build.cmake` を触っているので、ステージ生成が壊れていないかを
`-Clean` 付きで両チップ作り直して確認した。object 数は修正前と一致:

| チップ | minimal | m5-unified | wifi-connect |
| --- | --- | --- | --- |
| esp32s3 | 47 | 138 | 74 |
| esp32 (LX6) | 48 | 139 | 75 |

どちらも `EXIT=0`。`ARDUINO_ARCHIVE` は `fmp3_arduino_image` の経路にしか
関わらず、`fmp3_prebuilt` ターゲットには影響しないことが実測で確かめられた。

作り直したステージで M5Core (LX6) を焼き直したところ、修正前と同じ出力:

```
TOPPERS/FMP3 Kernel Release 3.4.0 for ESP32-DevKitC (Xtensa LX6) (Sep  3 2026, 00:17:56)
Processor 1 start.  /  Processor 2 start.
I (0) M5GFX: [Autodetect] M5Stack
[BEGIN] <- P2 M5Unified::_check_boardtype = 1
[M5] M5.begin and initial LCD draw PASS
[Arduino] setup complete
```

実機まで含めて回帰なし。

### ★10 実機 2 本が、出荷版イメージには入っていない self-test のマーカーを要求する

CoreS3 を繋ぎ直して `Test-DualCoreHardware.ps1` と
`Test-M5UnifiedHardware.ps1` を実行できた（★2・★5・★6・★8・★9 の修正で入力の
`.bin` が生成されるようになったため）。**両方 FAIL**。焼き込みは成功し、
ボードは健全に動いている。落ちるのは判定条件のほう。

`Test-DualCoreHardware.ps1`（COM24 / MAC `44:1B:F6:E2:73:AC`）が実際に受けたもの:

```
Processor 2 start.
Processor 1 start.
[Arduino] task start
[Arduino] task=2 processor=1
[Arduino] setup complete
[Arduino] loop heartbeat        (以降ずっと)
no time event is processed in hrt interrupt on PRC2.
```

両プロセッサが起動し、Arduino タスクは processor 1 に居て、heartbeat が続いて
いる。panic も無い。それでも:

```
Required DualCore hardware marker was not received:
  \[SMP\] Arduino loops=\d+ PRC2 iterations=\d+
```

`Test-M5UnifiedHardware.ps1` も同様に、1 つ目は通って 2 つ目で落ちる:

```
[M5] M5.begin and initial LCD draw PASS      <- 通る
[Arduino] setup complete
Required M5Unified hardware marker was not received:
  \[M5\] board=(10|17) display=320x240 pmic=4 battery_mV=\d+
```

マーカーの出所を全部当たると理由がはっきりする:

| マーカー | 出す場所 | 出荷パッケージに入るか |
| --- | --- | --- |
| `[M5] M5.begin and initial LCD draw PASS` | `ports/.../m5/adapter/m5_arduino_adapter.cpp` と `examples/M5Unified/M5Unified.ino` | **入る** |
| `[M5] board=... display=... pmic=... battery_mV=...` | `fmp_app/phase5/phase5_m5_selftest.c` | 入らない |
| `[M5] 60-second M5Unified integration PASS` | 同上 | 入らない |
| `[SMP] Arduino loops=... PRC2 iterations=...` | `fmp_app/phase6/phase6_smp_selftest.c` | 入らない |
| `[SMP] dual-core isolation PASS` | `phase5_m5_selftest.c` / `phase6_smp_selftest.c` | 入らない |

`New-Fmp3PrebuiltStages.ps1` の `-SelfTest` の説明が自分で書いている通り、
「self-test はテストスイートのものであって製品のものではないので、Boards
Manager パッケージに入るステージはこのスイッチ無しで作る」。ところがこの 2 本は
**出荷パッケージから作った `.bin` を焼いて、self-test だけが出すマーカーを
要求している**。構造的に PASS しない。

さらに `phase6_smp_selftest` はどのプロファイルからも選ばれなくなっている
（`dual` が廃止され、`Invoke-PortableFmp3Recipe.ps1` の
`$applicationName` の switch にも phase6 は無い）。同スクリプトのコメントも
「(The dual-core self-test, phase6_smp_selftest, is still built by Test-Smp.ps1
on the legacy cmake path - the profile is gone, the SMP isolation test is not.)」
と書いているが、その `Test-Smp.ps1` は ★3 で走らない。

**(b) で修正した**（判定を出荷イメージが実際に出すものに揃える）。この 2 本の
価値は「利用者が実際に受け取るものが実機で動く」ことの確認であって、self-test を
混ぜると出荷物を焼いていないことになる。self-test の網羅性が要るなら
`-SelfTest` を使う専用の項目として別に立てるのが素直。

要求マーカーは出荷イメージが実際に出すものだけにした。出所は
`src/bridge/ArduinoSketchBridge.cpp`（`[Arduino] task start` /
`task=N processor=M` / `setup complete` / 1000 loop ごとの `loop heartbeat`）と
`examples/M5Unified/M5Unified.ino`（`M5.begin and initial LCD draw PASS` /
`... or ... FAILED`）:

| テスト | 要求するもの |
| --- | --- |
| `Test-DualCoreHardware.ps1` | `Processor 1 start.` / `Processor 2 start.`（順序不定なので個別に照合）/ `[Arduino] task=\d+ processor=1` / `setup complete` / heartbeat を **3 回以上**（`-RequiredHeartbeats`） |
| `Test-M5UnifiedHardware.ps1` | `[M5] M5.begin and initial LCD draw PASS`（元からあった 1 本目。これは出荷版が出す）/ `task=\d+ processor=1` / `setup complete` / heartbeat 3 回以上 |

heartbeat は match ではなく**回数**を見る。1 回なら loop() が回ったこと、
複数回なら回り続けていることを言える。`M5.begin or initial LCD draw FAILED` は
出荷版の例題自身が出す否定側の判定なので、要求側ではなく失敗検出側に置いた。

### ★11 リセットが実際にはかかっておらず、起動時出力を取り逃していた

★10 を (b) で直した直後、`Test-M5UnifiedHardware.ps1` は PASS したのに
`Test-DualCoreHardware.ps1` は `Processor 1 start.` が来ないと言って落ちた。
取れていたのは heartbeat 20 回と、途中で切れた 2 行だけ:

```
oSystem logging task is started on port 1.
lete
```

`lete` は `[Arduino] setup complete` の尻尾である。つまりバナーも
`task=2 processor=1` も、ポートを開く前に流れ終わっていた。M5Unified 側が
通ったのは `M5.begin` に時間がかかって出力が長い窓に散らばるからで、
DualCore は `setup complete` までが一瞬なので間に合わない。

4 本すべてが持っていたシーケンスはこうだった:

```powershell
$serial.DtrEnable = $false; $serial.RtsEnable = $false; sleep 100
$serial.DtrEnable = $true;  $serial.RtsEnable = $true;  sleep 100
$serial.RtsEnable = $false;                             sleep 100
$serial.DtrEnable = $false
```

DTR と RTS を同時に true にしているので、ESP32 の自動リセット回路
（ESP32-S3 の USB-Serial/JTAG も同じ）では EN も IO0 もアサートされない。
**リセットがかかっていなかった。** esptool の "Hard resetting via RTS pin" で
起動した分をたまたま拾えていただけである。

きれいな EN パルスに置き換えた（COM24 で単体確認したうえで）:

```powershell
$serial.DtrEnable = $false
$serial.RtsEnable = $true      # EN low
Start-Sleep -Milliseconds 300  # 100 ms では S3 に足りないことがある
$serial.RtsEnable = $false     # EN release
```

これでハンドルも切れずにバナーから取れる。`Test-Hardware.ps1` と
`Test-Touch.ps1` も同一のシーケンスを持っていたので同じ置き換えを当てたが、
**この 2 本は ★3 で走らないので、シーケンス単体が COM24 で効くこと以上の
検証はできていない。**

### 実機 2 本 PASS

```
Packaged DualCore COM hardware probe passed.
  Processors: 1 and 2 started
  Arduino task on PRC1, 3 loop heartbeat(s)

Packaged M5Unified COM hardware probe passed.
  M5.begin and initial LCD draw PASS
  Arduino task on PRC1, 3 loop heartbeat(s)
```

取りこぼしが原因だった以上、一発では信用できないので **3 回連続で実行して
両方とも `EXIT=0`** を確認した。ボードは CoreS3 / COM24 / MAC
`44:1B:F6:E2:73:AC`、カーネルは
`TOPPERS/FMP3 Kernel Release 3.4.0 for ESP32-S3-DevKitC-1`。

## スイート外: 動く経路での実機確認（2 台とも PASS）

実機 5 本が全部未実行のままなので、**動いている経路**
（prebuilt stage + `install_platform.py`）で 2 台を叩いた。スイートに無い項目
なのでチェックリストには足していないが、README の「★対象外」が挙げる穴のうち
**M5Core を Windows で未検証**という一点はこれで埋まる。

ユーザの実 sketchbook は触らず、隔離した sketchbook に platform を入れた:

```powershell
python scripts\install_platform.py --sketchbook <temp>\sketchbook `
    --prebuilt-stage-root build\prebuilt
#   Board: M5Core (TOPPERS/FMP3)   (esp32)
#   Board: M5CoreS3 (TOPPERS/FMP3) (esp32s3)
#   Stages: 6
```

`examples/M5Unified` を `FMP3Runtime=m5` で両ボード分ビルドして焼いた。

### CoreS3 / COM24 / MAC 44:1B:F6:E2:73:AC — PASS

```
[BEGIN] <- P2 M5Unified::_check_boardtype = 10
[BEGIN] -> P6a Power_Class::begin (PMIC detect)
[BEGIN] <- P6a Power_Class::begin = 1
[BEGIN] -> P9b IMU_Class::begin (BMI270/BMM150)
[BEGIN] <- P9b IMU_Class::begin = 1
[M5] M5.begin and initial LCD draw PASS
[Arduino] setup complete
[Arduino] loop heartbeat
n time event is processed in hrt interrupt on PRC2.
```

`W (0) M5GFX: CoreS3 touch version read failed (CIPHER:0x64 / FIRMID:0x03 /
VENDID:0x01), panel:ILI9342C` は出るが、Touch_FT5x06::init 自体は = 1 で
返っていて PASS 判定は通っている。

### 無印 M5Core (LX6) / COM31 / MAC 5C:01:3B:0C:CA:44 — PASS

```
TOPPERS/FMP3 Kernel Release 3.4.0 for ESP32-DevKitC (Xtensa LX6) (Sep  2 2026, 21:44:29)
Processor 1 start.
Processor 2 start.
[Arduino] task=2 processor=1
I (0) M5GFX: [Autodetect] M5Stack
[BEGIN] <- P2 M5Unified::_check_boardtype = 1
[M5] M5.begin and initial LCD draw PASS
[Arduino] setup complete
no time event is processed in hrt interrupt on PRC2.
```

**Windows ホストで LX6 のステージを作り、焼いて、両プロセッサが上がるところまで
通った。** ★1 の修正が効いている先はここ。

`bt-classic` profile と 2 ボードが 1 パッケージに入った Release ZIP は、
依然として Windows では未検証（前者は `New-Fmp3PrebuiltStages.ps1` の
ValidateSet に無く、後者は ★2・★6 の先）。

### ★6 の修正が効いたことの確認

`Test-ArduinoReleasePackage.ps1` の LibraryInfo 段が ★6 をちょうど踏むので、
そこで判定した。オンデマンドのアーカイブに入ったのは、強制リンクしなかった
ライブラリ object ちょうど 3 個:

```
$ xtensa-esp32s3-elf-ar t libarduino_ondemand.a
ToppersFMP3_BT.cpp.o
ToppersFMP3_M5CoreS3.cpp.o
ToppersFMP3_WiFi.cpp.o
```

両側が成立していることを確認した。**足りなかった object が入る**:

```
$ xtensa-esp32s3-elf-nm -C LibraryInfo.ino.elf
42006354 T toppers::fmp3::m5cores3::libraryInfo()
```

**入ってはいけない object が入らない**: `Fmp3Minimal.ino.elf` と
`Blink.ino.elf` は minimal プロファイルでリンク成功していて、
`ToppersFMP3_WiFi.cpp.o` を引き込んでいない。全部を強制リンクにしていたら
ここで Wi-Fi シンボルの undefined reference で止まる。

### ★8 `Test-ArduinoReleasePackage.ps1` が廃止済みの menu 値で建てていた（この run で修正済み）

★6 を越えた先で止まった。★6 とは無関係な既存の欠陥:

```
Invalid FQBN: getting build properties for board toppers:esp32:m5cores3_fmp3:
  invalid value 'dual' for option 'FMP3Runtime'
```

このテストは 150 行付近で「`dual` と `wifi` は廃止され、生き残ったのは
`minimal`/`m5`/`wificonnect`」と**自分で assert しておきながら**、その後
`FMP3Runtime=dual`（DualCore）と `FMP3Runtime=wifi`（WiFiScan）で建てていた。
実際の boards.txt にある値は 3 つだけ。★2 と同じ族で、menu が廃止された時に
このテストが追従しなかったもの。

対応先は Linux 側 `scripts/verify_package.py` の matrix と一致させた
（`"m5": [..., "DualCore", ...]` / `"wificonnect": ["WiFiConnect", "WiFiScan", ...]`）:

- DualCore → `FMP3Runtime=m5`（SMP を持つのは m5-unified ランタイム。
  boards.txt の表示も "M5Unified + Dual Core"）。`--library` は足していない
  — DualCore が include するのは `ToppersFMP3_ArduinoBridge.h` だけ
- WiFiScan → `FMP3Runtime=wificonnect`（`ToppersFMP3_WiFi.h` を使う）

### ★9 legacy レシピは m5 プロファイルで M5Unified を include しないスケッチを建てられない

★8 を直した先で止まった。`FMP3Runtime=m5` で DualCore を建てるとこうなる:

```
extras	ools\Invoke-PortableFmp3Recipe.ps1:113
  Could not resolve the M5GFX source selected by Arduino.
```

`Invoke-PortableFmp3Recipe.ps1` は m5 プロファイルのとき M5GFX と M5Unified の
**ソース**を必要とする（CMake 側でランタイムに組み込むので）。その場所を
`Find-ArduinoLibrarySourceRoot` が `build/libraries/<名前>` の `.d` から読むが、
そこは **Arduino がそのライブラリをコンパイルしていなければ存在しない**。
DualCore が include するのは `ToppersFMP3_ArduinoBridge.h` だけなので、何も無い。

`--library` を足しても効かない。arduino-cli は `#include` の無いライブラリを
ビルドに含めないので、`Used library` に出てこない:

```
Used library         Version Path
ToppersFMP3-M5CoreS3 0.3.0   ...
```

**★8 の対応付け自体は正しい**ことを出荷経路で確認した。prebuilt stage を入れた
プラットフォームでは DualCore は `FMP3Runtime=m5` で普通に建つ:

```
$ arduino-cli compile --fqbn toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5 examples/DualCore
最大3145728バイトのフラッシュメモリのうち、スケッチが199372バイト（6%）を使っています。
EXIT=0
```

stage には M5GFX/M5Unified のオブジェクトが既に入っているので、スケッチが
include するかどうかに関係なくリンクできる。**legacy 経路だけがこの制約を持つ。**
★6 と同じファイルだが別の機構。

**この run で修正した。** `Find-ArduinoLibrarySourceRoot` は「Arduino が選んで
いない」を異常扱いせず空を返すようにし、`Resolve-M5LibrarySource` を足して
探す順序をこう決めた:

1. 明示の `-M5GfxSource` / `-M5UnifiedSource`（呼び出し側が知っている場合）
2. Arduino が選んだもの — include しているスケッチは、自分がコンパイルした
   のと同一のソースに対してリンクされる
3. インストール先の兄弟ライブラリ。`-LibraryRoot` は
   `<sketchbook>/libraries/<このライブラリ>` なので、その親は arduino-cli が
   実際に使っている libraries ディレクトリで、そこの兄弟は arduino-cli が
   選んだであろう同じコピーになる（推測ではない）
4. 既定の sketchbook — `New-Fmp3PrebuiltStages.ps1` が同じ 2 つのソースに
   対して使っているのと同じ場所

開発ツリー（`-LibraryRoot` がリポジトリ自身）は 3 で外れて 4 に落ちる。
どれにも無ければ、探した場所を全部並べて `-M5GfxSource` を渡せと言って止まる。

★8 を直した結果、`Invalid FQBN` は消え、その先の ★9 に当たった。
**FQBN の対応自体が正しいことは別途確認済み** — ステージ経路（出荷される経路）で
DualCore を `FMP3Runtime=m5` で建てると通る:

```
最大3145728バイトのフラッシュメモリのうち、スケッチが199372バイト（6%）
EXIT=0
```

### ★9 legacy レシピの m5-unified は、スケッチが M5Unified を include していないと建たない

★8 を直して DualCore が実際にレシピまで到達するようになった結果に出たもの。
★8 が作った問題ではなく、★8 が隠していた問題:

```
Invoke-PortableFmp3Recipe.ps1:113
    throw "Could not resolve the $LibraryName source selected by Arduino."
  -> Could not resolve the M5GFX source selected by Arduino.
```

`Find-ArduinoLibrarySourceRoot` は `build/libraries/M5GFX/**/M5GFX.cpp.d` を読んで
ソースの在処を特定する。つまり **arduino-cli が実際に M5GFX をコンパイルしている
こと**が前提で、それが起きるのはスケッチが M5Unified を include したときだけ。
DualCore が include するのは `ToppersFMP3_ArduinoBridge.h` のみ。

ステージ経路には無い制約である。あちらは M5GFX/M5Unified がステージに
コンパイル済みで入っているのでソースを要らない。`dual` プロファイルが
m5-unified に統合された時に、legacy 経路だけがこの依存を負ったと読める。

**この run で修正済み。** `Find-ArduinoLibrarySourceRoot` は throw をやめて
空を返すようにし、判断を新しい `Resolve-M5LibrarySource` に集めた。解決順:

1. 明示の `-M5GfxSource` / `-M5UnifiedSource`
   （`New-Fmp3PrebuiltStages.ps1` と同名でレシピにも追加）
2. **スケッチのビルドが選んだソース** — 従来の挙動。ランタイムをスケッチと
   同一ソースで建てるという趣旨はここで守られる
3. `-LibraryRoot` の親の兄弟。`-LibraryRoot` は
   `<sketchbook>\libraries\ToppersFMP3-M5CoreS3` なので、その親は
   **arduino-cli が実際に使っている** libraries ディレクトリであり、
   そこの兄弟は arduino-cli が選んだはずの同じコピー。推測が要らない
4. `Documents\Arduino\libraries\<name>\src` — 開発ツリー
   （`-LibraryRoot` がリポジトリ自身）向け

全部外れたときは、探した場所を列挙して `-M5GfxSource` を渡せと言って落ちる。

## `Test-ArduinoReleasePackage.ps1` PASS

★2・★5・★6・★8・★9 を直した結果、**完走した**（`EXIT=0`）。

```
Arduino Release asset ZIP passed isolated installation validation.
  LibraryInfo FQBN: toppers:esp32:m5cores3_fmp3
  FMP3 FQBN:        toppers:esp32:m5cores3_fmp3
  Blink FQBN:       toppers:esp32:m5cores3_fmp3
  DualCore FQBN:    toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5
  M5Unified FQBN:   toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5
  WiFi FQBN:        toppers:esp32:m5cores3_fmp3:FMP3Runtime=wificonnect
  CCAE66401CE961982C70A7AD9D3FA0B62A07FC6716C4A33AB6C9C22FFC1DE8E4  ToppersFMP3-M5CoreS3-0.3.0.zip
```

LibraryInfo / Fmp3Minimal / Blink / DualCore / M5Unified / WiFiScan /
WiFiConnect が隔離した sketchbook で建ち、ハッシュ照合まで通った。

これで実機 2 本が要求する `.bin` も生成されるようになった:

| 入力 | サイズ |
| --- | --- |
| `build\release\install-test\m5-unified-build\M5Unified.ino.bin` | 339568 |
| `build\release\install-test\dual-core-build\DualCore.ino.bin` | 288176 |

この 2 本は CoreS3 を繋ぎ直して**実行でき、★10・★11 を直して PASS した**。
初回の FAIL は、イメージではなく判定条件と取りこぼしが原因だった。
`Test-Hardware.ps1` と `Test-Touch.ps1` は依然 ★3 の先なので入力自体が無い。
実機 5 本はいずれも `--chip esp32s3` 固定で CoreS3 専用。M5Core では代替できない。

### ★3・★4 の解決: legacy 経路を vendored runtime に付け替えた

外部 `fmp3_esp_idf` と存在しない patch を要求していた `Build-SeamS3M5.ps1` を、
**このリポジトリに vendored された runtime を建てるもの**に置き換えた。

決め手は、5 本が必要とするものが**すべて既にここにある**ことだった:

| テスト | 必要なアプリ | 在処 |
| --- | --- | --- |
| SketchBridge | `phase3_arduino_app` | `ports/m5stack_xtensa/app/phase3/` |
| M5UnifiedLink | `phase4_freertos_app` | `fmp_app/phase4/` |
| M5Unified | `phase5_m5_selftest` | `fmp_app/phase5/` |
| Smp | `phase6_smp_selftest` | `fmp_app/phase6/` |
| WiFiScan | wifi scan アダプタ | `runtime/wifi/adapter/toppers_wifi_scan.c` |

カーネルは `third_party/fmp3_core`、ランタイムは `ports/m5stack_xtensa/runtime`。
`New-Fmp3PrebuiltStages.ps1` と `Invoke-PortableFmp3Recipe.ps1` は既にその木を
CMake・Ninja・M5Stack のツールチェーンだけで建てている。つまり外部リポジトリは
**リポジトリが自分で vendored して不要にした依存の残骸**だった。patch を
どこかから探してくるより、既に動いている経路に寄せるほうが筋が通る。

`Build-SeamS3M5.ps1` のパラメータは vendored の語彙に置き換えた:

| 廃止 | 代替 |
| --- | --- |
| `-Fmp3Repository` | なし（vendored） |
| `-GitBash` | なし（ROM linker-script setup は外部木のためのものだった） |
| `-SkipRomLinkSetup` | 同上 |
| `-Variant m5` / `-Variant wifi` | `-Profile m5-unified` / `-Profile wifi-connect` |
| `-WifiApplication wifi_scan` | なし（両アダプタが wifi-connect に入っている） |
| `-ProcessorCount` | `-Profile`（m5-unified が `FMP3_PRC_NUM=2`） |
| `-ExternalApplicationDirectory/-Name` | `-ApplicationDirectory` / `-ApplicationName` |

出力の契約（`xip/fmp_xip.elf`・`xip/app_xip.bin`・nm による未定義 0 検査・
SHA-256 表示）はそのまま。`-ExternalArchive`（★6 と同じオンデマンド機構）と
`-Chip` を追加した。

profile はアプリごとに実測で決めた。phase3 を minimal で建てると残る未定義が
`toppers_arduino_task` だけ（テストが供給するブリッジ）になることを確認して
アプローチを検証してから、他に広げた。

`Invoke-SketchLinkRecipe.ps1` の ★6 同族欠陥（sketch の主 TU とブリッジだけを
渡していた）も、経路が通るようになったのでここで直して検証した。

### ★12 CMakeCache の照合先が外部リポジトリ側の変数だった

★3・★4 を通しても、2 本はここで落ちるようになっていた:

| テスト | 見ていたもの | 実態 |
| --- | --- | --- |
| Smp | `A1_M5_PRC_NUM:STRING=2` | vendored に無い。プロセッサ数は profile から導かれる |
| WiFiScan | `A1_VARIANT:STRING=wifi` / `A1_WIFI_APP:STRING=wifi_scan` | 同じく存在しない |

生成物側の直接証拠に置き換えた。Smp は `build.ninja` の **`TNUM_PRCID=2`** で、
これは判別力がある（同じビルドを minimal で作ると `TNUM_PRCID=1`。実測）。
WiFiScan は `FMP3_RUNTIME_PROFILE:STRING=wifi-connect` と
`toppers_wifi_scan.c.obj`。

その過程で、`-DFMP3_RUNTIME_PROFILE` を型なしで渡すと cache に
`:UNINITIALIZED` として入ることに気づいた（自分が書いた照合が外れた）。
builder 側で `:STRING` を明示した。

WiFiScan の要求シンボルからは `main_task` を外した。あれは外部木の入口名で、
vendored では `sta_ker`（カーネル）と `toppers_arduino_task`（アプリ）になる。
さらに**スキャンアダプタ自身**の
`toppers_fmp3_wifi_scan_networks` / `toppers_fmp3_wifi_ssid` /
`toppers_fmp3_wifi_rssi` を要求に加えた。`esp_wifi_*` 4 本だけでは
「IDF の Wi-Fi API が繋がっている」しか言えず connect 経路でも満たせるので、
これが**スキャン**のテストである根拠が無かった。

WiFiScan は単体イメージが建たない（下記）ので、他の 4 本と同じく recipe 経由で
`examples/WiFiScan` を建てる形に変えた。

### ★13 vendored のアプリはどれも単体では建たない

`Invoke-FmpImageRecipe.ps1`（`Test-RecipeOverride.ps1` 専用）は、スケッチの
オブジェクトを**あえて繋がずに** FMP3 イメージを publish する。それが override の
主旨である。ところがどのアプリも建たない:

```
xip/objs/kernel_cfg.o:(.rodata+0x248): undefined reference to `toppers_arduino_task'
```

アプリの cfg が `CRE_TSK(ARDUINO_TASK, ... toppers_arduino_task ...)` を宣言して
いる。**注意**: `phase5_m5_selftest.cfg` と `phase6_smp_selftest.cfg` は自分では
宣言していないが、`INCLUDE("phase5_m5_app.cfg")` 等で取り込んでいる。cfg を
grep するときは INCLUDE を追うこと（この run で一度間違えた）。

このポートのランタイムは常に Arduino ブリッジタスクを作る設計なので、
**単体で建つアプリは存在しない。** `Test-RecipeOverride.ps1` の前提が
現在の設計と衝突している。

### ★14 `Test-RecipeOverride.ps1` は未解決

★13 のため、この 1 本だけ通っていない。判定内容そのもの（published ELF/BIN が
FMP3 のものと一致、アプリが merged 0x10000 に保存される、`_start` /
`_kernel_start_dispatch` / `sta_ker` があり `app_main` /
`vTaskStartScheduler` / `loopTask(void*)` が無い）は、実はスケッチの
オブジェクトを繋いでも壊れない。禁止シンボルは M5Stack コアの Arduino main が
持ち込むもので、override がまさにそれを置き換えているからである。

方向は二つあり、どちらもこのテストの存在意義に関わるので**この run では
決めていない**:

- (a) ブリッジ（＋スケッチ）のオブジェクトを繋ぐ。主張は保たれるが、
  `Invoke-FmpImageRecipe.ps1` が `Invoke-SketchLinkRecipe.ps1` とほぼ同一に
  なり、2 本を分けている理由が「どの recipe 行を override するか」だけになる
- (b) Arduino タスクを作らない最小のアプリを 1 本新設する。前提をそのまま
  生かせるが、製品に入らないテスト専用アプリが増える

### ★15 FreeRTOS 互換シムの動的セマフォ / キュー上限が 4 本では足りない（未修正）

★3 が解けて `Test-M5UnifiedLink.ps1` が
`build/arduino-phase4-m5unified/M5UnifiedLink.ino.bin` を作れるようになったので、
`Test-Hardware.ps1 -Port COM24` を初めて実行できた。焼き込みは成功し、
Arduino タスクも上がる（`[Arduino] task=3 processor=1` / `setup complete`）。
落ちているのは**実機のプローブ自身の判定**:

```
[FreeRTOS] FreeRTOS compatibility probe start
[FreeRTOS] FAIL: binary semaphore creation
esp_shim: acre_sem failed ercd=-34 (ctx=0 lock=0) n=1
m5_shim: queue pool exhausted (need >4)
esp_shim: acre_sem failed ercd=-34 (ctx=0 lock=0) n=2
esp_shim: acre_sem failed ercd=-34 (ctx=0 lock=0) n=3
esp_shim: acre_sem failed ercd=-34 (ctx=0 lock=0) n=4
[FreeRTOS] FAIL: FromISR semaphore creation
[FreeRTOS] FAIL: all four semaphores allocate
[FreeRTOS] FAIL: deleted semaphore slot can be reused
[APIProbe] FreeRTOS API boundary probe FAILED
```

`ercd=-34` は `E_NOID`。**n=1 の時点で既に枯れている**ので、プローブが動く前に
m5-unified ランタイム自身が上限まで使い切っている。

上限は 2 箇所:

| 場所 | 値 |
| --- | --- |
| `fmp_app/phase5/phase5_m5_app.cfg` の `AID_SEM(4)` | 動的セマフォ 4 本 |
| `ports/m5stack_xtensa/runtime/m5/shim/m5_kernel_shim.c` の `M5_QUE_MAX` | 4（SPI 有効時は 6） |

cfg のコメントが自分でこう書いている:

> 旧プールと同じ 4 本。… 上限を変えるのは非退行の確認とは別の判断なので、
> 本数はそのままにする。**足りないと分かったらこの数値だけを変えればよい。**

**足りないことが実機で分かった**、というのがこの run の結果。ただし
「いくつにするか」はメモリ費用を伴う製品側の判断なので、実行者の一存では
決めない。**未修正。** 出荷イメージ（M5Unified / DualCore）の実機テストは
どちらも PASS しているので、これは FreeRTOS 互換シムの容量の話に閉じている。

### ★7 BOM 無しの `.ps1` に非 ASCII を書くと Windows PowerShell 5.1 が壊す

★6 を直す過程で自分で踏んだので書いておく。`Invoke-PortableFmp3Recipe.ps1` は
BOM 無し・純 ASCII のファイルで、そこへ日本語のコメントを足したら配布 ZIP 側の
コピーがこうなった:

```
extras	ools\Invoke-PortableFmp3Recipe.ps1:227 char:56
+     -Recurse -Filter '*.o' -File | Sort-Object FullName)
+                                                        ~
Unexpected token ')' in expression or statement.
```

構文は正しい。**Windows PowerShell 5.1 は BOM の無いファイルをシステム ANSI
コードページ（この機械では cp932）として読む**ので、UTF-8 の日本語が文字化けし、
その中に現れた引用符やバッククォートが構文を壊す。`.NET` の
`[Parser]::ParseFile` は UTF-8 として読んでしまうので、**パーサ検査では捕まらない**。
捕まえるには `powershell.exe -File` で実際に走らせるしかない。

`scripts/*.ps1` の現状:

| ファイル | BOM | 非 ASCII 行 |
| --- | --- | --- |
| `Build-SeamS3M5.ps1` | あり | 7 — 正しい組み合わせ |
| `New-Fmp3PrebuiltStages.ps1` | **無し** | 1（`★`）— 既存。cp932 で化けても comment 内で無害なので今は通っている |
| その他 20 本 | 無し | 0 |

つまり `New-Fmp3PrebuiltStages.ps1` は既に同じ地雷を踏んでいて、たまたま
`★` の化け方が構文を壊さないだけ。**BOM 無しの `.ps1` には ASCII しか書かない**か、
日本語を書くなら `Build-SeamS3M5.ps1` のように BOM を付けるかを決めておくべき。
この run で足したコメントは全部英語（ASCII）にして、周囲のコメントの言語にも
合わせた。

## この run でリポジトリに入れた変更

| ファイル | 内容 |
| --- | --- |
| `scripts/Resolve-ArduinoEsp32S3Sdk.ps1` | `-Chip` を追加（★1） |
| `scripts/New-Fmp3PrebuiltStages.ps1` | resolver に `-Chip` を渡す（★1） |
| `scripts/Install-ArduinoIdeIntegration.ps1` | 空 `-PrebuiltStageRoot` を守る（★5） |
| `scripts/Test-ArduinoLibrary.ps1` | 既定 FQBN を `toppers:esp32:m5cores3_fmp3` に、`-Sketchbook` を追加、platform 未導入を名指しで拒否（★2） |
| `scripts/Test-ArduinoReleasePackage.ps1` | LibraryInfo のコンパイルを installer の後・`toppers:esp32` に移し、使われなくなった `-Fqbn` を削除（★2） |
| `scripts/Test-Regression.ps1` | `-Sketchbook` を `Test-ArduinoLibrary.ps1` へ中継 |
| `docs/windows-tests/README.md` | platform 導入を前提手順に追記、`pwsh` が無い場合、この記録への指し先 |
| `scripts/Build-SeamS3M5.ps1` | 外部リポジトリ依存を捨て vendored runtime を建てる形に。`-Profile`/`-ApplicationDirectory`/`-ApplicationName`/`-ExternalArchive`/`-Chip`（★3・★4） |
| `scripts/Invoke-SketchLinkRecipe.ps1` | `-Profile` を中継、object 分割を ★6 と同じ形に（★3） |
| `scripts/Invoke-FmpImageRecipe.ps1` | `-Profile`/`-ApplicationDirectory`/`-ApplicationName` を追加（★3） |
| `scripts/Test-SketchBridge.ps1` | `-Profile minimal`（★3） |
| `scripts/Test-M5Unified.ps1` / `Test-M5UnifiedLink.ps1` | `-Profile m5-unified`（★3） |
| `scripts/Test-Smp.ps1` | `-Profile m5-unified`、`TNUM_PRCID=2` 照合へ（★3・★12） |
| `scripts/Test-WiFiScan.ps1` | recipe 経由に書き換え、照合と要求シンボルを vendored の実態へ（★3・★4・★12） |
| `scripts/Test-BaselineEnvironment.ps1` | `-SourceTree` 化、submodule 確認、git 失敗時の扱い |
| `scripts/Invoke-PortableFmp3Recipe.ps1` | object を強制リンク分とアーカイブ分に分ける。スケッチは全 TU、残りのライブラリ object は `ar rcsD` でアーカイブ化（★6）。`ar` はコンパイラ名から導く |
| `ports/m5stack_xtensa/runtime/CMakeLists.txt` | `ARDUINO_ARCHIVE` を受けて xip_build へ素通し（★6） |
| `ports/m5stack_xtensa/runtime/cmake/xip_build.cmake` | `link_xip` でアーカイブを object の後・SDK ライブラリの前に置く（★6） |
| `scripts/Test-ArduinoReleasePackage.ps1` | 廃止済みの `FMP3Runtime=dual`/`=wifi` を `m5`/`wificonnect` へ（★8） |
| `scripts/Invoke-PortableFmp3Recipe.ps1` | m5-unified の M5GFX/M5Unified ソース解決にフォールバックを入れ、`-M5GfxSource`/`-M5UnifiedSource` を追加（★9） |
| `scripts/Invoke-PortableFmp3Recipe.ps1` | M5GFX/M5Unified ソース解決にフォールバックを追加、`-M5GfxSource`/`-M5UnifiedSource` を新設（★9） |
| `scripts/Test-DualCoreHardware.ps1` | 判定を出荷イメージが出すものへ、`-RequiredHeartbeats` を新設（★10）、リセットを EN パルスへ（★11） |
| `scripts/Test-M5UnifiedHardware.ps1` | 同上（★10・★11） |
| `scripts/Test-Hardware.ps1` | リセットを EN パルスへ（★11）。実機で検証済み——起動時出力は取れており、FAIL は ★15 |
| `scripts/Test-Touch.ps1` | 同上（★11。★3 のため未検証） |

★3・★4 は直していない。どちらも「どう直すか」がテストの意図や配布形態の
判断を含むので、実行者が勝手に決めるべきでないと考えた。

`scripts/Invoke-SketchLinkRecipe.ps1` にも ★6 と同じ欠落がある（sketch の主 TU と
ブリッジだけを `-ExternalObjects` に渡す）。**直していない**: その経路は ★3・★4 で
そもそも走らないので、直しても検証できない。★3・★4 を片付けるなら同時にここも見ること。

**★2 を直しても `Test-ArduinoReleasePackage.ps1` は PASS しない**: LibraryInfo が
`toppers:esp32` に乗った先で ★6（`Invoke-PortableFmp3Recipe.ps1` の
link-all-objects 欠落）に当たる。★2 の修正は「失敗が本当の欠陥の位置に動いた」
という意味しかない。修正後の実行で確認済み — ZIP 生成 PASS、platform 導入 PASS、
その次で:

```
TOPPERS/FMP3 Arduino board platform installed.
...
Building the portable FMP3 runtime failed (exit=1).
Compiling the installed LibraryInfo example failed (exit=1).
```

`ninja -C build\release\install-test\build\fmp3-runtime-build` を直に回すと出る中身:

```
xip_build.cmake:108 (message):
  undefined reference to `toppers::fmp3::m5cores3::libraryInfo()'
  collect2.exe: error: ld returned 1 exit status
```

つまり修正前の `target_fput_log`（stock 経路に FMP3 ランタイムが無い）から、
`libraryInfo()`（ランタイムはあるがライブラリの object が link 対象に入らない）へ
移った。前者はテストの FQBN の問題、後者は配布 recipe の問題。
