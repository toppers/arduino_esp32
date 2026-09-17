# M5Stamp-P4（ESP32-P4）対応の記録（2026-09-17、段A・実機未使用）

M5Stamp-P4 を 8 枚目の板 `m5stampp4_fmp3` として追加した記録です。計画と調査は
dev `.steering/20260917-stampp4-arduino-plan/{INVESTIGATION.md,PLAN.md}`（判断 P0-P12、
段A = Minimal・SMP、段B = hosted WiFi）。

**この板は実機なしで追加しました**（ユーザー指示: 実機は別の PC）。ここに書いてある
検証はすべて**コンパイル・リンク・X-check**で、実機の結果はありません。実機で見る
項目と期待値は 6 節。段B（WiFi）は段A4（実機）の PASS が入口条件です。

## 1. 板（core 3.3.8 boards.txt + 公式。実測ではない）

| 項目 | 値 | 根拠 |
|---|---|---|
| 上流の板行 | `m5stack_stamp_p4`（name=M5StampP4、`build.board=M5STACK_STAMP_P4`） | boards.txt:5160- |
| チップ | ESP32-P4（RISC-V RV32IMAFC x2、CLIC、360 MHz）。**`chip_variant=esp32p4_es`**（rev v3 未満の silicon 用 SDK。dev の実機は rev v1.3） | boards.txt:5184、dev 段2 |
| flash / PSRAM | 16MB qio 80m / 有り（本 port は未使用） | boards.txt |
| bootloader | **0x2000**（stock M5Stack `bootloader_qio_80m.elf`、`esp32p4_es-libs`） | boards.txt:5186 |
| partition 既定 | `default_16MB`（app0 = 6.5MB） | boards.txt |
| USB | P4 内蔵 USB-Serial/JTAG（書込みとコンソールを兼ねる。dev は `/dev/ttyACM*` で採取） | dev 段2 |
| C6（Wi-Fi） | Stamp AddOn C6 を SDIO で接続（CLK 43 / CMD 44 / D0-D3 45-48 / RST 42）。**段B** | variant `pins_arduino.h`、dev `esp/p4sdio/p4sdio_pins.h` |
| Grove / I2C | SDA 53 / SCL 54（IN: 31/32）、UART TX 37 / RX 38 | variant |

## 2. 決定（計画 P0-P12 の実装結果）

| # | 決定 | 実装 |
|---|---|---|
| P1 | stock bootloader（0x2000） | `esp32p4_es-libs 3.3.8` の bootloader の `.iram_loader.text` が **0x4ff2cbd0** で、dev の `esp32p4_xip.ld` が前提にする値と一致（`readelf -S`）。ld は無改変で成立 |
| P2 | SDK = `esp32p4_es-libs` | `arduino_sdk.SDK_TOOL_NAMES`（chip → SDK 名）と `CHIP_ARCHIVES`（P4 は phy/coex 無し）。他 chip の解決結果は不変 |
| P3 | `ChipVariant` menu を落とす | `install_platform.BOARD_DROP_MENUS`（新設）+ `BOARD_BUILD_OVERRIDES` で `build.chip_variant=esp32p4_es` を固定。負対照: FQBN に `ChipVariant=postv3` → `invalid option 'ChipVariant'` |
| P4 | Minimal = **2 コア** | chip 表 `A1_CHIP_PRC_NUM=2`、`TOPPERS_SEAM_P4_CORE1`（core1 起床）、`app/phase3_p4` の cfg に `CLS_PRC2` の `[P4-CORE2] alive N` 周期タスク |
| P5 | 360 MHz | `SEAM_P4_CLK360`（seam entry が 90 → 360 へ昇圧）。90 MHz の退避路は作らない |
| P6 | driver | `FIXED_VMA_LAYOUTS["esp32p4"]`（chip_id 0x0012、rev 103、loader_seg 0x4ff2cbd0）。C-1/C-2（flash セグメント 2 本・app_desc 先頭）は既存。**C-6 を一般化**（RAM セグメントの参照を alloc PROGBITS 全体に。P4 の seam entry は RAM の `.iram_text`） |
| P7 | toolchain 20260121 | `toolchain-riscv-esp32p4.cmake`（fail-closed）。dev の 20241119 はこの機械にも core にも無い |
| P8 | size 表示 | `upload.maximum_size=6553600`、`maximum_data_size=183248`（= 0x2CBD0。継承値 327680 より小さい） |
| P9 | profile | minimal のみ（`EXPECTED_PROFILES["esp32p4"]`、`BOARD_PROFILES`） |
| P12 | 採取台本 | `scripts/capture_p4_usj.sh`（3 節） |

## 3. 変更の中身

- vendoring（dev `a7453186`）: `arch/riscv_gcc/esp32p4`（19）、`target/m5stamp_esp32p4_gcc`
  （27）、`seam/seam_p4_*`（5）。`esp32p4_xip.ld` に C++ 表を足した以外は**全部バイト同一**
  （`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE_p4.md`）。dev の P4 chip 層は IDF ヘッダを
  読まず ROM ld も要らないので、SDK からは bootloader と存在証明（`esp32p4.rom.ld`）だけを使う。
- `runtime/CMakeLists.txt`: chip 表に P4 列。C5 に無かった論点は**新しい列**（`A1_CHIP_PRC_NUM` /
  `A1_CHIP_START_FILE` / `A1_CHIP_SEAM_EXTRA_SOURCES` / `A1_CHIP_EXTRA_DEFS` /
  `A1_CHIP_CLK_BOOST_DEFINE` / `A1_CHIP_CORE_CLK_DEFINE` / `A1_CHIP_NEWLIB_SYSCALLS` /
  `A1_CHIP_LDFRAG_DIR`、`A1_CHIP_USBJTAG_HAL` 空許容）で吸収し、C6 / C5 の列は書かない
  （既定値 = 従来の挙動）。
- `cmake/prebuilt_stage_p4.cmake`: ROM_LDS 空を許容、**xip ld の `INCLUDE` 断片を stage の写しへ
  インライン**（GNU ld は INCLUDE をカレント / -L からしか探さず、利用者側のリンクはどちらも
  持たない）、flash 16MB/qio。
- `scripts/`: `arduino_sdk`（chip 表）、`fmp3_link`（P4 行、C-6 一般化、`SDK_FALLBACKS`）、
  `install_platform`（BOARDS / BOARD_DROP_MENUS / overrides / SIZE_REGEX / UPLOAD_SIZE /
  EXPECTED_PROFILES）、`build_prebuilt_stages`（CHIPS、CHIP_APPLICATIONS）、
  `make_package_index` / allowlist（esp-rv32 + `esp32p4_es-libs`）、`verify_package`、
  `xcheck_compare`、ドリフト検査、driver テスト（P4 の C-6 併合セグメント・C-9・負対照 4 種）、
  CI yml。`scripts/capture_p4_usj.sh`（新規、selftest 付き）。

### M5Stack core 3.3.8 の欠陥（回避策つき）

`arduino-cli compile -b m5stack:esp32:m5stack_stamp_p4` は**空のスケッチでもコンパイルできない**
（`cores/esp32/esp32-hal-spi.c:299: 'BOARD_SDMMC_POWER_CHANNEL' undeclared`。variant
`m5stack_stamp_p4/pins_arduino.h` が定義していない。Tab5 の variant は `4` を定義している）。
Arduino はスケッチのたびに core 全体をコンパイルするので、本板も同じ場所で落ちる。
⇒ 板行の `build.extra_flags.esp32p4`（platform の値に `-DBOARD_SDMMC_POWER_CHANNEL=4` を
足したもの）で回避。**値は使われない**（FMP3 のリンクは core のオブジェクトを 1 本も
取らない）。core の版が動いたら要再確認。

### 採取台本 `scripts/capture_p4_usj.sh`

`capture_c5_usj.sh` の写しではなく、**書込みの可否を決めるゲートだけ**を残した小さな台本
（DUT_MAC 必須・FORBIDDEN・boards.txt の bootloader 番地 0x2000・書込み後の bootloader
領域 readback 一致・`stty`+`cat` で採取・マーカー計数・selftest）。板が別 PC にあるので
既定の DUT は無い。使い方は台本冒頭。esptool は M5Stack core 同梱の 5.2.0（サブコマンドは
`flash-id` / `write-flash` / `read-flash` / `chip-id`、reset は `hard-reset` / `watchdog-reset`）。

## 4. `m5` / `btclassic` / `wificonnect` を出さない理由

画面なし（`m5`）、P4 自身に無線なし（`btclassic`）。`wificonnect` は hosted Wi-Fi
（SDIO → AddOn C6 → esp-hosted RPC → lwIP）で、dev では **Tab5 実機でのみ**実証
（`esp/p4hosted` 約 57k 行 + `p4sdio` / `p4shim`）。Arduino の `ToppersFMP3_WiFi` が要求する
`toppers_fmp3_wifi_*` 層が dev に無く（手順は `rpc_probe.c` 3,494 行のプローブに埋まっている）、
その抽出が段B の主題。

## 5. この機械で測ったこと（実機なし）

| 対象 | 結果 |
|---|---|
| esp32p4/minimal stage | 49 objects、重複定義 0、`nm -u` 空。`_kernel_target_mprc_initialize` が `seam_p4_core1_start` を `jal` する（objdump。dev が踏んだ「fmp にだけ -D を渡して起床コードが消える」罠を回避） |
| `m5stampp4_fmp3:FMP3Runtime=minimal` | **Blink / LibraryInfo / TwoFileSketch がリンク**（Blink.bin 88,880 B） |
| driver の像検査 | **C-1..C-9 満足**: mapped 2（DROM 0x40100020 4,896 B = app_desc+rodata、IROM 0x40000100 23,052 B）、RAM 2（`.iram_text` 0x4ff00000 796 B、`.data` 0x4ff00320 30,384 B）、entry 0x4ff002d0、C-8 88,880/6,553,600、C-9 chip_id 0x0012 |
| 負対照 | (a) FQBN `ChipVariant=postv3` → `invalid option`。(b) stock `m5stack_stamp_p4` は空スケッチでコンパイル不能（上記）。(c) driver テスト: P4 行で C-6（`.iram_text` のバイト違い・セクション外へはみ出す RAM）/ C-9（C5 の chip_id）/ C-5（loader_seg へ達する RAM）が各々止まる |
| X-check | 11 stage（S3 3 + LX6 4 + C6 2 + C5 2）を clean で建て直して **11/11 MATCH**（`esp32p4` は ignored） |
| python テスト 4 本 / `check_host_paths`（1,128 files） | PASS |
| `capture_p4_usj.sh` | selftest 4 群 PASS。板なしのゲート順（DUT_MAC 無し → FORBIDDEN → 書式 → 像 → ポート不在）で止まることを実演 |
| `--list-builds` | 105 → **108**（8 板） |

## 6. 別 PC での実機確認（段A4。未実施・期待値つき）

板: M5Stamp-P4（+ AddOn C6 は段A では使わない）。ポートは P4 内蔵 USB-Serial/JTAG。

```bash
# 1) 板の MAC を読む（1 回だけ。台本はこの値を要求する）
~/.arduino15/packages/m5stack/tools/esptool_py/5.2.0/esptool --chip esp32p4 --port /dev/ttyACM0 --no-stub flash-id
# 2) 建てる（導入済み platform）
arduino-cli compile --fqbn toppers:esp32:m5stampp4_fmp3:FMP3Runtime=minimal --library . --output-dir /tmp/p4-blink examples/Blink
# 3) 焼いて採取（ゲート → 書込み → readback → reset → 30 s 採取 → 計数）
DUT_MAC=<1 の MAC> OUT_DIR=/tmp/p4-blink bash scripts/capture_p4_usj.sh
```

| 順 | 見るもの | 期待 |
|---|---|---|
| 1 | 先頭バイト | `S`（seam entry・core0）と `C`（core1 entry）。`C` が無ければ core1 が起きていない（dev 段6 と同じ切り分け） |
| 2 | バナー | `TOPPERS/FMP3 Kernel Release 3.4.0 for M5Stamp ESP32P4 <HP RV32IMAFC, RISC-V>` と `Processor 2 start.` |
| 3 | `[Arduino] loop heartbeat N` | 1 秒ごと（PRC1） |
| 4 | `[P4-CORE2] alive N` | 1 秒ごと（PRC2）。**これが SMP の証拠** |
| 5 | warm 5 回・真cold 5 回 | 台本を 5 回、電源断 5 回（USB 抜き差し） |

- 採取が空で 1 が通っているなら `RESET_MODE=watchdog-reset`（dev が Tab5 で見た「RTS リセット後に
  ダウンロードモードに留まる」症状。StampP4 では出なかった）。
- 落ちたときの退避路: 1 コア（`--cmake-define`… 未実装。chip 表の `A1_CHIP_PRC_NUM` を 1 にして
  stage を建て直す）、dev 製 bootloader（`esp/boot/seam_p4/`）へ 1 軸 A/B。
- ログに SSID / IP は出ない（Wi-Fi 無し）。

## 7. 残り

- 段A4（実機）。結果が出たら 5 節と README を実測に置き換える。
- 段B（hosted WiFi）: 入口条件は A4 PASS。計画 PLAN.md 段B0-B4。
- 1 コアの退避路を `--cmake-define` で出す（今は chip 表の定数）。
