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
## 2-5. 段 B1（hosted Wi-Fi の土台、2026-09-18）

この chip の Wi-Fi は**自前の無線ではなく**、SDIO で繋いだ ESP32-C6（Stamp
AddOn）が担います（esp_hosted）。したがって C6 / C5 の `wifi-connect` ブロック
——blob + PHY + supplicant 前提——は 1 行も流用できません。chip 表に
`A1_CHIP_WIFI_KIND`（`native` / `hosted`）を足し、**native ブロックには触れずに**
hosted 用の別ブロックを置きました。

| 段 | 内容 | 結果 |
|---|---|---|
| B1a | hosted 層の vendoring（54 ファイル） | 全数 dev とバイト同一（`cmp`） |
| B1b | lwIP アーカイブ | 481,652 B。dev の台本を `PORT_EXTRA=lwipopts_dns` / `OUT_DIR=<scratch>` で実行（39 本・失敗 0）。dev の `esp/lib` は無改変 |
| B1c | CMake の hosted ブロック・cfg 5 本・ROM ld・app | **stage が建つ**: 68 オブジェクト / 2.5 MB、重複シンボル監査 **0 duplicated** |

**非退行**: 既存 7 板は **X-check 11/11 MATCH**（native ブロックは guard を 1 行
足しただけで中身は不変）。

### B1c で決めた/見つけた 4 点

1. **`sdkconfig.h` は SDK が配っているものを使う**。M5Stack の SDK は
   `esp32p4_es-libs/3.3.8/<memory type>/include/sdkconfig.h` を持っていて、この板は
   `qio_qspi`（boards.txt の `build.boot=qio` / `build.psram_type=qspi`）。C6 / C5 は
   vendoring した stub（`config/<chip>/sdkconfig.h`）を使っていますが、あちらの移植が
   先だったからで、どちらでも構いません。
2. **レジスタヘッダは `hw_ver1` を選ぶ**。SDK は `soc/esp32p4/register/hw_ver1/` と
   `hw_ver3/` を並べて持っていて（silicon 世代の差）、この移植は pre-v3 に固定して
   いるので `hw_ver1`（`soc/sdmmc_struct.h` がその 1 つ）。SDK ディレクトリ名
   （`esp32p4_es-libs`）と同じ判断です。
3. **lwIP の port は dev と同じ場所**（`runtime/eth/lwip_port/`）に置きました。
   vendoring した `hosted/net/lwipopts_dns/lwipopts.h` が
   `../../../eth/lwip_port/include/lwipopts.h` を**相対で**読むためで、置き場を変える
   と vendoring 側を改変することになります。
4. **stage に残る未解決シンボルは 44 個で、すべて外から来るもの**（ld script の
   `__bss_*` 等、`peripherals.ld` が PROVIDE するレジスタブロック、ROM 関数、libc、
   それと sketch 側の `toppers_arduino_task`）。**Arduino 向けアダプタの記号は
   まだ現れません**——それを足すのが段 B2 です。


## 2-6. 段 B2（RPC の切り出しとアダプタ、2026-09-18）

| 段 | 内容 | 結果 |
|---|---|---|
| B2a | `rpc_probe.c`（dev・3,494 行）から「SDIO の上で esp_hosted の RPC を話す部分」だけを `hosted/rpc/p4hosted_rpc.{c,h}` へ（2,016 行） | 持ち込んだ行は 1 行も書き換えず、足したのは入口 5 本のみ。計測・verdict・実験分岐は持ち込まない |
| B2b | `wifi/adapter/toppers_wifi_hosted{.h,_core.c,_scan.c,_connect.c}` | **`WiFiScan` / `WiFiConnect` がリンク**（flash 69,912 / 149,612 B、RAM 170,008 / 171,840 B） |
| B2c | `arduino/arduino_gpio_p4.c` と `arduino/arduino_interrupt_p4.{c,cfg,h}` | **`GpioInterrupt` がリンク**（62,448 B / RAM 161,524 B） |

由来と差分の全数は `ports/m5stack_riscv/runtime/IMPORT_PROVENANCE_p4.md` の 7・8 節。

### B2b で見つけた 3 件（いずれも机上では出ず、リンクで初めて出た）

1. **リンカ断片が profile ごとに別物なのに、置き場所で選ばれていた**。
   `prebuilt_stage_p4.cmake` は `INCLUDE` 断片をリンカスクリプトと同じ
   ディレクトリから読んでいました。断片は minimal 用（コメントだけ）と hosted 用
   （`RAM_HIGH` つき）で**同名・別物**なので、wifi-connect でも minimal 側が入り、
   `undefined reference to __bss_high_start` という**断片ではなくスクリプトを指す**
   遠いメッセージで落ちます。呼び出し側の選択を `LDFRAG_DIR` で渡すようにしました。
2. **断片が stage の依存に入っていなかった**。断片を直しても再 stage されず、
   古いコピーが配られます（実際に 1 度空振りしました）。4 本を依存に足しました。
3. **`p4_bss_high.ld` の規則が、この repo の綴りに当たらない**。dev の
   `*libfmp3.a:target_kernel_impl.c.obj(.bss.target_heap)` は、カーネルを
   アーカイブにせず `.o` のまま 74 本渡す stage には当たりません。64 KB の newlib
   ヒープが low RAM に残り `region RAM overflowed by 52328 bytes`。断片自身の規律
   （アーカイブ名／オブジェクト名で限定する）を保ったまま 1 行足しました。

### B2c の決定

| 項目 | 値 | 根拠 |
|---|---|---|
| CLIC 線 | **23** | 空き線は 20, 21, 23..29, 36..39（`wifi/shim/esp_shim_intr_clic_lines.h` の表）。C5 が取ったのと同じ番号にして、2 つの RISC-V 板のログが同じに読めるようにした |
| 優先度 | `TMAX_INTPRI`(-1) | コンソール（-4）・CLIC シムのスロット（-4）・EMAC（-4）より低い。利用者のコールバックが SDIO を遅らせない |
| 割込みソース | `ETS_GPIO_INTR0_SOURCE`(74) | P4 は GPIO 割込みソースを 4 本持つが、SDK の hal は最初の 1 本しか使わない（`gpio_ll_intr_enable_on_core` が core_id を捨てて `GPIO_LL_INTR0_ENA` を書く。`gpio_ll_get_intr_status` は `intr_0` を読む）。他を選ぶと**永久に来ない線**ができる |
| 配線 | `INTMTX_MAP(0, src) = line` を自分で書く | C6 / C5 と違い P4 の chip 層は route 関数を公開していない。同 repo の CLIC シム（`esp_shim_clic_intr_route`）と**同じマクロ**を使い、2 つが構造的に一致するようにした |
| 拒否ピン | 24/25（USB-Serial/JTAG）と **42..48** | 42..48 は AddOn C6 への SDIO（reset 42・CLK 43・CMD 44・D0-D3 45..48）。この板ではそれが Wi-Fi そのもので、`pinMode` 1 回で無線が落ちる。値は `hosted/sdio/p4sdio_pins.h` から取り（配線とずれない）、M5Stack の variant も同じ値 |
| MSPI の拒否範囲 | **無し** | P4 の flash/PSRAM は GPIO マトリクス上に無い（`soc/spi_pins.h` の `MSPI_IOMUX_PIN_NUM_*` は全部 `GPIO_NUM_INVALID`）。「無いから書かない」ではなく `_Static_assert` で固定した |
| ステータス語 | **2 語**（0..31 と 32..54） | P4 は 55 本。C6 / C5 のファイルは `GPIO_NUM_MAX <= 32` を assert して 1 語しか読まない |
| `GpioInterrupt` のピン | **G16（A0）** | variant の castellated 端。SDIO 7 本・USB 2 本・内部 I2C（31/32）を避けた |

## 2-7. 段 B3（表と台本、2026-09-18）

- `BOARD_PROFILES` / `EXPECTED_PROFILES` / `packaging/release-allowlist.json` /
  CI の stage 表 / ドリフトテストを揃えて `esp32p4` に `wifi-connect` を追加。
  `--list-builds` は **108 → 116**（8 板、P4 は minimal 3 + wificonnect 8）。
  ドリフトテストは**先に落ちた**（`test_tables_match_the_real_allowlist_and_the_generator`）
  ——表を 1 つ直して他を忘れる形を実際に捕まえた、という記録。
- `scripts/capture_p4_usj.sh` に **redact/mask 層**と `EXTRA_MARKERS` と Wi-Fi の
  計数を足した。段A の時点でこの板には Wi-Fi が無かったので mask も無かったが、
  段B で実 AP に繋ぐ以上、S3 / C5 の台本と同じ fail-closed の層が要る
  （needle＝資格情報・peer MAC・IPv4・`address=0x`・**scan 行の SSID 列**。
  残渣が 1 行でもあればファイルを `*.UNREDACTED` へ改名して非 0 終了）。
  selftest は 10 群で、**positive control**（マスクしていないファイルを検査器が
  ちゃんと 4 と数えること、隔離が実際に起きること）を含む。

## 2-8. 段 A4 / B4（実機、2026-09-18）

板がこの機械へ来たので、6 節が「未実施・期待値つき」としていた段A4 と、段B の
実機（B4）を実施しました。板は **M5Stamp-P4 rev v1.3**（MAC `30:ed:a0:ea:98:0e`、
内蔵 USB-Serial/JTAG、flash 16MB。**Stamp AddOn C6 が載っている**ことも実測で
判明しました——下の B-10 の行が一次証拠です）。

### 結果

| 対象 | 構成 | 結果 |
|---|---|---|
| `Blink` warm | minimal | **5/5 PASS**（`heartbeat` / `[P4-CORE2] alive` とも計数、`romboot=1`＝意図したリセットのみ） |
| `Blink` 真cold（USB 給電断） | minimal | **5/5 PASS**（`banner=1 prc2_start=1`＝電源断からの起動で 2 コアが上がる） |
| `GpioInterrupt`（G16 自己駆動） | wificonnect | **VERDICT PASS** `pin=16 rising=5 falling=5 change=10 detached=0 dispatch=20 call=20 orphan=0 acre=8` |
| hosted の立ち上げ | wificonnect | **`[WiFiHosted] companion INIT chip_id=0x0d caps=0x0d`**。計画 B-10 が「最初に見る 1 行」と決めたものが、期待どおりの値で出ました |
| `WiFiScan` | wificonnect | **14〜16 AP**（3 回。`authmode` つき、SSID は `<SSID-N>` placeholder） |
| `WiFiConnect` | wificonnect | **STA の会合までは成功、DHCP に到達せず**（下記） |

### 実機でしか出なかった欠陥 5 件（すべて修正済み・1 件は未解決）

1. **起動ループ（修正済み）**。M5Stack の SDK が配る bootloader は
   `bootloader_config_wdt()` で **RWDT を約 9 秒で仕掛けたまま**アプリへ渡す
   （ESP-IDF のアプリは起動処理で止めるが FMP3 は止めない）。開発リポジトリの
   P4 は**自前の bootloader**（`esp/boot/seam_p4/sdkconfig` に
   `# CONFIG_BOOTLOADER_WDT_ENABLE is not set`）なので踏んでいません。
   `hardware_init_hook` で LP_WDT + super WDT を止めて解消
   （`A1_P4_DISABLE_BOOT_WDT`。C6 / C5 の target が同じ理由で同じことをしている）。
   **名前で犯人を決めないこと**: ROM は `rst:0x7 (HP_SYS_HP_WDT_RESET)` と印字し、
   SDK の `soc/reset_reasons.h` は 0x07 を `RESET_REASON_CORE_MWDT` と呼びますが、
   TIMG0 の MWDT を止めても直らず、**RWDT を止めて直りました**（1 軸ずつ変えて
   帰属を取った結果）。
2. **`[WiFiScan]` の行が 1 行も出ていなかった（修正済み）**。`examples/WiFiScan` は
   **自分では何も印字しません**——観測できる出力はすべて runtime 側の scan
   アダプタが出しています。hosted 版にその印字を書き忘れていたため、RPC 層は
   13 件を解析できているのに採取ログからは「スキャンできていない」と読めました。
   native 側と同じ綴り（`found N APs` / `AP[i] ... SSID=<SSID-i>` / `done`）を追加。
3. **PRC2 の alive タスクが 1 kHz で回っていた（修正済み）**。
   `app/wifi_connect_p4` の `CORE2_ALIVE_PERIOD_MS 1000U` にコメントで
   「1 秒」と書いてありましたが、**この移植の `RELTIM` はマイクロ秒**です
   （minimal 側の同じタスクは `1000000U` を渡している）。25 秒の採取に
   **24,589 行**出てコンソールを埋めていました。
4. **採取台本の 5 件（修正済み）**——(a) `--flash-mode qio` を明示すると esptool が
   ヘッダと末尾 SHA を書き換え、readback 照合が 33 バイト食い違って**正しい書込みを
   失敗と判定**していた → platform の upload recipe と同じ `keep` に。
   (b) `boot_app0`(0xe000) を書いていなかった。(c) マーカー文字列が実際の出力と
   違い（`loop heartbeat <N>` / `setup()` は存在しない）、**動いている板で 0 を
   数えて**いた。(d) `cat` / `stty` の blocking open が真cold で固まり、リセットで
   USB が落ちると再オープンできない → python の O_NONBLOCK + CLOCAL 再オープン式
   リーダへ。(e) 既定を `RESET_MODE=watchdog-reset` に（hard-reset は USB が落ちて
   **初期ログを丸ごと失う**。同じ像で banner=0/prc2_start=0 対 1/1）。
5. **DHCP に到達しない（未解決）**。会合までは通ります——
   `R4-4 set_config resp=0` / `R4-5 wifi_connect resp=0` /
   `R4-6 connected_ap ssid_match=1 rssi_neg=72 chan=10` / MAC 取得 OK。
   そこから 30 秒の DHCP 待ちが空振りし、計器はすべて 0 のままです:
   `rx_total=0 tx_ok=0 tx_fail=0 rx_frame=0 **pump_loops=0** slave_avail=20`。
   **`pump_loops=0` が効く手掛かり**で、`p4hosted_net_rx_thread_start()` は
   true を返している（返さなければ待ちに入らない）のに、スレッド本体の
   1 行目 `RPROBE R2-a rx_thread_entered` が**採取ログに 1 行も無い**
   ——つまり `_h_thread_create` は成功を返したがタスクが走っていません。
   次の一手は osi（`hosted/osi/p4hosted_osi.c`）の `_h_thread_create` が
   hosted 優先度 24 を FMP3 のどの優先度・どのクラス（PRC1/PRC2）へ写すかを
   実測すること。dev の probe は同じコードで通っている（段7f）ので、
   差分はアプリ側の文脈（プローブ専用タスク 対 Arduino タスク＋2 コア）にあります。

### コンソールが行頭を落とす（F-1 の P4 版・未解決）

この板の USB-Serial/JTAG コンソールは**行の先頭を落とします**。多くは 1 文字
（`[P4-CORE2] alive 2` → `[4-CORE2] alive 2`、`[WiFiScan] found 14 APs` →
`[iFiScan] found 14 APs`）ですが、1 文字とは限りません——
`[WiFiHosted] companion ready (STA)` が `on ready (STA)` として着いた実例が
あります（13 文字欠落）。頻度も低くありません（ある採取では alive 60 行中 52 行）。
⇒ `capture_p4_usj.sh` の計数は**タグの尾側に錨を打って**います。厳密な綴りで
数えると**動いている板を壊れていると報告する**ためです。欠落そのものは
`core2_full`（完全な綴りだけを数える）で**測り続けます**——寛容なパターンで
隠さないこと。

### この板の運用

```bash
# 建てる
arduino-cli compile --fqbn toppers:esp32:m5stampp4_fmp3:FMP3Runtime=minimal \
    --library . --output-dir /tmp/p4-blink examples/Blink
# 焼いて採取（ゲート -> 書込み -> readback -> watchdog-reset -> 採取 -> 計数 -> mask）
DUT_MAC=30:ed:a0:ea:98:0e OUT_DIR=/tmp/p4-blink bash scripts/capture_p4_usj.sh
# 真cold（電源断はこの機械では hub 1-1.4 の port 3）
sudo -n uhubctl -l 1-1.4 -p 3 -a off; sleep 3; sudo -n uhubctl -l 1-1.4 -p 3 -a on
NORESET=1 DUT_MAC=30:ed:a0:ea:98:0e bash scripts/capture_p4_usj.sh
```

## 2-9. DHCP 未達の切り分け（2026-09-18）

2-8 節が「未解決」として残した DHCP 未達は**解けました**。原因は 2 件で、
どちらも「返り値を見ても捕まらない」型です。

### 原因 1: cfg のオブジェクト ID が**フォールバック値**で固まっていた

`hosted/osi/p4hosted_pools.c` と `eth/lwip_port/fmp3_lwip_pools.c` は
`kernel_cfg.h` を include せず、`#ifndef HOSTED_TSK1` のような**フォールバックの
ID 一覧**を自前で持っています。その値は「書かれた当時の cfg の並び」に対して
正しかったもので、**この移植の並びでは違います**。実測（この build の
`kernel_cfg.h` と突合せ）:

| 記号 | フォールバック | 生成値 | 差 |
|---|---|---|---|
| `HOSTED_TSK1..8` | 2..9 | 1..8 | **+1（8 本すべて）** |
| `HOSTED_SEM1..16` | 5..20 | 3..18 | **+2（16 本すべて）** |
| `FMP3_LWIP_TSK1..8` | 11..18 | 10..17 | **+1（8 本すべて）** |
| `FMP3_LWIP_SEM1..16` | 21..36 | 19..34 | **+2（16 本すべて）** |
| 両ファイルのミューテックス・データキュー | — | — | 一致 |

**111 個中 48 個が違い、残り 63 個は合っている**——この「一部だけ合っている」が
いちばん危険です。ビルドは通り、コードは動き、**一部だけが別のオブジェクトを
掴みます**。

実害はまさに観測した症状でした。`_h_thread_create()` はプールのスロット i を
取って `act_tsk(HOSTED_TSK<i+1>)` を撃ちますが、フォールバック値ではそれが
**隣のタスク**を指します。隣のタスクは実在して DORMANT なので
**`act_tsk` は `E_OK` を返し**、起きたタスクはスロット i+1（未使用・
`start_routine == NULL`）を見て何もせず終了します。呼んだ側は「スレッドを
作れた」と信じて先へ進む——`pump_loops=0` / `rx_total=0` / `tx_ok=0` のまま
30 秒待って DHCP を諦める、という形です。**戻り値の検査では捕まりません。**

**対処**（開発リポジトリの `cmake/a1_p4_stage1.cmake` と同じ）: 生成された
`kernel_cfg.h` を 2 ファイルの**先頭へ前置 include** し、`#ifndef` のフォールバックを
負けさせる。cfg の並びが変わっても自動で追随します。

**効いていることの実測**: コンパイラが実際に使った値を `-dM -E` で採り、
`kernel_cfg.h` と全数突合せ——**113 個中 不一致 0**（前は 48）。

**negative control**: `-DA1_P4_HOSTED_NO_CFGID_FIX=ON` で前置 include を外すと、
新設した `hosted/osi/p4_cfgid_guard.h`（2 本目の `-include`）が
**ビルドを止めます**。止まることを実演してあります——止まらない番人は番人では
ありません。

### 原因 2: `host_by_name` の戻り値の向きが逆だった

原因 1 を直すと DHCP は通り（`lease_sec=14400 waited_ms=8300`）、次に
`[WiFiConnect] DNS failed` が出ました。**下の層は成功していました**——
`R9-a dns name=example.com err=0 addr=... elapsed_ms=22`。

`toppers_fmp3_wifi_host_by_name()` の約束は **1 = 成功 / 0 = 失敗**
（Arduino の `WiFi.hostByName` の約束。native 側の同名関数も 1/0）。
hosted 版を **0 = 成功 / -1 = 失敗**で書いていたのが誤りでした。

### 直した後（実機）

```
[WiFiConnect] connected and DHCP completed
RPROBE R9-a dns [WiFiHosted] name=example.com err=0 addr=<IPv4> elapsed_ms=24
[WiFiConnect] DNS completed
[WiFiConnect] TCP received=255
[WiFiConnect] TCP request completed
```

**STA 接続 -> DHCP -> DNS -> TCP が P4 で通りました**（無線は SDIO の先の
ESP32-C6 が担っています）。`rx_thread_entered=1`＝受信ポンプのスレッドが
実際に走っています（前は 1 行も出ませんでした）。

### 反復（2026-09-18）

| 条件 | 回数 | 結果 |
|---|---|---|
| warm（台本のリセット） | 3 | **3/3**。各回 `TCP received=255`、`hosted_err=0 dnsfail=0 disc=0` |
| 真cold（USB 給電断 -> 復電 -> 読むだけ） | 3 | **3/3**。各回 `dnsok=1 dnsfail=0 tcp=1 disc=0`、`TCP received=255` |

DHCP の所要は真cold 3 回で `waited_ms=8000 / 9000 / 8400`（lease 14400 s）。
リース取得までに 8〜9 秒かかるので、**採取窓は 70 秒以上**にしないと
「繋がらなかった」と読めてしまいます。

なお warm 3 回の `dnsok` が 0 と出ているのは台本側の取り違えで、板ではありません
——当時の `capture_p4_usj.sh` は `[WiFiConnect] DNS resolved` を数えていましたが、
スケッチが印字するのは `DNS completed` です。真cold 3 回は直したあとの計数で、
実ログにはどちらの回も `DNS completed` が出ています（**今日 3 件目の
「動いているのに 0 を数える」型**なので、マーカーは必ず出力側の文字列と
突き合わせること）。

## 2-10. F-1（コンソールの行頭欠落）の原因と修正（2026-09-18）

2-8 節が「未解決」としたコンソールの行頭欠落は、**原因が確定して直りました**。

**原因**: `fmp3_core` の `syssvc/serial.c` の `sio_irdy_snd()` が
`sio_snd_chr()` の**戻り値を捨てた**まま読出しポインタを進め、残数を減らして
いました。送信できなかった 1 文字はバッファから消えて二度と送られません。

このコールバックの契約（「送信できるようになったら呼ぶ」）の下では
`sio_snd_chr()` は false を返さないはずで、ハードウェア SIO ではそのとおりです。
成り立たなくしていたのは**同じ SIO ポートへドライバを経由せずに書く 2 本目の
経路**——`target_fput_log()`（低レベル出力。スピンロックを取らない）——で、
送信可能通知から `sio_irdy_snd()` が `sio_snd_chr()` を呼ぶまでの間に
送信レジスタを埋めてしまうことがあります。

**確定させた突き合わせ**（20 秒の窓、計数器は測定用の一時改変で測定後に撤去）:

| 測ったもの | 値 |
|---|---|
| ドライバが送信レジスタへ書いたバイト数 | 396 |
| ホストが受け取ったバイト数 | 396 |
| 本来送るべきだったバイト数 | 416（差 20） |
| 欠けた行 | 20 |
| `sio_irdy_snd()` が捨てた文字数 | 20 |

**20 = 20 = 20**。書いた数と受けた数が一致する＝失われた文字はハードウェアへ
渡っていない、が決め手でした（当初疑った USB のパケット競合は**外れ**）。

**修正**: `fmp3_core` を `adaec36` へ。送信できたときだけバッファから取り除きます
（START/STOP 側も同じ穴だったので一緒に）。送信できなかったとき `snd_count` は
0 にならないので送信可能コールバックは禁止されず、空いた時点で同じ文字から
再開します——**止まりません**。

**実機**: 欠陥を出す同じ A/B を同じ板で流し直して 欠け 14 -> **0** / 1 -> **0**、
40 秒で alive 40 行。この 1 行は全チップ共有なのでコンソールの種類が違う板でも
回帰しました（M5NanoC6 = USB-Serial/JTAG、M5Stack ATOM Lite = 実 UART。
どちらも `heartbeat=24 blink=24 unexpected=0`）。

**波及**: 全 stage を建て直すと 11 stage すべて DIFF になりますが、
**差は毎回 `objs/serial.o` の 1 本だけ**です。

切り分けの全経過は dev の
`.steering/20260918-p4-arduino-hw-findings/README.md`（F-1）。
