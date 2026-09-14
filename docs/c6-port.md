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
| D1 | bootloader | **stock M5Stack bootloader（同梱なし）を第一候補。** 段2 で stock / 開発側 seam bootloader / +開発側 ptable の 3 通りを 1 軸ずつ実測して確定 | 本リポジトリは bootloader を出荷していない（`BUILDING.md`・`README.md`・`packaging/README.release.md`・`scripts/install_platform.py` に bootloader の記述なし）。M5Stack platform.txt の prebuild hook を継承 | 同梱経路 = 板別の prebuild hook 上書き |
| D2 | port ディレクトリ | `ports/m5stack_riscv` | `ports/m5stack_xtensa/` と並ぶ。`scripts/build_prebuilt_stages.py:161`（runtime）・`:195-198`（app）の固定パスを chip -> port の表にする | 改名は機械的 |
| D3 | 共有 cmake | `prebuilt_stage.cmake` は共有して chip 分岐、chip 固有（seam 画像検査等）は `prebuilt_stage_c6.cmake` へ分離 | `ports/m5stack_xtensa/runtime/cmake/prebuilt_stage.cmake:57-62`（`A1_CHIP` 白リスト）ほか | 二重保守 vs 波及。どちらも X-check が検出 |
| D4 | manifest / driver 版 | **上げる**（`DRIVER_VERSION` 3、schema に `paddrMode` の新値と `linkBaseFlags` を加法で追加） | `scripts/fmp3_link.py:51`（`DRIVER_VERSION`）・`:53`（`MANIFEST_SCHEMA`）・`:161`（`paddrMode == "runtime-mmu"` 必須）・`:317`（`-mlongcalls`）・`:334`（`-lgcc -lc`） | 旧 manifest を読む経路が無いことを確かめて戻せる |
| D5 | C6 の CPU クロック | **minimal も wifi-connect も 160 MHz**（開発側は wifi=160 で較正済み、Arduino 利用者の期待に合わせる）。段2 の真cold が 160 で落ちたら minimal を 80 に戻す（1 軸） | -- | 段2 で判明する |
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

## 段ごとの到達点

| 段 | ゴール | 実機 | 状態 |
| --- | --- | --- | --- |
| 0 | X-check の道具と baseline、本文書、C6 の出自宣言 | 不要 | **完了（2026-09-15）。** AC 0a-0h の記録は開発リポジトリ `.steering/20260915-c6-arduino-plan/stage0/logs/` |
| 1 | `build_prebuilt_stages.py --chip esp32c6 --profiles minimal` が stage を出し、`m5nanoc6_fmp3:FMP3Runtime=minimal` で `Blink` / `LibraryInfo` / `TwoFileSketch` がリンクを通る。X-check で Xtensa 不変 | 不要 | 未着手 |
| 2 | M5NanoC6 で `Blink` が起動（USJ に banner・`[Arduino] setup complete`・heartbeat）。真cold 5/5・warm 5/5。bootloader 3 通りの表（D1） | 要 | 未着手 |
| 3 | `wifi-connect` stage が建ち、`WiFiScan` / `WiFiConnect` がリンク。`nm -u` 空、ROM ld 勝者一覧 | 不要 | 未着手 |
| 4 | M5NanoC6 で scan -> STA（WPA2）-> DHCP -> DNS -> TCP。真cold 3/3 | 要 | 未着手 |
| 5 | `verify_package.py` 4 板、`check_release_artifacts.py`、CI、文書、D8 の再評価 | 不要 | 未着手 |
| 6（任意） | `attachInterrupt` と RGB LED の例題 | 要 | 未着手 |
