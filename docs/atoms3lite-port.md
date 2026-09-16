# M5AtomS3 Lite（ESP32-S3）対応の記録（2026-09-17）

M5Stack AtomS3 Lite を 6 枚目の板 `m5atoms3lite_fmp3` として追加した記録です。
計画は dev `.steering/20260917-atoms3lite-plan/{INVESTIGATION.md,PLAN.md,stage-notes.md}`。

ユーザー指示は「範囲 C」——板定義 + `m5` profile を出すかの実機判断 + 本体 RGB LED の
点灯まで。**新しいチップ層は作っていません**。AtomS3 Lite は ESP32-S3 なので、
既存の `ports/m5stack_xtensa` ランタイムと既存の esp32s3 stage（stage は
**チップ x profile** 単位で、板単位ではない）に相乗りします。

## 1. 板（実測）

| 項目 | 値 | 取り方 |
|---|---|---|
| チップ | ESP32-S3 (QFN56) rev v0.2 | `esptool --no-stub flash-id` |
| flash | 内蔵 8MB（GD, quad, 3.3V） | 同上 |
| XTAL | 40 MHz | 同上 |
| PSRAM | 無し | 同上（Features に現れない） |
| USB | 内蔵 USB Serial/JTAG | 同上 |
| RGB LED | WS2812B-2020、データ **G35**、電源ゲート無し | M5Unified `_pin_table_other0` |
| ボタン | **G41**（active low） | M5Unified `M5Unified.cpp` |
| 外部 IO | G5 / G6 / G7 / G8 / G38 / G39、Grove は G1(白) / G2(黄) | M5Stack 公式ドキュメント |

## 2. 決定

| # | 決定 | 根拠 |
|---|---|---|
| A-1 | 板行は上流 `m5stack_atoms3` から派生（variant も同じ） | core 3.3.8 に `m5stack_atoms3lite` の行が無い（grep 0 件）。派生元は同じ 8MB / qio_qspi / upload サイズで、出荷済みの `m5stack_sticks3` 行と `build.board` 以外一致 |
| A-2 | `build.board` を **`M5STACK_ATOMS3LITE`** へ上書き（新表 `BOARD_BUILD_OVERRIDES`） | 継承した `M5STACK_ATOMS3` では LCD 付きの AtomS3 と区別できない。M5Unified / M5GFX は板を実行時 autodetect で決めており、`ARDUINO_M5STACK_ATOMS3*` を読む箇所は無い（grep 0 件）ので安全 |
| A-3 | 提供 profile は **minimal + wificonnect**（`m5` は出さない。新表 `BOARD_SKIP_ENTRIES`） | 4 節の実機実測（画面が無く `M5.begin` が LCD で失敗する）。ESP32-S3 は CoreS3 / StickS3 のために m5-unified stage を配るので、「チップが配る stage = 板が出すメニュー」が崩れる初めての板 |
| A-4 | RGB は RMT ch0 + `rmt_ll`、ポーリング（C6 と同形） | 既存 C6 実装の移植が最短。割込み線を増やさない |
| A-5 | RGB は S3 の `wifi-connect` にのみリンク | 出荷既定の minimal を汚さない（C6 と同じ） |
| A-6 | 例題は新規 `examples/AtomS3LiteRgb`（板ガード）。`NanoC6Gpio` は無改変 | 既存例題の改名は verify / CI / 文書の同時改訂を招く |
| A-7 | `GpioInterrupt` のプローブは **G7** | 底面ヘッダで未接続。G38 は M5Unified の autodetect が読む |
| A-8 | 採取は新設の `scripts/capture_s3_usj.sh`（gated） | 実機は gated 台本のみ、という運用規約。同ハブの M5NanoC6 / M5Stamp-C5 を FORBIDDEN に入れる |

## 3. 変更の中身

- `scripts/install_platform.py`: `BOARDS` に 1 行、`BOARD_BUILD_OVERRIDES` と
  `BOARD_SKIP_ENTRIES` を新設。板単位の上書きは chip 単位の `UPLOAD_SIZE_OVERRIDES` と
  同じ「1 度だけ差し替える」規律で当てる。
- `scripts/verify_package.py`: `BOARD_PROFILES` に 1 行、`PROFILES["wificonnect"]` に
  `AtomS3LiteRgb`。`--list-builds` は 68 -> 77 -> **83**（6 板）。
- `scripts/test_check_release_artifacts.py`: ドリフト検査が `BOARD_SKIP_ENTRIES` を
  引き算するようにした（板がチップの stage を 1 つ断るのは、この板が初めて）。
- `.github/workflows/verify-package.yml`: 板ループに追加、`m5` / `btclassic` を
  出さないことを検査。
- `ports/m5stack_xtensa/runtime/arduino/arduino_rgb_led.{c,h}`（新規、S3 専用）と
  `runtime/CMakeLists.txt` の 1 行（`$<$<STREQUAL:${A1_CHIP},esp32s3>:...>`）。
- `examples/AtomS3LiteRgb/`（新規）、`examples/GpioInterrupt`（G7 の分岐）。
- `scripts/capture_s3_usj.sh`（新規、1,600 行弱。`capture_c5_usj.sh` の複製 + S3 化）。

### RGB ドライバの S3 固有点（C6 版との差）

1. クロック源は `RMT_CLK_SRC_APB`（= このチップの `RMT_CLK_SRC_DEFAULT`）。80 MHz。
2. `rmt_ll_enable_bus_clock()` と `rmt_ll_reset_register()` が **`PERIPH_RCC_ATOMIC()`
   の中でしか呼べない**（C6 のヘッダには無いマクロ包み。`SYSTEM.perip_clk_en0` を
   他のペリフェラルと共有するため）。`periph_rcc_enter/exit` はこのランタイム自身が
   `wifi/hal_src/periph_ctrl.c` に持っている。
3. RMT ブロックが大きい（8 チャネル、うち 0-3 が TX。C6 は 4 チャネル / TX 2）。
4. `RMT` = 0x60016000、`RMTMEM` = 0x60016800、`RMT_SIG_OUT0_IDX` = 81。

WS2812 のタイミング・GRB 順・ポーリング完了・タイムアウト処理は C6 版のままです。

## 4. 実機で測ったこと

すべて `scripts/capture_s3_usj.sh` 経由（MAC / チップ / flash ゲート、書込み後に
flash 0x0-0x0FFF を読み戻して bootloader 像の先頭 4096 B と一致することを確認）。

| 対象 | 結果 |
|---|---|
| Blink warm | **5/5**（banner=1 setup=1 heartbeat=19 blink=19 unexpected=0） |
| Blink 真cold（`uhubctl -l 2-3.3 -p 1`） | **5/5**（heartbeat=20 blink=20 unexpected=0。cold は採取開始が起動後なので banner/setup は窓の外） |
| `GpioInterrupt`（G7 自己駆動） | **PASS**: `rising=5 falling=5 change=10 detached=0 dispatch=20 call=20 orphan=0 acre=2` |
| `AtomS3LiteRgb`（G35） | **tx_done=8 / write=8 / timeout=0**（warm 3 回 + **真cold 1 回**）。JTAG で `ard_rgb_tx_done` を直接読んで 8 を確認（console の文字落ちと独立の証拠）。**色そのものは目視待ち** |
| `WiFiScan` | **16 AP を検出**（`found 16 APs` + `AP[0..15]`）。スキャン自体は動く |
| `WiFiConnect`（実 AP、実 creds） | **3/3 失敗**。`disconnected reason=17` rssi=-61〜-63。下の F-3 |
| `M5Unified`（`m5` profile、実験用の一時 platform） | `_check_boardtype = 137`（= `board_M5AtomS3Lite`、**正しく同定**）、begin の P1-P9 は通るが `M5GFX::init_impl = 0`（パネル無し）で `LCD SPI bus was not available` -> 例題は FAILED |

## 5. 見つけた問題（この計画で入れたものではない）

- **F-1 console の文字落ち**: FMP3 の log task が出す行の先頭 1-2 文字が落ちることが
  ある（`[Arduino] task start` -> `rduino] task start`、`[WiFiScan] found 16 APs` ->
  `[iFiScan] found 16 APs`、`[S3-RGB] tx_done=1` -> `[3-RGB] tx_done=1`）。スケッチが
  `target_fput_log()` で直接書く行は無傷。Blink の段階から出ているので**この移植の
  console の問題**。当面の対処として採取台本の計数パターンはタグの**末尾**に合わせて
  ある（`Scan] found`、`duino] loop heartbeat`）。恒久対処は未着手。
- **F-2 スキャンが近隣 AP の実 SSID を印字する**: Xtensa の `toppers_wifi_scan.c` は
  `SSID=%s` をそのまま出す（RISC-V 側は `<SSID-i>` プレースホルダ）。採取台本側で
  SSID 列をマスクし、**マスクされていない SSID 列が残った採取ファイルは residue として
  隔離する**検査を足した（selftest に正負両対照）。ランタイムをプレースホルダに
  変えるかは**ユーザー判断**（S3 の stage バイトが変わり、出荷済みの CoreS3 /
  StickS3 にも及ぶ）。
- **F-3 実 AP への STA 接続が `reason=17` で切れる（3/3）**: `WIFI_REASON_IE_IN_4WAY_DIFFERS`。
  スキャンでは同じ AP が見えており（rssi -61〜-63）、設定は `authmode=3`
  （threshold）。同じ AP に M5Stamp-C5 / M5NanoC6 は WPA3-SAE で繋がる。使っている
  stage は CoreS3 / StickS3 と同じ esp32s3 wifi-connect なので、**この板固有ではなく
  Xtensa 側 Wi-Fi の課題**の可能性が高いが、**他の S3 板で同じ AP を試していないので
  帰属は未確定**（2026-08-18 に AP を入れ替えて以降、S3 板でこの AP に繋いだ記録が無い）。
  本計画では直していない。

## 6. 非退行

- X-check: 段1 は **11/11 MATCH**（C5 を含めた 11 stage の baseline を初めて取得）。
  段3 で `esp32s3/wifi-connect` のみ **DIFF**——増えたのは `objs/arduino_rgb_led.o` と、
  それを載せた `link-manifest.json`（objectCount / objectOrder）と `objects.rsp` だけで、
  **他の 80 ファイルはバイト同一**。残り 10 stage は MATCH。
- 既存 5 板の `boards.txt` 行・`platform.txt`・`programmers.txt` は不変（cmp 同一）。
- 負対照: `AtomS3LiteRgb` の板ガードを外すと M5Core / M5StampC5 で
  `undefined reference to 'rgbLedWrite'`。
- python テスト（`test_check_release_artifacts` ほか 3 本）すべて PASS。

## 7. 残り

- **RGB の色順（赤 -> 緑 -> 青）の目視確認**（板には `AtomS3LiteRgb` を書き込んで
  あります。電源を入れ直すと 1 秒ごとに 2 周します）。
- F-1 / F-2 / F-3 の扱い（いずれもユーザー判断）。
- リリース形の全板 verify（83 builds）と公開手順は未実施。
