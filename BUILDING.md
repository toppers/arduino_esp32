# ソースからビルドする

利用者がスケッチを建てるのに CMake も Python も要りません。ここは
**配布物そのものを作り直す**手順です。

## 前提

| 必要なもの | 備考 |
| --- | --- |
| M5Stack Arduino core **3.3.8** | ツールチェーンと ESP-IDF v5.5.4 の成果物を供給する |
| CMake / Ninja | ステージ生成にのみ使う |
| Python 3.9 以上 | 同上 |
| `M5GFX` 0.2.27 / `M5Unified` 0.2.20 | `M5Unified` 構成のステージが必要とする |

submodule を先に取得してください。

```bash
git submodule update --init --recursive
```

## 二段構えになっている理由

このポートは**配布物を作るときのビルド**と**利用者がスケッチを建てるときの
ビルド**が分かれています。

```text
[配布物を作るとき]  CMake + Ninja + FMP3 の cfg
                    → 構成ごとの prebuilt stage
                      （objs/ ld/ link-manifest.json objects.rsp）

[スケッチを建てるとき]  M5Stack core 同梱のツールチェーンと esptool だけ
                        → stage の .o とスケッチの .o をリンク
```

stage はスケッチに依存しません（cfg は構成ごとに固定で、スケッチに依存するのは
最終リンクだけ）。これが「利用者の環境に CMake を要求しない」ことの根拠です。

## 1. ステージを作る

```bash
python scripts/build_prebuilt_stages.py --cmake <path/to/cmake> --ninja <path/to/ninja>
```

`minimal` / `m5-unified` / `wifi-connect` が
`build/prebuilt/esp32s3/<構成>/` に出ます。実験的な `all-in-one` を作るには
`--profiles all-in-one` を渡します（既定には入っていません）。

Arduino のデータディレクトリは OS ごとに解決します
（`%LOCALAPPDATA%\Arduino15` / `~/Library/Arduino15` / `~/.arduino15`）。
別の場所にある場合は `--arduino-data` で渡してください。

### M5NanoC6（ESP32-C6）のステージ

```bash
python scripts/build_prebuilt_stages.py --chip esp32c6
```

`--chip esp32c6` の既定 profile は `minimal` / `wifi-connect` の 2 つだけです
（`m5-unified` / `bt-classic` は選べません。D11）。トゥールチェーンは
M5Stack Arduino core 3.3.8 が同梱する `esp-rv32` 2601 と `esp32c6-libs`
3.3.8 で、他の 3 ボードと同じ core から取れます（別途取得は不要）。

`wifi-connect` stage には、Wi-Fi 一式に加えて M5NanoC6 の GPIO API が入ります
（段6）: `ports/m5stack_riscv/runtime/arduino/arduino_gpio.c`（`pinMode` /
`digitalWrite` / `digitalRead`、`hal/gpio_ll.h` のみ）と `arduino_rgb_led.c`
（`rgbLedWrite`、RMT ch0 を `hal/rmt_ll.h` で直接叩き、割込みを使わず TX_DONE を
ポーリング。`RMT` / `RMTMEM` / `PCR` は `esp32c6.peripherals.ld` が供給）。
どちらも `runtime/CMakeLists.txt` の `wifi_objects` にだけ並んでいて、**`minimal`
stage には入りません**（minimal は段6 の前後で `banner.o` 以外 sha 同一）。
`attachInterrupt`（`arduino_interrupt.c`、段3）も同じく wifi-connect のみです。

Xtensa 側（`ports/m5stack_xtensa/runtime/arduino/arduino_gpio.c`、2026-09-15）にも
同じ `pinMode` / `digitalWrite` / `digitalRead` があり、`arduino_interrupt.c` と同じ
場所（m5-unified / wifi-connect / bt-classic の `seam_objects`）に並びます（minimal
には入りません）。ESP32（LX6）は IO_MUX のレジスタが等間隔でなく `hal/gpio_ll.h` の
IO_MUX 系が libsoc の `GPIO_PIN_MUX_REG_OFFSET[]` を要求するため、同じ表を
`PERIPHS_IO_MUX_*_U` マクロから私的に組み直して（数値リテラル無し、IDF の値と
`_Static_assert` で照合）レジスタマクロで書きます。ESP32-S3 は等間隔ですが
`gpio_ll_pullup_en` / `gpio_ll_pulldown_en` だけが同じ表を引くので、その 2 つは
避けて `IO_MUX_GPIO0_REG + 4 * n` へ直接書きます。wifi-connect の manifest には
`<chip>.peripherals.ld` が `extraLinkerScripts` として入ります（`GPIO` の供給。
同日に修正）。`examples/GpioInterrupt` が 4 板共通の自己駆動試験です。
ESP32 の m5-unified には `m5/compat/m5_newlib_stubs_lx6.c`（`_exit` / `_kill` /
`_getpid`）も入ります: SDK の `-fstack-protector` でスタック上の `char` 配列を
持つスケッチ関数が flash 側 newlib の `__stack_chk_fail` -> `_exit` を引くため
（S3 は `chip_rom_libc.c`、ESP32 の wifi-connect は自前の `__stack_chk_fail`、
bt-classic は `bt_idf_stubs.c` が同じ役をもつ）。

診断・対照用の CMake オプションは `--cmake-define` でそのままステージの
CMake 呼び出しへ渡せます。

| オプション | 既定 | 用途 |
| --- | --- | --- |
| `TOPPERS_C6_APM_UNBLOCK` | ON | LP/HP APM のロック解除。OFF にすると Wi-Fi scan/接続が意図的に 0 件になる対照ステージが作れます（`docs/c6-port.md` 段4「APM 対照」） |
| `TOPPERS_C6_WIFI_DIAG` | OFF | Wi-Fi 初期化の段階マーカー |
| `TOPPERS_C6_NET_DIAG` | OFF | port 7 の TCP/UDP echo サーバ、DHCP 直後の gateway ping、`ip=`/`gw=` 付きログ行。配布する stage では既定 OFF（段5 判断 S5-3。下記「変更するときに守ること」参照） |

対照ステージを既定の `build/prebuilt/esp32c6/<profile>/` に**上書きしない**
ように、`--output-directory <別の場所>` を必ず付けてください。既定のまま
`ON`/`OFF` を切り替えて作り直すと、直前に作った配布用ステージが対照用の
ものに置き換わります。

### M5Stamp-C5（ESP32-C5）のステージ

```bash
python scripts/build_prebuilt_stages.py --chip esp32c5
```

`--chip esp32c5` の既定 profile も `minimal` / `wifi-connect` の 2 つだけです
（`m5-unified` / `bt-classic` は選べません。C5 計画 A10）。トゥールチェーンは
M5NanoC6 と**同じ** `esp-rv32` 2601 で、SDK だけが `esp32c5-libs` 3.3.8 に
変わります。どちらも M5Stack Arduino core 3.3.8 に同梱されているので、
別途取得は要りません。

C5 は `ports/m5stack_riscv/runtime` の**チップ分岐**であって、別ポートの
複製ではありません（C5 計画 A2）。C6 と分かれているのは
`arch/riscv_gcc/esp32c5` / `target/m5stampc5_gcc` / `config/esp32c5` /
`seam/seam_c5_*` / `cmake/prebuilt_stage_c5.cmake` /
`cmake/toolchain-riscv-esp32c5.cmake` と、Arduino の GPIO・割込み層
（`arduino/arduino_gpio_c5.c`、`arduino/arduino_interrupt_c5.{c,cfg,h}`。
C6 版は USB や MSPI のピン番号と割込みの配線が C6 決め打ちなので、
共有せずチップごとに置いています）です。**C6 のステージのバイト列が
変わっていないこと**は X-check（下記）が毎段示します。

`wifi-connect` stage には Wi-Fi 一式と C5 の GPIO API（`pinMode` /
`digitalWrite` / `digitalRead` / `attachInterrupt`）が入り、`minimal` には
入りません（C6 と同じ切り分け）。**`rgbLedWrite` はありません**--
M5Stamp-C5 に WS2812 系の on-board RGB LED が無いためで、同梱例題
`NanoC6Gpio` は C5 では板ガードにより 1 行ログを出すだけの no-op です
（C5 計画 S5-6）。

| オプション | 既定 | 用途 |
| --- | --- | --- |
| `TOPPERS_C5_APM_UNBLOCK` | ON | LP/HP APM のロック解除。OFF にすると Wi-Fi scan が意図的に 0 件になる対照ステージが作れます（`docs/c5-port.md` 段4「APM 対照」） |
| `TOPPERS_C5_WIFI_DIAG` | OFF | Wi-Fi 初期化の段階マーカー |
| `TOPPERS_C5_NET_DIAG` | OFF | port 7 の TCP/UDP echo サーバ、DHCP 直後の gateway ping、`ip=`/`gw=` 付きログ行。**配布する stage では既定 OFF**（段5 判断 S5-3）。ON 経路は段5 で「今も建ってリンクできる」ことだけを机上で確認しています（同じスケッチで flash +2,224 B / RAM +64 B、最終 ELF の `nm -u` は ON / OFF とも空）。**ON 像の実機動作は未実測です** |

C6 と同じく、対照ステージは `--output-directory <別の場所>` と
`--work-directory <別の場所>` を必ず付けて、既定の
`build/prebuilt/esp32c5/<profile>/` を**上書きしない**ようにしてください。

### M5Stamp-P4（ESP32-P4）のステージ

```bash
python scripts/build_prebuilt_stages.py --chip esp32p4
```

`--chip esp32p4` の profile は **`minimal` だけ**です（StampP4 計画 段A。`wifi-connect`
＝AddOn C6 経由の hosted Wi-Fi は段B）。トゥールチェーンは C6 / C5 と同じ `esp-rv32`
2601 で、SDK は **`esp32p4_es-libs` 3.3.8**（rev v3 未満の silicon 用。上流の
`m5stack_stamp_p4` 行の既定 `chip_variant`。`scripts/arduino_sdk.py` の
`SDK_TOOL_NAMES` が chip 名から引く）。P4 の chip 層（dev 由来）は IDF ヘッダを読まず
ROM ld も要らないので、SDK からは bootloader と存在証明だけを使います。

P4 も `ports/m5stack_riscv/runtime` の**チップ分岐**です（`arch/riscv_gcc/esp32p4` /
`target/m5stamp_esp32p4_gcc` / `seam/seam_p4_*` / `cmake/prebuilt_stage_p4.cmake` /
`cmake/toolchain-riscv-esp32p4.cmake` / `app/phase3_p4`。出典と改変は
`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE_p4.md`）。C5 に無かった論点——**SMP
（2 コア）**、newlib syscall を target 層が持つ、chip_start.S が無い、seam の昇圧
define 名、xip ld の `INCLUDE` 断片——は `runtime/CMakeLists.txt` の chip 表の
**新しい列**で吸収してあり、C6 / C5 の列は書き換えていません（既定値 = 従来の挙動。
X-check 11/11 MATCH）。`prebuilt_stage_p4.cmake` は xip ld の `INCLUDE` 断片を stage の
写しへインラインします（GNU ld は INCLUDE をカレントと -L からしか探さず、利用者側の
リンクはどちらも持たない）。

**上流 core 3.3.8 の欠陥**: `m5stack_stamp_p4` は空のスケッチでも
`esp32-hal-spi.c:299` の `BOARD_SDMMC_POWER_CHANNEL` 未定義で落ちます（実測）。本板の
行は `build.extra_flags.esp32p4` に `-DBOARD_SDMMC_POWER_CHANNEL=4` を足して回避
（`install_platform.BOARD_BUILD_OVERRIDES`。値は FMP3 のリンクに入りません）。
core の版を上げるときに要再確認。

### ステージを建て直すときは tree hash を前後で採る

ステージを建て直す作業（対照ステージを作る、`--clean` で作り直す）では、
**「触っていないはずのツリーが本当に動いていないか」を、作業の後で
`diff -r` して確かめるのではなく、作業の前後で 1 行のハッシュとして
採ってください。** 事後の `diff` は「今は同じ」しか言えず、「作業中に
一度も変わらなかった」は言えません（C5 段4 の 4c がこの形で
NOT-EXERCISED になりました）。

```bash
treehash() { (cd "$1" && find . -type f -print0 | LC_ALL=C sort -z \
                | xargs -0 sha256sum | sha256sum | cut -d' ' -f1); }

treehash build/prebuilt/esp32c5            # 作業前
treehash ~/Arduino/hardware/toppers/esp32  # 作業前
# ... 対照ステージを別の --output-directory へ建て、別の sketchbook へ入れて使う ...
treehash build/prebuilt/esp32c5            # 作業後（同じ値であること）
treehash ~/Arduino/hardware/toppers/esp32  # 作業後（同じ値であること）
```

**`--clean` で建て直した既定のステージ自身は、この tree hash が変わって
当然です**（`objs/banner.o` がビルド時刻を持つため）。配布されるバイト列が
変わっていないことの判定は X-check（`xcheck_compare.py`、`banner.o` を
除外）であって、tree hash ではありません。tree hash は「触っていない
はずのツリー」に対して使う道具です。

## 2. platform ディレクトリを組み立てる

```bash
python scripts/install_platform.py --prebuilt-stage-root build/prebuilt
```

`boards.txt` / `platform.txt` / stage / リンクドライバ / partition テーブルを
sketchbook の `hardware/toppers/esp32` へ置きます。Arduino IDE を再起動すると
ボードが選べます。

`build/prebuilt/<チップ>/` が並んでいる親を渡すと、**そこにある全チップの
ボードが 1 つの platform に入ります**（CoreS3 と M5Stack Basic が同居する）。
チップ 1 つ分のディレクトリを渡せばそのボードだけになり、`--chip` で
親から一部だけ選ぶこともできます。

### M5NanoC6（`m5nanoc6_fmp3`）と M5StampC5（`m5stampc5_fmp3`）のイメージ形式

Xtensa 3 ボードは`paddrMode="runtime-mmu"`（ブート時に自分で MMU を
設定する形式）ですが、M5NanoC6 は RISC-V の `paddrMode="fixed-vma"`
（ELF が最初から仮想アドレスへリンクされ、bootloader が flash から
そのまま mmap する形式）です。manifest schema は 2 に上がっており
（`DRIVER_VERSION` は現在 4。段5 で 3 から上げた経緯は下記「M5NanoC6
イメージのビルドパス非依存化（S5-8）」）、`fmp3-link` は fixed-vma の
イメージを bootloader が受理する条件 C-1..C-8（`scripts/fmp3_link.py`
の `check_fixed_vma_image`）で検査してからリンクします。

| # | 検査内容 |
| --- | --- |
| C-1 | flash から map するセグメントがちょうど 2 本 |
| C-2 | セグメント #0 の先頭が `ESP_APP_DESC_MAGIC_WORD`（`0xABCD5432`） |
| C-3 | 各セグメントの `file_offset % page == vaddr % page` |
| C-4 | エントリポイントがイメージに含まれるセグメント内にある |
| C-5 | RAM セグメントが bootloader 自身の `iram_loader_seg` に重ならない |
| C-6 | 各 RAM セグメントのバイト列が ELF の `.data` と一致する |
| C-7 | 2 本の flash map セグメントの MMU ページ範囲が重ならない |
| C-8 | イメージが書込み先の app パーティションに収まる |

C-1..C-8 はすべて `fmp3-link` 実行時に検査され、満たさなければリンクは
失敗します（Xtensa の runtime-mmu 側にこの検査はありません）。

**M5StampC5 も同じ fixed-vma 形式です**（C5 計画 A5）。`FIXED_VMA_LAYOUTS` に
`esp32c5` の行（page 0x10000、drom `0x42000000-0x44000000`、iram
`0x40800000-0x40860000`、`loader_seg` `0x4084E5A0`）を足してあり、C5 では
C-1..C-8 に加えて **C-9**（app descriptor の `chip_id` が ESP32-C5 の
`0x0017` で、revision がイメージの宣言と矛盾しないこと）も検査します。
**bootloader の位置が C6 と違います**: M5StampC5 は **0x2000**（C6 は 0x0）で、
スケッチビルドの `flash_args` もそう出ます。asp3 の Direct Boot 像を焼いた
ことのある板は flash 0x0 に magic が残っていて ROM が 0x2000 の bootloader を
起動しないので、その場合は 0x0-0x1FFF を消してください
（`docs/c5-port.md` 段2）。

**M5StampP4 も fixed-vma 形式です**（StampP4 計画 P6）。`FIXED_VMA_LAYOUTS` の
`esp32p4` 行（page 0x10000、drom `0x40000000-0x44000000`＝D/I 共有窓、iram
`0x4ff00000-0x4ffc0000`、`loader_seg` `0x4ff2cbd0`、C-9 の chip_id `0x0012` /
rev 103）。C-1 / C-2（flash セグメントちょうど 2 本・app descriptor 先頭）は
P4 の bootloader（`bootloader_utility.c` の `SOC_MMU_DI_VADDR_SHARED` 枝）のために
書かれた形そのものです。**C-6 は P4 で一般化しました**: RAM セグメントの参照を
`.data` 単独から alloc PROGBITS セクション全体にしてあります（P4 の seam entry は
RAM の `.iram_text` にあり、esptool が `.data` と併合し得る。C6 / C5 は `.data` に
収まるので結果は従来どおり）。bootloader は C5 と同じ **0x2000**。

### この platform はライブラリを同梱しません

**同梱するのは `make_package_index.py`（配布パッケージ）だけです。** そのため
この platform でスケッチを建てるときは、ライブラリの供給元を自分で指定します。
リポジトリそのものがライブラリ（`library.properties` ＋ `src/`）なので、
そのまま渡せます。

```bash
arduino-cli compile --fqbn toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5 \
    --library . examples/StackChanBasic
```

**`--library` を付けないと、スケッチブックの `libraries/` にある古いコピーが
使われます。** そこに以前の版が残っていると、

```text
fatal error: ToppersFMP3_M5Unified.h: No such file or directory
```

のように**そのファイルが無いという形**で失敗します。例題の不備にも platform の
不備にも見えますが、原因はライブラリの供給元です。`verify_package.py` が同じ
例題を通すのは、あちらが配布パッケージを入れており、そこには最新のライブラリが
同梱されているからです。

> **スケッチブック側と Boards Manager 側は、両方向で邪魔をします。**
>
> スケッチブックに platform があるあいだ、`toppers:esp32` は Boards Manager から
> 管理できません（`install` / `uninstall` / `search` が「無い」ように振る舞い、
> `Platform 'toppers:esp32@x' not found` という何も指さないエラーになります）。
> 公開パッケージを試すときは、この platform を先に消します。
>
> 逆に、**Boards Manager のコピーがあると、そちらがビルドに使われます。**
> 組み立てたばかりの platform が黙って使われず、直したはずの箇所が反映されない
> という形で出ます。どちらが使われているかは次で確かめられます。
>
> ```sh
> arduino-cli board details -b toppers:esp32:m5cores3_fmp3
> ```

## 3. パッケージと index を作る

```bash
curl -fL -o package_toppers_index.published.json \
    https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
python scripts/make_package_index.py --version <ver> \
    --platform-dir <platform> --owner <owner> --repo <repo> \
    --driver <host>=<fmp3-link-host.zip> ... \
    --merge-into package_toppers_index.published.json --require-merge-target
```

`--merge-into` は公開済み index に載っている過去の版（platform と、リンク
ドライバの tool）をそのまま引き継ぎ、今回と同じ版だけを差し替えます。
**付けずに生成すると新しい index は今回の版だけになり、既存利用者の Boards
Manager から過去の版が消えます。** `--require-merge-target` は、取得に失敗して
ファイルが無いときに黙って新規 index を作らず止めるためのものです。初回
リリース以外では必ず両方付けてください。

`--owner` と `--repo` に既定値はありません。これらは Boards Manager が
アーカイブを取りに行く URL を組み立てるので、**誤った値は利用者の手元で初めて
失敗します。**

リンクドライバはホストごとに凍結したものが要ります
（`.github/workflows/build-link-driver.yml` が `v*` タグで生成します）。

## 4. 検証する

```bash
python scripts/verify_package.py --platform-dir <platform>
python scripts/check_release_artifacts.py --release-dir <出力>
python scripts/check_host_paths.py <platform または zip>
```

- `verify_package.py` … Boards Manager 経由で入れ直し、**既定で全ボードx
  対応する構成x例題を建てる**（下記「`verify_package.py` の板と構成」）
- `check_release_artifacts.py` … index の checksum、ホストの網羅、ドライバの版、
  各チップの stage・ツール依存（C6 なら `esp-rv32`/`esp32c6-libs`、
  C5 なら `esp-rv32`/`esp32c5-libs`）
- `check_host_paths.py` … 配布物にビルド機の絶対パスが混入していないか

### `verify_package.py` の板と構成

対象は `scripts/verify_package.py` の `BOARD_PROFILES`（板 -> 選べる構成の集合）
と `PROFILES`（構成 -> 例題）から機械的に決まります。固定の本数をこの文書に
書き写さないでください（ドリフトします）。導出は次のコマンドで確認できます。

```bash
python3 scripts/verify_package.py --list-builds   # 実行せず、計画だけを表示
```

`--list-builds` はパッケージも Boards Manager への出入れもせず、板x構成x例題の
表と合計だけを表示します（2026-09-17 実測: CoreS3 17・M5StickS3 17・
M5AtomS3 Lite 11・M5Core 22・M5AtomLite 16・M5NanoC6 11・M5StampC5 11・
M5StampP4 3 = 計 108。
導出の正本はコマンドそのもので、この数字は実測の一例です。同日午前は
M5AtomLite の 16 本と例題 AtomLiteRgb（各 wificonnect 板に 1 本）が無く 83、
2026-09-16 時点は M5AtomS3Lite の 10 本と例題 AtomS3LiteRgb も無く 68 でした）。

- **既定は 8 板すべて**です。`--boards`/`--profiles` で絞り込めます。
- **`verify_package.py` はローカルの package index を作って Boards Manager の
  設定を一時的に書き換えます。** これは開発機の `~/.arduino15/` にキャッシュ
  されている**公開 index（`package_toppers_index.json`）を同じファイル名で
  上書きします**（`cache clean` で `staging/packages/` も空にします）。
  実行後、そのまま Boards Manager で通常の導入作業をすると `0.4.2 @
  127.0.0.1` のようなローカル URL しか見えません。**検証が終わったら
  `arduino-cli core update-index` を実行して公開 index を復旧してください。**
- **Windows／Apple Silicon macOS 向けのリンクドライバ zip は、Linux 機では
  凍結できません。** `verify_package.py` はこの 2 つを stub zip として扱い、
  実行できるのはビルド機自身のホスト分だけです。3 ホストぶんの本物のドライバは
  CI（`.github/workflows/build-link-driver.yml`）が `v*` タグで生成します。

## 5. リリースする — 実際に踏んだ落とし穴

いずれも「生成も検証も通るのに、失敗するのは利用者の手元」という形をとります。

- **出荷する platform アーカイブは、`verify-package` の `platform`
  アーティファクトから作ってください。** 手元でステージを建て直して組むと
  **検証していないバイト列**を配ることになります。3 ホストの一致は
  「`package` job が上げた同一のアーカイブ」に対する確認です。
- **pre-release で公開してはいけません。**
  `releases/latest/download/…` は pre-release を除外するので、
  README が案内している URL が**全員に 404 を返します。**
- **イメージの SHA-256 をリリースノートに載せないでください。**
  カーネルのバナー文字列が `third_party/fmp3_core/syssvc/banner.c` の
  `__DATE__` / `__TIME__` を含むため、**ステージを建て直すと必ず変わります**
  （サイズは固定長なので変わりません）。同じソース・同じタグの別 run で
  7 件とも別の値になった実測があります。配布物の同定は index の checksum で
  行ってください（Boards Manager が強制します）。
- **M5NanoC6 イメージのビルドパス非依存化（S5-8）。** 段5 の 4 板 verify で、
  同じソース・同じステージから建てた M5NanoC6 の 7 成果物が、ビルドパスの
  綴りが違うだけで 64-65 B（esptool が書く app descriptor の ELF sha256・32 B
  とイメージ末尾ハッシュ・32 B の 2 本。実測では 64 バイトの差になることも
  65 バイトの差になることもあります--2 本のハッシュのどちらかで 1 バイトが
  たまたま一致する場合があるため）だけ異なることが分かりました。原因は
  arduino-cli がコンパイルするスケッチ／コアのオブジェクトの `.debug_str` にビルドパスが
  残ること（stage 側は `-ffile-prefix-map` で対策済みだが、スケッチ側の
  コンパイルはその外）です。**stage-5 の fix wave（driver 4、commit `aa62fde`）**で
  `fmp3-link` の fixed-vma 経路に、`elf2image` の前に ELF のコピーを
  `--strip-debug` する処理を追加し（読み込む内容は不変）、`app_elf_sha256`
  が DWARF に依存しないようにしました。実測（positive control）: 同じスケッチを
  2 つの異なる build path で建てて `.bin` の sha256 が一致することを確認済みです
  （strip を外すと 64-65 バイトが再び異なります）。Xtensa（schema 1／runtime-mmu
  経路）はこの変更の対象外（driver 3/4 の再リンクで app bin が byte 同一なことを
  確認済み）。**ただしこの実測は 1 ホスト内の build path 差のみで、3 ホストの
  バイト一致は Xtensa 側についての記述です。** M5NanoC6 の成果物についてホスト間
  バイト一致まで主張できるかどうかは、まだ確認していません。
- **GitHub のアーティファクトをブラウザから取得すると二重 zip になります。**
  外側は GitHub の包装で、index に載せるのは内側です。外側のまま登録すると
  Boards Manager が展開に失敗します。
- **公開後の確認は、浅いパスの隔離環境で行ってください。** Windows で
  `directories.data` を深い場所に置くと、M5Stack core の SDK ヘッダへの
  パスが 260 文字を超え、GCC が `deprecated_definitions.h: No such file`
  で止まります（長いパスの許可が既定で無効なため）。ファイルは展開されて
  いて、パッケージも正しいのに、全ボードのビルドが数秒で失敗する形で
  現れます。0.4.0 の確認で一度これを配布物の欠陥と疑いました。
  `C:\Users\<name>\AppData\Local\Temp\<短い名前>` のような場所に隔離環境を
  作れば、公開 index からの導入と全ボードのビルドがそのまま通ります。
- **リリースを作ったら、ファイルを添付したか確かめてください。** 添付を忘れても
  リリースは正常に見えます——タグも本文も正しく、`draft` でも `pre-release` でも
  なく、**手元の検査はすべて通っています。** それでも `latest` が
  アセットの無いリリースを指した瞬間に、README が案内している URL は
  **全員に 404 を返します。** 0.4.1 で実際にこれをやりました。
  影響は新規の利用者だけではありません。**既存の利用者も index を取得できなく
  なる**ので、0.3.0 や 0.4.0 を使っている人まで更新できなくなります。
  pre-release にしたときと**同じ URL が同じ形で壊れます。**

### 公開したら必ずこれを実行する

30 秒で終わり、上の 2 つ（pre-release・添付漏れ）をまとめて捕まえます。

```bash
curl -fL -o published.json \
    https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
cmp published.json <出力>/package_toppers_index.json
```

**`curl` が 404 で落ちたら、利用者から見て公開は失敗しています。** 成功して
バイト一致すれば、`latest` が今回のリリースを指し、index が検査を通したものと
同一だと確認できたことになります。

続けて、index が名指しするアーカイブが実際に取得できるかも見ておくと確実です。
**過去の版も含めて**確認してください。`--merge-into` で引き継いだ古い版は、その
リリースのアセットが消えていると Boards Manager に「入れられない版」として
並びます。

```bash
python - <<'PY'
import json, hashlib, urllib.request
idx = json.load(open("published.json", encoding="utf-8"))
pkg = idx["packages"][0]
entries = [(p["version"], p["url"], p["checksum"]) for p in pkg["platforms"]]
entries += [(t["version"], s["url"], s["checksum"])
            for t in pkg["tools"] for s in t["systems"]]
for version, url, checksum in entries:
    with urllib.request.urlopen(url, timeout=60) as response:
        digest = hashlib.sha256(response.read()).hexdigest()
    want = checksum.split(":", 1)[1].lower()
    print(f"{version:<8} {'OK ' if digest == want else 'MISMATCH'} {url}")
PY
```

> プロキシの内側では `HTTP_PROXY` / `HTTPS_PROXY` を設定してください。
> `urllib` はこれを見ます（`arduino-cli` は見ません）。同じ理由で、
> `check_release_artifacts.py` が引き継いだ版の URL を叩く検査も、
> プロキシ未設定だと接続タイムアウトで全件失敗します。

## 変更するときに守ること

- **cfg の `#ifdef` で構成を切り替えない。** cfg の pass1 はプリプロセッサを
  走らせず、`#ifdef` の中の `CRE_TSK` もそのまま静的 API として拾います。
  切り替えは「どの `.cfg` を渡すか」で行います。
- **リンクが通ることは動くことの証明になりません。** weak スタブは実装が無い
  構成で黙って失敗値を返します。
- **`requiredArduinoObjects` は「必ずリンクする物」の一覧であって、「リンクして
  よい物」の一覧ではありません。** ここに挙げたものと `build/sketch` の全
  オブジェクトは常にリンクされ、`build/libraries` の残りは `libarduino.a` に
  まとめて、参照されたときだけ引かれます。構成ごとに存在しないシンボルを呼ぶ
  オブジェクト（`ToppersFMP3_WiFi.cpp.o` など）を無条件にリンクしないための
  分け方です。**この一覧に足りないものがあっても、リンクエラーになるのは
  そのシンボルを実際に呼ぶスケッチだけ**なので、例題で気づけるとは限りません。
- **ランタイムを要求する例題には、選択中の構成を見るガードを付けてください。**
  `install_platform.py` が各メニュー項目に
  `-DTOPPERS_FMP3_RUNTIME_<構成>`（`BT_CLASSIC` / `WIFI_CONNECT` /
  `M5_UNIFIED` / `MINIMAL` / `ALL_IN_ONE`）と
  `-DTOPPERS_FMP3_RUNTIME_SELECTED=1` を渡します。**これが無いと、構成の
  選び間違いはリンカまで到達して、メニューではなく未定義シンボルの一覧として
  出ます。** `ToppersFMP3_BT.cpp` や `ToppersFMP3_WiFi.cpp` はどの構成でも
  コンパイルが通り、その先のシンボルが 1 つのステージにしか無いためです。
  `SELECTED` を条件に入れるのは、この define を持たない古い platform に対して
  ガードが誤って発火しないようにするためです。例題と構成の対応は
  `scripts/verify_package.py` の `PROFILES` が正本です。
- **特定の板にしか無い API を呼ぶ共有例題には、板ガードを付けてください。**
  `examples/` は 4 板共有で、`verify_package.py` は `PROFILES` の例題を板ごとに
  建てます。M5NanoC6 にしか無い `rgbLedWrite` を呼ぶ `NanoC6Gpio` は
  `#if defined(ARDUINO_M5STACK_NANO_C6)`（arduino-cli が板の `build.board` から
  付ける define）で本体を囲み、他の板では `setup()` が「この例題は M5NanoC6 向け」の
  1 行を `target_fput_log` で出して何もしません（`loop()` は空）。**ガードがある
  からこそ Xtensa 3 板でリンクが通る**ことは負対照で確かめてあります（ガードを
  `#if 1` にして CoreS3 の wificonnect を建てると `pinMode` / `digitalWrite` /
  `digitalRead` / `rgbLedWrite` の未定義参照で rc=1。段6 Task 1）。新しい板専用 API を
  足すときは、同じ形で例題を守り、負対照を 1 回取ってください。
- **多重定義もリンクでは捕まりません。** リンクは常に
  `-Wl,--allow-multiple-definition` を付けるので、重複があっても通り、
  どちらが生き残るかはオブジェクト名の順序で決まります。
  `scripts/audit_duplicate_symbols.py` がステージ生成の最後に検査します。
- **リンク順序は ordinal**（バイト単位・大文字小文字を区別）です。ロケール依存や
  大文字小文字を無視するソートでは別のイメージになります。
- **板や構成を追加・変更するときは、表駆動の各テーブルを揃えてください**
  （段5 で「3 構成 x 例題」「両ボード」という書き方から、板ごとに選べる
  構成が違う 4 板の表駆動へ変わりました）。触る箇所は次の名前で引けます。
  - `scripts/verify_package.py` の `BOARD_PROFILES`（板 -> 選べる構成の集合）
    と `PROFILES`（構成 -> 例題）。本数の導出はこの 2 つの直積です。
  - `scripts/build_prebuilt_stages.py` の chip -> profile の対応表（`CHIPS`）。
  - `ports/<xtensa|riscv>/runtime/CMakeLists.txt` の構成分岐。
  - `scripts/install_platform.py` の `BOARDS`／メニュー定義と
    `UPLOAD_SIZE_OVERRIDES`（size 表示の分母を上書きする板だけの表）。
  - `packaging/release-allowlist.json` の `releaseArtifacts.platformArchive`
    配下、`prebuiltStages`（チップ -> 必須 profile）と
    `chipToolDependencies`（チップ -> 必須ツール）の 2 つの表。
  - `.github/workflows/verify-package.yml` の chip ループ・板名検査・
    stage 存在検査の spec 文字列。

  **「チップを足すのは表に行を足すだけ」が成り立つのは、この
  `scripts/` と CI の表についてだけです**（段5 判断 S5-7）。
  `ports/*/runtime` 側は表ではなくチップ分岐を持つので、実際には
  arch / target / config / seam / toolchain / `prebuilt_stage_<chip>.cmake` と、
  ピン番号や割込み配線がチップ決め打ちの Arduino 層
  （`arduino_gpio_*.c` / `arduino_interrupt_*.c`）を足す作業が要ります
  （C5 = 段1 の実績）。C3 / H2 を足すときも同じ量の作業になります。

  **このうち機械的に一致を強制されている（ドリフト検査がある）のは次だけです。**
  `scripts/test_check_release_artifacts.py:352-380` が、`packaging/
  release-allowlist.json` の `prebuiltStages`／`chipToolDependencies` と、
  `make_package_index.CHIP_TOOL_DEPENDENCIES`、`install_platform.
  EXPECTED_PROFILES`＋`CHIP_ONLY_ENTRIES`、`xcheck_compare.profiles_for()`
  （`build_prebuilt_stages.CHIPS` から導出）の 4 つが同じ内容であることを
  比較します。**それ以外**--`verify_package.py` の `BOARD_PROFILES`／
  `PROFILES`、CI yml の spec 文字列と板名リスト、`ports/*/runtime/
  CMakeLists.txt` の構成分岐、`install_platform.py` の `BOARDS`／
  メニュー定義／`UPLOAD_SIZE_OVERRIDES`--は**どのテストにも縫い付けられて
  いません**。ずれても `test_check_release_artifacts.py` は落ちず、実際に
  `verify_package.py` を走らせるか CI を回さない限り気づけません
  （レビューで `BOARD_PROFILES` に架空の `"m5"` を M5NanoC6 行へ足しても
  テストは green のままであることを実演済み）。

### どこに何を置くか

- **`src/` 配下に FMP3 の cfg や `kernel.h` に依存するソースを置かないでください。**
  Arduino builder は同梱ライブラリの `src/` を**再帰的にコンパイルします**。
  スケッチのビルドには FMP3 のヘッダも cfg ツールも無いので、置いた時点で
  利用者側のビルドが壊れます。FMP3 側のコードは `ports/` か `fmp_app/` へ。
  `src/` に置けるのは `Arduino.h` だけに依存するコードです。
  境界は `src/bridge/ArduinoSketchBridge.cpp` で、FMP3 API を `extern "C"`
  宣言で参照し、`kernel.h` を include しません。
- **Wi-Fi のコールバックテーブルを別ファイルへ複製しないでください。**
  `esp_wifi_init()`／`esp_wifi_start()`／auth backend の選択と、WPA
  コールバックテーブル（offset `0x1b4`・27 エントリ）は
  `runtime/wifi/adapter/toppers_wifi_core.c` だけが持ちます。かつて 3 箇所に
  あり、**private な ABI の記述が運で一致していました。** 複製すると、
  片方だけ直したときに壊れ方が Wi-Fi の失敗として現れます。

### 依存の固定

- **M5Stack Arduino core は 3.3.8 固定です。** 同梱 ESP-IDF v5.5.4 の
  **private な Wi-Fi ABI**、prebuilt archive、include 配置に依存しています。
  version を上げるには、この 3 つと board recipe を全面的に再検証する必要が
  あります。「ビルドが通った」では足りません。
- **ESP-IDF を複製しないでください。** Wi-Fi blob、PHY、lwIP、ツールチェーン、
  ヘッダはすべて M5Stack core から検出して使います。ツリーへ持ち込むと、
  利用者が入れた core との二重管理になります。

  > **例外（M5NanoC6 と M5StampC5、D8 / C5 計画 A8、段5 判断 S5-2 で維持）。**
  > `ports/m5stack_riscv/runtime/wifi/` は、この原則から逸脱する ESP-IDF
  > 原本ファイルを C6 について計 9 本、C5 について 3 本 vendoring しています。
  >
  > - **esp-idf 原本 6 本**（Apache-2.0）: `periph_ctrl.c` `modem_clock.c`
  >   `modem_clock_hal.c` `efuse_hal.c` `efuse_hal_esp32c6.c`
  >   `phy_init_data.c`。**理由**: M5Stack core の `esp32c6-libs` が持つ
  >   `.a`（`libesp_hw_support.a`／`libhal.a`）のこれらに対応するメンバは、
  >   `vPortEnterCritical`／`vPortExitCritical`／`xPortInIsrContext`
  >   （FreeRTOS のクリティカルセクション API）を未解決参照として要求します
  >   が、FMP3 はこれらを提供しません。そのため `.a` のメンバをそのまま
  >   リンクする経路は使えず、開発リポジトリはこの 6 本を esp-idf の
  >   ソースからコンパイルし、FreeRTOS スタブ（`esp/bt/stub/include`）に
  >   対してリンクしています（出典: 開発リポジトリ
  >   `.steering/20260915-c6-arduino-plan/INVESTIGATION.md` 2-3 節）。
  > - **lwIP contrib ヘッダ 3 本**（BSD-3-Clause）: `ping.h` `tcpecho_raw.h`
  >   `udpecho_raw.h`。`netif_esp32s3.c` が include するが、M5Stack core の
  >   SDK は lwIP contrib apps のヘッダを含まない（実体は `liblwip.a` の
  >   中にある）ため、ヘッダだけを補っている。C5 と共有する。
  > - **C5 の同名原本 3 本**（Apache-2.0、C5 計画 A8、段3 で追加）:
  >   `phy_init_data_esp32c5.c` `modem_clock_hal_esp32c5.c`
  >   `efuse_hal_esp32c5.c`。理由は上の 6 本とまったく同じで、チップ固有の
  >   3 本だけが C5 用に増えた（チップ非依存の `periph_ctrl.c` /
  >   `modem_clock.c` / `efuse_hal.c` は C6 と共有する）。出自は
  >   `packaging/release-allowlist.json` の `portBaseCommitC5`（`1d96bcba`）で、
  >   1 本ごとの記録は
  >   [`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`](ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md)
  >   の「ESP32-C5」節（shim の分は
  >   `runtime/wifi/shim/IMPORT_PROVENANCE_c5.md`）。
  >
  > **出自**は開発リポジトリ（`https://github.com/exshonda/
  > fmp3_esp_idf_dev.git`）で、内容は無改変・原ライセンスヘッダ保持。
  > 1 本ごとの正確な出自と改変境界は
  > [`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`](ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md)
  > （このファイルでは再掲しない）。**再評価の条件**は、(1) 「core の
  > `.a` メンバ + `vPort*` シム」経路を実際に検証する、または (2) 固定中の
  > M5Stack core バージョンが動く、のどちらかが起きたときです。いずれも
  > 段5 時点ではまだ起きていません。それまではこの例外のまま維持します
  > （段5 判断 S5-2）。
- **M5Unified 構成は大量の `-Wl,--wrap=` で mangled C++ シンボルを差し替えて
  います**（`ports/m5stack_xtensa/runtime/CMakeLists.txt`）。M5Unified／M5GFX の
  version を上げると mangled 名が変わり得るので、`--wrap` が空振りします。
  **空振りしてもリンクは通ります。**

### 壊れ方が静かなもの

- **Wi-Fi の OPEN／WPA 初期化分離を壊さないでください。**
  `-Wl,--wrap=esp_supplicant_init` で、`WiFi.begin()` が認証方式を知るまで
  supplicant の初期化を遅延させています。常時初期化すると**オープン AP が
  `AUTH_EXPIRE` で失敗**します。ここを触ったら Open／WPA2-PSK／WPA3-SAE の
  3 経路を実機で回帰してください。関連：`runtime/CMakeLists.txt`、
  `runtime/wifi/adapter/toppers_wifi_connect.c`、
  `runtime/wifi/adapter/toppers_wifi_core.c`、`runtime/wifi/prebuilt/wpa2/`。
- **未実装 API を無条件スタブで成功扱いにしないでください。** Arduino／FreeRTOS
  API は完全互換ではなく、各構成と例題で実際に使った範囲だけが対応済みです。
  `runtime/wifi/adapter/toppers_wifi_optional_stubs.c` の weak スタブは、
  実装が無い構成で**失敗値を返すだけで、リンクは通ります**。追加するときは
  未定義シンボル一覧を正としてください。
- **時間の単位に注意してください。** FreeRTOS API は tick、FMP3 の `dly_tsk` の
  RELTIM は**このポートではマイクロ秒**です。変換は一か所へ集約し、
  `configTICK_RATE_HZ` を暗黙に使わないでください。

### 配布物に入れる／入れない

- **`runtime/wifi/prebuilt/wpa2/` の `.a` は Git 管理対象です**
  （`esp32/` と `esp32s3/` に `libsupplicant.a`／`libmbedcrypto.a`）。
  **`.gitignore` に一般的な `*.a` 除外を追加しないでください。** 更新するときは
  由来 commit、build recipe、SHA-256、ライセンスを同時に更新します。
- **配布アーカイブにビルド出力を入れないでください。** `arduino-cli` は相対パスで
  スケッチをコンパイルすると `<sketch>/build` にも出力するため、同梱ライブラリの
  `examples/` をそのままコピーすると `.ino.bin` が配布物へ入ります。
  `make_package_index.py` は `build` を除外し、バイナリ成果物が残っていたら
  生成を止めます。
- **`gen_esp32part.exe` は同梱しません。** partition 生成はリンクドライバが行うので、
  `{tools.gen_esp32part.cmd}` を参照する recipe が残っていたらインストールを
  止めます。変換結果は `scripts/Test-PartitionTable.ps1` で
  `gen_esp32part.exe` とのバイト一致を確認します。
- **資格情報を残さないでください。** Wi-Fi の SSID／パスワードを commit せず、
  実機ログを文書化するときは SSID、BSSID、割当 IP を書かないでください。
  `examples/WiFiConnect/WiFiConnect.ino` は公開前に空であることを確認します。
- **`scripts/capture_lx6_uart.sh` も同じく開発者向けで、配布しません。**
  M5Stack ATOM Lite（ESP32-PICO-D4）用で、S3 台本の写しです。差分は 4 点:
  (1) **この板に USB Serial/JTAG は無い**——外付けブリッジ（FTDI 0403:6001）経由
  なので、`DUT_PORT` を MAC から導けず（既定はブリッジの by-id 名）、板を同定するのは
  書込み前の esptool の MAC ゲートだけです。JTAG 生存 probe は openocd ごと削除しました。
  (2) bootloader は **0x1000**（`m5atomlite_fmp3.build.bootloader_addr` から読み、
  それ以外はゲート失敗）。読み戻しも同じ番地を見ます。(3) esptool は `--chip esp32`。
  (4) **`BAUD` の既定は 115200**——この板とケーブルでは 921600 も 460800 も
  `The chip stopped responding` で落ちることを実測しました（`docs/atomlite-port.md` 6 節）。
  伏字化・SSID マスク・selftest は S3 台本と同じです。
- **`scripts/capture_s3_usj.sh` も同じく開発者向けで、配布しません。**
  M5AtomS3 Lite（ESP32-S3）用で、C5 台本の写しです。差分は 4 点:
  (1) esptool の MAC 行が `MAC:` 1 本（C5 は `BASE MAC:` と EUI-64 の 2 本）、
  (2) bootloader は **0x0**（`m5atoms3lite_fmp3.build.bootloader_addr` から読み、
  それ以外はゲート失敗）、(3) **Direct Boot 消去は無い**——このチップの
  bootloader は 0x0 にあるので、代わりに書込み後 `read-flash 0x0 0x1000` が
  焼いた bootloader 像の先頭 4096 B と一致することを確かめます、
  (4) APM 計数と PCR / clk_result 読みは C5 専用なので落とし、JTAG は
  `board/esp32s3-builtin.cfg`・USJ レジスタは `0x60038004`/`0x60038008`。
  **この板の console は log task の行の先頭 1-2 文字を落とすことがある**ので
  （`docs/atoms3lite-port.md` F-1）、計数パターンはタグの末尾に合わせてあります
  （`Scan] found`、`duino] loop heartbeat`）。**スキャンが近隣 AP の実 SSID を
  印字する**ため（同 F-2）、SSID 列をマスクし、マスク漏れがあれば採取ファイルを
  隔離します。FORBIDDEN に M5NanoC6 と M5Stamp-C5 を入れてあります。
- **`scripts/capture_p4_usj.sh` も同じく開発者向けで、配布しません。**
  M5Stamp-P4（ESP32-P4）用。C5 台本の写しではなく、**書込みの可否を決めるゲート**
  （`DUT_MAC` 必須・FORBIDDEN・boards.txt の bootloader 番地 0x2000・書込み後の
  bootloader 領域 readback 一致）と `stty`+`cat` の採取、マーカー計数
  （`heartbeat` / `[P4-CORE2] alive` / `Processor 2 start.`）、selftest だけの小さな
  台本です。板が別 PC にあるので既定の DUT はありません（`docs/p4-port.md` 3 節・6 節）。
- **`scripts/capture_c5_usj.sh` も同じく開発者向けで、配布しません。**
  C6 台本の写しで、chip が `ESP32-C5`、bootloader が **0x2000**、毎回
  flash `0x0-0x1FFF` を消してから焼きます（asp3 の Direct Boot magic 対策。
  `docs/c5-port.md` 段2）。FORBIDDEN リストで M5NanoC6 を掴まないようにして
  あります。伏字化の仕組みは C6 台本と同じです。
- **`scripts/capture_c6_usj.sh` は開発者向けの採取台本で、配布しません。**
  M5NanoC6 の実機 USB Serial/JTAG からログを採り、`C6_REDACT_ONLY` モードで
  SSID／パスワード／割当 IP などを機械的に伏字化するためのものです
  （`git ls-files` で追跡ファイルを拒否し、`LOG_DIR` の外のファイルも
  `C6_REDACT_ANYWHERE=1` を明示しない限り拒否する guard 付き）。
  `packaging/release-allowlist.json` にはこの台本のエントリを置いていません。
  段6 で足した **JTAG 生存 probe** の要点（詳細は台本のヘッダ）:
  - `EXTRA_MARKERS='a|b|...'` は固定文字列の追加集計で、`extra:` 行に出ます
    （`grep -F`。ERE ではありません）。
  - probe は monitor を殺した後に `openocd`（core 同梱の
    `openocd-esp32/v0.12.0-esp32-20251215`、`OPENOCD=` で差替え、PATH は探さない）を
    `board/esp32c6-builtin.cfg` で起動し、halt -> `reg pc` / USJ `EP1_CONF` / `INT_RAW` /
    `toppers_arduino_loop_calls` -> resume -> 2 秒 -> halt -> 再読み -> resume の順で
    読みます。reset も flash もしません。走る条件は `COLD=1` かつ heartbeat=0
    （無音 cold、`C6_JTAG_ON_SILENT=1` 既定）か、`C6_JTAG_FORCE=1`（positive
    control）。`DRYRUN=1` では走りません。
  - **板の pin は必須**: `adapter serial <DUT の serial>` を最初の `-c` に置き、出力に
    `esp_usb_jtag: serial (<同じ値>)` が無ければ `verdict: refused` として値を一切
    信用しません（開発側で別の板を掴んだ事故が根拠）。serial 行が 1 行も無く
    openocd が非 0 で終わった場合（device 無し）は `not-run (openocd rc=N, no serial
    line)` で、これは別物です。
  - `ELF=`（既定 `$SKETCH_BUILD/fmp3-prebuilt-link/link/fmp_xip.elf`）から `nm` で
    `toppers_arduino_loop_calls` の番地を取ります。`COLD=1` の run は普通
    `SKETCH_BUILD` が無いので、`loop_calls` が欲しければ `ELF=` を渡してください。
  - **verdict は `loop_calls` の 2 回読みだけで決めます**（alive / not-advancing /
    not-run）。`EP1_CONF` の `data_free`（bit1）は健全性の基準では**なく**、生の値を
    無音 run との比較用に記録するだけです（段6 ruling R7: probe は monitor を殺した
    後に走るので host 側の reader が無く、健全な板でも bit1 は 0 と読めます。実測:
    alive delta=1755 で data_free=0）。
  - **halt 残留の危険**: 30 秒の timeout（rc=124）でも、halt と resume の間で
    コマンドが失敗して `-c` の連鎖が止まった場合でも、chip は次の reset か電源断まで
    halt のままです。直後に reset 無しで採取すると、その無音は調べたい無音では
    ありません。
- **ライセンスはリポジトリ単一ではありません。** 各ファイルのヘッダと
  `THIRD_PARTY_NOTICES.md` が正で、`LICENSE` はこのリポジトリ向けに書かれた
  部分に適用されます。取り込んだファイルはヘッダを保持してください。

## X-check -- 共有スクリプトが既存板の配布物を変えていないことの機械判定

M5NanoC6 の追加は `build_prebuilt_stages.py` / `install_platform.py` /
`fmp3_link.py` など、Xtensa 3 板も使う共有スクリプトを触ります。「Xtensa は
変えていない」をバイト列で示すのが X-check です。M5StampC5 の追加はさらに
`ports/m5stack_riscv/runtime` を C6 と共有するので、**C5 の段では baseline に
C6 の 2 stage も入れて 9 stage で回します**（C5 計画 A4）。

```bash
# C5 の段の起点で
python scripts/xcheck_baseline.py --force --clean --chips esp32s3 esp32 esp32c6
```

```bash
# 作業前（作業ツリーが clean な段の起点で 1 回）
python scripts/xcheck_baseline.py            # 既定は Xtensa 7 stage を退避
# 作業後、対象 chip を建て直してから
python scripts/xcheck_compare.py             # 7/7 MATCH で rc=0
python scripts/xcheck_compare.py --strict    # banner.o（__DATE__/__TIME__）も比べる
python scripts/test_xcheck.py                # 判定器自身の自己テスト
```

- **既定の比較対象は Xtensa（`esp32s3`／`esp32`）の 7 stage のままです。**
  `xcheck_baseline.py --chips esp32c6` を既定の baseline ディレクトリに対して
  実行すると、既存の baseline がある限り **`--force` 無しでは拒否されます**
  （rc=1、「先に取り直す理由がない限り上書きしない」設計）。**`--force` を
  付けると、指定した `--chips` に関わらず既存の baseline セット全体
  （Xtensa の分も含む）を消して置き換えます。** C6 だけの baseline が欲しい
  ときは、既定のディレクトリへ `--force` するのではなく、
  `--baseline-directory <別の場所>` で完全に別の置き場所を指定してください。
  C6 の golden を比較対象にするかどうかは今後の判断です（C6 段5 時点では未定。
  C5 統合では上記のとおり **C6 を baseline に入れて 9 stage** で回しました）。
- **C5 の stage（`build/prebuilt/esp32c5/`）は baseline に入れていません。**
  `xcheck_compare.py` は `ignored (not in baseline): esp32c5` の 1 行で
  読み飛ばします（比較対象にも失敗にもしません）。C5 統合の段5 では、
  C5 自身が変更対象だったのでこのままにしました（C5 計画 段5 の判断）。
  C5 を「これ以上変えない」段に入ったら、`--chips esp32s3 esp32 esp32c6 esp32c5`
  で baseline を取り直せば 11 stage になります（`CHIPS` には既に
  `esp32c5` が入っています）。
- 比較するのは `link-manifest.json`（バイト一致。時刻系キーだけの差は注記つき
  MATCH）、`objects.rsp`、`objs/*.o` の sha256（`banner.o` は `--strict`
  無しでは除外）、`lib/*.a`、その他のファイル。stage が片側にしか無ければ
  差分です。
- **baseline は段の起点・作業ツリーが clean な状態で採り、編集後に採り直さない
  でください。** 編集後の生成物どうしを比べても、編集が Xtensa の配布物を
  変えたかどうかは分かりません。

## リリース経路の検証（`scripts/verify_package.py`）

パッケージを組み、Boards Manager 経由で入れ直し、対応するボードx構成を
建て直す（既定は 8 板すべて、上記「`verify_package.py` の板と構成」参照）。

```sh
python3 -m venv ~/.venvs/toppers-verify
~/.venvs/toppers-verify/bin/pip install pyinstaller
~/.venvs/toppers-verify/bin/python scripts/verify_package.py \
    --platform-dir <プラットフォーム> --arduino-cli ~/bin/arduino-cli \
    --skip-core --skip-libraries --config-file <検証用の設定>
```

PyInstaller はリンクドライバの凍結に要る。多くのディストリの Python は
外部管理（PEP 668）なので venv で入れること。

**`--platform-dir` はスケッチブックの外を指すこと。** `<sketchbook>/hardware/
toppers/esp32` に置いたままだと、arduino-cli はそれをスケッチブック
プラットフォームとして扱い、Boards Manager 側の `toppers:esp32` を
「見つからない」と言う（install も uninstall も効かない）。

**開発機のスケッチブックに `hardware/toppers/esp32` が入っている状態
（`install_platform.py` を一度でも走らせた機械）では、それだけでは足りません。**
スケッチブックの platform は同じ ID `toppers:esp32` を持ち、Boards Manager 側の
platform を覆い隠すからです。C6 段5 はこれを `~/Arduino/hardware/toppers` を
一時的に `mv` してどける形で回避しましたが、**`--config-file` に空の
`directories.user` を渡すほうが、利用者のスケッチブックを一切動かさずに済みます**
（C5 段5 で実施）。

```yaml
# <検証用の設定>: directories.user だけを別の空ディレクトリへ向ける。
# data は共有したままにする（ツールチェーンと SDK を再取得しないため）。
directories:
  user: /path/to/scratch/verify-sketchbook
  data: /home/<user>/.arduino15
```

```bash
mkdir -p /path/to/scratch/verify-sketchbook
ln -s ~/Arduino/libraries /path/to/scratch/verify-sketchbook/libraries  # M5GFX / M5Unified
arduino-cli --config-file <検証用の設定> core list    # toppers:esp32 が出ないことを確認
```

`libraries` を symlink するのは `--skip-libraries` で済ませるためで、
これをしないと M5Unified 構成のビルドが落ちる。**`directories.data` は
共有したままにする**--分けると `esp-rv32`（約 2 GB）と各 SDK を
取り直すことになる。共有する代償として、検証が
`~/.arduino15/package_toppers_index.json` をローカル index で上書きし
`staging/packages/` を空にするので、**検証後に
`arduino-cli core update-index` を 1 回実行して公開 index を復旧すること**
（上記「`verify_package.py` の板と構成」の注意と同じ）。

同じ理由で、`~/.arduino15/packages/` に `toppers` の Boards Manager 版が
残っていると、その `installed.json` を arduino-cli が読み続けて古い index の
URL を掴む（実測: 消したはずのポートへ HEAD を投げ続けた）。検証中は
`~/.arduino15/packages/` の**外**へ出しておくこと。

## Windows 側テスト

`scripts/Test-*.ps1` は Windows でしか走らず、Linux/macOS の CI も
`verify_package.py` も一切呼ばない。手順・一覧・**既知の対象外**（15 本すべて
CoreS3 前提で、M5Core ボードと `bt-classic` は未検証）は
[`docs/windows-tests/README.md`](docs/windows-tests/README.md)。
実施記録は `docs/windows-tests/runs/` に置く。

