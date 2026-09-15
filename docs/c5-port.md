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
| A10 | profile / 例題 | `minimal`（Blink / LibraryInfo / TwoFile）+ `wificonnect`（WiFiConnect / WiFiScan / Blink / LibraryInfo / NanoC6Gpio(no-op) / GpioInterrupt）。GpioInterrupt の C5 ピン = **G1**（パッド、Grove 相当。青 LED G28 は BOOT なので試験ピンにしない）。verify 59 -> 68（段1 は minimal のみで 62、68 は段3 で wificonnect を足してから） | E-1、板の与件 | -- |
| A11 | APM / 診断 | `TOPPERS_C5_APM_UNBLOCK`（既定 ON、FUNC_CTRL x4 + TEE）、`TOPPERS_C5_NET_DIAG`（既定 OFF）を C6 と対称に。対照は段4 | 計画 3 段4 | -- |
| A12 | DUT / 台本 | `scripts/capture_c5_usj.sh`（C6 台本の写し: chip `ESP32-C5`、4MB、bootloader 0x2000、**0x0-0x1FFF 消去**、COLD = `uhubctl -l 2-3.3 -p 4`、FORBIDDEN に NanoC6）。JTAG 後処理は `board/esp32c5-builtin.cfg` | 計画 3 段2 | -- |

## 段分け

| 段 | ゴール | 実機 | 状態 |
| --- | --- | --- | --- |
| 0 | X-check 4 系統（baseline + positive control）、本文書の骨子、allowlist の C5 出自 | 不要 | 完了（下記「段0 の記録」） |
| 1 | runtime の chip 表化（C6 値不変）、C5 層の並置（arch / target / config / seam / prebuilt_stage_c5 / toolchain / arduino_*_c5）、scripts の表に C5 行、`--chip esp32c5 --profiles minimal` の stage、`m5stampc5_fmp3:FMP3Runtime=minimal` で Blink / LibraryInfo / TwoFile リンク、重複定義監査、X-check 9/9 | 不要 | 完了（下記「段1 の記録」） |
| 2 | M5Stamp-C5 で Blink（stock bootloader @0x2000）、warm 5/5・真cold 5/5、`capture_c5_usj.sh` | 要 | 完了（下記「段2 の記録」。条件 A で warm 5/5・真cold 5/5、A1 = stock、A6 = 240 MHz を JTAG の PCR 読出しで確定） |
| 3 | wifi-connect stage（shim C5 分岐 + clic shim、`.a` x4 vendored、idf_src C5 原本、DNS liblwip）、`nm -u` 空、ROM ld 勝者、移し漏れ表 | 不要 | 完了（下記「段3 の記録」。6 例題リンク、C6 objs 不変、未焼き） |
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
| S1-C5-6 | profile 表は段1 では `minimal` のみ | 実装どおり。`CHIPS["esp32c5"].profiles` / `EXPECTED_PROFILES` / allowlist `prebuiltStages` / `test_check_release_artifacts.py` の `STAGES` / CI yml の 5 表が `minimal` で一致（ドリフト検査 PASS）。`verify_package.py BOARD_PROFILES["m5stampc5_fmp3"]` も **`{"minimal"}`**（fix round 1 の ruling: 存在しない stage に対する build を verify / CI が計画してはならない）。`--list-builds` は **62**（5 板。68 は段3 で wificonnect を足してから）、`--boards m5stampc5_fmp3` は minimal の 3 本だけを計画する |

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

## 段2 の記録（2026-09-16、branch `feature/c5-arduino-stage2`、commit `f7194cc`（Task 1）/ `7b37d07`（記録））

M5Stamp-C5 実機で `minimal` 版 `Blink` が **stock M5Stack bootloader（@0x2000）** で起動し、
A1（bootloader）と A6（CPU クロック 240 MHz）を確定した段。書込み・採取は台本
`scripts/capture_c5_usj.sh`（C6 台本の写し、C6 台本は無改変）のみで行い、実機操作は
uhubctl による電源断／投入のみ。証跡は開発リポジトリ
`.steering/20260916-c5-arduino-plan/stage2/{AC.md,logs/}`（`task1-*` = 台本の同定・負対照・
selftest、`task2-A-*` = 実機実験 条件 A、`task2-A-recount.txt` = `.log` からの独立集計）。
Task 1 の実装報告は本リポジトリ `.superpowers/sdd/PLAN-stage2-impl/task-1-report.md`
（gitignore、未追跡）。記法: 事実 = 実測（ログ名を引く）、推測 = そこからの読み、
未確認 = 測っていない。

### AC 2a-2h

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 2a | `capture_c5_usj.sh` の DRYRUN が「erase-region 0x0 0x2000 + 焼く物 4 点（bootloader @0x2000 / 0x8000 / 0xe000 / 0x10000）の sha256・番地 + 読み戻し」を出し、MAC 不一致・FORBIDDEN（NanoC6）・DUT 不在・`bootloader_addr` != 0x2000 で rc!=0、selftest PASS | PASS | `task1-dryrun.txt`（実機 `flash-id --no-stub` で `BASE MAC: 3c:dc:75:8d:ed:20` / `ESP32-C5 (revision v1.0)` / 4MB を確認後、3 手の実コマンド行と 4 点の sha256・番地、rc=0）、`task1-gate-neg.txt`（(a) FORBIDDEN = NanoC6 の MAC -> esptool 未呼出 rc=1、(b) MAC 偽値 = by-id 不在 rc=1、(c) MAC 偽値 + 実ポート = `BASE MAC does not match` rc=1、(d) 別 rev = rc=1、(e) boards.txt の番地 0x0 = esptool 未呼出 rc=1、(f) key 欠落 = rc=1）、`task1-imgcheck-neg.txt`（0 バイト / 不正 magic / 31 バイト / ptable 3071 / boot_app0 8191 / 不在の 6 通り、いずれも esptool 未呼出 rc=1）、`task1-selftest.txt`（20 項目 PASS。(19) 番地ゲート、(20) PCR decode は新設）、`task1-selftest-mutants.txt`（fix round 1 で採取。scratch 複製の変異体 3 種 = 期待番地 0x1000 -> (19) FAIL rc=1、PLL_F240M の 160 化 -> (20) FAIL rc=1、boards.txt key の `.` を未エスケープ -> (19) の dots-as-any-char 項目 FAIL rc=1。無変異は PASS rc=0） |
| 2b | 条件 A（stock bootloader @0x2000 + stock `default` ptable + boot_app0、240 MHz）warm 5/5 | **PASS** | `task2-A-warm{1..5}.log/.run.txt/.sha.txt`（warm1 は `.ident.log`/`.flash.log`、warm5 は `.jtag.log`/`.jtag.txt` も）。5 回とも banner=1・setup=1・heartbeat=39・unexpected=0・smark=1・blink=39。独立集計（`task2-A-recount.txt`、python）でも同数、heartbeat 行の文字落ち 0 |
| 2c | 条件 A の真cold 5/5 | **PASS（5/5）** | `task2-A-cold{1..5}.log/.cold.txt/.journal.txt/.run.txt/.sha.txt/.ctl.txt`。5 回とも setup=1・heartbeat=39・unexpected=0・blink=39（banner/`S` は host が tty を開く前に出るため 0、R11 どおり判定に使わない）。`.cold.txt` は 5 回とも「absent at start」-> 「appeared」（wait 開始から 3.07 s）、`.journal.txt` は電源サイクルごとに `USB disconnect` 1 回 + `new full-speed USB device` 1 回で採取中の再列挙なし。`.ctl.txt` に uhubctl の off/on 出力（`Port 4` のみ、NanoC6 の by-id は電源断中も present） |
| 2d | 不成立なら B（dev bootloader）-> C（+dev ptable） | **不要（実施せず）** | A が 2b/2c を満たしたため。dev 側 bootloader（`be37126b...`）は読んでもいない |
| 2e | 240 MHz で不成立なら 80 MHz を 1 軸 | **不要（実施せず）** | A が 240 MHz のまま成立。段1 の watch item「240 MHz は実機未検証」は本段で解消 |
| 2f | 像の size、最初の行（`S` mark）、WDT 停止の根拠、CPU 240 MHz の根拠 | **PASS** | 下記「最初の行の観察」「WDT が止まっていることの根拠」「CPU クロックの根拠（JTAG）」。像 size は「板の最終状態」 |
| 2g | 本節、A1/A6 の確定、段3 の入口条件、dev `stage2/AC.md` | PASS（本 commit） | 本節、`README.md` の C5 行 |
| 2h | 非退行: X-check 9/9（scripts を触ったため）、C6 側ファイル無改変 | PASS | `task1-xcheck.txt`（`expected=9 compared=9 match=9 diff=0`、`--strict` の差は 9 stage とも `banner.o` のみ、`ignored (not in baseline): esp32c5`。`git diff --stat HEAD -- ports/m5stack_xtensa src third_party ports/m5stack_riscv scripts/capture_c6_usj.sh` は空）。段1 の 3 例題リンクは `ports/`・`src/`・prebuilt を触っていないので状態不変と判断（Blink は本段で再コンパイル rc=0、C-1..C-9 OK） |

### 軸表（書込み前に固定）

| 軸 | 値 | 条件 A（採った条件） | 条件 B | 条件 C | 80 MHz 腕 |
| --- | --- | --- | --- | --- | --- |
| bootloader | stock（`bootloader_qio_80m.elf` -> `Blink.ino.bootloader.bin`、@0x2000、WDT 9 秒 armed、UART0 主 + USJ secondary、QIO）／dev seam（`be37126b...`、WDT 無効、USJ、DIO） | stock | dev | dev | A と同じ |
| ptable | stock `default`（otadata 0xe000、app0 0x10000 +0x140000）／dev（nvs 0x9000、phy 0xf000、factory 0x10000） | stock | stock | dev | A と同じ |
| boot_app0 @0xe000 | 焼く／焼かない | 焼く | 焼く | 焼く（dev ptable では nvs と重なる。記録のみ） | 焼く |
| flash 0x0-0x1FFF | 毎回 erase-region + 読み戻し全 0xFF（asp3 の Direct Boot magic の消去） | 消す | 消す | 消す | 消す |
| CPU | 240（entry `seam_c5_entry_boost`）／80（entry `seam_c5_entry`） | 240 | 240 | 240 | 80 |
| reset | warm（`NOFLASH=1`、monitor の hard reset）／真cold（`COLD=1` + `uhubctl -l 2-3.3 -p 4`） | 両方 5 回 | 両方 5 回 | 両方 5 回 | 両方 5 回 |

手順は A warm x5 -> A cold x5 ->（不成立なら）B -> C -> 80 MHz。**A で成立したため B/C/80 MHz は未実施**。

### 結果表

| 条件 | warm | 真cold | 判定 |
| --- | --- | --- | --- |
| A: stock bootloader @0x2000 + stock `default` ptable + boot_app0 + 240 MHz | **5/5** | **5/5** | **成立**（A1 = stock、A6 = 240 MHz） |
| B: dev bootloader + stock ptable | 未実施（A 成立のため） | 未実施 | -- |
| C: dev bootloader + dev ptable | 未実施（A 成立のため） | 未実施 | -- |
| 80 MHz | 未実施（240 MHz のまま A が成立したため） | 未実施 | -- |

書込みは全 10 run 中 **1 回だけ**（`task2-A-warm1`: `erase-region 0x0 0x2000` -> `write-flash` 4 点
（`Hash of data verified x4`）-> `read-flash 0x0 0x2000` の読み戻しが 8192 バイト全 0xFF）。以後は
`NOFLASH=1`（warm）または `COLD=1`（cold）で flash に触れていない（各 `.sha.txt` に
"NOT written by this run"）。C6 段2 で 1/10 あった cold の無音は C5 の 5 回では 0 回。

### 最初の行の観察（AC-2f）

warm（5/5 とも同一の系列、`task2-A-warm{1..5}.log`）:

```
ESP-ROM:esp32c5-eco2-20250121
Build:Jan 21 2025
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
SPI mode:DIO, clock div:1
load:0x408556b0,len:0x1258
load:0x4084bba0,len:0xca4
load:0x4084e5a0,len:0x315c
entry 0x4084bba0
S
TOPPERS/FMP3 Kernel Release 3.4.0 for M5Stamp-C5 (ESP32-C5) (Sep 16 2026, 05:39:57)
...
Processor 1 start.
no time event is processed in hrt interrupt on PRC1.
System logging task is started on port 1.
task start                 <- [Arduino] task start の頭欠け（C6 段2 と同型）
[Arduino] task=2 processor=1
[Blink] serial indicator start
[Arduino] setup complete
[Blink] ON / [Arduino] loop heartbeat ...（交互に 39 回）
```

- 系列は **ROM 起動バナー -> `load:`/`entry 0x4084bba0` -> 生の `S` 単独行（`SEAM_C5_ENTRY_MARK`）->
  FMP3 banner**。**stock bootloader 自身は USJ に 1 行も出さない**（UART0 主、
  `BOOTLOADER_LOG_LEVEL=ERROR`）。dev 段2 の seam bootloader（USJ コンソール）では `I (21) boot: ...`
  の行が出ていたので、これは stock との見える差。
- `load:0x4084e5a0,len:0x315c` = stock bootloader の `iram_loader_seg` が `0x4084E5A0` に置かれる
  （段1 の `task4-stock-bootloader-readelf.txt` の値と一致、xip ld の RAM 上限と同じ）。
- `S` は `seam_c5_entry_boost` の昇圧（PCR の CPU_DIV_NUM 2->0）より**後**に出る（`seam_c5_entry.S`
  の順序）。`S` が 5/5 出た = 昇圧を通過してから USJ に書けている。
- 真cold（5/5 とも同一）: 最初の行は **`System logging task is started on port 1.`**。ROM 行・`S`・
  banner・`Processor 1 start.`・hrt notice は host が tty を開く前に出るため失われる（C6 段2 と同じ、
  R11。判定は heartbeat 回数）。`no time event is processed in hrt interrupt on PRC1.` は warm 5/5 で
  1 回ずつ、cold は先頭欠落のため 0（C6 段2 (c) と同じ型、害は観測していない）。
- `[Arduino] task start` の頭欠けは 10/10（行ごと無い（空行）のは warm4 のみ。残りは warm1 `task start` /
  warm2 `k start` / warm3 `sk start` / warm5 `ask start` / cold1 `task start` / cold2 ` task start` /
  cold3 `] task start` / cold4 `art` / cold5 `start`）。C6 段2 (a) と同じ経路（USJ TX FIFO の競合）と推定。
  heartbeat 行の文字落ち（C6 の cold7 `hartbeat`）は本段の 390 行中 0。

### WDT が止まっていることの根拠（AC-2f、推論と明記）

- stock bootloader は `CONFIG_BOOTLOADER_WDT_ENABLE=y`・9000 ms で armed のまま app へ飛ぶ
  （段1 記載、`task4-stock-bootloader-readelf.txt`）。`CAPTURE_SEC=40` の窓で heartbeat（`loop()`
  1000 回ごと、40 s の窓に 39 本 = 約 1 s 周期）が **39 本**、10 run すべてで観測。9 秒の壁を
  大きく超えて連続している。
- 採取中の USB 再列挙は無い（`.journal.txt` は電源サイクルごとに 1 回の列挙のみ）、warm の `rst:`
  行は毎回 `0x15 (USB_UART_HPSYS)`（USJ 経由の host reset）で、RTC WDT の reset 理由は 10 run の
  どこにも出ていない。
- **これは間接証拠であり、LP_WDT レジスタの読み戻しはしていない**。以上から `hardware_init_hook`
  （`target/m5stampc5_gcc/target_kernel_impl.c`）が stock bootloader の armed した 9 秒 RTC WDT を
  実際に止めている、と**推論する**（実証ではない）。

### CPU クロックの根拠（AC-2f、JTAG で実測）

minimal stage は PCR を印字しない（dev の報告タスクは持ち込まない、S1-C5-3）ので、warm5 で
台本の JTAG probe を強制（`C5_JTAG_FORCE=1`、`board/esp32c5-builtin.cfg`、`adapter serial
3C:DC:75:8D:ED:20` を最初の `-c` に、halt -> 読出し -> resume、リセット無し）し、採取後に読んだ
（`task2-A-warm5.jtag.{log,txt}`、openocd rc=0、`esp_usb_jtag: serial (3C:DC:75:8D:ED:20)` を確認）:

```
pcr: sysclk_conf=0xb0030200 cpu_freq_conf=0x00000000 soc_clk_sel=3 (PLL_F240M) xtal=48 cpu_div_num=0 cpu=240MHz
clk_result: sysclk_before=0xb0030200 cpu_freq_before=0x00000002 ahb_before=0x00000005
            sysclk_after=0xb0030200 cpu_freq_after=0x00000000 ahb_after=0x00000005 busupd_wait=0 rc=1 (OK)
loop_calls: 40709 -> 42543 delta=1834   verdict: alive
```

- `PCR_CPU_FREQ_CONF`（0x60096118）= 0 = `CPU_DIV_NUM` 0（divider 1）、`PCR_SYSCLK_CONF`（0x60096110）
  の `SOC_CLK_SEL` = 3 = PLL_F240M。**= 240 MHz**（dev 段4 Task 1 の実測値と同じ組合せ）。
- `g_seam_c5_clk_result`（`.data` 0x40800004）は `cpu_freq_before=2`（bootloader が渡す 80 MHz）->
  `after=0`、`rc=1 OK`、`busupd_wait=0`。= `seam_c5_entry_boost` -> `seam_c5_clk_set()` が走り、
  分周の書換えと `bus_clk_update` の自己クリアが通った。
- **A6 = 240 MHz（確定、実測で裏付け）**。80 MHz へのフォールバックは発生していない。
- probe は halt を 2 回（各 1 回の読出し分）挟むが、採取（monitor）が終わったあとなので marker
  には影響しない。probe 後の cold1-5 は成立（probe が板を壊していない）。

### A1・A6 の確定

- **A1 = stock M5Stack bootloader（@0x2000）（確定、実測で裏付け）**。条件 A で warm 5/5・真cold 5/5。
  bootloader を本リポジトリに同梱する必要は無い。dev seam bootloader（B）・+dev ptable（C）は
  A1 の確定に不要となったため未実施。
- **A6 = 240 MHz（確定、JTAG の PCR 読出しで実測）**。

### 台本のインタフェースと安全ゲート（`scripts/capture_c5_usj.sh`）

C6 台本（`docs/c6-port.md` 段2 の同名節）と同じ入力・EXIT トラップ・marker 集計・JTAG probe に、
C5 で違う次の 5 点を足した（台本ヘッダに列挙）:

1. **DUT**: `3c:dc:75:8d:ed:20` / `ESP32-C5 (revision v1.0)`（rev 込みの完全文字列、dev 台本と同じ）
   / 4MB。FORBIDDEN に **M5NanoC6 `9c:13:9e:d3:62:18`** を追加（隣の hub port の板 = C6 台本の DUT）。
2. **bootloader 番地はリテラルではない**: `BOARDS_TXT`（既定 `~/Arduino/hardware/toppers/esp32/
   boards.txt`）の `m5stampc5_fmp3.build.bootloader_addr` を読み、**0x2000 以外は拒否**
   （上書き変数は意図的に無い。fix round 1 で `WRITE_ARGS` のリテラル fallback `0x2000` を撤去し、
   書込み経路で未設定なら die、key の `.` は ERE でエスケープ）。負対照 (e)(f) と selftest (19)。
3. **書込み run は毎回 `erase-region 0x0 0x2000` -> `write-flash` 4 点 -> `read-flash 0x0 0x2000` の
   読み戻し全 0xFF を要求**（asp3 の Direct Boot magic。dev 段0/段2 の発見）。3 手は ROM download
   mode で `--before no-reset` に連鎖（dev 台本で実証済みの列）。DRYRUN は 3 手をすべて表示し、
   `--after hard-reset` で板を起こして終わる（C6 台本は download mode に置き去り）。
4. **JTAG probe** は `board/esp32c5-builtin.cfg`。USB_SERIAL_JTAG レジスタ（C5 でも 0x6000F004/8）に
   加えて `PCR_SYSCLK_CONF` / `PCR_CPU_FREQ_CONF` と `g_seam_c5_clk_result`（8 語）を読み、
   `pcr: ... cpu=<MHz>` / `clk_result: ...` を `.jtag.txt` と `.sha.txt` に残す（verdict には使わない）。
   selftest (20)。
5. `wifi_adapter(c5): apm ` の marker（段4 用、本段では 0）。

### 懸念・持ち越し（段3 以降）

(a) **`[Arduino] task start` の頭欠け（10/10）**: C6 段2 (a) と同型。
  warm5 の `.jtag.log` には 8 語読み（`mdw 0x40800004 8`）の直前に `Warn : [esp32c5] Failed to read
  memory via program buffer.` が 1 回ある（値は出ており、直読みの PCR 2 語と `*_after` が一致 =
  自己整合。openocd が別経路へ fallback したと推測）。marker 判定には影響しないが、
  行単位の厳密照合をする試験は偽陰性を作る。
(b) **B / C / 80 MHz は Blink で未検証**。A1/A6 の確定に不要だが、stock bootloader 固有の挙動
  （WDT・QIO）が問題になったときの切り分け腕は残っている。
(c) **WDT 停止は間接証拠**（上記）。LP_WDT の読み戻しは JTAG probe で足せる（レジスタ番地の確認が
  要る、未実施）。
(d) **PCR 読出しは warm5 の 1 回**（cold では読んでいない。cold の PCR は dev 段4 で 240 を 3/3 実測）。
(e) 台本の `WIFI_CREDS` 既定は dev の `wifi_credentials.sh` を読む（needles=6）。本段に Wi-Fi は無いが、
  マスク層は 10 run とも "masked and checked clean"、`.UNREDACTED` 0 本。

### 板の最終状態

最後に書込みをした run は `task2-A-warm1`（`.sha.txt` に `Hash of data verified x4`、
0x0-0x1FFF 読み戻し全 0xFF）。以後 flash には書いていない。

| 番地 | sha256 | size | 物 |
| --- | --- | --- | --- |
| 0x0-0x1FFF | （全 0xFF） | 8192 | 消去済み（asp3 の Direct Boot magic は無い。工場退避像は dev `build/c5-backup/`） |
| 0x2000 | `98aa7b90...` | 20656 | stock bootloader（`Blink.ino.bootloader.bin`、`bootloader_qio_80m.elf` 由来） |
| 0x8000 | `148b959c...` | 3072 | stock `default` OTA ptable（`Blink.ino.partitions.bin`、C6 と同一バイト） |
| 0xe000 | `f94c5d78...` | 8192 | `boot_app0.bin`（M5Stack core 3.3.8） |
| 0x10000 | `c73915da...` | 83536 | `Blink.ino.bin`（minimal、240 MHz、C-1..C-9 OK） |

= 条件 A が焼かれたまま。最後の run（cold5）は成立して終わっており、板は Blink を実行中
（電源 on、by-id 存在、hub `2-3.3` port 4 connect）。dev 計画 3 の像（seam-c5-wifi のダミー creds
`3d8658bb...` + dev bootloader/ptable + Direct Boot の頭）は本段の初回書込みで上書き・消去されている。

### 段3 の入口条件

`wifi-connect` stage 着手前に（C6 段2「段3 の入口条件」の C5 版。C6 で決着した項目は結論だけ書く）:

- **shim の C5 分岐 + clic shim**: `ports/m5stack_riscv/runtime/wifi/shim`（C6 段3 で dev `esp/shim`
  の第2コピーとして持ち込んだ版）に dev の C5 分岐（`TOPPERS_ESP32C5`、`esp_shim_intr_clic.*`
  相当）を A2 の型（同一ファイル内の chip 分岐、C6 側のバイト列不変を X-check の C6 2 stage で示す）
  で載せる。dev 段4 0-3 節の「共通ファイルへの C5 分岐は 3 形に限る」は dev の golden の話であり、
  本リポジトリでは X-check（debug 情報無し）が判定器なので `__LINE__` を焼く行にだけ注意する。
- **`.a` x4 vendored**: dev 台本 `build_{wpa_libs,mbedtls_tls,lwip_lib}_espidf_esp32c5.sh` +
  arduino 専用 `liblwip.a`（LWIP_DNS=1、A7）を `wifi/prebuilt/{wpa2,lwip}/esp32c5` に置く。
  libsupplicant は C6 版と 3 .o が違う（dev 段4 Task 2）ので C6 の `.a` を流用しない。
- **idf_src の C5 原本**: A8（C6 の D8 例外 6 本 + lwIP contrib 3 本の C5 同名原本、
  `IMPORT_PROVENANCE.md` に出自）。
- **ROM ld と libc 供給**: minimal の 2 本から wifi-connect の本数へ広げるとき、C6 段3 の勝者表
  （`{rand, md5_vector}` の交差）を C5 の `esp32c5.rom.*.ld` で採り直す。`nm -u` 空を段1 と同じ
  手法で確認。C6 段1 の M-6 parity gap（`chip_rom_libc.c` 相当）の扱いは C6 段3 の結論に従う。
- **APM 解除の置き場所**（A11）: dev の `c5_apm_unblock`（FUNC_CTRL x4 + TEE、既定 ON）を
  C6 の `c6_apm_unblock` と同じ位置で呼ぶ。対照（OFF で 0 AP）は段4。
- **`arduino_interrupt_c5` / `arduino_gpio_c5`** はまだどの stage にも入っていない（段1 は
  `-fsyntax-only` のみ）。wifi-connect stage に入れ、GpioInterrupt（G1）は段4 で実機。
- **表の 6 箇所を同時に `wificonnect` へ**（S1-C5-6）: `CHIPS` / `EXPECTED_PROFILES` / allowlist
  `prebuiltStages` / drift test / CI yml / `verify_package.py BOARD_PROFILES`。`--list-builds` 62 -> 68。
- **移し漏れ表**: dev C5 段4 の Task 0 差分 20 項目（port/measure/new/skip）を段3 の AC に写す。
- 実機の入口: 板は条件 A の Blink のまま。段4 の書込みは同じ台本（`erase 0x0-0x1FFF` は冪等）。

## 段3 の記録（2026-09-16、branch `feature/c5-arduino-stage3`、commit `057fadc`（Task 1）/ `dd5eb6f`（Task 2）+ 記録）

`wifi-connect` stage が建ち、`m5stampc5_fmp3:FMP3Runtime=wificonnect` で `WiFiConnect` /
`WiFiScan` / `Blink` / `LibraryInfo` / `NanoC6Gpio`（no-op）/ `GpioInterrupt`（G1）の 6 例題が
リンクを通った段。実機は使っていない（登記のみ、書込みは段4）。証跡は開発リポジトリ
`.steering/20260916-c5-arduino-plan/stage3/{AC.md,logs/}`（`task1-*` = Task 1、`task2-*` = Task 2。
`task2-supply-table.md` が移し漏れ表、`task2-rom-winners.txt` が ROM ld 勝者一覧。dev 側は未 commit）、
実装報告は本リポジトリ `.superpowers/sdd/PLAN-stage3-impl/task-1-report.md`。

- **Task 1**（`057fadc`）: dev `c7fef18..1d96bcba` の shim 差分をそのまま適用（S3-1）、C5 の esp-idf
  原本 3 本、`.a` x4（DNS 版 lwIP は dev 台本で生成）、CMake の chip 表 wifi 行（ROM ld 13 本）。
- **Task 2**（`dd5eb6f`）: adapter の C5 分岐、`app/wifi_connect_c5/`、scripts の 6 表、6 例題のリンク。

### AC 3a-3l

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 3a | `build_prebuilt_stages.py --chip esp32c5 --profiles wifi-connect --clean` rc=0、stage（objs/ld/lib/manifest/rsp）、重複定義監査 PASS、`check_host_paths.py` rc=0、manifest の `romLinkerScripts` 13 本 | PASS | `task2-build-wifi-connect-1.txt`（85 objects / 837 strong / 0 duplicated / 0 allowed、`romLinkerScripts` = `esp32c5.rom.ld` `esp32c5.rom.api.ld` + dev `A1_C5_ROM_LD_WIFI_LIST` 11 本の順、eco3 無し、`linkBaseFlags` 末尾 `-Wl,-e,seam_c5_entry_boost`、`lib/` に `.a` 4 本、`check_host_paths` 92 files PASS）。commit 後の `--clean` 再ビルドは 92 本中 91 本 sha 同一（差は `objs/banner.o`、`task2-build-wifi-connect-2-committed.txt`） |
| 3b | X-check 9/9 MATCH（Task ごとに `--clean` 再ビルド後）。`git diff --stat 755a833 -- ports/m5stack_xtensa src third_party` 空 | PASS | `task1-xcheck.txt`（Task 1 末）、`task2-xcheck.txt`（Task 2 commit 後。`--strict` の差は 9 stage とも `banner.o` のみ。`examples/` の差は `GpioInterrupt.ino` の C5 ピン 5 行だけ） |
| 3c | platform 再生成で `m5stampc5_fmp3` の FMP3Runtime に `wificonnect`。既存 4 板の `boards.txt` 行と `platform.txt` は不変 | PASS | `task2-install-platform.txt`（5 板・11 stage。`boards.txt` の差は `m5stampc5_fmp3.menu.FMP3Runtime.wificonnect` の 3 行の追加のみ、`platform.txt` / `programmers.txt` は cmp 同一、`listall` に 5 板） |
| 3d | `m5stampc5_fmp3:FMP3Runtime=wificonnect` で 6 例題 rc=0、C-1..C-9 PASS、最終 ELF の `nm -u` 空 | PASS | `task2-compile-c5-{WiFiConnect,WiFiScan,Blink,LibraryInfo,NanoC6Gpio,GpioInterrupt}-wificonnect.txt`（C-8: WiFiConnect **548816**、WiFiScan **487312**、Blink 111552、LibraryInfo 111600、NanoC6Gpio 111552、GpioInterrupt 113920 / 1310720。C-9 `chip_id=0x0017`、rev 100。WiFiConnect は RAM セグメント 2 本で C-6 とも OK）。`task2-nm-u-elf.txt`（6 本とも `nm -u` 0。総記号 5013 / 4517 / 1610 / 1612 / 1607 / 1632） |
| 3e | 移し漏れ表: dev C5 供給表の非 blob 記号それぞれに arduino 側の供給元。UNRESOLVED 0 | PASS | `task2-supply-table.md`（203 行）、`task2-nm-u-stage.txt`（130 記号）。内訳は下記「供給表の要約」 |
| 3f | ROM ld 勝者一覧と、stage obj / `.a` の定義が置換された記号の交差 | PASS | `task2-rom-winners.txt`。内訳は下記「ROM ld 勝者」 |
| 3g | `size`・RAM 余裕（LENGTH 320,928）。`.iram1` 群が RAM セグメントに載る | PASS | `task2-size.txt`。内訳は下記「size と RAM」 |
| 3h | R5: 生成 `kernel_cfg.c` に Xtensa 線の inthdr が無く、C5 線 25..39 と線 23 がある。`TOPPERS_ESP32C5` を消す対照で cfg が止まる | PASS | `task2-r5-kernel_cfg.txt`、`task2-r5-control.txt`。内訳は下記「R5」 |
| 3i | `IMPORT_PROVENANCE.md` に全件、`THIRD_PARTY_NOTICES.md` に A8 の逸脱と `.a` の由来、`wifi/prebuilt/*/README.md` に C5 の sha256 | PASS | `ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`「ESP32-C5 段3 Task 1」「ESP32-C5 段3 Task 2」節（改変方針 10 を追加）、`THIRD_PARTY_NOTICES.md`「C5 段3」節、`wifi/prebuilt/{wpa2,lwip}/README.md` の C5 節 |
| 3j | minimal の非退行 | PASS | `task2-minimal-sha-compare.txt`（`--clean` 再ビルドで 51 本中 50 本 sha 同一、差は `objs/banner.o`）、`task2-compile-c5-{Blink,LibraryInfo}-minimal.txt`（rc=0、Blink 21664 / 21488 B = 段1 と同じ） |
| 3k | **C6 wifi-connect の objs が byte 不変**（shim の共有ファイルを触った直後） | PASS | `task1-xcheck-after-shim-diff.txt`（shim 差分の適用直後・CMake を触る前に esp32c6 の 2 stage を `--clean` で建て直し 9/9 MATCH = dev の同行数編集の検証） |
| 3l | **dev diff の適用が clean** | PASS | `task1-apply-shim-diff.txt`（13 + 1 ファイル `git apply` で hunk 失敗 0、手編集なし。適用後の shim 34 本 + `FreeRTOS.h` が dev `1d96bcba` と `cmp` 同一） |

### 判断 S3-1..S3-6 の結果

| # | 判断 | 結果 |
| --- | --- | --- |
| S3-1 | shim の C5 分岐 | dev の差分をそのまま当てた（3l）。共有ファイル 6 本は同じ行数の `#if` 書換え・空行 1 本の `#include "*_c5.inc"` 置換・`esp_shim.h` の EOF 追記だけ。C6 の objs 不変（3k）。`esp_shim_intr_intmtx.*`（C6）と `esp_shim_intr_c5.*`（C5）は並置し、CMake の chip 表 `A1_CHIP_WIFI_INTR_CFG` / `_SOURCE` が片方を組む |
| S3-2 | lwIP | A7 = dev 型 + `LWIP_DNS=1`。dev 台本 `build_lwip_lib_espidf_esp32c5.sh` を `PORT_EXTRA` / `OUT_DIR` で走らせた（既定引数の対照で dev golden の sha `85859f70...` を再現 = 台本の決定性、dev tree は clean のまま）。結果は C6 の DNS 版 `liblwip.a`（`5bfbc3ef...`）と**バイト同一**（lwIP は chip 非依存）。`hostByName` は C6 段4 の DNS 実装（`toppers_wifi_connect.c`、共有）をそのまま使う |
| S3-3 | 割込み | dev の `esp_shim_intr_c5.*`（blob の `intr_num` 1..15 -> CLIC 線 25..39）。`arduino_interrupt_c5`（線 23）は `esp_shim_intr_c5_lines.h` の範囲検査を `#error` で通っている（コンパイル時）。R5 で線表を確認（3h） |
| S3-4 | APM | vendored shim の `esp_wifi_adapter_c5.inc`（`c5_apm_unblock`: FUNC_CTRL x4 + TEE 32 master）。CMake option `TOPPERS_C5_APM_UNBLOCK`（既定 ON）が `-DA1_C5_APM_UNBLOCK=1 -DA1_C5_APM_FUNC_CTRL=1 -DA1_C5_APM_TEE=1` を渡す（A11）。adapter は APM 読み戻し（`esp_wifi_adapter_c5_apm_readback`、scan 後）を呼ぶだけ。OFF の配線は `task2-apm-off-control-wiring.txt`（別出力先で建てると `-DA1_C5_APM*` が消え `esp_wifi_adapter.o` から `before-unblock` の文字列が消える、`build/prebuilt` は不変） |
| S3-5 | esp-idf 原本 | A8 = vendored。C5 の同名原本 3 本（`phy_init_data_esp32c5.c` / `modem_clock_hal_esp32c5.c` / `efuse_hal_esp32c5.c`、esp-idf v5.5.4、無改変）。chip 非依存の 3 本と lwIP contrib ヘッダ 3 本は C6 と共有。段5 で再評価 |
| S3-6 | 5 GHz | blob 既定の両帯 scan。adapter（C5 のみ）が scan 後に `[WiFiScan] bands: 2.4GHz=N 5GHz=M (of K listed)` を出す（ch > 14 = 5 GHz、dev と同じ規則）。AC は「N>0」、5 GHz の有無は記録のみ（段4） |

### vendored inventory の要約（C5 で増えた分）

| 出自 | 本数 | 内訳 |
| --- | --- | --- |
| dev `1d96bcba`（バイト同一） | 8 + 4 | shim 7（`esp_wifi_adapter_c5.inc` `esp_shim_blobglue_c5.inc` `esp_shim_intr_c5.{c,cfg,h}` `esp_shim_intr_c5_lines.h` `IMPORT_PROVENANCE_c5.md`）+ shim の変更 6 本（差分適用後 dev と同一）+ `FreeRTOS.h` 1 行、prebuilt `.a` 4（`libsupplicant.a` は C6 と 3 .o 違い、他 3 本は C6 とバイト同一） |
| esp-idf v5.5.4 `735507283d` 原本（A8 = D8 の逸脱） | 3 | `idf_src/{phy_init_data,modem_clock_hal,efuse_hal}_esp32c5.c`（Apache-2.0、ファイル名のみ chip 接尾辞） |
| arduino 側の派生（新規） | 3 + 改変 | `app/wifi_connect_c5/`（cfg の INCLUDE 1 行が C6 と違う）、adapter 4 本の C5 分岐、`netif_esp32s3.c` の alias 3 行（改変方針 10）、`GpioInterrupt.ino` の C5 ピン |

C6 の 99 本（hal_src / freertos_stub / net / config / hal_stub_include / lwIP contrib）はそのまま共有。

### 供給表の要約（AC-3e、`task2-supply-table.md`）

dev C5 `blob-supply-table.md` の非 blob 記号 203（class a 13 / b 175 / c 2 / d 10 / g 3）を
WiFiConnect 最終 ELF の link map / `nm` で 1 記号ずつ引いた結果:

| arduino 側の供給元 | 件数 |
| --- | --- |
| ROM ld（13 本のどれかの代入） | 175 |
| stage obj | 16（`WIFI_EVENT esp_wifi_connect esp_wifi_disconnect`（wifi_init.o）、`coexist_printf free puts sprintf`（esp_shim_libc.o）、`esp_event_handler_register`、`g_espnow_user_oui putchar rtc_clk_xtal_freq_get`（esp_shim_blobglue.o）、`net80211_printf pp_printf phy_printf`、`regdomain_table regulatory_data`） |
| stage `.a`（`hexstr2bin` = libsupplicant） | 1 |
| toolchain（`floor` `log10` = `libm_nano.a`） | 2 |
| gc'd（定義は在るが参照する側ごと `--gc-sections` で落ちた: `esp_event_handler_unregister esp_mesh_send_event_internal g_mt mt_get_peer_info mesh_printf snprintf`、dev の未解決 3 = eco3 の弱参照 2 + NAN の `strtok` は参照元の blob メンバごと不在） | 9 |
| UNRESOLVED | 0 |

175+16+1+2+9 = 203。dev C5 段4 Task 0 の 20 項目（`asp3-c5-inventory.md` 1 節）は port 側が
全部 vendored shim の中（線制御 / RNG / eFuse / `g_misc_nvs` / MODEM_SYSCON 番地 / `rtc_clk_xtal_freq_get` /
`esp_clk_tree_enable_src` の縮小 / APM+TEE / eco3 空実装 / `putchar` / `log10` = `-lm`）、measure 3 項目
（ICG / regi2c / LPCON）は dev が「入れない」と決めた形のまま、skip 4 項目は arduino でも持ち込まない
（`wifi_trace`、`0x314` 書込み、C5 固有 5 ソース、`MALLOC_CAP` の `-D`）。240 MHz は段1 の seam entry。
stage 単位の `nm -u`（objs の未定義 - objs の定義）は **130**（C6 は 132。差は `esp_wifi_adapter_c5.inc` が
`__errno` / `putchar` を持つ分）で、130 本すべてに WiFiConnect / WiFiScan それぞれの最終供給元が付く
（WiFiConnect: ROM ld 17 / SDK blob 49 / stage `.a` 30 / xip・peripherals ld 26 / stage obj 1 / toolchain 1 /
not in ELF 6（scan API 5 + `phy_i2c_master_command_mem_cfg`、C6 と同じ）。unknown 0）。

### ROM ld 勝者と `{rand, md5_vector}` の交差（AC-3f）

WiFiConnect 最終 ELF で「13 本の ROM ld が代入する名前」かつ `A` の記号は **1002**（pp 400 / rom.ld 327 /
libgcc 92 / net80211 60 / libc 39 / newlib 32 / coexist 24 / systimer 13 / libc-suboptimal 7 / **phy 6** /
version 2。うち 25 は `esp32c5.rom.pp.ld` の `//name = addr;` 行が作る実害の無い `//` 付き記号）。
C6 の 1129 との違いは主に **`esp32c5.rom.phy.ld` が 6 本しか無い**（C6 は 224）こと = C5 の PHY は blob の
RAM/flash 版が走る、逆に pp は ROM 版が増える（dev 段4 3-3 節の `.data` 20 KB 減の理由と同じ）。

- **stage obj との交差 = `rand`**（`esp_shim_libc.o` の定義に `esp32c5.rom.newlib.ld` の `rand = 0x400005a0` が勝つ）。
- **stage `.a` との交差 = `md5_vector`**（`libsupplicant.a(wpa_crypto_mbedtls.o)` に `esp32c5.rom.ld` の
  `0x40000794` が勝つ）。dev C5 の 2 記号（`romld-winners.txt`）と同じ、C6 とも同じ。
- **どちらも到達不能**（実測）: `rand` の参照元は `libmbedcrypto.a(mb_rsa.o)` だけで map の Discarded に
  59 本（memory map 0 本）、`md5_vector` の参照元 `libsupplicant.a(wpa_chap.o)` は map に 1 行も無い
  （メンバごと引かれていない）。
- `syscall_table_ptr`（`0x4085ffd4`）/ `_global_impure_ptr`（`0x4085ffd0`）を張る者は居ない（C6 段3 と同じ
  未解決ハザード。`random()` を呼ぶスケッチで顕在化しうる、段4 以降の watch item）。
- 参考: ROM 勝者と toolchain（libc_nano / libm_nano / libgcc）の共通 165、6 blob との共通 342。

### size と RAM 余裕（AC-3g）

| 例題 | image bytes（C-8） | RAM（LOAD の末尾 - 0x40800000） | 余裕（320,928 B に対し） |
| --- | --- | --- | --- |
| WiFiConnect | 548816 / 1310720 | **287,744 B**（89.7%） | **33,184 B** |
| WiFiScan | 487312 / 1310720 | 249,824 B | 71,104 B |
| Blink / LibraryInfo / NanoC6Gpio（wificonnect） | 111552 / 111600 / 111552 | 46,736 B | 274,192 B |
| GpioInterrupt（wificonnect） | 113920 | 46,928 B | 274,000 B |

WiFiConnect の `size -A`: `.text` 417,008、`.flash.rodata` 87,760、`.data` 43,696、`.bss` 244,032。
dev `wifi_sta` 像（286,528 B、余裕 34,400 B）と同程度（+1.2 KB = adapter と DNS）。**余裕 33 KB は
C6（150 KB）より小さい**: `.bss` の shim heap（98,784 B）と lwIP / syslog 256 本が固定で載る。
段4 の `[SHIM-HEAP]` 高水位（dev AC-6d 相当）を見ること。`.iram1` / `.wifi*iram` / `.coexiram` /
`.phyiram` の input section 139 本はすべて `.data` 出力セクション（RAM LOAD）に載り、flash 側に落ちた物は 0
（C-6: `ram seg1` / `ram seg3` とも `.data` と一致）。IDE の `Global variables use ... Maximum is 320928 bytes`
は C5 では ld の `RAM LENGTH` と同じ値（A9）なので、C6 と違って % がそのまま読める。

### R5: define が cfg 経路に届いている（AC-3h）

- **正側**: 生成 `kernel_cfg.c` に `esp_shim_inthdr_*`（Xtensa 線）0 件、`esp_shim_intmtx_inthdr_*`（C6 線）0 件、
  `esp_shim_c5_wifi_inthdr_1..15` が線 25..39（`0x10019..0x10027`）、`_kernel_inthdr_17`（USJ）、
  `_kernel_inthdr_23`（attachInterrupt の動的 ISR 枠）。`TNUM_CFG_INTNO 18`（15 + tick 16 + USJ 17 + 23）。
- **対照**: `chip_stddef.h` の `#define TOPPERS_ESP32C5` を 1 回だけコメントアウトし scratch へ建てると
  **`esp_shim.cfg:244-245: error: E_OBJ: intno '23'/'27' is duplicated in CFG_INT`** で cfg が止まり rc=1
  （S3/LX6 の線 23 が `ARD_GPIO_INTNO` 23 と衝突する）。直後に `git checkout` で復元、`build/prebuilt` は不変。
  （最初の試行は sed が行末コメントに一致せず define が残ったまま通った = 対照になっていない。ログに明記。）

### 正直な失敗の一覧（Low#）

| # | 対象 | 挙動 | 理由 |
| --- | --- | --- | --- |
| Low#1（dev） | `esp_sleep_pd_config` / `esp_sleep_clock_config`（`esp_shim_blobglue_c5.inc`） | `ESP_ERR_NOT_SUPPORTED` | 省電力は射程外（dev IMPORT_PROVENANCE_c5.md 11 節） |
| Low#2（dev） | `putchar` | -1 | C6 と同じ |
| Low#3（dev） | `esp_clk_tree_enable_src` | 原本の未初期化経路と同値の `ESP_OK`、`pll_160m_clk_en`=0 なら syslog | ゲート管理は未実装 |
| （設計） | `attachInterrupt()` の未対応モード | attach せず WARNING | C6 と同じ（`arduino_interrupt_c5.c`） |
| （既知・未検証） | Open AP、WPA3、5 GHz 帯への接続 | driver に渡す（NOTICE の文言は C6 のまま） | 段4 は WPA2-PSK のみ実測 |
| （既存） | `toppers_wifi_optional_stubs.c` の weak 群 | Xtensa と同じ失敗値 | wifi-connect では本体が勝つ |

C6 段3 の Low#1（DNS 名前解決 0）は無い（C6 段4 の DNS 実装と DNS 版 `liblwip.a` を最初から持つ）。
成功扱いのスタブは足していない。

### 段4 の入口条件

- **creds はスケッチに書く**（`examples/WiFiConnect/WiFiConnect.ino` の `WIFI_SSID` / `WIFI_PASSWORD`、
  作業コピーで）。手順は C6 段3「段4 の入口条件」と同じ: 値は dev `esp/boot/wifi_credentials.sh` と一字一句同じ
  （`scripts/capture_c5_usj.sh` の redact 針はそこからしか作られない）、`LOG_DIR` を
  `.steering/20260916-c5-arduino-plan/stage4/logs/` に、採取後ダミーへ戻し、**`git add` の前に
  `git diff --exit-code examples/WiFiConnect/WiFiConnect.ino`**（本リポジトリに secret guard は無い）。
- **APM OFF 対照**: `--cmake-define TOPPERS_C5_APM_UNBLOCK=OFF` を **必ず `--output-directory` を分けて**
  （既定の `build/prebuilt` へ建てると次の `install_platform.py` が対照を本番に据える。C6 段3 と同じ罠）:
  ```bash
  python3 scripts/build_prebuilt_stages.py --chip esp32c5 --profiles wifi-connect \
      --cmake-define TOPPERS_C5_APM_UNBLOCK=OFF --output-directory <separate dir>
  ```
  配線は段3 で確認済み（`task2-apm-off-control-wiring.txt`）。dev の 1 軸対照（`A1_C5_APM_FUNC_CTRL` /
  `A1_C5_APM_TEE` を片方ずつ）は本リポジトリの option には無い（両方 ON か両方 OFF）。
  `TOPPERS_C5_WIFI_DIAG=ON` / `TOPPERS_C5_NET_DIAG=ON` も同じ経路で別出力先へ。
- **WiFiScan**: `[WiFiScan] found N APs` の N>0 と `[WiFiScan] bands: 2.4GHz=... 5GHz=...` を記録（S3-6）。
  近隣 AP の SSID は runtime のログには出ない（`<SSID-N>`）が、スケッチ側の `WiFi.SSID(i)` の印字は出る
  = 採取ログの scan 行を文書へ貼らないこと。
- **GpioInterrupt（G1）**: `examples/GpioInterrupt` は `ARDUINO_M5STACK_STAMP_C5` で `PROBE_PIN 1`。
  線 23 の配線・`acre_isr` は未計測（C6 W-3 と同型）。
- **RAM 余裕 33 KB**: 段4 で `[SHIM-HEAP]` の高水位を採る（dev は `ESP_SHIM_HEAP_STATS` の計器を持つ）。
- **C6 段3 の watch item W-1..W-4**（scan 無しの connect、scan 後の stop->start サイクル、線の未発火、
  `stage_log` の `logtask_flush`）は C5 にもそのまま当てはまる。adapter のマーカー文字列も C6 と同じ
  （`docs/c6-port.md`「adapter が出すマーカー文字列」）+ C5 の `bands:` 行。
- 実機の入口: 板は段2 の条件 A（minimal Blink）のまま。書込みは `scripts/capture_c5_usj.sh`
  （`erase 0x0-0x1FFF` は冪等）。
