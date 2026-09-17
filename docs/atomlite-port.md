# M5Stack ATOM Lite（ESP32-PICO-D4）対応の記録（2026-09-17）

M5Stack ATOM Lite を 7 枚目の板 `m5atomlite_fmp3` として追加した記録です。
計画は dev `.steering/20260917-atomlite-plan/{INVESTIGATION.md,PLAN.md}`。

**この板は実機なしで追加しました**（ユーザー指示: 実機の検証は別の PC で行う）。
ここに書いてある検証はすべて**コンパイル・リンク・X-check**で、実機の結果は
ありません。実機で確かめるべきことは 6 節にまとめてあります。

AtomS3 Lite（`docs/atoms3lite-port.md`）と同じ形の追加です。**新しいチップ層は
作っていません**。ATOM Lite は ESP32（LX6）なので、既存の `ports/m5stack_xtensa`
ランタイムと既存の esp32 stage（M5Core が使う minimal / m5-unified / wifi-connect /
bt-classic。stage は**チップ x profile** 単位）に相乗りします。

## 1. 板（公式ドキュメント・M5Unified 0.2.20・core 3.3.8 boards.txt。実測ではない）

| 項目 | 値 | 根拠 |
|---|---|---|
| モジュール | ESP32-PICO-D4（Xtensa LX6 x2、240 MHz） | docs.m5stack.com/en/core/ATOM%20Lite |
| flash / PSRAM | 4MB / 無し | 同上、`m5stack_atom.build.flash_size=4MB` |
| RGB LED | **SK6812 3535 x1、データ G27**、電源ゲート無し | 公式 / `M5Unified.cpp:264` `{ board_M5AtomLite, GPIO_NUM_27 }` |
| ボタン | **G39**（入力専用 pad、active low） | 公式 / `M5Unified.cpp:2094` |
| IR 送信 | G12 | 公式 |
| Grove HY2.0-4P | G26（黄）/ G32（白）、5V、GND | 公式 / variant `pins_arduino.h` SDA=26 SCL=32 |
| 側面ヘッダ | G19 / G21 / G22 / G23 / G25 / G33 | 公式 |
| USB | 外付け USB-serial ブリッジ（公式が FTDI VCP ドライバを案内。USB-JTAG では**ない**） | 公式 |
| bootloader | flash **0x1000**（M5Core と同じ） | `m5stack_atom.build.bootloader_addr=0x1000` |
| partition | 既定 `huge_app`（app0 = 0x10000 から 3MB） | `m5stack_atom.build.partitions=huge_app` |

## 2. 決定

| # | 決定 | 根拠 |
|---|---|---|
| B-1 | 板行は上流 `m5stack_atom` から派生（variant も同じ）。`build.board=M5STACK_ATOM` は**そのまま継ぐ** | 上流にその板の行がある（ATOM Lite / Matrix / Echo が共有）。AtomS3 Lite のような `BOARD_BUILD_OVERRIDES` は不要。例題は `ARDUINO_M5STACK_ATOM` でガードする |
| B-2 | 提供 profile は **minimal + wificonnect + btclassic**、`m5` は出さない（`BOARD_SKIP_ENTRIES`） | `m5` は 4 節（**類推**であって実測ではない）。btclassic は PICO-D4 が BR/EDR を持ち、bt-classic stage が板に依存しない（`ports/m5stack_xtensa/app/bt_classic` は GPIO も M5 も触らない。M5Core で実機済み）ため。**実機未確認**。落ちたら `BOARD_SKIP_ENTRIES` に `btclassic` を足す 1 行で引っ込む |
| B-3 | RGB は RMT ch0 + `rmt_ll`、ポーリング。**別ファイル** `arduino_rgb_led_lx6.c` | S3 版 `arduino_rgb_led.c` を触らない（stage は `-g` 付きで、同一ファイル内の `#if` 分岐でも S3 の .o が DWARF で動く）。実測: S3 の 3 stage は MATCH（5 節） |
| B-4 | RGB は esp32 の **wifi-connect にのみ**リンク | S3 / C6 と同じ判断。この `target_sources` は bt-classic にも効くので generator expression に profile を足し、M5Core の bt-classic stage を変えない（当初は入ってしまい X-check で DIFF になった。直して MATCH） |
| B-5 | ビットタイミングは SK6812 と WS2812B の**両規格の交差集合** | 3 節。S3 版の T1H 0.90 us は SK6812 の窓（0.45-0.75）の外 |
| B-6 | 例題は新規 `examples/AtomLiteRgb`（板ガード + **ランタイムガード**） | この板は Minimal / Bluetooth Classic も持つので、そこでは `#error` でメニュー名を言う（AtomS3LiteRgb には無いガード。あちらの板は WiFi 以外に minimal しか無い） |
| B-7 | `GpioInterrupt` のプローブは **G23** | 側面ヘッダで基板上の機能が無く、M5Unified の autodetect が読む G19/G22/G33 でもない |
| B-8 | 採取台本は作らない | 実機が無く、USB-JTAG でもない（7 節） |

## 3. 変更の中身

- `scripts/install_platform.py`: `BOARDS` に 1 行、`BOARD_SKIP_ENTRIES` に 1 行。
- `scripts/verify_package.py`: `BOARD_PROFILES` に 1 行、`PROFILES["wificonnect"]` に
  `AtomLiteRgb`。`--list-builds` は 83 -> 98 -> **105**（7 板）。
- `scripts/test_check_release_artifacts.py`: `XTENSA_BOARDS` に 1 行。
- `.github/workflows/verify-package.yml`: 板ループに追加、`m5` を出さない /
  `btclassic` を出すことを検査。
- `ports/m5stack_xtensa/runtime/arduino/arduino_rgb_led_lx6.c`（新規、LX6 専用）、
  `arduino_rgb_led.h`（コメントのみ）、`runtime/CMakeLists.txt` の 1 行。
- `examples/AtomLiteRgb/`（新規）、`examples/GpioInterrupt`（G23 の分岐）、
  `examples/BluetoothSPP`（板ガードに `ARDUINO_M5STACK_ATOM`）。
- `packaging/release-allowlist.json`（例題 1 本）、README / BUILDING / README.release /
  `library.properties`。

### RGB ドライバの LX6 固有点（S3 版との差）

1. esp32 の `hal/rmt_ll.h` に無い 4 関数を使わない: `rmt_ll_tx_clear_sync_group`
   （TX 同期グループ無し）、`rmt_ll_tx_reset_loop_count` / `rmt_ll_tx_enable_loop_count`
   （loop count 無し）、**`rmt_ll_tx_stop`**（`SOC_RMT_SUPPORT_ASYNC_STOP` 無し）。
   タイムアウト時は IDF v5.5.4 の `rmt_tx_disable()` がこのチップでやる通り、
   チャネルメモリを 0（end marker）で埋めて TX_DONE を待つ（IDF は無限待ち、
   ここは有限）。
2. `rmt_ll_set_group_clock_src()` は divider 引数を無視して APB 80 MHz 直結
   （`conf1.ref_always_on = 1`）。S3 が group divider 1 で到達する 80 MHz と同じ
   なので、チャネル divider 4・50 ns tick は S3 の値のまま。
3. `rmt_ll_enable_bus_clock()` は `DPORT_READ_PERI_REG` → `esp_dport_access_reg_read()`
   を呼ぶ。この runtime では `wifi/shim/wifi_stubs.c` が持つ（wifi-connect にリンク
   される）。S3 と同じく `PERIPH_RCC_ATOMIC()` の中でしか呼べない。
4. RMT ブロックは 8 チャネル全部 TX 可・各 64 語。`RMT = 0x3ff56000`、
   `RMTMEM = 0x3ff56800`（`esp32.peripherals.ld`）、`RMT_SIG_OUT0_IDX = 87`。
5. 入力専用 pad 34-39 を `SOC_GPIO_VALID_OUTPUT_GPIO_MASK` で拒否する（pinMode の
   static な検査と同じ規則を、pad に何か繋ぐ前に繰り返す。S3 には入力専用 pad が無い）。
6. ログのタグは `[LX6-RGB]`。

### ビットタイミング（50 ns tick）

| | WS2812B（±0.15） | SK6812（±0.15） | 交差 | 採用 |
|---|---|---|---|---|
| T0H | 0.40 → [0.25, 0.55] | 0.30 → [0.15, 0.45] | [0.25, 0.45] | **0.35 us (7)** |
| T0L | 0.85 → [0.70, 1.00] | 0.90 → [0.75, 1.05] | [0.75, 1.00] | **0.90 us (18)** |
| T1H | 0.80 → [0.65, 0.95] | 0.60 → [0.45, 0.75] | [0.65, 0.75] | **0.70 us (14)** |
| T1L | 0.45 → [0.30, 0.60] | 0.60 → [0.45, 0.75] | [0.45, 0.60] | **0.55 us (11)** |
| reset | ≥ 50 us | ≥ 80 us | ≥ 80 us | 50 us x2 = 100 us |

## 4. `m5` profile を出さない根拠（類推）

- この port の m5 runtime は `m5/adapter/m5_arduino_adapter.cpp` が
  `M5.Display.getPanel()` の SPI bus を前提にする。画面の無い AtomS3 Lite では
  `LCD SPI bus was not available` で例題が FAILED になることを実測済み
  （`docs/atoms3lite-port.md` 4 節）。ATOM Lite も画面が無いので同じ経路を辿る。
- 加えて M5Unified の PICO-D4 autodetect（`M5Unified.cpp:1444-1526`）は G27 の
  **タッチセンサ**と `delay()` / `taskENTER_CRITICAL` を使う。この port は
  FreeRTOS を持たないので、AtomS3 Lite より条件が悪い。
- 実機で `m5` を試すなら、AtomS3 Lite の段4 と同じ「実験用の一時 platform」
  （`BOARD_SKIP_ENTRIES` から `m5` を外して導入）で `M5Unified` 例題を焼き、
  `_check_boardtype` が `board_M5AtomLite` を返すか・`M5.begin` がどこで止まるかを
  見る。

## 5. この機械で測ったこと（実機なし）

環境は着手時点で何も無く（`~/.arduino15` 無し・arduino-cli 無し・submodule
未取得）、arduino-cli 1.5.2-rc.1 + core 3.3.8（tools 一式）+ M5Unified 0.2.20 /
M5GFX 0.2.27 を入れて建てた（dev `.steering/20260917-atomlite-plan/INVESTIGATION.md` 0 節）。

| 対象 | 結果 |
|---|---|
| X-check（baseline: clean tree 29d9f4c、7 stage） | **esp32/wifi-connect のみ DIFF**——増えたのは `objs/arduino_rgb_led_lx6.o` と、それを載せた `link-manifest.json`（objectCount / objectOrder）と `objects.rsp` の 3 ファイルだけ。**残り 6 stage は MATCH**（S3 の 3 つは `arduino_rgb_led.h` のコメント変更後も MATCH。esp32/bt-classic は B-4 の修正後 MATCH） |
| 既存 6 板の `boards.txt` 行・`platform.txt` | 不変（diff / cmp） |
| 新板の例題 | **14/14 が rc=0**（minimal 2 + wificonnect 8 + btclassic 4。`TwoFileSketch` は verify が生成するので直接 compile では対象外） |
| 同じ esp32 stage を使う M5Core との比較 | `Blink`（wificonnect）と `BluetoothSPP` の `.bin` は**バイト一致**、`GpioInterrupt` だけ差がある（ピン分岐 16 vs 23） |
| `AtomLiteRgb` の他板 | CoreS3 / StickS3 / AtomS3 Lite / M5Core / NanoC6 / StampC5 の wificonnect で no-op としてリンク |
| 負対照 (a) ランタイムガード | `AtomLiteRgb` を minimal / btclassic で建てると `#error AtomLiteRgb needs the WiFi runtime ...` で止まる |
| 負対照 (b) ランタイムガードを外す | btclassic で `undefined reference to rgbLedWrite`（bt-classic に駆動側が無い） |
| 負対照 (c) 板ガードを外す | M5StampC5 wificonnect で `undefined reference to rgbLedWrite` |
| 駆動側を足したことによる像の変化 | M5Core wificonnect の `Blink.bin` は前後で同サイズ（192288 B。呼ばなければ像に入らない） |
| duplicate-symbol audit | esp32 4 stage とも 0 duplicated |
| `check_host_paths.py`（導入済み platform） | PASS |
| python テスト 4 本 | PASS |
| 全 105 builds の直接 compile（7 板、11 stage 導入） | **96 PASS / 0 FAIL / 9 SKIP**（SKIP は `verify_package.py` が生成する `TwoFileSketch`——minimal 7 板 + btclassic 2 板——で、直接 compile では作れない。Boards Manager 経由の `verify_package.py` はこの機械では走らせていない） |

## 6. 別 PC での実機確認（未実施。期待値つき）

書込みは M5Core と同じ（bootloader 0x1000・ptable 0x8000・app 0x10000、外付け
USB-serial なので Arduino IDE の Upload がそのまま使える。**USB-JTAG ではない**
ので `capture_s3_usj.sh` 系の台本は使えない）。

| 順 | 例題 / 構成 | 期待するログ | 見るもの |
|---|---|---|---|
| 1 | `Blink` / Minimal | `[Arduino] loop heartbeat N` が 1 秒ごと | warm 5 回・真cold 5 回 |
| 2 | `GpioInterrupt` / WiFi | `VERDICT PASS ... rising=5 falling=5 change=10 detached=0 dispatch=20 call=20 orphan=0` | G23 に何も繋がない |
| 3 | `AtomLiteRgb` / WiFi | `[LX6-RGB] tx_done=1` … `=8`、`tx timeout` 無し | **赤 -> 緑 -> 青 -> 消灯 x2 を目視**。色順が違えば GRB の仮定が外れている（SK6812 3535 は GRB のはず） |
| 4 | `WiFiScan` / WiFi | `found N APs` | N ≥ 1 |
| 5 | `WiFiConnect` / WiFi（実 creds） | DHCP → DNS → TCP | AtomS3 Lite で見えた `reason=17`（F-3、Xtensa 共通の可能性）が LX6 でも出るか。M5Core と同じ stage なので M5Core の結果と揃うはず |
| 6 | `BluetoothSPP` / Bluetooth Classic | `M5Stack-SPP` でペアリング・echo | 落ちたら B-2 を覆す |

- 電源断（真cold）の方法は板の USB を抜く／ハブのポート電源で。
- 採取ログに SSID / BSSID / IP を残さない（`CLAUDE.md`「出してはいけないもの」）。

## 7. 残り

- 6 節すべて（実機）。結果が出たら 5 節の表と README の「確認済みの範囲」を
  実測に置き換える。
- `m5` profile の可否（4 節は類推）。
- gated 採取台本（LX6 / 外付け USB-serial 用）。今回は作っていない。
- AtomS3 Lite の F-1（console の文字落ち）/ F-2（scan が実 SSID を印字）/ F-3
  （`reason=17`）は Xtensa 共通の可能性があり、この板でも出るかは実機待ち。
