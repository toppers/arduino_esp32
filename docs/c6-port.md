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
| D3 | 共有 cmake | `prebuilt_stage.cmake` は共有して chip 分岐、chip 固有（seam 画像検査等）は `prebuilt_stage_c6.cmake` へ分離。**段1 で確定・実装済み、ただし実際には `prebuilt_stage.cmake` 自体は無改変**（chip -> port の表（Task 1）だけで C6 の runtime CMakeLists が自分の `prebuilt_stage_c6.cmake` を呼ぶため、Xtensa 側への白リスト追加も委譲コードも不要だった。委譲は「行を足す」形ではなく「表の port 列」で実現）。**段3 で確定**: Task 1/2 とも `runtime/CMakeLists.txt` と自分の `prebuilt_stage_c6.cmake` だけを触り、Xtensa 側の `prebuilt_stage.cmake` は無改変のまま（X-check 7/7 が両 Task で成立） | `ports/m5stack_xtensa/runtime/cmake/prebuilt_stage.cmake:57-62`（`A1_CHIP` 白リスト）ほか | 二重保守 vs 波及。どちらも X-check が検出 |
| D4 | manifest / driver 版 | **上げる**（`DRIVER_VERSION` 3、schema に `paddrMode` の新値と `linkBaseFlags` を加法で追加）。**段1 で確定・実装済み**（`DRIVER_VERSION="3"`、`MANIFEST_SCHEMA=2`、`SUPPORTED_MANIFEST_SCHEMAS=(1,2)`。schema 1 は literal のまま不変、schema 2 は `linkTailFlags`（任意キー、ledger に無かったが Task 1 が追加）も持つ） | 段1 完了時点（fix wave 1）の `scripts/fmp3_link.py`: `:87`（`DRIVER_VERSION`）・`:92-93`（`MANIFEST_SCHEMA` / `SUPPORTED_MANIFEST_SCHEMAS`）・`:98-99`（`PADDR_MODES`、schema 1 は `runtime-mmu` のみ）・`:105`（`SCHEMA1_LINK_BASE_FLAGS` = `-nostdlib -mlongcalls`）・`:106`（`SCHEMA1_LINK_TAIL_FLAGS` = `-lgcc -lc`）。以後は名前で引くこと | 旧 manifest を読む経路が無いことを確かめて戻せる |
| D5 | C6 の CPU クロック | **確定（160 MHz）**。段2 で条件 A を 160 MHz のまま warm 5/5・真cold 9/10 を実機実測し成立、80 MHz へのフォールバックは不要（**S1-6 の watch item は解消**） | -- | -- |
| D6 | OPEN AP の私的 ABI（`g_ic+0x1b4`） | **C6 では表を差し込まない。** 開発側と同じく `esp_wifi_init` に supplicant を任せる（開発側で STA/DHCP/ping 実測済み）。`--wrap=esp_supplicant_init` の経路は Xtensa 側を触らない。C6 の Open AP は**段4 で実測するまで対応を主張しない**。**段3 で実装どおり確定**: `toppers_wifi_core.c`（C6）はコールバック表・`__real_esp_supplicant_init`・`wpa_crypto_funcs` ゼロ化のいずれも持たない（ソース 0 件を実測）。空パスワード要求は NOTICE を出して driver へそのまま渡す。Open AP の可否は段4 の実機まで未確定のまま。**段4 で実測範囲が確定**: 実測できたのは **WPA2-PSK のみ**（ユーザーの実 AP、真cold 3/3 含む）。Open AP / WPA3-SAE は AP を用意できず**未実測のまま**（下記「段4 の記録」）。D6 の判断（表を差し込まない）自体は変更なし | `ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_core.c:30`（offset）、`BUILDING.md:288-294`（OPEN/WPA 分離） | Open AP が要るなら C6 blob の offset を求め直す（別作業） |
| D7 | lwIP | **開発側の型（自前 `liblwip.a` + `netif_esp32s3.c` / `port/sys_arch.c`）**で通し、core の `liblwip.a` へ寄せるのは後。**段3 で確定・リンク成立**: 「段3 でリンクが通らなければ Xtensa 型へ」の分岐は発生しなかった（AC-3d PASS）。gateway/netmask は `netif_esp32s3.h` が公開しないため、adapter 側で lwIP の `netif_default` を読んで吸収（vendored ファイルは無改変のまま） | Xtensa 側は core の `liblwip.a` + `ports/m5stack_xtensa/runtime/wifi/net/` の別系統 | 段3 でリンクが通らなければ Xtensa 型へ（**不要になった**） |
| D8 | esp-idf 原本 6 本 | **開発側と同じく vendored**（provenance と改変境界を記録）。`BUILDING.md:278`「ESP-IDF を複製しない」からの**逸脱として明記**し、段5 で core の `.a` メンバ + `vPort*` シム案を再評価。**段3 で対象が 3 本増えた**: `netif_esp32s3.c` が要求する lwIP contrib ヘッダ 3 本（BSD-3、入れ子 submodule `fd432e4ee2`）を同じ逸脱の枠で受理（Task 1 レビュー承認）。D8 の逸脱は計 9 本（esp-idf 原本 6・lwIP contrib ヘッダ 3）に確定 | `BUILDING.md:278` | 段5 の再評価で置換 |
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
     FORBIDDEN MAC 一覧と期待 MAC 以外は書込み前に rc!=0（FORBIDDEN/偽 MAC は esptool 未呼出・ファイル 0 本。ポートを強制した対照 (c) は read-only の flash-id だけ走り .ident.log を残す）。
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
  負対照 rc=1、cold の起動時 by-id 不在（uhubctl off 後）と出現時刻の記録（電源断そのものと reset 理由はログに無い）、redact "masked and checked clean" 全 run）を確認した上で、
  書込みは 1 回（`task2-A-warm1`）のみ行った。

### 懸念・持ち越し（段3 以降、owner: 段3+）

(a) **`[Arduino] task start` の頭欠け（14/14）と定常時の 1 文字落ち（cold7 で 1/約430 行）**:
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

## 段3 の記録（2026-09-15、commit `760fce9` / `4448a8d` / `a4c346a` / `45122a5` / `8912a35`）

`wifi-connect` stage が建ち、`m5nanoc6_fmp3:FMP3Runtime=wificonnect` で `WiFiScan` /
`WiFiConnect` / `Blink` / `LibraryInfo` の 4 例題がリンクを通った段。実機は使っていない（登記のみ、
書込みは段4）。詳細な証跡は本リポジトリ `.superpowers/sdd/PLAN-stage3-impl/
{task-1-report.md,task-2-report.md,progress.md}` と、開発リポジトリ
`.steering/20260915-c6-arduino-plan/stage3/{AC.md,logs/}`（`task1-*` が Task 1 の採取、
`task2-*` が Task 2 の採取。`task2-supply-table.md` が移し漏れ表、`task2-rom-winners.txt` が
ROM ld 勝者一覧。いずれも dev 側で未 commit）。

- **Task 1**（commit `760fce9` ソース+CMake、`4448a8d` prebuilt `.a`）: dev `c7fef18` の C6
  Wi-Fi 構成（shim / hal / esp-idf 原本 / net / FreeRTOS スタブ / config / `.a`）を
  `wifi-connect` profile として vendoring し、コンパイルまで通した。
- **Task 2**（commit `a4c346a`）: adapter の C6 版（`toppers_wifi_core.{h,c}` / `_connect.c` /
  `_scan.c` / `_optional_stubs.c`）、`attachInterrupt`（`arduino/arduino_interrupt.{c,cfg,h}`）、
  `app/wifi_connect/` を足し、stage を建てて 4 例題をリンクした。

### AC 3a-3j

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 3a | `build_prebuilt_stages.py --chip esp32c6 --profiles wifi-connect --clean` rc=0、stage（objs/ld/lib/manifest/rsp）、重複定義監査 PASS、`check_host_paths.py` rc=0 | PASS | `task2-build-wifi-connect-1.txt`（83 objects / 833 strong definitions / 0 duplicated / 0 allowed）、`task2-check-host-paths.txt`（90 files PASS）。commit 後の `--clean` 再ビルドは 90 本中 89 本 sha 同一（差は `objs/banner.o` の `__DATE__`/`__TIME__` のみ、`task2-build-wifi-connect-2-committed.txt`） |
| 3b | X-check 7/7（scripts/共有 cmake を触った Task ごと）。`git diff --stat main -- ports/m5stack_xtensa src examples third_party` 空 | PASS | `task1-xcheck.txt`（Task 1、`--clean` 再ビルド後）、`task2-xcheck.txt`（Task 2、plain compare。Task 2 が触った共有ファイルは `install_platform.py` の `EXPECTED_PROFILES` だけで stage 生成に無関係） |
| 3c | platform 再生成で `m5nanoc6_fmp3` の FMP3Runtime に `wificonnect` が出る。Xtensa の boards/platform.txt は不変（cmp） | PASS | `task2-boards-platform-diff.txt`（`boards.txt` の差は `m5nanoc6_fmp3.menu.FMP3Runtime.wificonnect` 3 行の追加のみ、`platform.txt` は cmp 同一）、`task2-install-platform.txt` |
| 3d | `arduino-cli compile -b toppers:esp32:m5nanoc6_fmp3:FMP3Runtime=wificonnect` で 4 例題 rc=0、C-1..C-8 PASS（C-8 の使用量）、`nm -u` 空 | PASS | `task2-compile-c6-{WiFiScan,WiFiConnect,Blink,LibraryInfo}-wificonnect.txt`。C-8（Task 2 時点）: WiFiScan 520832、WiFiConnect 592416、Blink 111232、LibraryInfo 111280（分母 1310720）。最終 ELF の `nm -u` は 4 本とも 0（`task2-nm-u-elf.txt`）。**fix wave（`8912a35`、`notify_link_if_started` gate 追加分）で再計測: WiFiConnect 592,560 B、WiFiScan 520,688 B**（RAM は不変、下記「size と RAM 余裕」参照）、C-1..C-8 とも再度 PASS、`nm -u` 引き続き 0（`task2-fw-compile-c6-{WiFiScan,WiFiConnect}-wificonnect.txt`） |
| 3e | 移し漏れ表: dev 供給表の 261 記号それぞれについて arduino 側の供給元を 1 行ずつ。UNRESOLVED 0 | PASS | `task2-supply-table.md`、`task2-nm-u-stage.txt`。内訳は下記「供給表の要約」 |
| 3f | ROM ld 勝者一覧と、kernel/shim の関数が置換されていないことの確認（stage obj の定義 vs ROM ld 代入の交差） | PASS | `task2-rom-winners.txt`。内訳は下記「ROM ld 勝者」 |
| 3g | `size`・RAM 余裕（LENGTH 0x6E610）。`.iram1` 群が RAM セグメントに載る（C-6） | PASS | `task2-size.txt`。内訳は下記「size と RAM」 |
| 3h | R5: 生成 `kernel_cfg.c` に Xtensa 線 0-3 の inthdr が無く、`-DTOPPERS_ESP32C6` を外す対照で cfg が止まる（1 回） | PASS | `task2-r5-kernel_cfg.txt`、`task2-r5-control.txt`。内訳は下記「R5」 |
| 3i | `IMPORT_PROVENANCE.md` に全件、`THIRD_PARTY_NOTICES.md` に D8 の逸脱と `.a` の由来/ライセンス、`wifi/prebuilt/*/esp32c6/README.md` に sha256 | PASS（本コミット） | `ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`「段3 Task 1」「段3 Task 2」節、`THIRD_PARTY_NOTICES.md`、`wifi/prebuilt/{wpa2,lwip}/README.md`（Task 1 で作成済み） |
| 3j | minimal の非退行: 段1/2 の 3 例題が引き続きリンクし sha 不変（minimal stage を触らないこと） | PASS | `task2-minimal-sha-compare.txt`（50 本中 49 本 sha 同一、差は `objs/banner.o` のみ）、`task2-compile-c6-{Blink,LibraryInfo}-minimal.txt` |

### 判断 S3-1..S3-6 の結果

| # | 判断 | 結果 |
| --- | --- | --- |
| S3-1 | libc 供給 | `newlib_syscalls.c` を wifi-connect でも維持（`esp_shim_libc.c` と同名記号を持たない。probe / 本ビルドとも重複定義監査 0 duplicated）。ただし ROM `rand()` が `syscall_table_ptr` を未初期化のまま辿りうる経路（段1 の M-6 parity gap）は解消していない（下記「ROM ld 勝者」参照、watch item のまま） |
| S3-2 | lwIP | D7 = 開発側の型（自前 `liblwip.a` + `netif_esp32s3.c` + `port/sys_arch.c`）で通した。`netif_esp32s3.h` は address しか公開しないため、gateway / netmask は lwIP の `netif_default` を adapter 側で読んで補う（vendored ファイルは無改変） |
| S3-3 | OPEN AP | D6 = 表を差し込まない。`toppers_wifi_core.c`（C6）は `esp_wifi_init()` に supplicant を任せ、空パスワード要求時は NOTICE を出して driver へそのまま渡す |
| S3-4 | attachInterrupt | 線 19。`arduino_interrupt.c`（C6 版）が GPIO 割込みソース（`ETS_GPIO_INTR_SOURCE`=30）を kernel の `_kernel_esp32c6_intmtx_route` で配線。コンパイル・リンクまで確認、動作確認は段6 |
| S3-5 | esp-idf 原本 6 本 | D8 = vendored（`periph_ctrl.c` `modem_clock.c` `modem_clock_hal.c` `efuse_hal.c` x2（うち1本は `efuse_hal_esp32c6.c` に改名） `phy_init_data.c`）。段5 で再評価する方針は不変 |
| S3-6 | APM 解除 | dev と同じ adapter 内（vendored `esp_wifi_adapter.c` の `c6_apm_unblock`、`esp_wifi_init()` 中の osi コールバックから呼ばれる）。adapter 自身は何も足していない。CMake option `TOPPERS_C6_APM_UNBLOCK`（既定 ON）を段4 の対照用に残した |

### vendored inventory の要約（`runtime/wifi/`、計 99 本）

| 出自 | 本数 | 内訳 |
| --- | --- | --- |
| dev `c7fef18`（バイト同一） | 82 | shim 26（.c 15 + .cfg 2 + .h 8 + `IMPORT_PROVENANCE_c6.md`）、hal_src 6、freertos_stub 14、net 8、config/hal_stub_include 24、prebuilt `.a` 4 |
| esp-idf v5.5.4 `735507283d` 原本（D8 の逸脱） | 6 | `idf_src/{periph_ctrl,modem_clock,modem_clock_hal,efuse_hal,efuse_hal_esp32c6,phy_init_data}.c`。Apache-2.0 ヘッダ保持。`efuse_hal_esp32c6.c` は basename 衝突のためファイル名のみ改名（中身はバイト同一） |
| lwIP contrib ヘッダ（D8 の逸脱の追加分、入れ子 submodule `fd432e4ee2`） | 3 | `net/lwip_contrib_include/{ping,tcpecho_raw,udpecho_raw}.h`。BSD-3-Clause。M5Stack core の SDK が contrib apps を含まないため、`netif_esp32s3.c` の `#include` を満たすヘッダだけを同梱（実体は `liblwip.a` の中） |
| prebuilt `.a`（sha256・生成台本つき） | 4 | 下記「prebuilt `.a`」表 |
| README・上流ライセンス本文（新規） | 8 | `prebuilt/{wpa2,lwip}/README.md` 2 本 + `net/lwip_contrib_include/README.md` 1 本（段3 Task 3 で追加、lwIP contrib ヘッダの出典 commit とライセンスを記す）+ ライセンス本文 5 本 |

**合計 82+6+3+4+8 = 99 本。** dev 由来 82 本は `git show c7fef18:<path> | cmp` でバイト同一を
確認済み（Task 1 実測、集合の導出は dev `build/c6-wifi/build.ninja` の 31 TU と `ninja -t deps`
のヘッダ閉包から機械列挙）。Task 2 が新規に足した adapter / `attachInterrupt` / app の 11 本
（vendoring ではなく Xtensa port からの派生）はこの 99 本に含まない。

#### prebuilt `.a`（`runtime/wifi/prebuilt/{wpa2,lwip}/esp32c6/`）

| アーカイブ | sha256 | 生成台本（開発リポジトリ） | 由来 |
| --- | --- | --- | --- |
| `libsupplicant.a` | `DEEA3B35...` | `esp/boot/build_wpa_libs_espidf_esp32c6.sh` | ESP-IDF v5.5.4 `735507283d` の WPA supplicant、BSD-3-Clause |
| `libmbedcrypto.a` | `AB0B175A...` | 同上 | mbedTLS 3.6.5（`ffb280bb63`）、Apache-2.0 OR GPL-2.0-or-later |
| `libmbedtls.a` | `993C0C95...` | `esp/boot/build_mbedtls_tls_espidf_esp32c6.sh` | 同上。Xtensa 側には無い（C6 の supplicant の EAP-TLS 経路が要求、WPA2-PSK 経路では未使用と実測済み） |
| `liblwip.a` | `85859F70...` | `esp/boot/build_lwip_lib_espidf_esp32c6.sh` | ESP-IDF v5.5.4 の lwIP（BSD-3-Clause）。`LWIP_DNS 0`（段4 で扱う、下記「段4 の入口条件」） |

いずれも開発リポジトリ `c7fef18` 時点で 1 回建てた物をそのまま持ち込む（sha256 全文と由来は
`wifi/prebuilt/{wpa2,lwip}/README.md`）。`check_host_paths.py` PASS（ビルド機の絶対パス無し）。

### 供給表の要約（AC-3e、`task2-supply-table.md`）

dev の非 blob 記号 261（供給元表）を WiFiConnect 最終 ELF の link map / `nm` で 1 記号ずつ
引いた結果:

| arduino 側の供給元 | 件数 |
| --- | --- |
| ROM ld（13 本のどれかの代入） | 237 |
| stage obj | 17 |
| stage `.a`（`hexstr2bin` = libsupplicant） | 1 |
| toolchain（`floor` = `libm_nano.a`、`-lm` の本物） | 1 |
| gc'd（定義は stage obj にあるが `--gc-sections` で落ちた。供給元は在るので移し漏れではない） | 5 |
| UNRESOLVED | 0 |

237+17+1+1+5 = 261。UNRESOLVED 0 = 移し漏れ無し。stage 単位の `nm -u`（objs 全体の未定義）は
Task 1 の 108 から Task 2 で 132 に増えたが（adapter・`attachInterrupt` の参照が加わったため）、
132 本すべてに WiFiConnect / WiFiScan それぞれの最終供給元が付いている（unknown 0、
`task2-nm-u-stage.txt`）。

### ROM ld 勝者と `{rand, md5_vector}` の交差（AC-3f）

WiFiConnect 最終 ELF で「13 本の ROM ld が代入する名前」かつ実際に `A`（絶対番地）で解決した
記号は**1129**（pp 353 / rom.ld 307 / phy 224 / libgcc 92 / libc 39 / net80211 37 / newlib 32 /
coexist 23 / systimer 13 / libc-suboptimal 7 / version 2。うち 66 は `esp32c6.rom.pp.ld` の
`//name = addr;` 行が作る実害の無い `//` 付き記号）。

- **stage obj との交差 = `rand`**（`esp_shim_libc.o` の定義に `esp32c6.rom.newlib.ld` の
  `rand = 0x40000590` が黙って勝つ、`--allow-multiple-definition` 下）。
- **stage `.a` との交差 = `md5_vector`**（`libsupplicant.a(wpa_crypto_mbedtls.o)` の定義に
  `esp32c6.rom.ld` の `md5_vector = 0x40000748` が勝つ）。
- **どちらも現在の 4 例題では到達不能**（実測）: `rand` を参照するのは
  `libmbedcrypto.a(mb_rsa.o)` だけ、`md5_vector` を参照するのは `libsupplicant.a(wpa_chap.o)`
  だけで、いずれも `--gc-sections` で discard されている（RSA blinding と EAP-CHAP は
  WPA2-PSK STA の経路に無い）。Task 1 の smoke リンク・dev の C6 ELF と同じ 2 記号で、
  段1 の minimal（M-6 parity gap）から引き継いだ状態のまま増減は無い。
- **`syscall_table_ptr`（`0x4087ffd4`）/ `_global_impure_ptr`（`0x4087ffd0`）を張る者は
  この像に居ない**（Xtensa port の `chip_rom_libc.c` に相当するものを C6 は持たない、S3-1）。
  ROM `rand()` がこの表を辿るなら NULL 参照になりうるが、**この 4 例題では `rand` を呼ぶ経路が
  discard されているため顕在化しない**。`random()` 等を呼ぶスケッチを wifi-connect で建てたときに
  初めて意味を持つ**未解決のハザード**として持ち越す（段4 以降の watch item）。
- **minimal との違い**: minimal profile は ROM ld を 2 本（`esp32c6.rom.ld` /
  `esp32c6.rom.api.ld`）に絞ってこの経路自体を避けている（段1）。wifi-connect は
  phy/pp/net80211/coexist/systimer が要る残り 11 本を意図的に戻し、上記 2 件の既知の衝突を
  （到達不能であることを確認したうえで）許容している -- 「3/4 例題が通る」を「任意のスケッチが
  安全」とは読まないこと（段1 と同じ注意）。

### size と RAM 余裕（AC-3g）

| 例題 | image bytes（flash、C-8、Task 2 時点） | image bytes（flash、fix wave `8912a35`） | RAM（LOAD MemSiz） | 余裕（LENGTH 0x6E610=452112 に対し） |
| --- | --- | --- | --- | --- |
| WiFiConnect | 592416 / 1310720 | **592,560 / 1310720**（`task2-fw-*` ログ） | 301,264 B（不変） | 150,848 B |
| WiFiScan | 520832 / 1310720 | **520,688 / 1310720**（`task2-fw-*` ログ） | 264,976 B（不変） | 187,136 B |
| Blink（wificonnect） | 111232 / 1310720 | 111232（未再計測、`notify_link_if_started` の分岐を通らない） | 43,536 B | 408,576 B |
| LibraryInfo（wificonnect） | 111280 / 1310720 | 111280（同上） | 43,536 B | 408,576 B |

fix wave（`8912a35`）は `toppers_wifi_connect.c` に `notify_link_if_started()` のゲートと
Open AP NOTICE の移動を足しただけの `.text` 差分で、**RAM（`.data`+`.bss`）は不変**（コード側の
数バイトのみ増減、WiFiScan は接続イベントハンドラを持たないため一部減少）。固定値として恒久
扱いしないこと（段1 と同じ注意）。

dev の `wifi_sta` 像（302,856 B）・Task 1 smoke リンク（300,496 B）と同程度。`.iram1` 群
（245 input section）はすべて RAM LOAD セグメント（`.data` 出力セクション）に載り、flash 側へは
落ちない（C-6 PASS）。Blink / LibraryInfo が minimal（83 KB台）より約 28 KB 大きいのは、
`--gc-sections` が Wi-Fi 本体を落としても cfg が静的に作る shim のタスク・セマフォ・`NET_TSK` と
kernel 表は残るため。

### R5: define が cfg 経路に届いている（AC-3h）

- **正側**: 生成 `kernel_cfg.c` に Xtensa 線 0-3/5,7,8/23,27 の `esp_shim_inthdr_*` は 0 件。
  `_kernel_inh_table_prc1` は 0x10001..0x1000f（線 1-15 = shim）、0x10011（線17=USJ）、
  0x10013（線19=attachInterrupt の動的 ISR 枠）。`TNUM_CFG_INTNO 18`。
- **対照（負対照）**: `chip_stddef.h` の `#define TOPPERS_ESP32C6`（cfg が読む唯一の定義箇所、
  `-D` ではないので `-U` で消せない）を 1 回だけコメントアウトして scratch でビルドすると
  **`esp_shim.cfg:163-165: error: E_OBJ: intno '1'/'2'/'3' is duplicated in CFG_INT`** で
  cfg pass 2 が止まり rc=1。直後に `git checkout` で復元、`git status` clean を確認、
  `build/prebuilt` には触れていない。
- 以上により、vendored `esp_shim.cfg` の `#ifndef TOPPERS_ESP32C6` は実際に `-D` が cfg 経路に
  届いていることに依存した分岐であり、死んだ `#ifdef` ではないことを実証した
  （`BUILDING.md`「cfg を `#ifdef` で切らない」との整合は `INVESTIGATION.md` 5 節どおり）。

### 正直な失敗の一覧（Low#）

| # | 対象 | 挙動 | 理由 |
| --- | --- | --- | --- |
| Low#1 | `toppers_fmp3_wifi_host_by_name()` | 数値表記の IP（`a.b.c.d` 形式の文字列）は `ip4addr_aton` で正しく解決する。**名前解決は 0（失敗）を返す** | `liblwip.a` が `LWIP_DNS 0` で建っており `lwip_getaddrinfo` / `dns_gethostbyname` が存在しない。初回に WARNING、以後毎回 WARNING を出す（下記「adapter が出すマーカー文字列」） |
| （派生） | `toppers_fmp3_wifi_tcp_request()` | 宛先が名前のとき Low#1 経由で失敗（-1） | 同上 |
| （設計） | `attachInterrupt()` の未対応モード（mode 0、`>= GPIO_INTR_MAX`、`ONLOW_WE`/`ONHIGH_WE`） | attach せず WARNING（下記） | 黙って「発火しない登録」を作らない |
| （既知・未検証） | Open AP（空パスワード） | `esp_wifi_init()` に supplicant を任せたまま driver へ渡す。NOTICE を出す（下記） | D6。Xtensa で観測された「常時 supplicant だと `AUTH_EXPIRE`」が C6 でも起きるかは段4 の実機で測るまで不明 |
| （既存） | `toppers_wifi_optional_stubs.c` の weak 群 | Xtensa と同じ失敗値 + 1 回の WARNING | wifi-connect では全部本体が勝つ（stub は discard、map で確認） |

成功扱いのスタブは足していない。

### `-L@STAGE@/lib` の順序依存（レビュー指摘、要恒久記録）

`runtime/CMakeLists.txt` の `linkLibGroup` は
`-L@STAGE@/lib -L@SDK_LIBRARY_ROOT@ -L@SDK_LD_ROOT@ --start-group ... --end-group` の順で、
**stage 側（dev の台本で建てた自前 `.a`）を SDK のライブラリ根より先に探させる**。ld は
`-l<name>` を検索パスの先勝ちで解決するため、この順序を逆にすると `-lsupplicant` /
`-lmbedtls` / `-lmbedcrypto` / `-llwip` が SDK 側の同名アーカイブ（存在するなら ESP-IDF/FreeRTOS
前提の別物）に**リンクエラー無く**すり替わる可能性がある（`BUILDING.md`「多重定義もリンクでは
捕まらない」と同型の罠）。Task 1 レビューが指摘した Minor 項目で、恒久的な注記としてここに記録する。

### 段4 の入口条件

- **creds はスケッチに書く**（`examples/WiFiConnect/WiFiConnect.ino` の `WIFI_SSID` /
  `WIFI_PASSWORD`。stage は creds を持たない、Task 1 の決定どおり）。手順:
  1. 採取直前にスケッチをローカルで編集し実値を入れる。**入れる値は開発リポジトリの
     `esp/boot/wifi_credentials.sh` の `WIFI_STA_SSID` / `WIFI_STA_PASS` と一字一句同じで
     なければならない**: `scripts/capture_c6_usj.sh` の EXIT トラップは redact の針
     （マスク対象として探す文字列）をそのファイルの値からしか作らない。別の AP の値や
     手で変えた値を入れると、ログに混入してもマスクされず**そのまま残る**。
  2. 採取時は `LOG_DIR` を段4 のログ保存先（`.steering/20260915-c6-arduino-plan/stage4/logs/`
     等、開発リポジトリ側）に設定してから台本を起動する。既定のままだと redact 済みログが
     どこに置かれたか分からなくなる。
  3. 採取は `scripts/capture_c6_usj.sh`（段2 の台本）だけで行う。その EXIT トラップの redact は、
     開発リポジトリの creds ファイル（`esp/boot/wifi_credentials.sh`）が存在すればそれを針として
     読み、ログに混入した実値を伏字化する。
  4. 採取後、commit 前にスケッチの値をダミーへ戻す。
  5. **本リポジトリには commit 時点の secret guard が無い**（`scripts/check_no_secrets.sh` +
     `.githooks/pre-commit` は開発リポジトリ側の仕組みで、本リポジトリには存在しない）。
     戻し忘れをフックが止めてはくれないので、**`git add` の前に必ず
     `git diff --exit-code examples/WiFiConnect/WiFiConnect.ino` を打ち、終了コード 0
     （差分なし = ダミーへ戻っている）を確認してから add する**。差分が出たら add せず、
     まず値を戻す。
- **APM 制御オプションを 0-AP 対照に使う**: `runtime/CMakeLists.txt` の `TOPPERS_C6_APM_UNBLOCK`
  （既定 ON）を `OFF` にした build を段4 の 0-AP 対照とする（開発側は OFF で scan が 0 AP に
  なることを実測済みだが、本リポジトリ側の build ではまだ回していない -- 段4 で行う）。**必ず
  既定の `build/prebuilt` とは別のディレクトリへ出す**:
  ```bash
  python3 scripts/build_prebuilt_stages.py --chip esp32c6 --profiles wifi-connect \
      --cmake-define TOPPERS_C6_APM_UNBLOCK=OFF --output-directory <separate dir>
  ```
  **既定の `build/prebuilt` へ建てると、`install_platform.py --prebuilt-stage-root build/prebuilt`
  が次に組む platform はこの対照（APM OFF = 常に 0 AP）を本番の腕として据えてしまう**（気付く
  手掛かりが無いまま、以後 wifi-connect で建てるどのスケッチも scan で 0 件になる）。段4 の実機
  試験がこの対照から本番へ戻すのを忘れないよう、`--output-directory` を必ず分けること。
  診断用の `TOPPERS_C6_WIFI_DIAG=ON`（既定 OFF、Task 1 で追加済みの option）を試すときも
  同じ経路（`--cmake-define TOPPERS_C6_WIFI_DIAG=ON --output-directory <別の dir>`）を使う
  こと -- 既定の出力先を上書きしない。
- **DNS**: `liblwip.a` を単独で `LWIP_DNS=1` へ建て直すだけでは足りない。
  vendored `wifi/net/port/include/lwipopts.h`（`LWIP_DNS 0`）は 2 箇所から同時に読まれている:
  (1) 開発リポジトリの `build_lwip_lib_espidf_esp32c6.sh` が `liblwip.a` をコンパイルすると
  きの `lwipopts.h`、(2) `runtime/CMakeLists.txt` の `WIFI_SDK_INCLUDE_DIRS`（`net/port/include`
  が include path に載る）経由で、本 stage の TU（`netif_esp32s3.c` / `port/sys_arch.c` /
  adapter の `toppers_wifi_connect.c`）が `#include "lwip/dns.h"` 等をコンパイルするときの
  `lwipopts.h`。**アーカイブだけを `LWIP_DNS=1` で建て直し、この vendored `lwipopts.h` を
  `LWIP_DNS 0` のまま残すと、`lwip_getaddrinfo()` はアーカイブの中に実体があっても
  ヘッダ側で宣言されず**（`LWIP_DNS` ガードの `#if` でプロトタイプごと消える）、
  adapter がそれを呼ぶコードを書いた瞬間に暗黙宣言または未定義参照になる。
  ⇒ **段4 Task 0 では、1 つの `lwipopts.h`（`LWIP_DNS 1`）を、(1) 開発リポジトリの
  `build_lwip_lib_espidf_esp32c6.sh` が読む側と (2) 本リポジトリの
  `wifi/net/port/include/lwipopts.h`（vendored、stage の include path）の両方へ同時に
  適用すること**。vendored ファイルの内容変更は「dev 由来ファイルはバイト同一で持ち込む」
  という通常方針（R12）からの**逸脱として明記**し、`IMPORT_PROVENANCE.md` に記録する。
  `liblwip.a` の再生成・`lwipopts.h` の変更・`wifi/prebuilt/lwip/esp32c6/README.md` の
  sha256 更新は**同じコミットで**行うこと（sha と中身がずれた状態を commit 間に残さない）。
  dev の `seam-c6-wifi` golden の `liblwip.a`（`LWIP_DNS 0` のまま）には触れない
  （dev 側は別出力先で建て直すので golden は不変）。

### adapter が出すマーカー文字列（実ソース確認、2026-09-15）

段4 の実機ログを読むときの手がかり。文言は変更されうるため正本は各ソースファイル
（`ports/m5stack_riscv/runtime/wifi/adapter/`、`runtime/arduino/arduino_interrupt.c`）。

| 文脈 | 文字列 |
| --- | --- |
| Wi-Fi 初期化・停止 | `"%s esp_wifi_init=%d"` / `"%s esp_wifi_start=%d"` / `"%s esp_wifi_stop=%d"`（`toppers_wifi_core.c`、LOG_NOTICE） |
| Open AP 未検証 | `"[WiFiConnect] begin: open AP requested - unverified on ESP32-C6 (supplicant is initialized regardless; stage 4)"`（`toppers_wifi_connect.c`、空パスワードの `begin()` のたび。fix wave で `toppers_wifi_core.c` の初回 init から移した -- scan-first のスケッチでも出るように） |
| tcpip 起動前の link 通知（fix wave） | `"[WiFiConnect] link %s before tcpip start: not forwarded to lwIP"`（`toppers_wifi_connect.c`、`netif_esp32s3_start()` 前に STA_CONNECTED/DISCONNECTED が来たとき。lwIP へは渡さない） |
| 接続・DHCP | `"[WiFiConnect] esp_wifi_set_config=%d"` / `"[WiFiConnect] esp_wifi_connect=%d"` / `"[WiFiConnect] DHCP address=0x%08x"`（`toppers_wifi_connect.c`） |
| DNS（段4 Task 0 で Low#1 を実装に置換） | 成功 `"[WiFiConnect] DNS resolved host=%s address=0x%08x"`（LOG_NOTICE）、失敗は 1 行 `"[WiFiConnect] DNS failed host=%s error=%d (%s)"`（LOG_WARNING、`%s` = `timeout` / `unresolved` / `not connected` / `name too long` / `request in progress` / `tcpip mailbox`）。段3 の `"DNS is not built into this runtime"` 行は無くなった |
| TCP | `"[WiFiConnect] socket creation failed"` / `"[WiFiConnect] TCP connect failed host=%s port=%d"` / `"[WiFiConnect] TCP send failed"` |
| attachInterrupt 拒否 | `"arduino_interrupt: attach refused pin=%u fn=%s"` / `"arduino_interrupt: attach refused pin=%u mode=%d (unsupported on this port)"` |

### 継承した dev の診断フック（`netif_esp32s3.c`、段4 の証拠採取用に残す。出荷可否は段5）

vendored `wifi/net/netif_esp32s3.c` は dev のデモ用の挙動をそのまま持っている（無改変で写す方針、R12）:

- **DHCP bound のたびに `net: DHCP bound ip=<a.b.c.d> gw=<a.b.c.d>` を LOG_NOTICE で出す**（`netif_status_cb`）。
- **DHCP bound 直後にデフォルトゲートウェイへ raw API の ping を 1 回（`ping_init`。停止 API は無く、
  足す側を一度きりにしてある = BL-H-8 の訂正、ソースコメント）**。
- **`tcpip_init` 完了時に TCP echo / UDP echo サーバ（ポート 7、`tcpecho_raw_init` / `udpecho_raw_init`）を開く**。

これらは dev が STA/DHCP/ping を実証したときの証拠フックで、**段4 の実機採取ではそのまま使う**（`ip=`/`gw=` 行と
ping の応答が到達の証拠になる）。採取ログの伏字化は `scripts/capture_c6_usj.sh` の redact 段が IPv4 の dotted quad を
`<IPv4>` に置き換える（creds ファイルが無くても IPv4 mask と peer-MAC mask は効く）ので、`ip=`/`gw=` 行を
そのまま文書へ写さないこと。**Arduino のランタイムがこれら（特に port 7 の echo サーバ）を出荷すべきかは段5 で
決める**（外すなら vendored ファイルの改変か adapter 側での抑止 = R12 の分岐点）。

### Arduino IDE の size 行は ld の上限を見ていない（段5 の項目）

`m5nanoc6_fmp3:FMP3Runtime=wificonnect` で WiFiConnect を建てると IDE / `arduino-cli` は
`Global variables use 301240 bytes (91%) of dynamic memory ... Maximum is 327680 bytes.` と出す。
この 327680 は M5Stack core の `m5stack_nano_c6` から継承した **`upload.maximum_data_size`**（FreeRTOS 前提の値）で、
本 port の像を実際に縛るのは `esp32c6_xip.ld` の `RAM LENGTH 0x6E610 = 452,112`（余裕 150,848 B、上記「size と
RAM 余裕」節、ドライバの C-1..C-8 が検査する側）。**91% を「あと 26 KB」と読むのは誤り**。段5 で
`install_platform.py` の板行に `upload.maximum_data_size=452112` を上書きするか（`recipe.size.regex` は既に C6 用に
上書きしている）、値の意味を `README.release.md` に書くかを決める。

### 段4 の watch item: dev のデモ経路との未計測の差、および fix wave が足した経路

Task 2 のレビュー（stage-4 readiness）が挙げた、**dev `esp/app/wifi_sta.c` の C6 経路と adapter の経路の差で、
まだ実機で測っていないもの**。どれもリンクでは分からない。

| # | 差 | 何が起こりうるか（推測） | 段4 で回す対照 |
| --- | --- | --- | --- |
| W-1 | dev は `esp_wifi_start` の直後・`esp_wifi_connect` の前に scan を 1 回打つ（`wifi_sta_c6_scan_run`）。adapter の `begin()` は scan を打たずに connect する | scan 無しの connect で `NO_AP_FOUND` が出る可能性（dev がその条件を測っていない） | 最初の connect が失敗したら、**WiFiScan を先に建てて scan が AP を見つけることを確かめ**、次に scan -> `begin()` の順のスケッチで再試行する。それで通るなら差は W-1 |
| W-2 | scan の後に `begin()` すると adapter は driver を `esp_wifi_stop` -> `set_config` -> `esp_wifi_start` と 1 回サイクルする（Xtensa で `AUTH_EXPIRE` 回避のために実測して入れた順序）。dev の C6 経路にはこのサイクルが無い | C6 の blob / shim で stop -> start が dev と同じ状態に戻るかは未計測（osi の `_wifi_clock_enable` の 2 回目の呼出し等） | scan-then-begin と begin-only の両方を焼き、`esp_wifi_stop=%d` / `esp_wifi_start=%d` の行と接続の成否を比べる |
| W-3 | `attachInterrupt` は線 19 へ配線するコードがリンクされているだけで、一度も発火させていない | GPIO 割込みが線 19 で kernel の dispatcher に届くか、`acre_isr` が通るか、未計測 | 段6（LED / ボタンの例題）。段4 では `attachInterrupt` を呼ぶスケッチを焼かない |
| W-4 | `wifi_event_handler`（STA_CONNECTED/DISCONNECTED）の `stage_log()` は `logtask_flush(0)` を呼ぶ。esp_event_shim は同期呼出しなので、この flush は **Wi-Fi blob の内部タスクの文脈**でそのまま実行される（コメントの是正、上記のコード注記参照）。flush がログタスクを待つ・止めるいずれかの経路を持つなら、blob のタイムアウト系（4-way handshake 等）を遅らせうる | 未計測。段4 で `4WAY_HANDSHAKE_TIMEOUT` が頻発したら、`stage_log()` の呼び先を `syslog()` のみに変えて（`logtask_flush` を落として）同じ AP へ再試行し、頻度が変わるか比べる |

fix wave（`8912a35`）が足した `notify_link_if_started()` のゲート自体は、上記コードコメントの
是正で説明したとおり STA_DISCONNECTED（scan-first の driver サイクル由来）だけを通す設計だが、
**gate の分岐そのものは段4 で未計測**（W-3 と同種、リンクでは分からない）。

### 段4 採取のマーカーと注意

- `scripts/capture_c6_usj.sh` の `MARKERS`（早期終了用の ERE）に、接続成功・DNS 失敗・TCP 応答・
  `begin()` 拒否・lwIP assert の 5 つを渡すと、成功でも失敗でも採取が早く終わる:
  ```bash
  MARKERS='DHCP completed|DNS failed|TCP received|begin: rejected|LWIP-ASSERT' \
      bash scripts/capture_c6_usj.sh ...
  ```
  （`"DHCP completed"` / `"begin: rejected or initialization failed"` / `"TCP received=%d"` は
  `toppers_wifi_connect.c` の実文字列の部分一致。`LWIP-ASSERT` は vendored
  `wifi/net/port/sys_arch.c` の `lwip_port_assert_fail()` が出す `"[LWIP-ASSERT] ..."` の部分一致）。
- **台本の `n_unexp` 集計（既定の異常判定パターン）には `[LWIP-ASSERT]` が入っていない**
  （現行パターンは `## Unexpected|## Assertion|## Internal|Unregistered (exception|interrupt)|
  mcause ?=`、`sys_arch.c` の実際の出力は `## Assertion` ではなく `[LWIP-ASSERT]` なので
  一致しない）。段4 で `scripts/capture_c6_usj.sh` を改修するときは `n_unexp` の正規表現へ
  `\[LWIP-ASSERT\]` を足すこと（本 Task では script は無改変、記録のみ）。
- **WiFiScan は見えた近隣 AP の SSID を全部 printf する。採取ログの scan 行をそのまま文書へ
  貼らないこと**（`CLAUDE.md`「出してはいけないもの」・本リポジトリ `BUILDING.md` と同じ規律。
  自分の AP の SSID だけでなく、近隣の第三者の AP の SSID も写り込む）。

> **是正（D-1、段4 で判明）**: 上記 `MARKERS='DHCP completed|DNS failed|TCP received|
> begin: rejected|LWIP-ASSERT'` は**誤り**だった。`"DHCP completed"` は adapter の
> `"[WiFiConnect] connected and DHCP completed"` にも一致するため、DNS 解決・TCP 受信より
> **前**に採取が早期終了してしまい、`dnsok`/`tcp` のカウントが構造的に 0 になる（実測は段4
> Task 2、下記「段4 の記録」参照）。段4 では TERMINAL な行だけに絞った
> `MARKERS='TCP request failed|DNS failed|begin: rejected|connection timeout|LWIP-ASSERT'`
> （固定 `CAPTURE_SEC=45`）を使った。成功時は早期終了せず 45 秒間全体を採取する
> （`ping gateway -> OK` の複数回カウントもここから得た）。台本（`capture_c6_usj.sh`）自体は
> 本段・段4 とも無改変（マーカーは呼び出し側の引数）。

### Xtensa の切り分け順を C6 にも適用する

`CLAUDE.md`「実機の単発失敗を実装のせいにしない」の表（`NO_AP_FOUND` = AP がまだ上がっていない、
`AUTH_FAIL` = 一時的な拒否、`4WAY_HANDSHAKE_TIMEOUT` = パスワードが違う）は Wi-Fi ドライバ blob と
supplicant が Xtensa と C6 で共通（同じ ESP-IDF v5.5.4 系列）なので、**C6 の段4 の切り分けにも
そのまま適用する**: scan で見えるか確認 -> 再試行する -> 変更を `git stash` して基準側でも
再現するか確かめる、の順。本段はリンク時点で実機ログが無いため、上記マーカー文字列の存在確認
までが本段の射程。

## 段4 の記録（2026-09-15、commit `6602cd7`（Task 0）/ `211a067`・`f7da79e`（Task 1）、Task 2 はコード変更なし、記録 `265bfd2`、最終レビュー是正 fix wave `a251202`（台本）・`fafe685`（本節））

M5NanoC6 の実機で `WiFiScan`（scan）と `WiFiConnect`（STA -> DHCP -> DNS -> TCP）を、ユーザーの
実 AP（WPA2-PSK）に対して回した段。詳細な証跡は本リポジトリ
`.superpowers/sdd/PLAN-stage4-impl/{task-0-report.md,task-1-report.md,task-2-report.md,progress.md}`
と、開発リポジトリ `.steering/20260915-c6-arduino-plan/stage4/{AC.md,logs/}`
（`task0-*`/`task1-*`/`task2-*`、いずれも dev 側で未 commit）。

- **Task 0**（commit `6602cd7`）: arduino 専用 `liblwip.a`（`LWIP_DNS=1`）を再生成し、
  `hostByName` を実装に置換した。dev 側のビルド台本は無改変。
- **Task 1**（commit `211a067` = 採取台本、`f7da79e` = scan adapter の SSID 伏字）:
  `scripts/capture_c6_usj.sh` に Wi-Fi マーカー集計を足し、`WiFiScan` を warm 3 回・
  APM OFF 対照 1 回・ON 復帰 1 回、実機で焼いた。
- **Task 2**（コード変更なし、commit なし）: `WiFiConnect` をユーザーの実 AP（WPA2-PSK）に対し
  warm・真cold・APM OFF 対照・W-2 で焼いた。

### AC 4a-4h

| # | 基準 | 判定 | 根拠 |
| --- | --- | --- | --- |
| 4a | Task 0: 新 `liblwip.a` に DNS 記号あり、dev `seam-c6-wifi` golden MATCH（dev 不変）、4 例題リンク、X-check 7/7 | PASS | `liblwip.a` sha256 `5bfbc3ef...`（473,586 B）に `dns_gethostbyname`/`lwip_getaddrinfo` 等を確認、dev `seam-c6-wifi` app_xip.bin sha256 `2b21d6a5...` = golden MATCH、`WiFiScan`/`WiFiConnect`/`Blink`/`LibraryInfo` rc=0、X-check 7/7 |
| 4b | WiFiScan warm 3/3 で `found N APs` N > 0（採取ログに実 SSID 0 件を grep で示す） | PASS | warm1/2/3 = 12/13/13（下記「WiFiScan 結果」）。5 ログ全件で `SSID=` 行のうち placeholder でないものは **0**（`<SSID-i>` 12+13+13+0+11 = 49 件、実 SSID 0 件） |
| 4c | APM OFF 対照で 0 AP、ON に戻して復帰（軸表つき） | PASS | Task 1: warm OFF 対照 = 0 AP（`lp_apm_func_ctrl` は warm 残留で `0x0`）。Task 2: 真cold OFF 対照（`scan-cold-apmoff`）= 0 AP・`lp_apm_func_ctrl=0x3`（pristine）。ON 復帰 = warm4 11 AP、warm5 で WiFiConnect も復帰（下記「APM 対照」） |
| 4d | WiFiConnect warm: connected / DHCP bound / DNS 解決 / TCP 受信の各 marker >= 1、`## Unexpected`/`mcause=` 0 | PASS（初回 1 回の `NO_AP_FOUND` を除く） | warm1-retry・warm2・warm3 のいずれも connected=1 dhcp=1 dnsok=2 tcp=255、unexpected=0。**warm1（最初の接続試行）は `NO_AP_FOUND` で失敗**、1 回の再試行で成功（下記「正直な観察」） |
| 4e | 真cold 3/3 で 4d と同じ marker | **真cold 3/3（出力のあった run。cold2 は無音 1/6）** | cold1・cold3・cold4 は connected=1 dhcp=1 dnsok=2 tcp=255、unexpected=0（4 回中 3 回が出力あり run で成功、cold2 は 45 秒間 0 バイトの無音採取で成否判定不能。詳細は下記「正直な観察」） |
| 4f | 秘密: repo の `git status` clean、採取ログに creds 針 0（台本の検査）、docs に SSID/IP/BSSID なし | PASS（残存する軽微な懸念 1 件あり） | `git status --porcelain` 0 行、`git diff --exit-code examples/` rc=0（Task 2）。採取ログ 75 本の creds 針 grep = ssid 0 / pass 0、`.UNREDACTED` 0。本節・本文書に SSID/IP/BSSID を書いていない。**是正済み（fix wave、`a251202`）**: `DHCP address=0x%08x`（hex 形の LAN アドレス）が redact 段の対象外で dev 側 `.steering/` ログに残っていた件は、台本に `address=<HEX32>` マスクを足し、既存ログ 9 本（DHCP 9 行 + DNS 応答 18 行 = 27 語）を台本の `C6_REDACT_ONLY=1` モードで機械的にマスクした（手編集なし、residue 0）。**注記**: 生 creds の針（SSID/PASS）は Task 2 の実採取では一度も発火していない（スケッチは creds を印字しないので平文が元々出ない）＝針の経路の positive control は fixture の selftest のみで、実採取では未実証 |
| 4g | W-1..W-3 の結果（begin-only で繋がったか、scan->begin の cycle が通ったか） | PASS（W-4 は不発、観測なし） | 下記「W-1..W-4 の結果」 |
| 4h | minimal/wificonnect 非退行: 4 例題リンク、X-check | PASS | Task 0/1 とも X-check 7/7、minimal stage 非改変（Task 0: sha 比較同一、Task 1: 触っていない）。Task 2 はコード変更なし（非退行は自明） |

### 軸表

**Task 1（WiFiScan、書込み前に固定）**

| 軸 | 本 Task で動かした値 | 固定 |
| --- | --- | --- |
| イメージ | `examples/WiFiScan`（`wificonnect` runtime） | 1 スケッチ・1 板 |
| stage（APM） | ON（既定）/ OFF（`TOPPERS_C6_APM_UNBLOCK=OFF`、別 `--output-directory`） | ON(warm1-3) -> OFF(apmoff) -> ON(warm4) の順で 1 軸ずつ |
| リセット | warm のみ（初回は書込み+実リセット、以後 `NOFLASH=1`） | COLD なし、電源断なし |
| bootloader/ptable | stock（段2 条件 A） | 不変 |

**Task 2（WiFiConnect、書込み前に固定）**

| 軸 | 本 Task で動かした値 | 固定 |
| --- | --- | --- |
| イメージ | `examples/WiFiConnect` の作業コピー（`WIFI_SSID`/`WIFI_PASSWORD` を dev creds ファイルの値と一字一句同じに注入、repo の例題は無改変） | 生 creds は毎回同一 |
| stage（APM） | ON（warm1-3・cold1-4・warm4・W-2）/ OFF（apmoff-warm-write・cold-apmoff・scan-apmoff-warm-write・scan-cold-apmoff） | 1 軸ずつ切替え |
| リセット | warm（書込み or `NOFLASH=1`）/ **真cold**（`COLD=1` + `uhubctl -l 2-3.3 -p 3` 電源断 10 秒 -> 採取開始 -> 2 秒後投入） | uhubctl はこの Task でのみ使用 |
| scan-先行/begin-only | begin-only（既定）/ scan-then-begin（W-2、作業コピーのみの変種を 1 回） | 数えた run は begin-only |
| 認証方式 | WPA2-PSK（ユーザーの実 AP） | Open/WPA3-SAE 用 AP は用意できず未実測（D6） |

### WiFiScan 結果表

| run | stage | scan（N） | ssidraw（実 SSID 漏れ） | apm 行 | 判定 |
| --- | --- | --- | --- | --- | --- |
| warm1 | ON | **12** | 0 | 6 | OK |
| warm2 | ON | **13** | 0 | 6 | OK |
| warm3 | ON | **13** | 0 | 6 | OK |
| apmoff | **OFF** | **0** | 0 | 2 | 想定どおり 0 AP |
| warm4 | ON | **11** | 0 | 6 | OK（復帰） |

### WiFiConnect 結果表

| run | stage | リセット | connected | dhcp | dnsok | tcp | disc（reason） | 判定 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| warm1 | ON | warm（書込み） | 0 | 0 | 0 | - | 1（201 NO_AP_FOUND） | NG（`connection timeout`） |
| warm1-retry | ON | warm（NOFLASH） | 1 | 1 | 2 | 255 | 0 | OK |
| warm2 | ON | warm（NOFLASH） | 1 | 1 | 2 | 255 | 0 | OK |
| warm3 | ON | warm（NOFLASH） | 1 | 1 | 2 | 255 | 0 | OK |
| cold1 | ON | 真cold | 1 | 1 | 2 | 255 | 0 | OK |
| cold2 | ON | 真cold | - | - | - | - | - | **無音採取**（0 バイト、成否判定不能） |
| cold3 | ON | 真cold | 1 | 1 | 2 | 255 | 0 | OK |
| cold4 | ON | 真cold | 1 | 1 | 2 | 255 | 0 | OK |
| apmoff-warm-write | **OFF** | warm（書込み） | 0 | - | - | - | 1（201 NO_AP_FOUND） | 想定どおり非接続 |
| **cold-apmoff** | **OFF** | **真cold** | 0 | - | - | - | 1（201 NO_AP_FOUND） | **対照成立** |
| scan-cold-apmoff（WiFiScan OFF 像） | **OFF** | 真cold | - | - | - | - | - | **scan=0 AP、pristine 読み戻し**（下記「APM 対照」） |
| W-2（scan-then-begin、作業コピー） | ON | warm（書込み） | 1 | 1 | 2 | 255 | 0 | OK、scan=11 |
| warm4/warm5（最終確認・板の最終状態） | ON | warm（書込み） | 1 | 1 | 2 | 255 | 0 | OK |

- `dnsok=2` は `hostByName()`（スケッチ）と TCP 経路内部の名前解決の 2 回で、`dnsfail` は
  全 run で 0（DNS 解決は 9/9 接続 run で成功）。
- `tcp=255` は 9/9 接続 run で一致（256 バイト受信バッファから終端 1 バイトを引いた値）。
- `disconnected reason=` の合計はどの run でも `201 (NO_AP_FOUND)` のみ（`4WAY_HANDSHAKE_TIMEOUT`
  や `AUTH_EXPIRE` は 0 件、W-4 参照）。

### DNS 実装の要約（Task 0）

- vendored `wifi/net/port/include/lwipopts.h` を 3 箇所編集: (1) `LWIP_DNS 0 -> 1`、
  (2) **`MEMP_NUM_SYS_TIMEOUT 8 -> 9`**（サイクリックタイマが DNS 追加で 6 本から 7 本に増え、
  8 のままだと定常状態で pool が満杯になり次の `sys_timeout()` が assert で tcpip スレッドを
  停止させる経路になるため。**9 は旧値と同じ 1 個の余裕を保つ数字**で、恒久的な安全マージンの
  保証ではない）、(3) `#ifndef ERANGE` ガードで `ERANGE 34` を追加（`netdb.c` が要求するが
  dev 側の `hal_stub_include/errno.h` に定義が無く、このリポジトリからは編集できないため）。
- `hostByName` は `tcpip_callback()` + `dns_gethostbyname()`（tcpip スレッド、5000 ms 上限、
  10 ms ポーリング、世代カウンタで遅延コールバックを破棄）で実装。`netconn_gethostbyname` は
  待ちが非有界（lwIP 自身の再試行スケジュール）なので不採用。
- DHCP option 6（DNS サーバ）-> `dns_setserver()` の経路は `esp-idf` の `dhcp.c` で確認済み
  （adapter 側に手動の `dns_setserver` 呼び出しは無い）。
- `liblwip.a` は arduino 専用の別出力（`5bfbc3ef...`、473,586 B）で、**dev 側の golden `.a`
  （`85859f70...`）は無改変**。差分は vendored `lwipopts.h` のみ（決定性は既定引数ビルドが
  dev golden と byte 一致することで実証済み）。
- **恒久化の宿題**: `ERANGE` の本来の置き場所は dev リポジトリ側の `hal_stub_include/errno.h`
  （この定義は dev の C6 ビルドでは未参照なので golden を動かさずに足せる）。そちらへ移せば
  arduino 側の `#ifndef` ブロックは不要になる（Task 0 reviewer の指摘、まだ未実施）。

### APM 対照（レジスタ値）

| 状態 | `hp_func_ctrl` | `lp_apm0_func_ctrl` | `lp_apm_func_ctrl` | scan 結果 |
| --- | --- | --- | --- | --- |
| ON、unblock 後（warm/cold 共通） | `0x00000000` | `0x00000000` | `0x00000000` | N > 0 |
| OFF、**warm**（Task 1、ON 実行の残留状態から） | `0x0000000f` | `0x00000001` | **`0x00000000`（残留）** | 0 |
| OFF、**真cold**（Task 2、`scan-cold-apmoff`） | `0x0000000f` | `0x00000001` | **`0x00000003`（pristine）** | 0 |

Task 1 の懸念（「warm の OFF 対照は LP_APM が ON 側の残留で `0x0` のまま、真の初期値ではない」）
は Task 2 の真cold 対照で解消した: 電源断を経た直後は 3 レジスタとも有効（`lp_apm_func_ctrl=0x3`
を含む）状態で、それでも scan は 0 AP。HP_APM の M1 例外ラッチ（`status=0x00000001`、
`info0=0x00130001`）も真cold 側で確認済みで、0 AP の機序（modem 側アクセスの拒否）が
ON/OFF・warm/真cold のいずれでも一貫している。**ラッチの帰属について**: unblock 前
（`before-unblock`）に M1 ラッチが立っている読み戻しは、OFF の run の直後（Task 1 warm4）
だけでなく **ON の run の直後（Task 2 warm5、scan-first の ON run の後）にも現れる**。
したがって「unblock 前のラッチ = 直前の OFF run が残したもの」とは言えない（ON の scan
でも何かが一度 M1 で拒否されている）。どちらの場合も unblock がラッチを消し
（`after-unblock ... latch: none`）、scan は N > 0 になる。

### WPA3-SAE / Open は未実測（D6）

ユーザーのルーター以外に AP を用意できなかったため、WPA3-SAE と Open（無認証）は
**本段では実測していない**。D6 の判断（C6 では私的 ABI の表を差し込まず `esp_wifi_init` に
supplicant を任せる）自体は段3 の実装どおりで変更なし。実測できたのは WPA2-PSK のみ
（真cold 3/3 を含む）。Open AP で Xtensa 側に見られた「常時 supplicant だと `AUTH_EXPIRE`」が
C6 でも起きるかは、依然として未確認のまま持ち越す。

### 正直な観察

- **初回接続の `NO_AP_FOUND`（warm1、書込み直後の最初の接続試行 1/9）**: 単発・再現なし。
  CLAUDE.md の切り分け順（scan で見えるか -> 再試行 -> 基準側で確認）に従い 1 回再試行して
  成功した。9 回の begin-only 接続試行（出力のあった run）のうち失敗はこの 1 回のみで、
  この証拠からは実装起因と判断できない。同じ `NO_AP_FOUND` + 非接続のパターンは APM OFF 像
  （想定どおりの失敗）でも決定的に 2/2 再現しており、区別が付く形にはなっている。
- **最初の association 前の `WIFI_EVENT_STA_DISCONNECTED` は terminal（W-5）**: 最初の
  association より前に `STA_DISCONNECTED`（例: `201 NO_AP_FOUND`、C6 warm1 の 1/9）が来ると、
  adapter（C6 も Xtensa も -- `DISCONNECTED` の処理は同じコード）もスケッチ例題も
  `esp_wifi_connect()` を再発行しない。`WiFi.begin()` はスケッチ側のタイムアウトまで
  `WL_CONNECTION_LOST` のまま座り続ける。Xtensa でも同じ挙動（`ports/m5stack_xtensa/runtime/
  wifi/adapter/toppers_wifi_connect.c` を実ソースで確認）で、C6 の回帰ではない。有界の再試行を
  adapter に置くか例題に置くかは段5 で決める。scan-first（W-2、1/1 OK）がこれを避けるかは
  未検証。
- **無音の真cold 採取（cold2、1/6）**: by-id の出現は**採取開始（台本の by-id 待ち開始）から
  3.91 秒後**で、この 6 回（3.55-3.91 秒）の中で最も遅い（`journalctl -k` では電源投入から
  約 1.6-1.9 秒の範囲。`uhubctl ... on` の直後からではなく、台本の待ち開始を起点にした値
  である点に注意）。45 秒の採取窓で 0 バイト。段2（cold5、1/10、同じ起点で 3.86 秒）と
  **同型の現象**で、台本は「USJ が無音」と「アプリがハング」を区別できない。次の電源投入
  （cold3）は即座に成功しており、板のハングやフラッシュ破損の兆候は無い。
- **`DHCP address=0x%08x` の hex 表記が未マスクだった（fix wave `a251202` で是正）**:
  `capture_c6_usj.sh` の redact 段はドット十進表記の IPv4（`ip=`/`gw=` 行）はマスクしていたが、
  adapter の `"[WiFiConnect] DHCP address=0x%08x"`（と `DNS resolved ... address=0x%08x`）が
  持つ hex 形のアドレスは対象外で、dev 側ログに DHCP 9 行（+ DNS 応答 18 行）残っていた。
  **このログは開発リポジトリの `.steering/` 配下にのみ存在し、公開対象ではない**
  （本文書・本節にも書いていない）。fix wave で transformer と独立 checker の両方に
  `address=0x<8 hex>` -> `address=<HEX32>` を足し（selftest に fixture と変異対照）、既存の
  9 本は台本の `C6_REDACT_ONLY=1` モードで機械的にマスクした（手編集なし、residue 0）。
- **`no time event is processed in hrt interrupt on PRC1.`**: Task 2 の warm ON run **7/7**
  （warm1・warm1-retry・warm2・warm3・W-2・warm4・warm5）で起動ごとに 1 回、Task 1 の warm ON
  4/4 も同じ。OFF の run と真cold の run では 0（真cold は起動先頭が USB enumeration 中に
  失われるため、出ていないのか採れていないのか区別できない）。段2 (c) の既知の行で、害は
  観測していない。
- **早期終了マーカーの誤り（D-1）**: 段3 の「段4 の入口条件」節に書いた
  `MARKERS='DHCP completed|...'` が、DNS/TCP より前に採取を打ち切ってしまう誤りだったこと
  が本段で判明し、該当節に是正を追記した（上記「段4 採取のマーカーと注意」節末尾）。
  台本本体は無改変で、呼び出し側の引数を直した。

### creds の運用（実施内容）

- スケッチの**作業コピー**（scratchpad 配下）にのみ実 creds を書いた。値は 1 回の Bash 呼出しで
  開発リポジトリの creds ファイルから読み `sed` で注入し、値そのものは一度も表示していない
  （空欄行 0 件をチェックサムでのみ確認）。
- リポジトリの `examples/WiFiConnect/WiFiConnect.ino` は**無改変**。`git diff --exit-code
  examples/` の終了コード 0（差分なし）で確認し、`git add`・commit の前後とも確認済み。
- 採取ログの redact は開発リポジトリの creds ファイルの値を針として使う（本リポジトリには
  commit 時点の secret guard フックが無いため、台本の EXIT トラップと `git diff --exit-code`
  の手動確認が唯一の防波堤）。**ただし Task 2 の実採取では生 creds の針は一度も発火して
  いない**（スケッチは creds を印字せず、`REDACTED_` トークン 0 = 平文が元々出ていない）。
  針の経路が実際に効くことの positive control は台本の selftest（合成 creds の fixture）
  のみで、実採取ログでは未実証である。

### 板の最終状態

最後の書込みは warm5（`WiFiConnect` の実 creds 入りイメージ）で、ユーザーの AP に接続し
DHCP/DNS/TCP まで完了した状態で終了している。**フラッシュには実 creds が残っている**
（ユーザーの標準運用ルールにより許容。書込み直後にダミー値へ戻すのはリポジトリ側の
ファイルのみで、板そのものを毎回ダミーへ焼き直す運用にはしていない）。stock bootloader/
ptable/`boot_app0` は不変。

### W-1..W-4 の結果

| # | 内容 | 結果 |
| --- | --- | --- |
| W-1 | `begin()` は scan 無しで connect（scan-無しの connect で `NO_AP_FOUND` が出るか） | 出力のあった 9 回の begin-only 接続試行中 8 回は初回で接続、1 回（warm1）は `NO_AP_FOUND` で失敗し再試行で成功。scan 無し自体が原因と断定できる証拠は無い（1 件のみ、非再現） |
| W-2 | scan -> begin の driver cycle（stop -> set_config -> start）が C6 で動くか | **動く**。`esp_wifi_stop` -> `esp_wifi_set_config` -> `esp_wifi_start` -> `esp_wifi_connect` の順で 1 回サイクルし、`AUTH_EXPIRE` も再起動失敗も無く接続・DHCP/DNS/TCP まで完了（scan=11 AP） |
| W-3 | `attachInterrupt` の動作確認 | 段4 では対象外（段6 で扱う。リンクのみの状態は段3 のまま不変） |
| W-4 | STA ハンドラ内 `logtask_flush(0)` が blob のタイムアウト系を遅らせるか | **不発・未計測**。9 回の接続 run すべてで `4WAY_HANDSHAKE_TIMEOUT` は 0 件のため、flush を外す対照を回す条件が発生しなかった |

### 段5 の入口条件

- **`scripts/verify_package.py` に C6 を追加**: 4 板（Xtensa 3 板 + M5NanoC6）× C6 は
  `minimal`/`wifi-connect` の 2 profile を対象に足す（現状 `BOARDS`/`PROFILES` は C6 を含まない、
  段1 から持ち越しの D9/D10 実装の延長）。
- **CI（`.github/workflows/verify-package.yml`）の `for chip in esp32s3 esp32` に `esp32c6` を
  追加**するループ化。
- **`packaging/release-allowlist.json` に C6 向け entry を追加**（`portBaseRepositoryC6`/
  `portBaseCommitC6`/`portBaseCommitDateC6` は本文書冒頭の「出自」節で既に宣言済みだが、
  出荷を許可する allowlist 側のエントリは未追加）。
- **利用者向け文書の更新**: `README.md`（本 commit で暫定の 1 行を追加済み。配布物収録が
  決まった段階で「統合作業中」の記述を外す）、`BUILDING.md`（C6 の vendoring 経路と D8 の
  逸脱 9 本の記述）、`packaging/README.release.md`（M5NanoC6 の導入手順）、
  `THIRD_PARTY_NOTICES.md`（段3 で足した分に段4 の DNS liblwip 差分の由来を追記するか判断）、
  `library.properties`（C6 対応を謳うかどうか）。
- **D8 の再評価**: esp-idf 原本 6 本 + lwIP contrib ヘッダ 3 本（vendored、計 9 本の逸脱）を、
  core の `.a` メンバ + `vPort*` シム案へ置き換えられるか段5 で再評価する（段3 から継続）。
- **Arduino IDE の size 行の分母上書き**: `upload.maximum_data_size=327680`（FreeRTOS 前提の
  継承値）は実際の ld 上限 `0x6E610=452,112` と合わず「91% 使用」のように誤解を招く表示になる
  （段3 で確認済み）。`install_platform.py` の板行を上書きするか、`README.release.md` に
  値の意味を書くかを段5 で決める。
- **`scripts/capture_c6_usj.sh` の 2 つの Minor 是正**（Task 1 レビュー、まだ未実施）:
  `_cnt` カウンタの動的スコープ（関数境界をまたぐ暗黙の共有）を明示化する、selftest の
  fixture の形（現状の埋め込みヒアドキュメント）を読みやすい形に整理する。いずれも挙動は
  変えない整理で、段5 の fix wave 候補。
- **`examples/WiFiScan/WiFiScan.ino` のコメント修正**: 下記「WiFiScan.ino のコメント」参照。
  本 commit で対応済み（chip-neutral な表現）。
- **`MEMP_NUM_SYS_TIMEOUT` の余裕注記**: 現在の値（9、余裕 1）は「サイクリックタイマ + ping の
  1 個」という段4 時点の計算に基づく。将来 lwIP 側の機能を追加してタイマが増える変更をすると
  この余裕が再びゼロになりうるため、変更のたびに `lwip_num_cyclic_timers`（`lw_timeouts.o` の
  `.srodata`）を読み直すことを、次にこの値を触る人への申し送りとする。
- **`ERANGE` の恒久的な置き場所**: 上記「DNS 実装の要約」の宿題（dev 側 `hal_stub_include/
  errno.h` への移設）は段5 でも未実施のまま持ち越し。

## 段ごとの到達点

| 段 | ゴール | 実機 | 状態 |
| --- | --- | --- | --- |
| 0 | X-check の道具と baseline、本文書、C6 の出自宣言 | 不要 | **完了（2026-09-15）。** AC 0a-0h の記録は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage0/logs/` |
| 1 | `build_prebuilt_stages.py --chip esp32c6 --profiles minimal` が stage を出し、`m5nanoc6_fmp3:FMP3Runtime=minimal` で `Blink` / `LibraryInfo` / `TwoFileSketch` がリンクを通る。X-check で Xtensa 不変 | 不要 | **完了（2026-09-15、`07b239b`/`709b36a`/`ffefc52`/`5dbb8d1`/`f40490e` + 最終レビュー是正 fix wave 1）。** AC 1a-1h 全 PASS、記録は「段1 の記録」節 |
| 2 | M5NanoC6 で `Blink` が起動（USJ に banner・`[Arduino] setup complete`・heartbeat）。真cold 5/5・warm 5/5。bootloader 3 通りの表（D1） | 要 | **完了（2026-09-15、`639331a`）。** 条件 A（stock bootloader）で warm 5/5・真cold 9/10（成立（条件付き）、cold5 無音 1 回・再試行後 5 連続）、D1/D5 確定。B/C/80 MHz は未実施（A が成立したため不要）。記録は「段2 の記録」節、AC は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage2/AC.md` |
| 3 | `wifi-connect` stage が建ち、`WiFiScan` / `WiFiConnect` がリンク。`nm -u` 空、ROM ld 勝者一覧 | 不要 | **完了（2026-09-15、`760fce9`/`4448a8d`/`a4c346a`/`45122a5`/`8912a35`）。** AC 3a-3j 全 PASS（`WiFiScan`/`WiFiConnect`/`Blink`/`LibraryInfo` の 4 例題、`8912a35` の最終レビュー是正後の値で確定）、記録は「段3 の記録」節、AC は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage3/AC.md` |
| 4 | M5NanoC6 で scan -> STA（WPA2）-> DHCP -> DNS -> TCP。真cold 3/3 | 要 | **完了（2026-09-15、`6602cd7`/`211a067`/`f7da79e`、Task 2 はコード変更なし、記録 `265bfd2` + 最終レビュー是正 fix wave `a251202`/`fafe685`）。** AC 4a-4h 全 PASS（4e は「真cold 3/3（出力のあった run。cold2 は無音 1/6）」、4f の hex アドレス未マスクは fix wave `a251202` で是正済み）。WPA3-SAE/Open は AP が用意できず未実測のまま（D6）。記録は「段4 の記録」節、AC は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage4/AC.md` |
| 5 | `verify_package.py` 4 板、`check_release_artifacts.py`、CI、文書、D8 の再評価 | 不要 | 次に着手。**段1 から持ち越し（owner: 段5）**: (1) `scripts/verify_package.py` の `BOARDS` / `PROFILES` に `m5nanoc6_fmp3` / C6 の profile を足す（段1 では未改変、C6 は verify の対象外）、(2) CI（`.github/workflows/verify-package.yml` の `for chip in esp32s3 esp32` 2 箇所）に esp32c6 を足す、(3) `packaging/release-allowlist.json` の C6 向け entry（例題を C6 で出荷するときの `boardsManager` / 板ガード）、(4) `scripts/xcheck_compare.py` の `CHIPS` が Xtensa 固定である点の扱い（C6 の golden を持つかどうか）。fix wave 1 で先に済ませたのは `make_package_index.py` の C6 tool 依存の gate（stage の有無で切替え）と `tests.yml` への `test_xcheck.py` 追加のみ。**段4 から持ち越し**: 「段4 の記録」節「段5 の入口条件」（利用者向け文書の更新、D8 再評価、IDE size 分母上書き、`capture_c6_usj.sh` の Minor 是正 2 件、`MEMP_NUM_SYS_TIMEOUT`/`ERANGE` の恒久化） |
| 6（任意） | `attachInterrupt` と RGB LED の例題 | 要 | 未着手 |
