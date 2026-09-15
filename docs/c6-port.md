# ESP32-C6（M5NanoC6）の統合 -- 判断の記録と段ごとの到達点

- 作成: 2026-09-15（段0）。起点は本リポジトリ `fb57c9e`（Open 0.4.2）と
  開発リポジトリ `fmp3_esp_idf_dev` の `c7fef18`（C6 の段0-5 完了）。
- 位置づけ: **C6 統合に関する判断（D0-D11）と、段ごとに何がどこまで動いたかの正本。**
  調査の事実（file:line つき）と計画そのものは開発リポジトリ側の
  `.steering/20260915-c6-arduino-plan/{INVESTIGATION,PLAN}.md` にあり、
  ここには判断と結果だけを置く（両方に書くと片方が古くなる）。
- 段0 はコードを足していない。足したのは X-check（後述）とこの文書と出自の宣言だけで、
  `ports/`・`src/`・`examples/`・`third_party/`・既存 `scripts/*.py` の挙動は不変。

## 目的

M5Stack Arduino core 3.3.8 を入れた利用者が、`ToppersFMP3` パッケージで
**M5NanoC6 (TOPPERS/FMP3)** を選び、`setup()` / `loop()` を FMP3 のタスクとして動かし、
`ToppersFMP3_WiFi` で scan / STA 接続 / DHCP / DNS / TCP が使えること。
そのあいだ、**Xtensa 3 板（CoreS3 / M5StickS3 / M5Stack Basic）の配布物は変えない。**

## 出自（provenance）

| 項目 | 値 |
| --- | --- |
| C6 ソースの基準 | 開発リポジトリ `https://github.com/exshonda/fmp3_esp_idf_dev.git` の `c7fef186d3b98e9046005a3f3ab0f2dfb1a2fdfe`（2026-09-15） |
| 宣言場所 | `packaging/release-allowlist.json` の `portBaseRepositoryC6` / `portBaseCommitC6` / `portBaseCommitDateC6`（既存の `portBase*` は Xtensa 側の出自で、不変） |
| 公開スナップショット #8 | **未公開。** 公開リポジトリ `toppers/fmp3_esp_idf` の現行 `fdd89f8`（#7）には C6 が無い。#8 が出たら上記 2 キーを公開側へ差し替える（ユーザー操作） |
| fmp3_core | `685b36a`（本リポジトリの submodule と開発リポジトリの submodule が同一） |
| ESP-IDF / toolchain | M5Stack core 3.3.8 同梱の `esp32c6-libs` は `v5.5.4 735507283d`、`esp-rv32/2601` は `esp-14.2.0_20260121`。開発リポジトリの submodule・toolchain 固定と同一 |

wpa2 の `.a`（`ports/m5stack_xtensa/runtime/wifi/prebuilt/wpa2/README.md`）が
開発リポジトリを直接出自にしている前例に倣った。

## 判断 D0-D11（仮決定。ユーザーが上書きしたら該当段の計画と本表を直す）

「本リポジトリ側の箇所」は `fb57c9e` 時点の file:line。段1 以降で行がずれるので、
以後は名前（関数名・キー名）で引くこと。

| ID | 判断 | 仮決定 | 本リポジトリ側の箇所 | 外れたときの費用 |
| --- | --- | --- | --- | --- |
| D0 | 出自 | 開発リポジトリ `c7fef18` を直接出自にする。公開 #8 は別途ユーザー操作 | `packaging/release-allowlist.json:3-6`（Xtensa の出自）、同ファイルの `*C6` キー | #8 が出たら出自欄を差し替えるだけ |
| D1 | bootloader | **確定（stock M5Stack bootloader、同梱なし）**。段2 で条件 A（stock bootloader + stock `default` ptable + boot_app0、160 MHz）warm 5/5・真cold 9/10 を実機実測し成立。B（開発側 seam bootloader）/ C（+開発側 ptable）/ 80 MHz は**未実施**（A が成立したため不要。詳細は「段2 の記録」節） | 本リポジトリは bootloader を出荷していない（`BUILDING.md`・`README.md`・`packaging/README.release.md`・`scripts/install_platform.py` に bootloader の記述なし）。M5Stack platform.txt の prebuild hook を継承 | 同梱経路 = 板別の prebuild hook 上書き |
| D2 | port ディレクトリ | `ports/m5stack_riscv`。**段1 で確定・実装済み**（`ports/m5stack_xtensa/` は完全に不変。X-check 7/7 で実測） | `ports/m5stack_xtensa/` と並ぶ。`scripts/build_prebuilt_stages.py:161`（runtime）・`:195-198`（app）の固定パスを chip -> port の表にする | 改名は機械的 |
| D3 | 共有 cmake | `prebuilt_stage.cmake` は共有して chip 分岐、chip 固有（seam 画像検査等）は `prebuilt_stage_c6.cmake` へ分離。**段1 で確定・実装済み、ただし実際には `prebuilt_stage.cmake` 自体は無改変**（chip -> port の表（Task 1）だけで C6 の runtime CMakeLists が自分の `prebuilt_stage_c6.cmake` を呼ぶため、Xtensa 側への白リスト追加も委譲コードも不要だった。委譲は「行を足す」形ではなく「表の port 列」で実現） | `ports/m5stack_xtensa/runtime/cmake/prebuilt_stage.cmake:57-62`（`A1_CHIP` 白リスト）ほか | 二重保守 vs 波及。どちらも X-check が検出 |
| D4 | manifest / driver 版 | **上げる**（`DRIVER_VERSION` 3、schema に `paddrMode` の新値と `linkBaseFlags` を加法で追加）。**段1 で確定・実装済み**（`DRIVER_VERSION="3"`、`MANIFEST_SCHEMA=2`、`SUPPORTED_MANIFEST_SCHEMAS=(1,2)`。schema 1 は literal のまま不変、schema 2 は `linkTailFlags`（任意キー、ledger に無かったが Task 1 が追加）も持つ） | 段1 完了時点（fix wave 1）の `scripts/fmp3_link.py`: `:87`（`DRIVER_VERSION`）・`:92-93`（`MANIFEST_SCHEMA` / `SUPPORTED_MANIFEST_SCHEMAS`）・`:98-99`（`PADDR_MODES`、schema 1 は `runtime-mmu` のみ）・`:105`（`SCHEMA1_LINK_BASE_FLAGS` = `-nostdlib -mlongcalls`）・`:106`（`SCHEMA1_LINK_TAIL_FLAGS` = `-lgcc -lc`）。以後は名前で引くこと | 旧 manifest を読む経路が無いことを確かめて戻せる |
| D5 | C6 の CPU クロック | **確定（160 MHz）**。段2 で条件 A を 160 MHz のまま warm 5/5・真cold 9/10 を実機実測し成立、80 MHz へのフォールバックは不要（**S1-6 の watch item は解消**） | -- | -- |
| D6 | OPEN AP の私的 ABI（`g_ic+0x1b4`） | **C6 では表を差し込まない。** 開発側と同じく `esp_wifi_init` に supplicant を任せる（開発側で STA/DHCP/ping 実測済み）。`--wrap=esp_supplicant_init` の経路は Xtensa 側を触らない。C6 の Open AP は**段4 で実測するまで対応を主張しない** | `ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_core.c:30`（offset）、`BUILDING.md:288-294`（OPEN/WPA 分離） | Open AP が要るなら C6 blob の offset を求め直す（別作業） |
| D7 | lwIP | **開発側の型（自前 `liblwip.a` + `netif_esp32s3.c` / `port/sys_arch.c`）**で通し、core の `liblwip.a` へ寄せるのは後 | Xtensa 側は core の `liblwip.a` + `ports/m5stack_xtensa/runtime/wifi/net/` の別系統 | 段3 でリンクが通らなければ Xtensa 型へ |
| D8 | esp-idf 原本 6 本 | **開発側と同じく vendored**（provenance と改変境界を記録）。`BUILDING.md:278`「ESP-IDF を複製しない」からの**逸脱として明記**し、段5 で core の `.a` メンバ + `vPort*` シム案を再評価 | `BUILDING.md:278` | 段5 の再評価で置換 |
| D9 | C3 / C5 | 板は足さない。`BOARDS` / `--chip` / `PROFILES` を表駆動にするだけ | `scripts/install_platform.py:66`（`BOARDS`）・`:311`（`--chip`）、`scripts/verify_package.py:77`（`PROFILES`）・`:91`・`:97` | -- |
| D10 | 板 id / 表示名 | `m5nanoc6_fmp3` / `M5NanoC6 (TOPPERS/FMP3)` | 既存 `m5cores3_fmp3` の型（`scripts/install_platform.py:66-73`） | -- |
| D11 | C6 の profile | `minimal` + `wifi-connect`（m5-unified / bt-classic 無し。C6 target は `TNUM_PRCID` 1 以外を `#error` にする） | `scripts/install_platform.py:123`（`EXPECTED_PROFILES`）、`ports/m5stack_xtensa/runtime/CMakeLists.txt:287-289`（`FMP3_PRC_NUM`） | -- |

他に段1 で触る共有箇所（file:line は上と同じ時点）: `scripts/build_prebuilt_stages.py:111`
（`--chip` の選択肢）・`:140`（`esp-x32` / `xtensa-{chip}-elf-gcc`）、
`scripts/arduino_sdk.py:87`（`xtensaCoreIsa` を必須にしている。C6 の SDK には
`include/xtensa` が無い）、`scripts/make_package_index.py:53`（tool 依存。`esp-rv32` と
`esp32c6-libs` を書き漏らすと clean な機械でだけ失敗する前例がある）、
`scripts/install_platform.py:287-290`（`recipe.size.regex` が Xtensa の ld のセクション名）、
`.github/workflows/verify-package.yml`（`for chip in esp32s3 esp32`）、`library.properties`。

## 不変条件（開発側 C6 計画から引継ぎ、`CLAUDE.md` と合流）

- **Xtensa 3 板の配布物不変は X-check で毎段示す**（下記）。共有スクリプトを触る段は、
  作業前に baseline、作業後に compare。
- creds は開発リポジトリの `esp/boot/wifi_credentials.sh`（PC ローカル）からのみ。
  本リポジトリのスケッチには**ダミー**を書き、実 creds は採取台本の環境変数経由で
  伏字化の配下に置く。**SSID / BSSID / 割当 IP / AP の MAC を commit・文書化しない**
  （`CLAUDE.md`「出してはいけないもの」と同じ）。
- `-Wl,--allow-multiple-definition` 下の勝者は `nm` / `readelf` で記録する
  （リンクが通ることは証明にならない。`BUILDING.md`「変更するときに守ること」）。
- 両リポジトリとも origin へ push しない。本リポジトリは `feature/c6-*` ブランチで作業し、
  段ごとに `main` へ ff。`--amend` はしない。

## 射程外

- BLE。
- C3 / C5（表駆動化のみ行い、板は足さない = D9）。
- fmp3_core への upstream。
- 公開スナップショット #8 の push そのもの（ユーザー操作）。
- Windows 側テスト（`scripts/Test-*.ps1`）の C6 対応。既存 15 本は CoreS3 前提で、
  C6 は当面「対象外」。

## X-check -- Xtensa 3 板の配布物が変わっていないことの機械判定

C6 統合は `build_prebuilt_stages.py` / `prebuilt_stage.cmake` / `fmp3_link.py` /
`install_platform.py` といった**共有ファイル**を触る。「Xtensa は変えていない」は
バイト列で示す。

```bash
# 段の起点 commit で、作業ツリーが clean なうちに 1 回（共有ファイルを触る前）
python scripts/xcheck_baseline.py            # 7 stage を建て build/xcheck-baseline/ へ退避
# 作業後に stage を建て直してから
python scripts/build_prebuilt_stages.py --chip esp32s3 --profiles minimal m5-unified wifi-connect
python scripts/build_prebuilt_stages.py --chip esp32 --profiles minimal m5-unified wifi-connect bt-classic
python scripts/xcheck_compare.py             # 7/7 MATCH で rc=0、1 つでも DIFF なら rc=1
python scripts/xcheck_compare.py --strict    # banner.o も比べる（毎回 DIFF になるのが正常）
python scripts/test_xcheck.py                # 判定器の自己テスト
```

- **baseline の規律。** baseline は「段の起点 commit・clean な作業ツリー」で採る。
  `xcheck_baseline.py` は `build/xcheck-baseline/BASELINE.json` に採取時の HEAD・dirty の有無
  （`git status --porcelain` の件数。パスは記録しない）・採取時刻・chip / profile / stage の
  一覧を書き、既に baseline があれば `--force` 無しでは上書きしない。`xcheck_compare.py` は
  先頭にその HEAD と dirty を出し、現在の HEAD が違えば警告する（失敗にはしない）。
  **段1 で `build_prebuilt_stages.py` / `prebuilt_stage.cmake` / `fmp3_link.py` /
  `install_platform.py` を編集したあとに baseline を採り直してはいけない** -- 編集後の
  生成物どうしを比べても、編集が Xtensa の配布物を変えたかどうかは分からない。採り直すのは
  段が終わって `main` へ ff した後、次の段の起点でだけ。
- 比べるもの（stage = `<chip>/<profile>` ごと）: `link-manifest.json`（バイト一致。違えば
  JSON として読み、**時刻系キーだけの差なら MATCH（注記つき）**、整形やキー順だけの差でも
  DIFF）、`objects.rsp`、`objs/*.o`（sha256。`objs/banner.o` だけは `--strict` 無しでは除外
  -- `__DATE__` / `__TIME__` で毎回変わるため。他の場所の `banner.o` は除外しない）、
  `lib/*.a`、その他のファイル（`ld/` 等）。片側にしか無いファイル・stage は差分。
  **期待する stage（`BASELINE.json` の一覧、無ければ `build_prebuilt_stages.py` の表から
  導く 7 本）が両側とも無ければ rc=1、比較対象が 0 件でも rc=1**（比較していないことを
  成功と読まない）。出力末尾は `expected=N compared=N match=M diff=K`。
- 出力は stage からの相対パスだけを持つ（`check_host_paths.py` で 0 件を確認済み）。
- **X-check が見るのは配布されるバイト列で、ソースの文面ではない。** stage は debug 情報を
  落として配るので、`__LINE__` / `__FILE__` を即値にしない `.c` へコメントを足しても
  MATCH のまま（段0 で実測）。逆に言えば、MATCH は「配布物が同一」の証明であって
  「ソースを触っていない」の証明ではない。
- 段0 の実測（この機械。全 7 stage の clean build は 24 s）:
  baseline 直後 7/7 MATCH。clean で建て直しても 7/7 MATCH、`--strict` では 7 stage とも
  `banner.o` だけが DIFF（= それ以外は決定的）。positive control として
  `wifi/hal_src/phy_common.c` の `ESP_ERROR_CHECK`（`__LINE__` 即値）の上にコメント 1 行を
  足して `esp32s3/wifi-connect` を建て直すと `objs/phy_common.o` が 1 行で名指しされ rc=1、
  戻すと 7/7 MATCH に戻る。

## 実機

- M5NanoC6 1 枚。コンソールは USB Serial/JTAG（USJ）で、書込みと採取が同一ポート。
- 段2・段4・段6 だけが実機を要する。書込みは gated な台本のみ（段2 で本リポジトリの
  成果物向けに作る。開発側の `esp/boot/flash_and_capture_c6_usj.sh` は開発側の
  ディレクトリ構成が前提）。
- 実機ログを文書化するときは、上記の不変条件（SSID / BSSID / IP / AP MAC を書かない）に従う。
  Wi-Fi の単発失敗の切り分けは `CLAUDE.md`「実機の単発失敗を実装のせいにしない」。

## 段1 の記録（2026-09-15、commit `07b239b` / `709b36a` / `ffefc52` / `5dbb8d1` / `f40490e` + 最終レビュー是正 fix wave 1）

C6 の `minimal` stage が建ち、`m5nanoc6_fmp3:FMP3Runtime=minimal` で 3 例題スケッチが
リンクを通った段。実機は使っていない（登記のみ）。詳細な証跡は開発リポジトリ
`.steering/20260915-c6-arduino-plan/stage1/reports/task-N-report.md`（N = 1..4、Task ごとの
実装報告）と同 `stage1/logs/`（本節の丸括弧はそこのログファイル名。`task<N>-*.txt` が
Task N の採取、`fw1-*.txt` が最終レビュー後の fix wave 1 の再採取。**同じ項目が両方に
あるときは `fw1-*` が現状**）。判定の表は同 `stage1/AC.md`。

### AC 1a-1h

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 1a | `build_prebuilt_stages.py --chip esp32c6 --profiles minimal` rc=0、stage 一式あり、重複定義監査 PASS | PASS | `fw1-build-c6-minimal.txt`（最終: 47 objects / 410 strong definitions / 0 duplicated、manifest の `romLinkerScripts` は 2 本。段中に `newlib_syscalls.c` が増えたため Task 2 時点の 46 objects から 1 増、fix wave 1 で ROM ld が 13 本から 2 本に減った。Task 3 時点は `task3-build-c6-minimal.txt`） |
| 1b | X-check 7/7 MATCH、Xtensa 側 `git diff --stat` が空 | PASS | `fw1-xcheck.txt（fix wave 1 で --clean 再ビルド後に再採取。段1 途中の値は task3-xcheck.txt）`（`expected=7 compared=7 match=7 diff=0`、`ignored (not in baseline): esp32c6`）。Task 1 の初回計測は stage を建て直さない再計測（ninja `no work to do`）だったため、**実質的な証跡は Task 2 の `--clean` 再ビルド後の 7/7 MATCH**（`task2-rebuild-esp32{s3,}.txt`、`task2-xcheck.txt`） |
| 1c | `install_platform.py` で 4 板が組め、`arduino-cli board listall` に `m5nanoc6_fmp3` が出る | PASS | `fw1-install-platform.txt`、`task3-board-listall.txt`。platform.txt / boards.txt の不変性の再現手順は後述「platform.txt / boards.txt の再現」 |
| 1d | 3 例題（Blink/LibraryInfo/TwoFileSketch）が compile rc=0、C-1..C-8 PASS | PASS | `fw1-compile-c6-{Blink,LibraryInfo,TwoFileSketch}.txt`（ROM ld 2 本の stage。Task 3 時点の 13 本は `task3-compile-c6-*.txt`） |
| 1e | `check_host_paths.py` rc=0、`test_fmp3_link_objects.py` PASS（schema 2 ケース込み）、旧 schema 1（Xtensa 板）も同じドライバで compile rc=0 | PASS | `task3-check-host-paths.txt`、`fw1-tests.txt`、`fw1-xtensa-blink-driver2-vs-3.txt`（driver 2 と 3 で ELF/BIN とも sha256 同一） |
| 1f | リンク前 `nm -u` の一覧と勝者一覧の記録 | PASS | `task2-nm-u-stage.txt`（stage 単体）、`fw1-link-winners-2ld.txt`（3 例題 + SspProbe、ROM ld 2 本）、`fw1-romprobe-winners.txt`（newlib 系を呼ぶ probe、2 本 vs 13 本の対照）。Task 3 時点（13 本）は `task3-link-analysis-*.txt`。詳細は後述「ROM linker script の勝者」節 |
| 1g | `IMPORT_PROVENANCE.md` に dev 由来ファイル全件、本文書に段1 の記録 | PASS | `ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`、本節 |
| 1h | `--show-properties` の `recipe.size.regex` が C6 のセクション名を含む | PASS | `task3-size-regex.txt`（板別 override が効き、platform.txt 側 regex の拡張は不要だった） |

### 判断 S1-1..S1-7 の結果

| # | 判断 | 結果 |
| --- | --- | --- |
| S1-1 | `paddrMode` に `fixed-vma` を新設 | 実装どおり。ドライバは `runtime-mmu`（Xtensa）と `fixed-vma`（C6）の 2 値を受理し、schema 1（Xtensa）の挙動は無改変 |
| S1-2 | 画像検査 C-1..C-8 をドライバへ移植し `fixed-vma` で必ず実行 | 実装どおり。esptool **5.2.0**（arduino 側）の elf2image 出力で C-1..C-8 が成立することを smoke リンクと 3 例題実コンパイルの両方で実測（R2 解消） |
| S1-3 | C6 も minimal から SDK ヘッダを `-I` | 実装どおり。`arduino_sdk.py` の `ARCH_HEADERS` に `riscvCsr`（`include/riscv/include/riscv/csr.h`）を追加、`NEEDS_SDK_HEADERS` は chip 別表の `sdk_headers_always` で判定 |
| S1-4 | app cfg から `TA_FPU` を除去 | 実装どおり。生成 `kernel_cfg.c` に `TA_FPU` 0 件を確認 |
| S1-5 | C6 xip ld に `.init_array`/`.ctors` を追加 | 実装どおり。`.flash.rodata` セクション内へ追加（別出力セクションにしていない。理由は下記「逸脱」）。smoke リンクと 3 例題実リンクの両方で `__init_array_start != __init_array_end` を確認 |
| S1-6 | CPU クロック 160 MHz を既定 | 実装どおり（`CORE_CLK_MHZ` 既定 160、`SEAM_C6_CLK_BOOST=1`）。**実機の hello で 160 MHz は未検証**（開発側は wifi=160 のみ実測、min=80 は実測済み）。段2 の watch item |
| S1-7 | `recipe.size.regex` の板別 override | 実装どおり、かつ**有効に動くことを実測**（AC-1h）。platform.txt 側 regex を C6 名で拡張する代替案は不要だった |

### ROM linker script の勝者（AC-1f）-- minimal は 2 本、newlib 系の ROM 代入は持ち込まない

C6 の `minimal` stage がリンクする ROM linker script は **`esp32c6.rom.ld` / `esp32c6.rom.api.ld`
の 2 本**（開発側の `seam-c6-min`、`fmp3/target/m5nanoc6_gcc/target.cmake` と同じ）。
`runtime/CMakeLists.txt` の `A1_ROM_LDS_<profile>` が profile ごとの表で、manifest の
`romLinkerScripts` にそのまま入る。

**経緯（最終レビュー I-A、ruling (a)）**: Task 2/3 の stage は開発側の Wi-Fi 構成
（`cmake/a1_c6_stage1.cmake`）が足す 11 本（`rom.libc` / `rom.libgcc` / `rom.newlib` /
`rom.libc-suboptimal_for_misaligned_mem` / `rom.version` / riscv `rom.api` / `rom.net80211` /
`rom.pp` / `rom.phy` / `rom.systimer` / `rom.coexist`）も含めた 13 本を「段3 の先取り」として
リンクしていた。これは危険だった。理由:

- これらの ld は `PROVIDE` ではなく**素の代入**（`rand = 0x40000590;` 等）で、ドライバが常に
  付ける `-Wl,--allow-multiple-definition` の下では、toolchain の `libc_nano.a` の実体より
  **黙って勝つ**（リンクは通る。BUILDING.md「多重定義もリンクでは捕まらない」の型）。
- ROM の newlib（`atoi` / `rand` / `strtol` / `printf` / `malloc` ...）は、errno・malloc・
  ロック・reent を **`syscall_table_ptr`（`0x4087ffd4`）と `_global_impure_ptr`（`0x4087ffd0`、
  どちらも `esp32c6.rom.libc.ld` の代入で ROM のデータ領域）** 経由で外の世界へ出す。
  ESP-IDF なら `esp_libc_init` がこの表を張るが、C6 minimal にはそれに当たるものが無い
  （ヒープも `_sbrk` も ROM 向けの reent も無い）。**この表を張らないまま ROM の `rand()` を
  呼ぶと NULL テーブルを辿って落ちる**のは、開発側が S3/LX6 で実際に踏んだ障害
  （`esp/shim/wifi_stubs.c` の記録: 「ROM rand() -> ROM __getreent stub ->
  syscall_table_ptr(NULL) -> LoadProhibited」、`chip_rom_libc.c` の stub table で解消）。
- 3 例題では効いていない（下表）が、`atoi()` や `rand()` の呼び出し 1 つでこの経路に入る。「3 例題が通る」
  を「任意のスケッチが安全」と読めない典型。

**probe による実測（`fw1-romprobe-winners.txt`）**: `atoi` / `rand` / `strtol` / `abs` /
`strlen` を呼ぶ試作スケッチ（値は volatile 経由で畳めない）を同じ build で 2 通りにリンク。

| ROM ld | リンク | `atoi` / `rand` / `strtol` / `strlen` の解決先 | `abs` | 未定義 |
| --- | --- | --- | --- | --- |
| **2 本（現状）** | **rc=1（正直な失敗）** | `libc_nano.a` の実体（像内 `0x4200477a` 等、`atoi.o` / `rand.o` / `strtol.o` / `strlen.o`） | 出ない（gcc 組込みで inline） | `_sbrk`（`rand` -> `malloc` -> `_sbrk_r`）と `_close` / `_lseek` / `_read` / `_fstat`（`rand` の `__assert_func` -> `fiprintf` -> stdio） |
| 13 本（Task 2/3 時点、対照） | rc=0（**黙って通る**） | ROM の絶対番地（`0x400005a0` / `0x40000590` / `0x400005a8` / `0x400004c8`、`nm` type `A`） | `0x40000578`（ROM） | 無し。`_global_impure_ptr` = `0x4087ffd0`、`syscall_table_ptr` = `0x4087ffd4` が絶対記号として載るが、**像内のどのオブジェクトも参照しない**（`nm -u` 0 件）= 誰も初期化しない |

つまり 2 本では「newlib の実体が要るもの（`_sbrk` 等）を要求して失敗する」か「`libc_nano.a` の
オブジェクトが像に入る」のどちらかで、**ROM の newlib が黙って勝つ経路は無い**。

**3 例題 + SspProbe の勝者（ROM ld 2 本、`fw1-link-winners-2ld.txt`）**: リンク前のオブジェクト群で
未定義かつ絶対番地に解決した記号のうち、ROM ld 由来は **`esp_rom_set_cpu_ticks_per_us`
（`0x40000048`、`esp32c6.rom.api.ld` の PROVIDE）だけ**（4 本とも）。他の絶対記号は
`esp32c6_xip.ld`（`__init_array_*` / `__ctors_*` / `__idata_*`）と `esp32c6.peripherals.ld`
（`SYSTIMER` / `USB_SERIAL_JTAG`）。stage の定義を ROM 代入が置き換えたものは 0 件。
`libc_nano.a` から引かれるのは `impure.o` / `errno.o`（`newlib_syscalls.o` が参照、
保護フレームの無いスケッチでは `--gc-sections` で全部落ちる）と、LibraryInfo の **`memcpy.o`**
（Task 3 時点では 13 本中の `rom.libc-suboptimal_for_misaligned_mem.ld` が `0x400004ac` に
落としていたもの。2 本では newlib の実体になり、LibraryInfo の像が 83408 -> 83632 bytes に
増えた。Blink / TwoFileSketch はバイト数不変）。

**M-6 parity gap（Xtensa minimal との差、段3 へ持ち越し）**: Xtensa port の minimal は
`arch/xtensa_gcc/esp32s3/chip_rom_libc.c`（タスク毎 `_reent`・ROM syscall stub table・
`software_init_hook` での初期化）を持ち、ROM newlib を安全に使える。**C6 minimal にはそれが無く、
ヒープ（`_sbrk`）も無い。** 帰結として、NanoC6 では `String` / `printf` / `rand` 等
newlib の malloc・stdio に届くスケッチは**リンク時に `undefined reference to _sbrk`（等）で
明確に失敗する**（実行時に静かに壊れるのではなく）。C6 に `chip_rom_libc.c` 相当（ROM newlib を
使うなら stub table、使わないなら `_sbrk` + ロック + reent の供給）を置くかどうかは、残り 11 本の
ROM ld をどう扱うかと一緒に**段3 の計画（owner: 段3）で決める**。段1 では決めない。

### `newlib_syscalls.c`（新設）の意味論

M5Stack core は全スケッチを `-fstack-protector` で建てる。ローカル `char` 配列を 1 つ持つ
（Arduino では普通の）スケッチは `__stack_chk_fail` を参照し、newlib-nano の
`__stack_chk_fail -> write -> _write_r -> __getreent, _write` / `abort -> raise ->
_getpid_r -> _getpid` / `_kill_r -> _kill` / `_exit` という鎖を引く。C6 の stage にも
ROM ld にもこの 5 本の実体が無く（ROM は memcpy/strlen 等のみ）、保護フレームを持つ
スケッチはリンクできなかった（3 例題自体は保護フレームを持たないため、この鎖なしでも通る。
実測は試作スケッチ 1 本で確認: `fw1-compile-c6-SspProbe-before.txt`、`newlib_syscalls.o` を
外した stage に対して rc=1、`_exit` / `__getreent`（2 箇所）/ `_kill` / `_getpid` / `_write` の
5 記号が未定義。Task 3 の `task3-compile-c6-SspProbe-before.txt` は `cmd | ...` の `$?` を
記録したため rc=0 と書かれており、fix wave 1 で採り直した）。

実装した意味論は次のとおり（誠実に書く。本番の堅牢化ではなく最小の link 成立）:

- **`_write(fd, buffer, length)`**: `fd == 1 || fd == 2` のときだけ 1 バイトずつ
  `target_fput_log()`（カーネルのログポート）へ送り `length` を返す。それ以外の `fd` は
  **`errno = EBADF` を立てて `-1`**（fix wave 1。newlib の `_write_r` は `errno` を呼び出し側の
  reent へ写すので、見る側には「無い記述子」として見える。`errno` の参照で `libc_nano.a` の
  `errno.o` が引かれるが葉で、保護フレームの無いスケッチでは gc で落ちる）。
  ファイルディスクリプタ表もプロセスも無いので、これで「スタック破壊検出」のメッセージが
  コンソールへ出るという 1 目的だけを満たす。
- **`_exit(status)`** / **`_kill(pid, sig)`**: `syslog(LOG_EMERG, ...)` で状況を記録した後、
  無限ループで停止する（プロセスが無いので戻る先が無い）。
- **`_getpid(void)`**: 常に `1` を返す固定値（`raise()` 経由でのみ呼ばれる）。
- **`__getreent(void)`**: newlib の**単一グローバル reent**（`_impure_ptr`）を返す。
  minimal stage にタスクごとの reent は無い（Xtensa port はタスクコンテキストへ埋め込むが、
  C6 arch 層は dev からの無改変 vendoring でそれを持たない）。このパスで reent を実際に
  参照するのは `_write_r` の errno 処理だけ。
- **`__stack_chk_guard`**: あえて手を入れず `libc_nano.a` の**ゼロ初期化語（.bss）のまま**。
  ESP-IDF は `esp_system` がハードウェア RNG から seed するが、それは段2 以降の
  hardening 項目であって本段のリンク要件ではない（watch item）。

いずれも葉（`errno.o` / `impure.o` 以外は何も引き込まない）で、`--gc-sections` により
保護フレームを持たないスケッチの image には残らない（3 例題のバイナリはこのファイル追加の
前後でサイズ不変、差分は `app_elf_sha256`・banner の時刻・末尾 checksum の 65 bytes のみ。
Task 3 時点の実測）。

### 像サイズと RAM 余裕（AC-1d、2026-09-15 実測）

C6 xip ld の `RAM` は `ORIGIN=0x40800000, LENGTH=0x4086E610-0x40800000` = 452112 bytes
（bootloader の `iram_loader_seg` 手前まで）。C-8 の分母（flash 側）は M5Stack 既定 OTA
partition table の app0 = `0x140000` = 1310720 bytes（stock ptable、開発側の
`/1048576` とは異なる）。

| スケッチ | image bytes（flash、C-8 判定値） | RAM（`.data`+`.bss`、LOAD の MemSiz） | RAM 余裕 |
| --- | --- | --- | --- |
| Blink | 83360 / 1310720（6.4%） | 17792 | 434320 bytes（96.1%） |
| LibraryInfo | 83632 / 1310720（Task 3 の 13 本時点は 83408。差は ROM の `memcpy` が `libc_nano.a` の実体になった分） | 17792 | 434320 bytes |
| TwoFileSketch | 83376 / 1310720 | 17792 | 434320 bytes |
| SspProbe（`newlib_syscalls.c` の動作確認用試作） | 84448 / 1310720（Task 3 時点 84096） | 18048 | 434064 bytes |

**注意**: この数字はビルド時点（2026-09-15 fix wave 1、C6 stage 47 objects、ROM ld 2 本）の
もの（`fw1-compile-c6-*.txt`）。段2/3 でオブジェクトが増減すれば動く。固定値として恒久扱いしないこと。

### 逸脱の受理（レビュー ruling、`progress.md`）

Task 2 が加えた逸脱のうち次の 3 件は reviewer が受理済み（`IMPORT_PROVENANCE.md` に
記載済み。以後の段で見直す条件はそこに書いてある）。

- **逸脱 3**: `esp32c6_xip.ld` に `.eh_frame`/`.eh_frame_hdr`/`.gcc_except_table` の
  入力規則を追加（S1-5 の `.init_array` に加えて）。既存 `esp32s3_xip_m5.ld` と同型の対応。
- **逸脱 4**: `phase3_arduino_app.c` から `TOPPERS_XIP_PADDR_PROBE` ブロックを削除。
  `fixed-vma` では対応する PADDR 解決が無く死コードのため。
- **逸脱 5**: `runtime/CMakeLists.txt` に `-ffile-prefix-map=<SDK include>=sdk/include` を
  追加。現状の minimal 像には効果が出ていない（strings に `sdk/include` 0 件）が、
  段3 で hal の assert 文字列が `.rodata` に載ったときに利用者のホームディレクトリの
  綴りが漏れる経路を先に塞ぐ意図。

**`ESP32C6_CONSOLE` はハードコード**: `chip.cmake` は `ESP32C6_CONSOLE` 未指定時に
`usbjtag` を既定にし、`runtime/CMakeLists.txt` はこれをビルド設定として外へ出していない
（M5NanoC6 は native USB のみで UART ブリッジを持たないため、Xtensa 板のような
コンソール選択の必要が無い）。UART0 コンソールへ切り替える経路自体は `chip.cmake` に
残っている（`ESP32C6_CONSOLE STREQUAL "uart0"` 分岐）が、`runtime/CMakeLists.txt` からは
到達しない。

### THIRD_PARTY_NOTICES

`THIRD_PARTY_NOTICES.md` に C6 節を追加し、`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`
を出自の一次記録として指す（ファイル一覧は再掲しない。本節末尾参照）。

### 段2 への watch item

- **160 MHz は hello で実機実測されていない**（S1-6）。段2 の最初の書込みで確認し、
  もし 160 MHz で起動しなければ dev と同じ条件（min=80）に戻す 1 軸切替えができる。
  手順（fix wave 1 で `build_prebuilt_stages.py` に `--cmake-define` を足した。
  実測 `fw1-cmake-define-80mhz.txt`: CMakeCache が `A1_C6_CPU_FREQ_MHZ=80`、全 47 objs の
  compile 行が `-DCORE_CLK_MHZ=80` で `SEAM_C6_CLK_BOOST` 無し、160 と比べて
  `seam_c6_clk.o` / `seam_c6_entry.o` / `core_support.o` / `target_kernel_impl.o` / `banner.o`
  だけが変わる）:
  ```bash
  python3 scripts/build_prebuilt_stages.py --chip esp32c6 --profiles minimal --clean \
      --cmake-define A1_C6_CPU_FREQ_MHZ=80
  python3 scripts/install_platform.py --prebuilt-stage-root build/prebuilt
  ```
  `--cmake-define KEY=VALUE` は chip 非依存の素通し（repeatable、未指定なら configure 行は
  不変。Xtensa は `--clean` 再ビルド後の X-check 7/7 で不変を確認）。
- **stock M5Stack bootloader は未検証**（D1）。段2 で stock / 開発側 seam bootloader /
  +開発側 ptable の 3 通りを 1 軸ずつ実測して確定する。
- **C-8 の上限は stock OTA partition table の app0 = `0x140000`** に基づく（上記「像サイズ」節）。
  ptable を変えれば分母が変わるので、段2 で実際に焼く ptable と揃っているか確認すること。
- **ROM ld は minimal で 2 本に絞った**（上記「ROM linker script の勝者」節）。段3 で
  wifi-connect に残り 11 本（の一部）を戻すときは、`syscall_table_ptr` / `_global_impure_ptr`
  を誰が張るか（libc 供給の決定、M-6）を先に決め、勝者一覧を `nm` で採り直すこと。
  「minimal ではどれも効いていない」は 2 本の集合についての言明で、11 本を戻せば成立しない。
- `__stack_chk_guard` がゼロ初期化のまま（RNG seed 無し）。段2 以降の hardening 項目。

### 段2 の入口条件

- **焼く物**: 本リポジトリの成果物（driver が出す `fmp_xip.bin` 相当）+ stock M5Stack
  bootloader + stock（`default`）partition table + `boot_app0`。焼き方は
  `arduino-cli upload` 経由（`D1` の第一候補どおり。開発側の直接書込み手順は前提が異なるため
  流用しない）。`arduino-cli compile` が出す `merged.bin` の構成（`fw1-compile-c6-Blink.txt`）:
  `0x0 bootloader.bin` / `0x8000 partitions.bin` / `0xe000 boot_app0.bin` / `0x10000 app`。
- **採取**: USB Serial/JTAG（USJ）。書込みと採取が同一ポート。
- **真cold**: `uhubctl -l 2-3.3 -p 3 -a cycle`（本機のハブ構成、電源を実際に切って入れ直す）。

### stock bootloader と開発側 seam bootloader の差（段2 で見るべきもの）

開発側の C6 は自前の seam bootloader（`esp/boot/seam_c6/`、WDT 無効・USJ コンソール・DIO・
factory ptable）でしか実機実測していない。段2 の第一候補は stock なので、差を先に書いておく
（出典: M5Stack core 3.3.8 `esp32c6-libs/sdkconfig`、`bin/bootloader_*.elf`、platform.txt。
証跡 `fw1-stock-bootloader-readelf.txt`）。

- **どの bootloader か**: platform.txt の prebuild hook 4 が
  `bin/bootloader_{build.boot}_{build.boot_freq}.elf` から作る。`m5nanoc6_fmp3` は
  `build.boot=qio`・`build.flash_freq=80m` なので **`bootloader_qio_80m.elf`**。
- **`.iram_loader.text` は `0x4086e610`**（4 変種とも同じ。`readelf -S` / `-l` の LOAD
  `0x4086e610`、長さ `0x305c`〜`0x3154`）。ドライバの C-5 `loader_seg = 0x4086E610`
  （esp-idf v5.5.4 `bootloader.ld` の `bootloader_iram_loader_seg_start`）と一致し、
  FMP3 の xip ld の `RAM` 上限（`0x4086E610`）もこれに合わせてある。stock でも app の RAM 域が
  loader を踏まない。
- **コンソール**: stock は `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`（UART0 主）+
  `SECONDARY_USB_SERIAL_JTAG=y`、`BOOTLOADER_LOG_LEVEL=ERROR`。**正常系では bootloader は
  USJ に何も出さない**。期待する USJ の系列は: ROM の起動バナー -> seam entry の生 `'S'`
  1 文字（`SEAM_C6_ENTRY_MARK=0x53`）-> FMP3 の banner。`'S'` が出て banner が出なければ
  「bootloader は飛んだが FMP3 が黙っている」、`'S'` すら出なければ「bootloader が app を
  受理していない」（後者は UART0 に ERROR が出ているかもしれない。段2 では UART0 も見ること）。
- **RTC WDT**: stock は `CONFIG_BOOTLOADER_WDT_ENABLE=y`、`WDT_TIME_MS=9000`（9 秒）で
  **armed のまま app へ飛ぶ**。開発側の seam bootloader は `WDT_ENABLE=n` だったので、
  FMP3 の `hardware_init_hook`（`target/m5nanoc6_gcc/target_kernel_impl.c`、MWDT0/1・RTC WDT・
  SWD を全部止める）が**この WDT を実際に止めた実績は無い**。9 秒後にリブートループするなら
  ここが第一容疑。
- **flash mode**: stock bootloader は QIO 構成（`CONFIG_ESPTOOLPY_FLASHMODE_QIO=y`）で、
  bootloader.bin / app の**ヘッダはどちらも dio**（`CONFIG_ESPTOOLPY_FLASHMODE="dio"`、
  実測 flash_mode=2）。bootloader が実行時に QIO を有効にする（`bootloader_flash_qio_mode`）。
  開発側は DIO のまま。
- **partition table**: stock は OTA 型（`default.csv`: `nvs 0x9000` / `otadata 0xe000` /
  `app0 0x10000 +0x140000` / `app1` / `spiffs` / `coredump`）。app は **app0 = `0x10000`**、
  `otadata` と `boot_app0`（`0xe000` に焼く 8 KB、全 `0xFF` = UNDEFINED）で app0 を選ぶ。
  `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` だが OTA を使わない限り不活性
  （`esp_ota_mark_app_valid` を呼ぶ相手が無く、otadata が UNDEFINED のままなら
  bootloader は app0 を factory 扱いで起動する）。C-8 の分母 1310720 はこの app0。
  **「+開発側 ptable」の腕を使うとき**: 開発側の ptable は `boot_app0` が焼かれる `0xe000` に
  `nvs` を置いているので、stock の焼き方（`0xe000 boot_app0.bin`）と混ぜると nvs を上書きする。
  その腕では `boot_app0` を焼かない書込み列にすること。
- **R1（stock で動かなかったとき）の対照**: 「開発側 bootloader で動いたら、開発側 bootloader の
  設定で **WDT だけ有効**にした bootloader」を建てて焼く（1 軸）。それでも切り分かないなら
  **QIO だけ有効**にした変種を追加する（もう 1 軸）。同時に 2 つ変えない。

### platform.txt / boards.txt の再現（Xtensa 板の配布物不変の確かめ方）

X-check は stage（`objs/` 等）を見るが、platform.txt / boards.txt は見ない。段1 で
`install_platform.py` に C6 の行を足したので、Xtensa 側の生成物が変わっていないことは
次で確かめる（`fw1-install-platform-cmp.txt`）:

```bash
# 段0 の installer を scratch へ取り出し、同じ build/prebuilt から scratch sketchbook へ組む
git show 0f40140:scripts/install_platform.py > <scratch>/old/scripts/install_platform.py
git show 0f40140:scripts/arduino_sdk.py     > <scratch>/old/scripts/arduino_sdk.py
git show 0f40140:scripts/fmp3_link.py       > <scratch>/old/scripts/fmp3_link.py
python3 <scratch>/old/scripts/install_platform.py --library-root . --sketchbook <scratch>/sb-old --prebuilt-stage-root build/prebuilt
python3 scripts/install_platform.py --sketchbook <scratch>/sb-new --prebuilt-stage-root build/prebuilt
cmp  <scratch>/sb-old/hardware/toppers/esp32/platform.txt <scratch>/sb-new/hardware/toppers/esp32/platform.txt   # 同一
diff <scratch>/sb-old/hardware/toppers/esp32/boards.txt <(grep -v '^m5nanoc6_fmp3\.' <scratch>/sb-new/hardware/toppers/esp32/boards.txt)  # 同一
```

実測: `platform.txt` はバイト同一、`boards.txt` の差は `m5nanoc6_fmp3.*` の **191 行の追加
だけ**（削除 0、他の追加 0）、`tools/` と `programmers.txt` も同一。link driver 2（`0f40140`）と
3（HEAD）で Xtensa Blink を同じ build から再リンクすると ELF / BIN とも sha256 同一
（`fw1-xtensa-blink-driver2-vs-3.txt`）。

## 段2 の記録（2026-09-15、commit `2f7bc0f` / `639331a`）

M5NanoC6 実機で `minimal` 版 `Blink` が起動し、D1（bootloader）と D5（CPU クロック）を
確定した段。書込み・採取は台本 `scripts/capture_c6_usj.sh` のみで行い、実機操作は
uhubctl による電源断／投入のみ。詳細な証跡は開発リポジトリ
`.steering/20260915-c6-arduino-plan/stage2/{AC.md,logs/}`（`task1-*` が採取台本の同定・
selftest、`task2-A-*` が実機実験）と本リポジトリ `.superpowers/sdd/PLAN-stage2-impl/
{task-1-report.md,task-2-report.md,progress.md}`。

### AC 2a-2h

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 2a | `capture_c6_usj.sh` の DRYRUN が焼く物 4 点の sha256・番地を出し、MAC 不一致・DUT 不在で rc!=0、selftest PASS | PASS | `task1-dryrun.txt`（4 点の sha256・番地・実 esptool コマンド行）、`task1-gate-neg.txt`（FORBIDDEN MAC / DUT_MAC 不一致 / port 固定の 3 通り、いずれも rc=1・書込み 0 本）、`task1-selftest.txt`（9 項目 PASS、変異体 4 種 FAIL）、`task1-imgcheck-neg.txt`（fix round 1、画像妥当性検査の負対照） |
| 2b | 条件 A（stock bootloader + stock `default` ptable + boot_app0、160 MHz）warm 5/5 | PASS | `task2-A-warm{1..5}.log/.run.txt/.sha.txt`（banner=1・setup=1・heartbeat=39・unexpected=0 が 5/5） |
| 2c | 条件 A の真cold 5/5 | **成立（条件付き）: 9/10、cold5 無音 1 回、再試行後 5 連続**（5/5 と丸めない） | `task2-A-cold{1..10}.log/.cold.txt/.run.txt/.sha.txt`。cold1-4,6,8-10 は heartbeat=39、cold7 は heartbeat=38（1 文字落ち、下記 2-6）、**cold5 のみ heartbeat=0（40 秒無音）**。初回 5 回中 4/5、無音回の再試行（cold6）と追加 4 回（cold7-10）で連続 5/5。先頭行の欠落（ROM banner/`S`/FMP3 banner/`Processor 1 start.`）は R11 どおり不成立の根拠にしていない |
| 2d | 2b/2c 不成立なら B -> C、結果表、R1 の対照 | **不要（実施せず）** -- A が 2b/2c を満たしたため B（開発側 seam bootloader）・C（+開発側 ptable）は走らせていない。下記「結果」節に未実施と明記 |
| 2e | 160 MHz 不成立なら 80 MHz を 1 軸 | **不要（実施せず）** -- A が 160 MHz のまま成立したため（D5 確定、S1-6 の watch item 解消） |
| 2f | 選んだ条件の像の size、USJ 最初の行（`S` mark の有無）、WDT 停止の根拠 | PASS | 下記「最初の行の観察」「WDT の根拠」節。像 size は「板の最終状態」節 |
| 2g | `docs/c6-port.md` 段2 節、`README.md` 到達点 | PASS（本コミット） | 本節、`README.md` |
| 2h | 非退行: 段1 の 3 例題が引き続きリンク、X-check 7/7（scripts を触った場合） | PASS（再検証不要と判断） | Task 1/2 とも `ports/`・`src/`・`third_party/`・既存 `scripts/*.py` を触っていない（新設は `scripts/capture_c6_usj.sh` のみで、X-check が対象とする共有ファイル `build_prebuilt_stages.py`/`prebuilt_stage.cmake`/`fmp3_link.py`/`install_platform.py` は無改変）。段1 の PASS（AC 1a-1h）から状態は不変 |

### 軸表（書込み前に固定、`task-2-report.md` 1 節）

独立に変えられる軸を先に書き出し、1 回の書込みで変えるのは 1 軸とした。

| 軸 | 値 | 条件 A（採った条件） | 条件 B | 条件 C | 80 MHz 腕 |
| --- | --- | --- | --- | --- | --- |
| bootloader | stock（`bootloader_qio_80m.elf`、WDT 9 秒 armed、UART0 主）／開発側 seam（WDT 無効、USJ、DIO） | stock | 開発側 | 開発側 | A と同じ |
| ptable | stock `default`（OTA。otadata 0xe000、app0 0x10000 +0x140000）／開発側（nvs 0xe000、factory 0x10000） | stock | stock | 開発側 | A と同じ |
| boot_app0 @0xe000 | 焼く／焼かない | 焼く | 焼く | 焼く（開発側 ptable の nvs と重なる。記録のみ、実施せず） | 焼く |
| CPU | 160／80 MHz | 160 | 160 | 160 | 80 |
| reset | warm（`NOFLASH=1`、monitor の hard reset）／真cold（`COLD=1` + uhubctl） | 両方 | 両方 | 両方 | 両方 |

手順の順序は A warm x5 -> A cold x5 ->（不成立なら）B -> C -> 80 MHz。**A で成立したため B/C/80 MHz は未実施**。

### 結果表

| 条件 | warm | 真cold | 判定 |
| --- | --- | --- | --- |
| A: stock bootloader + stock `default` ptable + boot_app0 + 160 MHz | **5/5** | **9/10**（初回ブロックで 4/5、無音回の再試行で成立、追加 4 回で連続 5/5） | **成立**（D1 = stock、D5 = 160 MHz） |
| B: 開発側 bootloader + stock ptable | 未実施（A 成立のため） | 未実施 | -- |
| C: 開発側 bootloader + 開発側 ptable | 未実施（A 成立のため） | 未実施 | -- |
| 80 MHz | 未実施（160 MHz のまま A が成立したため） | 未実施 | -- |

書込みは全 15 run 中 **1 回だけ**（`task2-A-warm1`）。以後は `NOFLASH=1`（warm）または `COLD=1`（cold）で
flash に触れていない（各 `.sha.txt` に "NOT written by this run"）。

### 最初の行の観察（AC-2f）

warm（5/5 とも同一の系列、`task2-A-warm{1..5}.log`）:

```
ESP-ROM:esp32c6-20220919
...
load:0x4086b910,len:0xdd0
...
entry 0x4086b910
S
TOPPERS/FMP3 Kernel Release 3.4.0 for M5NanoC6 (ESP32-C6) (...)
...
Processor 1 start.
System logging task is started on port 1.
```

系列は **ROM 起動バナー -> `load:`/`entry 0x4086b910` -> 生の `S` の単独行（`SEAM_C6_ENTRY_MARK`）
-> FMP3 banner**。**stock bootloader 自身は USJ に 1 行も出力しない**（ROM の `load:`/`entry` 行の
直後が即 `S`）-- docs 冒頭「stock bootloader と開発側 seam bootloader の差」節の予測どおり実測できた。

真cold（9/9 とも同一）: 最初の行は **`System logging task is started on port 1.`**。ROM 行・`S`・
FMP3 banner・`Processor 1 start.`・hrt notice は host が tty を開く前に出るため**必ず失われる**
（R11 どおり、不成立の根拠にはしない。判定は heartbeat 回数で行う）。

### WDT が止まっていることの根拠（AC-2f、推論と明記）

- stock bootloader は `CONFIG_BOOTLOADER_WDT_ENABLE=y`・9000 ms で armed のまま app へ飛ぶ
  （段1 記載）。`CAPTURE_SEC=40` の窓で **heartbeat（約 1 秒周期、`ArduinoSketchBridge.cpp:72-81`
  の `loop()` 1000 回ごと）が 39 本**（15 run 中 14 run。cold7 は文字落ちで 38 本、下記）出ており、
  9 秒の壁を大きく超えて連続している。
- 採取中に USB 再列挙が無い（journal は電源サイクルごとに 1 回の列挙のみ）、warm の `rst:` 行は
  毎回 `0x15 (USB_UART_HPSYS)`（USJ 経由のホスト reset）で **RTC WDT の reset 理由は 15 run のどこにも
  出ていない**。
- **これは間接証拠であり、LP_WDT レジスタの読み戻しはしていない**（minimal stage にその手段が無い、
  ledger の pre-flight どおり）。以上から `hardware_init_hook`（`target/m5nanoc6_gcc/target_kernel_impl.c`）
  が stock bootloader の armed した 9 秒 RTC WDT を実際に止めている、と**推論する**（実証ではない）。

### D1・D5 の確定

- **D1 = stock M5Stack bootloader（確定、実測で裏付け）**。条件 A（stock bootloader + stock
  `default` ptable + boot_app0）で warm 5/5・真cold 9/10 が成立し、bootloader を本リポジトリに
  同梱する必要は無い（`D1` の仮決定どおり）。開発側 seam bootloader（B）・+開発側 ptable（C）は
  D1 の確定に不要となったため未実施のまま残る（下記「懸念・持ち越し」(4)）。
- **D5 = 160 MHz（確定、実機の hello で実測済み）**。段1 時点の watch item「160 MHz は実機未検証」
  （S1-6）は本段で解消した。80 MHz へのフォールバックは発生していない。

### cold5（1 回の無音）の扱い

- 事実: by-id は正常に出現（他の cold より約 0.3 秒遅い 3.86 秒）、monitor は `--no-reset` で開いた、
  40 秒間 0 バイト。採取中に再列挙は無い（= WDT reset・panic 再起動のどちらも起きていない）。
  再試行（cold6）は成立、以後 cold7-10 も成立（連続 5/5）。同じ flash 内容・同じ手順で 9/10。
- **「app が動いていたが USJ が黙っていた」のか「app が hang した」のかは、この台本だけでは
  区別できない**（COLD は reset を伴わないため生存確認の手段が無く、LED も見ていない）。推測としては
  開発側段4 で報告されている USJ の host 列挙／tty 開閉と app 側送信のタイミング競合（H1）と同型。
  bootloader 軸（B）で cold を回せば「stock bootloader 固有か」は切り分けられるが、A が成立した
  以上、板を焼き替えてまでは行っていない（未検証のまま残す）。
- 判定: `CLAUDE.md`「実機の単発失敗を実装のせいにしない」の規約に従い、単発の無音 1 回を根拠に
  不成立とはしない。ただし **cold の無音は 1/10 の頻度で起き得る**ことは事実として記録し、
  段3 以降の入口条件（下記）へ持ち越す。

### cold7 の 1 文字落ち

`task2-A-cold7.log` の `[Arduino] loop heartbeat` が 1 箇所 `hartbeat`（'e' が 1 文字欠落）。
marker の正規表現に掛からず heartbeat=38（blink 行は 39 のまま）と数えた。起動直後の
`[Arduino] task start` の頭欠け（次項）と同じ経路（USJ の TX 文字落ち）と推定。定常状態でも
約 430 heartbeat 行に 1 回程度の頻度で起き得る。marker の判定（>=5）自体には影響しないが、
**行を厳密に照合する試験では偽陰性を生む**。

### 台本のインタフェースと安全ゲート（`scripts/capture_c6_usj.sh`）

- 入力: `SKETCH_BUILD`（`arduino-cli --build-path` の成果物）、`BOOTLOADER`/`PTABLE`/`APP`
  の個別上書き（B/C 軸用）、`BOOT_APP0`（`none` で 0xe000 を焼かない選択肢もあるが既定は常に焼く）、
  `DRYRUN`/`NOFLASH`/`NORESET`/`COLD`/`CAPTURE_SEC`/`OUT` など。esptool は M5Stack core 同梱の
  `esptool_py 5.2.0`。番地は M5Stack platform.txt の upload recipe と同一（0x0/0x8000/0xe000/0x10000）。
- **安全ゲート**（すべて実測、`task-1-report.md`）:
  1. **DUT 同定**: `flash-id --no-stub`（read-only）で `BASE MAC:`／chip 完全文字列／flash size を照合。
     FORBIDDEN MAC 一覧と期待 MAC 以外は書込み前に rc!=0（esptool 未呼出、ファイル 0 本）。
  2. **画像妥当性検査**（fix round 1 で追加）: 0 バイト拒否、bootloader/app は先頭 0xE9 + ヘッダ長・
     セグメント数、ptable は 3072 バイトちょうど、boot_app0 は 8192 バイトちょうど。いずれも
     esptool を呼ぶ前に入力段で拒否（負対照 `task1-imgcheck-neg.txt`）。
  3. **COLD**: esptool を一切呼ばず、by-id の消失 -> 出現を待って `.cold.txt` に時刻を記録し、
     出現直後に `--no-reset` で開く。
  4. **EXIT トラップ**: SSID/PASS/BSSID とその hex 綴りの針（`WIFI_CREDS` があれば）、peer-MAC
     マスク（DUT の MAC/EUI-64 綴りだけ保持）、IPv4 マスクを実行し、selftest で「壊れた検証も
     成功と同じ顔をする」対策済み（positive control 4 種）。redact 不成立時は `.UNREDACTED` へ
     隔離し rc=93（fail-closed）。
- 段2 の実機実験ではこれらのゲートが実際に効くこと（DUT 同定 OK、負対照 3 通り rc!=0、画像検査
  負対照 rc=1、cold の消失/出現記録、redact "masked and checked clean" 全 run）を確認した上で、
  書込みは 1 回（`task2-A-warm1`）のみ行った。

### 懸念・持ち越し（段3 以降、owner: 段3+）

(a) **`[Arduino] task start` の頭欠け（15/15）と定常時の 1 文字落ち（cold7 で 1/約430 行）**:
  `target_fput_log()` の busy-poll 経路と logtask（ISR 経路）が同じ USJ TX FIFO を同時に使う瞬間に
  文字が落ちる型（Xtensa 板では同じ行が無傷）。marker（banner/setup/heartbeat）の判定には影響しないが、
  **段4 以降で行単位の厳密照合をする試験は偽陰性を作る**ため、marker 判定を厳密な文字列一致にしないこと。

(b) **真cold の無音 1/10（cold5）が USJ 無音と app hang のどちらか切り分けられない**: COLD は reset を
  伴わないため生存確認の手段が無い。段3 以降で採取台本に reset を伴わない生存確認（例: LED 点滅の
  監視、または一定時間無音なら 1 回だけ NORESET で再オープンする）を足すか判断すること。

(c) **`no time event is processed in hrt interrupt on PRC1.` が毎起動 1 回（LOG_NOTICE）**: 15 run
  すべてで観測。開発側 C6 Wi-Fi ログにも同じ行があり、Xtensa 板の Windows 実測にも `on PRC2.` がある
  （既知の型で、本段固有ではない）。挙動上の害は観測していない（heartbeat の周期は正常）が、
  起源（最初の hrt 割込みがイベント無しで入ること）は未確認。

(d) **B（開発側 seam bootloader）/ C（+開発側 ptable）は Blink で未検証**。D1 は stock で確定した
  ため必須ではないが、上記「stock bootloader と開発側 seam bootloader の差」節の「3 通りを 1 軸ずつ
  実測」という記述は本段の実施範囲（A のみ）に合わせて読むこと。cold5 が bootloader 固有かどうかも
  B を回せば切り分けられるが、未実施のまま残る。

### 板の最終状態

最後に書込みをした run は `task2-A-warm1`（`.sha.txt` に `Hash of data verified x4`）。以後 flash には
書いていない。

| 番地 | sha256 | size | 物 |
| --- | --- | --- | --- |
| 0x0 | `d8499f43...` | 20976 | stock bootloader（`Blink.ino.bootloader.bin`、`bootloader_qio_80m.elf` 由来） |
| 0x8000 | `148b959c...` | 3072 | stock `default` OTA ptable（`Blink.ino.partitions.bin`） |
| 0xe000 | `f94c5d78...` | 8192 | `boot_app0.bin`（M5Stack core 3.3.8） |
| 0x10000 | `feb533de...` | 83360 | `Blink.ino.bin`（minimal、160 MHz） |

= 条件 A（成立した条件）が焼かれたまま。最後の run（cold10）は成立して終わっており、板は Blink を
実行中（電源 on、by-id 存在）。開発側の seam-c6-wifi 像 + 開発側 bootloader/ptable（Task 2 実験前の
板の状態）は本段の初回書込みで上書きされている。

### 段3 の入口条件

`wifi-connect` stage 着手前に必要な判断・準備（`INVESTIGATION.md` 3-2 節、D6-D8 と対応）:

- **shim 第2コピー**: `ports/m5stack_xtensa` の Wi-Fi shim（公開版由来の fork、動的 mtx・ring
  キュー・xcore crit を経た土台）へ、開発側 C6 分岐（動的 mtx 以前の土台に C6 分岐が乗った版、
  出自 `c7fef18`）を当てることはできない（土台が違う）。C6 port は**開発側 `esp/shim` を独立した
  第2コピーとして持つ**（Xtensa 側は無改変のまま）。以後の shim 修正は 2 系統に分かれることを
  前提に計画すること（R12）。
- **adapter の C6 分岐**: 開発側の `esp_shim.c`／`esp_shim_libc.c`／`esp_wifi_adapter.c`／
  `esp_shim_blobglue.c`／`esp_shim.cfg` に入っている `#if defined(TOPPERS_ESP32C6)` 分岐と、
  C6 専用の `esp_shim_intr_intmtx.{c,h,cfg}` + `_lines.h`（割込みマトリクス予約、CFG_INT/DEF_INH
  x15、16=systimer+FROM_CPU_0）を、そのまま vendoring するか本リポジトリの adapter
  （`toppers_wifi_core.c`/`_connect.c`/`_scan.c`/`_optional_stubs.c`）から呼ぶ形にするかを決める。
- **`.a` の事前生成**: 開発側と同じ自前 `liblwip.a`（D7）＋ software crypto の `libsupplicant`/
  `libmbedcrypto`＋ core の `libnet80211`/`libpp`/`libcore`/`libcoexist`/`libmesh`/`libphy` の
  リンク構成を、本リポジトリの prebuilt stage としてどう事前生成・配布するか（D8「vendored」の
  実体）。
- **13 ROM ld + libc 供給の決定**: 段1 は minimal を ROM ld 2 本（`esp32c6.rom.ld`/`.rom.api.ld`）に
  絞り、残り 11 本（`rom.libc`/`rom.libgcc`/`rom.newlib`/`rom.libc-suboptimal_for_misaligned_mem`/
  `rom.version`/`rom.api`(riscv)/`rom.net80211`/`rom.pp`/`rom.phy`/`rom.systimer`/`rom.coexist`）を
  wifi-connect でどう扱うかを**段1 の「M-6 parity gap」節**の指摘どおり先に決める必要がある:
  ROM の newlib（`atoi`/`rand`/`strtol`/`malloc` 等）を使うなら `syscall_table_ptr`/
  `_global_impure_ptr` を張る **`chip_rom_libc.c` 相当**（Xtensa port が持つタスク毎 `_reent`・
  ROM syscall stub table・`software_init_hook` 初期化）を C6 側にも用意すること。決めないまま
  11 本を戻すと、素の代入が `-Wl,--allow-multiple-definition` の下で黙って勝つ経路が復活する
  （段1 の実測どおり）。
- **`nm -u` をゼロにする**: wifi-connect stage をリンクしたあと、未定義記号が残らないことを
  段1 の smoke リンクと同じ手法（`nm -u`）で確認する。
- **`esp_shim_intr_intmtx` の割込み線割当**: 上記の C6 専用割込みマトリクス予約が、本リポジトリの
  既存割込み使用（USJ・timer 等）と衝突しないことを確認する。
- **APM 解除の置き場所**: 開発側は `esp_wifi_adapter.c` の `c6_apm_unblock`（`A1_C6_APM_UNBLOCK`
  ビルドフラグ）で行っており、OFF にすると scan が 0 AP になることを対照実験で確認済み
  （開発側実測）。本リポジトリでもこの解除をどこで呼ぶか（adapter 初期化のどのタイミングか）を
  決める必要がある。
- **D6/D7/D8 の実装**: D6（OPEN AP の私的 ABI は差し込まない、段4 で実測するまで対応を主張しない）、
  D7（lwIP は開発側の自前型で通す）、D8（esp-idf 原本 6 本は vendored、`BUILDING.md`「ESP-IDF を
  複製しない」からの逸脱として明記済み）は上記 D0-D11 表のとおり。段3 ではこれらの決定を
  実装へ落とし込む。

## 段ごとの到達点

| 段 | ゴール | 実機 | 状態 |
| --- | --- | --- | --- |
| 0 | X-check の道具と baseline、本文書、C6 の出自宣言 | 不要 | **完了（2026-09-15）。** AC 0a-0h の記録は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage0/logs/` |
| 1 | `build_prebuilt_stages.py --chip esp32c6 --profiles minimal` が stage を出し、`m5nanoc6_fmp3:FMP3Runtime=minimal` で `Blink` / `LibraryInfo` / `TwoFileSketch` がリンクを通る。X-check で Xtensa 不変 | 不要 | **完了（2026-09-15、`07b239b`/`709b36a`/`ffefc52`/`5dbb8d1`/`f40490e` + 最終レビュー是正 fix wave 1）。** AC 1a-1h 全 PASS、記録は「段1 の記録」節 |
| 2 | M5NanoC6 で `Blink` が起動（USJ に banner・`[Arduino] setup complete`・heartbeat）。真cold 5/5・warm 5/5。bootloader 3 通りの表（D1） | 要 | **完了（2026-09-15、`639331a`）。** 条件 A（stock bootloader）で warm 5/5・真cold 9/10（成立（条件付き）、cold5 無音 1 回・再試行後 5 連続）、D1/D5 確定。B/C/80 MHz は未実施（A が成立したため不要）。記録は「段2 の記録」節、AC は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage2/AC.md` |
| 3 | `wifi-connect` stage が建ち、`WiFiScan` / `WiFiConnect` がリンク。`nm -u` 空、ROM ld 勝者一覧 | 不要 | 次に着手。入口条件は「段2 の記録」節「段3 の入口条件」 |
| 4 | M5NanoC6 で scan -> STA（WPA2）-> DHCP -> DNS -> TCP。真cold 3/3 | 要 | 未着手 |
| 5 | `verify_package.py` 4 板、`check_release_artifacts.py`、CI、文書、D8 の再評価 | 不要 | 未着手。**段1 から持ち越し（owner: 段5）**: (1) `scripts/verify_package.py` の `BOARDS` / `PROFILES` に `m5nanoc6_fmp3` / C6 の profile を足す（段1 では未改変、C6 は verify の対象外）、(2) CI（`.github/workflows/verify-package.yml` の `for chip in esp32s3 esp32` 2 箇所）に esp32c6 を足す、(3) `packaging/release-allowlist.json` の C6 向け entry（例題を C6 で出荷するときの `boardsManager` / 板ガード）、(4) `scripts/xcheck_compare.py` の `CHIPS` が Xtensa 固定である点の扱い（C6 の golden を持つかどうか）。fix wave 1 で先に済ませたのは `make_package_index.py` の C6 tool 依存の gate（stage の有無で切替え）と `tests.yml` への `test_xcheck.py` 追加のみ |
| 6（任意） | `attachInterrupt` と RGB LED の例題 | 要 | 未着手 |
