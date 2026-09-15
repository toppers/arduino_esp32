# ESP32-C5（M5Stamp-C5）の統合 -- 判断の記録と段ごとの到達点

- 作成: 2026-09-16（段0）。起点は本リポジトリ `90872e9`（C6 段6 完了、Open 0.4.2 +
  C6）と開発リポジトリ `fmp3_esp_idf_dev` の `1d96bcba`（C5 計画 3 完了: seam hello、
  機能テスト、Wi-Fi、preset `seam-c5-min` / `seam-c5-wifi`）。
- 位置づけ: **C5 統合に関する判断（A0-A12）と、段ごとに何がどこまで動いたかの正本。**
  調査の事実（file:line つき）と計画そのものは開発リポジトリ側の
  `.steering/20260915-c5-plan/INVESTIGATION.md`（E 節が arduino 側）と
  `.steering/20260916-c5-arduino-plan/{PLAN,PLAN-stage<N>-impl}.md` にあり、
  ここには判断と結果だけを置く（両方に書くと片方が古くなる）。
- 型は C6 の統合（`docs/c6-port.md`、計画 2）。C6 と同じ書き方で、C6 と違うところだけを
  書く。

## 目的

M5Stack Arduino core 3.3.8 を入れた利用者が、`ToppersFMP3` パッケージで
**M5StampC5 (TOPPERS/FMP3)** を選び、`setup()` / `loop()` を FMP3 のタスクとして動かし、
`ToppersFMP3_WiFi` で scan（2.4 / 5 GHz）/ STA 接続 / DHCP / DNS / TCP が使えること。
そのあいだ、**Xtensa 3 板（CoreS3 / M5StickS3 / M5Stack Basic）と M5NanoC6 の配布物は
変えない**（X-check 4 系統 = 9 stage）。

## 出自（provenance）

| 項目 | 値 |
| --- | --- |
| C5 ソースの基準 | 開発リポジトリ `https://github.com/exshonda/fmp3_esp_idf_dev.git` の `1d96bcba32a043eb7066126550b0dbe598e4aad6`（2026-09-16、計画 3 完了 commit） |
| 宣言場所 | `packaging/release-allowlist.json` の `portBaseRepositoryC5` / `portBaseCommitC5` / `portBaseCommitDateC5`（既存の `portBase*`（Xtensa）と `*C6` は不変） |
| 公開スナップショット | **未公開。** 公開リポジトリ `toppers/fmp3_esp_idf` の現行 `fdd89f8`（#7）には C6 も C5 も無い。次のスナップショットが出たら上記キーを公開側へ差し替える（ユーザー操作） |
| fmp3_core | `685b36a`（本リポジトリの submodule と開発リポジトリの submodule が同一） |
| ESP-IDF / toolchain | M5Stack core 3.3.8 同梱の `esp32c5-libs` は `v5.5.4 735507283d`、`esp-rv32/2601` は `esp-14.2.0_20260121`。開発リポジトリの submodule・toolchain 固定と同一（C6 と同じ組合せ） |

## 判断 A0-A12（仮決定。ユーザーが上書きしたら該当段の計画と本表を直す）

開発リポジトリ `.steering/20260916-c5-arduino-plan/PLAN.md` 1 節の写し（段0 時点）。
「本リポジトリ側の箇所」は `90872e9` 時点の名前で引く（file:line は段1 以降ずれる）。

| ID | 判断 | 仮決定 | 根拠 | 外れたときの費用 |
| --- | --- | --- | --- | --- |
| A0 | 出自 | dev `1d96bcba`（計画 3 完了 commit）を C5 の出自として `release-allowlist.json` に第 3 出自（`portBaseCommitC5`）を足す。公開 snapshot はユーザー操作 | C6 D0 と同じ | -- |
| A1 | bootloader | stock M5Stack bootloader（`bootloader_qio_80m.elf`、@**0x2000**）+ stock `default` ptable + boot_app0。dev seam が bootloader 経路で成立済み（計画 3 段2） | INVESTIGATION B | 段2 で dev bootloader へ 1 軸 A/B |
| A2 | port の置き方 | **`ports/m5stack_riscv` を chip 分岐**（`A1_CHIP` を表にし、`arch/riscv_gcc/esp32c5`、`target/m5stampc5_gcc`、`config/esp32c5`、`seam/seam_c5_*`、`cmake/prebuilt_stage_c5.cmake`、`toolchain-riscv-esp32c5.cmake`、`wifi/prebuilt/{wpa2,lwip}/esp32c5` を並置、shim は intmtx/clic 両方を置き分岐）。別 port の複製はしない | 計画 2 D2 の設計意図 | C6 stage のバイト不変を X-check で毎段示す（A4） |
| A3 | `arduino_gpio.c` / `arduino_interrupt.c`（`ports/m5stack_riscv/runtime/arduino`） | C6 決め打ち（`#error` 非 C6、USB 12/13、MSPI 24-30、intmtx route）なので **C5 版を別ファイル**（`arduino_gpio_c5.c`、`arduino_interrupt_c5.{c,cfg,h}`: USB 13/14、MSPI 15-22、GPIO 0-28、CLIC 線）にして CMake で chip ごとに選ぶ | E-2 | -- |
| A4 | X-check | **Xtensa 3 板 + C6 stage（minimal / wifi-connect）の 4 系統**へ拡張（`xcheck_baseline.py --chips esp32s3 esp32 esp32c6`、C5 stage は ignorable）。段0 で baseline + positive control | E / G-2 | -- |
| A5 | driver / manifest | driver 4 / schema 2 のまま。`FIXED_VMA_LAYOUTS` に esp32c5 行（page 0x10000、drom 0x42000000-0x44000000、iram 0x40800000-0x40860000、loader_seg 0x4084E5A0）。C-1..C-8 + C-9（chip_id 0x0017 / rev）を fixed-vma 検査に | E-1 | -- |
| A6 | CPU クロック | minimal も wifi-connect も **240 MHz**（M5Stack `f_cpu`、dev 段4 の実測条件、SIL_DLY 13/10）。80 は `--cmake-define` の退避路 | 計画 3 D5 | 段2 真cold が 240 で落ちたら 80 を 1 軸で |
| A7 | lwIP / DNS | C6 と同じ dev 型（arduino 専用 `liblwip.a` LWIP_DNS=1 を dev 台本 `build_lwip_lib_espidf_esp32c5.sh` の `OUT_DIR`/`PORT_EXTRA` で生成、`netif_esp32s3.c` + `port/sys_arch.c`） | 計画 2 D7 + 段4 Task 0 | -- |
| A8 | esp-idf 原本 | C6 の D8 例外（6 本 + lwIP contrib 3 本）を C5 でも同じ扱い（C5 の同名原本、`IMPORT_PROVENANCE.md` に出自） | 計画 2 D8 | -- |
| A9 | 板 id / 表示名 / 元板 | `m5stampc5_fmp3` / `M5StampC5 (TOPPERS/FMP3)` / `m5stack_stamp_c5`。`UPLOAD_SIZE_OVERRIDES`: maximum_size 1310720、**maximum_data_size 320928** | E-1 | -- |
| A10 | profile / 例題 | `minimal`（Blink / LibraryInfo / TwoFile）+ `wificonnect`（WiFiConnect / WiFiScan / Blink / LibraryInfo / NanoC6Gpio(no-op) / GpioInterrupt）。GpioInterrupt の C5 ピン = **G1**（パッド、Grove 相当。青 LED G28 は BOOT なので試験ピンにしない）。verify 59 -> 68 | E-1、板の与件 | -- |
| A11 | APM / 診断 | `TOPPERS_C5_APM_UNBLOCK`（既定 ON、FUNC_CTRL x4 + TEE）、`TOPPERS_C5_NET_DIAG`（既定 OFF）を C6 と対称に。対照は段4 | 計画 3 段4 | -- |
| A12 | DUT / 台本 | `scripts/capture_c5_usj.sh`（C6 台本の写し: chip `ESP32-C5`、4MB、bootloader 0x2000、**0x0-0x1FFF 消去**、COLD = `uhubctl -l 2-3.3 -p 4`、FORBIDDEN に NanoC6）。JTAG 後処理は `board/esp32c5-builtin.cfg` | 計画 3 段2 | -- |

## 段分け

| 段 | ゴール | 実機 | 状態 |
| --- | --- | --- | --- |
| 0 | X-check 4 系統（baseline + positive control）、本文書の骨子、allowlist の C5 出自 | 不要 | 完了（下記「段0 の記録」） |
| 1 | runtime の chip 表化（C6 値不変）、C5 層の並置（arch / target / config / seam / prebuilt_stage_c5 / toolchain / arduino_*_c5）、scripts の表に C5 行、`--chip esp32c5 --profiles minimal` の stage、`m5stampc5_fmp3:FMP3Runtime=minimal` で Blink / LibraryInfo / TwoFile リンク、重複定義監査、X-check 9/9 | 不要 | 完了（下記「段1 の記録」） |
| 2 | M5Stamp-C5 で Blink（stock bootloader @0x2000）、warm 5/5・真cold 5/5、`capture_c5_usj.sh` | 要 | 未着手 |
| 3 | wifi-connect stage（shim C5 分岐 + clic shim、`.a` x4 vendored、idf_src C5 原本、DNS liblwip）、`nm -u` 空、ROM ld 勝者、移し漏れ表 | 不要 | 未着手 |
| 4 | WiFiScan（2.4 / 5 GHz 可視）/ WiFiConnect（STA -> DHCP -> DNS -> TCP）真cold 3/3、APM OFF 対照 0 AP、GpioInterrupt（G1） | 要 | 未着手 |
| 5 | verify 5 板 68 本、`check_release_artifacts`、CI、README / BUILDING / README.release / THIRD_PARTY_NOTICES / allowlist、docs | 不要 | 未着手 |

公開・tag・版上げは行わない（ユーザー判断）。

## 不変条件（C6 統合から引継ぎ、`CLAUDE.md` と合流）

- **Xtensa 3 板 + M5NanoC6 の配布物不変は X-check で毎段示す**（下記）。共有スクリプトや
  `ports/m5stack_riscv/runtime` を触る段は、作業前に baseline、作業後に compare。
- `ports/m5stack_xtensa/**`、`src/**`、`third_party/**` は無改変。`ports/m5stack_riscv` の
  **C6 の値**は不変（表化は構造の変更であって値は同じ。X-check の C6 2 stage MATCH で証明）。
- creds は開発リポジトリの `esp/boot/wifi_credentials.sh`（PC ローカル）からのみ。
  スケッチにはダミー。**SSID / BSSID / 割当 IP / AP の MAC を commit・文書化しない。**
- `-Wl,--allow-multiple-definition` 下の勝者は `nm` / `readelf` で記録する。
- 実機は M5Stamp-C5 のみ（NanoC6 / CoreS3 / StickS3 / Basic に触れない）。
- 両リポジトリとも origin へ push しない。本リポジトリは `feature/c5-arduino-stage<N>`
  ブランチで作業し、段ごとに `main` へ ff。`--amend` はしない。

## 射程外

- BLE、Zigbee / Thread（C5 の 802.15.4）。
- C3 / H2 / P4 の板。
- fmp3_core への upstream。
- 公開スナップショットの push そのもの（ユーザー操作）。
- Windows 側テスト（`scripts/Test-*.ps1`）の C5 対応（C6 と同じく対象外）。

## X-check -- 4 系統（Xtensa 3 板 + M5NanoC6）の配布物が変わっていないことの機械判定

C5 統合は C6 統合と同じ共有スクリプト（`build_prebuilt_stages.py` / `fmp3_link.py` /
`install_platform.py` / ...）に加えて、**C6 と同じ `ports/m5stack_riscv/runtime`
（`CMakeLists.txt`、`arduino/`、`seam/`、段3 では `wifi/shim`）を chip 分岐にする**（A2）。
「C6 は変えていない」もバイト列で示す。判定器は C6 段0 のもの（`docs/c6-port.md`
「X-check」節）で、baseline に C6 の 2 stage を加えるだけ。

```bash
# 段の起点 commit で、作業ツリーが clean なうちに 1 回（共有ファイルを触る前）
python3 scripts/xcheck_baseline.py --force --clean --chips esp32s3 esp32 esp32c6
                                             # 9 stage を建て build/xcheck-baseline/ へ退避
# 作業後に stage を建て直してから
python3 scripts/build_prebuilt_stages.py --chip esp32s3 --clean
python3 scripts/build_prebuilt_stages.py --chip esp32 --clean --profiles minimal m5-unified wifi-connect bt-classic
python3 scripts/build_prebuilt_stages.py --chip esp32c6 --clean
python3 scripts/xcheck_compare.py             # 9/9 MATCH で rc=0、1 つでも DIFF なら rc=1
python3 scripts/xcheck_compare.py --strict    # banner.o も比べる
python3 scripts/test_xcheck.py                # 判定器の自己テスト
```

- `--force` は既存の baseline セット全体（Xtensa の分も）を消して置き換える
  （`BUILDING.md`「X-check」節）。段の起点 commit・clean tree でだけ使う。
- C5 の stage（`build/prebuilt/esp32c5/`）は baseline に無いので、`xcheck_compare.py` は
  `ignored (not in baseline): esp32c5` の 1 行で読み飛ばす（比較対象にも失敗にもしない）。
  C5 の golden を比較対象にするかどうかは段5 以降の判断。
- **X-check が見るのは配布されるバイト列で、ソースの文面ではない。** stage は debug 情報を
  落として配るので、`__LINE__` / `__FILE__` を即値にしない行を足しても MATCH のまま。
  positive control は `__LINE__` を焼く `assert()` の上にコメントを足して行う（下記）。

## 段0 の記録（2026-09-16、起点 `90872e9`、branch `feature/c5-arduino-stage1`）

段0 はコードを足していない。足したのは本文書と allowlist の C5 出自だけで、
`ports/`・`src/`・`examples/`・`third_party/`・`scripts/*.py` は無改変。証跡は開発リポジトリ
`.steering/20260916-c5-arduino-plan/stage0/{AC.md,logs/}`（本節の丸括弧はそこのログ名）。

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 0a | `xcheck_baseline.py --force --clean --chips esp32s3 esp32 esp32c6` が 9 stage（Xtensa 7 + C6 2）を退避し rc=0 | PASS | `stage0-0a-baseline.txt`（HEAD `90872e9`、dirty 0、clean build 合計 27 s: esp32s3 10 s / esp32 14 s / esp32c6 3 s） |
| 0b | 直後の `xcheck_compare.py` が 9/9 MATCH で rc=0、`--strict` も 9/9（baseline は同じ build の複製なので `banner.o` も一致） | PASS | `stage0-0b-compare.txt`（`expected=9 compared=9 match=9 diff=0`、strict も同じ） |
| 0c | positive control: C6 の runtime ファイル 1 本にコメント 1 行を足して C6 の stage を建て直すと該当 `.o` が 1 行で名指しされ rc=1、戻すと 9/9 に戻る | PASS | `stage0-0c-positive-control.txt`（`ports/m5stack_riscv/runtime/arch/riscv_gcc/esp32c6/chip_kernel_impl.c` の `intmtx_config_int()` の上に 1 行。`assert()` の `__LINE__` が動くので `objs/chip_kernel_impl.o` が **C6 の 2 stage とも** DIFF（libfmp3.a の一員）、Xtensa 7 stage は MATCH。`git checkout` で戻して再ビルドすると 9/9 MATCH） |
| 0d | 既存の自己テスト（`test_xcheck.py` / `test_check_release_artifacts.py` / `test_fmp3_link_objects.py` / `test_check_host_paths.py`）が起点で PASS、allowlist に C5 出自を足したあとも `test_check_release_artifacts.py` PASS | PASS | `stage0-0d-selftests-before.txt`、`stage0-0d-selftests-after.txt` |

### allowlist の C5 出自

`packaging/release-allowlist.json` に `_portCommentC5` / `portBaseRepositoryC5` /
`portBaseCommitC5`（`1d96bcba32a043eb7066126550b0dbe598e4aad6`）/ `portBaseCommitDateC5`
を C6 の 3 キーの直後に足した。`prebuiltStages` / `chipToolDependencies` の C5 行は段1
（`test_check_release_artifacts.py` のドリフト検査がこの 2 表と `make_package_index` /
`install_platform` / `xcheck_compare` の表を同時に見るので、段0 で片方だけ足すと落ちる）。

### 段1 の入口条件

- baseline は `90872e9` の clean tree で採ったもの（`build/xcheck-baseline/BASELINE.json`）。
  **段1 の途中で採り直さない。**
- 段1 は `feature/c5-arduino-stage1` で続ける（段0 と同じブランチ）。

## 段1 の記録（2026-09-16、branch `feature/c5-arduino-stage1`、commit `146e6db` / `79596cd` / `4e67891` / `523d4b9` + 記録）

C5 の `minimal` stage が建ち、`m5stampc5_fmp3:FMP3Runtime=minimal` で 3 例題スケッチが
リンクを通り、C-1..C-9 を満たした段。実機は使っていない。証跡は開発リポジトリ
`.steering/20260916-c5-arduino-plan/stage1/{AC.md,logs/}`（本節の丸括弧はそこのログ名。
`task<N>-*.txt` = Task N の採取）。

### AC 1a-1i

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 1a | `build_prebuilt_stages.py --chip esp32c5 --profiles minimal --clean` rc=0、stage 一式あり、重複定義監査 PASS、manifest の `romLinkerScripts` は 2 本 | PASS | `task3-build-c5-minimal-first.txt`（48 objects / 414 strong definitions / 0 duplicated、`romLinkerScripts` = `esp32c5.rom.ld` / `esp32c5.rom.api.ld`、`chip` = `esp32c5`、`paddrMode` = `fixed-vma`、`linkBaseFlags` 末尾に `-Wl,-e,seam_c5_entry_boost`。C6 の 47 objects との差は `clic_kernel_impl.o`（共通 CLIC 層）の 1 本） |
| 1b | X-check 9/9 MATCH、`ports/m5stack_xtensa` / `src` / `examples` / `third_party` の `git diff --stat` が空 | PASS | `task4-xcheck-all.txt`（Xtensa 7 + C6 2 を `--clean` で建て直し `expected=9 compared=9 match=9 diff=0`、`ignored (not in baseline): esp32c5`。`--strict` の差は 9 stage とも `banner.o` のみ）、`task4-tests.txt`（`git diff --stat 1d9caec -- ports/m5stack_xtensa src examples third_party` が空） |
| 1c | `install_platform.py` で 5 板が組め、`arduino-cli board listall` に `m5stampc5_fmp3` が出る。既存 4 板の `boards.txt` 行と `platform.txt` は不変 | PASS | `task4-install-platform.txt`（5 板・10 stage、`m5stampc5_fmp3.*` 182 行）、`task4-install-platform-cmp.txt`（段0 commit の installer との比較: `platform.txt` / `programmers.txt` / `tools/` はバイト同一、`boards.txt` の差は `m5stampc5_fmp3.*` の追加だけ） |
| 1d | 3 例題（Blink / LibraryInfo / TwoFileSketch）が compile rc=0、C-1..C-8 + C-9 PASS | PASS | `task4-compile-c5-{Blink,LibraryInfo,TwoFileSketch}.txt`（`mapped=2 ram=1 pad=1`、`C-8 OK: image 83536 / 83824 / 83552 / 1310720`、`C-9 OK: chip_id=0x0017 min_chip_rev_full=0 <= board rev 100 <= max_chip_rev_full=65535`、`C-1 to C-9 satisfied`） |
| 1e | `check_host_paths.py` rc=0、`scripts/test_*.py` 全 PASS（C-9 ケース込み）、Xtensa 板と C6 板の Blink も同じドライバで rc=0 | PASS | `task4-check-host-paths.txt`（982 files、0 件）、`task4-tests.txt`（4 本 PASS。`test_check_release_artifacts.py` は 13 tests）、`task4-xtensa-c6-blink-same-driver.txt`（CoreS3 29720/19036 bytes・NanoC6 83360 bytes = C6 段1 の値と同一、NanoC6 は `C-1 to C-8 satisfied` のまま = C-9 は C5 だけ） |
| 1f | リンク前 `nm -u` の一覧と勝者一覧の記録、実リンク後 `nm -u` は空 | PASS | `task4-nm-u-stage.txt`（stage 単体、未定義 22 記号 = ld 定義 17 + peripherals.ld 2 + ROM api ld 1 + スケッチ供給 1 + newlib 2（`__errno` / `_impure_ptr`、`newlib_syscalls.o` が参照））、`task4-link-winners.txt`（3 例題とも実リンク後 `nm -u` 空。ROM ld 由来の勝者は `esp_rom_set_cpu_ticks_per_us` = `0x40000044` だけ。stage 側定義の置換は 0 件）。詳細は下記「ROM linker script の勝者」 |
| 1g | `IMPORT_PROVENANCE.md` に C5 の dev 由来ファイル全件、本文書に段1 の記録 | PASS | `ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md` の C5 節（chip 23 + target 29 + seam 4 + config 1、改変 2 ファイルの内容）、`THIRD_PARTY_NOTICES.md` の C5 節、本節 |
| 1h | `--show-properties` の `recipe.size.regex` が C5 のセクション名を含み、`upload.maximum_size=1310720` / `upload.maximum_data_size=320928`、`bootloader_addr` は継承値 `0x2000` | PASS | `task4-show-properties.txt`（`build.bootloader_addr=0x2000`、`build.f_cpu=240000000L`、`build.toppers_chip=esp32c5`、`compiler.sdk.path` = `esp32c5-libs/3.3.8`。C6 は `0x0` / `452112` のまま） |
| 1i | chip 表化の commit 単独で C6 の 2 stage を `--clean` で建て直して X-check 9/9 MATCH | PASS | `task2-xcheck-chip-table.txt`（commit `79596cd` の状態で esp32c6 を `--clean` 再ビルド、9/9 MATCH = manifest / objs / rsp とも同一）、`task2-c6-80mhz-fallback.txt`（`--cmake-define A1_C6_CPU_FREQ_MHZ=80` が表化後も効く: CMakeCache 80、47 objs とも `CORE_CLK_MHZ=80`、`SEAM_C6_CLK_BOOST` 無し） |

### 判断 S1-C5-1..S1-C5-6（段1 で確定）

| # | 判断 | 結果 |
| --- | --- | --- |
| S1-C5-1 | `ports/m5stack_riscv/runtime/CMakeLists.txt` を `A1_CHIP`（esp32c6 / esp32c5）の chip 表にする | 実装どおり。表の行 = target / arch / xip ld / ROM ld 一覧 / USJ HAL / seam ソース / prebuilt script / CPU クロック変数（`A1_<TAG>_CPU_FREQ_MHZ`、選択肢、既定、boost 値）/ `A1_CHIP_WIFI`。C6 行は従来の文字列のまま（AC-1i）。wifi-connect ブロック（include 一覧・shim・idf_src・prebuilt archives）は C6 名のままで、C5 は `A1_CHIP_WIFI OFF` により `FATAL_ERROR`（段3 で表に載せる） |
| S1-C5-2 | minimal の ROM ld は dev と同じ 2 本 | 実装どおり（C6 段1 ruling (a) の教訓。wifi-connect の 13 本、eco3 無しは段3） |
| S1-C5-3 | CPU クロック 240 MHz 既定、entry は `-Wl,-e` で manifest から | 実装どおり。`A1_C5_CPU_FREQ_MHZ` 既定 240（`SEAM_C5_CLK_BOOST=1`、entry `seam_c5_entry_boost` = `0x42000140`）、80 は `--cmake-define A1_C5_CPU_FREQ_MHZ=80`（entry `seam_c5_entry`）。dev の報告タスク `seam_c5_clk.cfg` は持ち込まない（配布 stage にタスクを足さない）。結果 `g_seam_c5_clk_result` は `.data`（`0x40800004`）に残るので、段2 で証跡が要れば probe スケッチが `extern "C"` で読める。**240 MHz の実機起動は未検証**（段2 の watch item。dev は seam-c5-wifi の 240 で実測、min は 80） |
| S1-C5-4 | C-9 をドライバへ | 実装どおり。`ImageLayout` に `chip_id` / `board_rev_full`（既定 None = 検査しない）を足し、`FIXED_VMA_LAYOUTS["esp32c5"]` だけ `0x0017` / `100`。C6 行は None で C-1..C-8 の挙動不変（`task4-xtensa-c6-blink-same-driver.txt`）。`test_fmp3_link_objects.py` に 7 ケース（他チップの chip_id、chip_id 0、rev 窓の下 / 上、窓の両端、片方だけの layout）。arduino 側の esptool 5.2.0 は `min/max_chip_rev_full` を `0 / 65535` で書く（dev の esptool 4.12 と違う既定値。どちらも rev 100 を含む） |
| S1-C5-5 | `arduino_interrupt_c5` の CLIC 線 | 23（dev の線表で shim が使わない空き線。19 は C5 のテスト用 `INTNO_UNOPTED` として空けておく）。段1 では stage に入らず `-fsyntax-only` と負対照のみ（`task3-syntax-check-arduino-c5.txt`: C6 版は `#error`、線 30 / 19 / 16 / 42 はそれぞれ該当の `#error`） |
| S1-C5-6 | profile 表は段1 では `minimal` のみ | 実装どおり。`CHIPS["esp32c5"].profiles` / `EXPECTED_PROFILES` / allowlist `prebuiltStages` / `test_check_release_artifacts.py` の `STAGES` / CI yml の 5 表が `minimal` で一致（ドリフト検査 PASS）。**`verify_package.py BOARD_PROFILES` だけは `{minimal, wificonnect}`**（A10 の 68 本を `--list-builds` が出す）。帰結: 段3 まで `verify_package.py` を C5 の全 profile で走らせると wificonnect の 6 本は FQBN 解決で落ちる（stage が無い）。C5 を検証するときは `--boards m5stampc5_fmp3 --profiles minimal` |

### ROM linker script の勝者（AC-1f）

C6 段1 と同じ 2 本（`esp32c5.rom.ld` / `esp32c5.rom.api.ld`）なので結論も同じ。3 例題とも
リンク前に未定義かつ絶対番地に解決した記号のうち ROM ld 由来は
**`esp_rom_set_cpu_ticks_per_us`（`esp32c5.rom.api.ld` の PROVIDE -> `ets_update_cpu_frequency`
`0x40000044`）だけ**。他の絶対記号は `esp32c5_xip.ld`（`__init_array_*` / `__ctors_*` / `__idata_*`）と
`esp32c5.peripherals.ld`（`SYSTIMER` / `USB_SERIAL_JTAG`）。stage の定義を ROM 代入が置き換えた
ものは 0 件。`libc_nano.a` から引かれるのは `impure.o` / `errno.o`（`newlib_syscalls.o` が参照）と
LibraryInfo の `memcpy.o`（C6 と同じ）。C6 段1 の M-6 parity gap（`chip_rom_libc.c` 相当が無く、
`String` / `printf` / `rand` 等は `_sbrk` 未定義でリンク時に落ちる）は C5 でも同じで、段3 で
libc 供給の決定と一緒に扱う。

### 像サイズと RAM（AC-1d、2026-09-16 実測）

C5 xip ld の `RAM` は `0x4084E5A0 - 0x40800000` = 320928 bytes（stock bootloader 4 変種とも
`.iram_loader.text` が `0x4084e5a0`、`task4-stock-bootloader-readelf.txt`）。C-8 の分母は
stock `default` partition table の app0 = `0x140000` = 1310720。

| スケッチ | image bytes（C-8） | RAM（`.data` + `.bss`） | RAM 余裕 |
| --- | --- | --- | --- |
| Blink | 83536 / 1310720 | 432 + 21056 = 21488 | 299440 bytes（93.3%） |
| LibraryInfo | 83824 / 1310720 | 21488 | 299440 bytes |
| TwoFileSketch | 83552 / 1310720 | 21488 | 299440 bytes |

C6 の Blink（17760 bytes）より RAM が 3.7 KB 多いのは主に `_kernel_istack_prc1` が 8 KB
（C6 は 4 KB。C5 の target 層の値、dev のまま）。`size` が bss に数える 68 KB は
`.flash_rodata_dummy`（NOLOAD の隙間）で、IDE の表示（`recipe.size.regex.data`）は除いている。
固定値として恒久扱いしないこと。

### 段2 への watch item

- **240 MHz は実機未検証**（S1-C5-3）。段2 の最初の書込みで `'S'` -> banner を確認。240 で
  黙ったら `--cmake-define A1_C5_CPU_FREQ_MHZ=80` の像（entry `seam_c5_entry`、昇圧無し）を
  1 軸で試す。`seam_c5_entry_boost` は昇圧がマークより先なので、`'S'` すら出なければ昇圧側。
- **stock M5Stack bootloader は未検証**（A1）。stock は `bootloader_qio_80m.elf`（`@0x2000`）、
  `CONFIG_BOOTLOADER_WDT_ENABLE=y` 9000 ms、QIO、`CONFIG_ESP_CONSOLE_UART_DEFAULT=y` +
  USJ secondary、rev 窓 100..199（`task4-stock-bootloader-readelf.txt`）。C6 段1 の同名節の
  読み方がそのまま当てはまる。
- **`esp_rom_set_cpu_ticks_per_us`** は `hardware_init_hook`（`CORE_CLK_MHZ` = 240）と
  `seam_c5_clk_set()` の両方が呼ぶ（dev と同じ、無害）。
- 焼く物: `fmp_xip.bin` 相当 + stock bootloader（`0x2000`）+ stock `default` ptable + `boot_app0`。
  `arduino-cli compile` の `merged.bin` 構成は実測で `0x2000 Blink.ino.bootloader.bin`
  （`bin/bootloader_qio_80m.elf` から elf2image）/ `0x8000 partitions` / `0xe000 boot_app0` /
  `0x10000 app`（C6 の `0x0` と違う。`stage1/logs` の `Blink.full.log` は scratch にのみ残る）。
  採取台本は A12（`0x0-0x1FFF` 消去）。
- `arduino_interrupt_c5` / `arduino_gpio_c5` はどの stage にも入っていない（段3）。
