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
| `GpioInterrupt`（G7 自己駆動） | **PASS**: `rising=5 falling=5 change=10 detached=0 dispatch=20 call=20 orphan=0 acre=2`（warm。**真cold でも同一の VERDICT PASS** を v0.5.0 リリース物の確認時に実測、2026-09-17） |
| `AtomS3LiteRgb`（G35） | **tx_done=8 / write=8 / timeout=0**（warm 3 回 + **真cold 1 回**）。JTAG で `ard_rgb_tx_done` を直接読んで 8 を確認（console の文字落ちと独立の証拠）。**点灯と色順（赤 -> 緑 -> 青）はユーザーが目視確認（2026-09-17）** |
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

  **（2026-09-18 追記）帰属が決まった: これは板固有ではなく S3 共通である。**
  同じ日・同じ AP・同じ計器で 3 板を測った（計器は `docs/atomlite-port.md` 8 節の
  使い捨てスケッチ。スキャン結果の SSID を目的 SSID と突き合わせ、真偽と
  rssi/ch/auth だけを印字する）:

  | 板 | チップ | AP を見つけたか | 結果 |
  |---|---|---|---|
  | M5Stack ATOM Lite | ESP32 (LX6) | 見つけた（rssi -62〜-71） | **CONNECTED**（warm 3/3・真cold 3/3） |
  | **M5Stack Basic** | ESP32 (LX6) | 見つけた（rssi -70、ch 10、auth 7） | **CONNECTED**（2026-09-19 追加） |
  | **M5CoreS3** | ESP32-S3 | 見つけた（rssi -65、ch 10、auth 7） | `begin returned=0` のあと **`reason=17`** |
  | **M5AtomS3 Lite** | ESP32-S3 | 見つけた（rssi -66、ch 10、auth 7） | `begin returned=0` のあと **`reason=17`** |

  ⇒ **LX6 が 2 板とも通り、S3 が 2 板とも同一の症状で落ちる。** F-3 が
  「他の S3 板で試していないので未確定」としていた帰属は、**S3 共通**で確定した。
  各チップ 1 枚ずつだった段階では「ATOM Lite が例外だった」可能性が残っていたが、
  2026-09-19 に M5Stack Basic（LX6 の 2 枚目、ESP32-D0WDQ6-V3、別個体・別ブリッジ）を
  足して消えた。4 板とも同じ AP（WPA2/WPA3 混在 `auth=7`、ch 10）を同程度の
  電波強度（rssi -62〜-71）で見つけたうえでの差である。

  **絞り込み済みの範囲**（LX6 との差がどこに無いか）:
  - STA 設定を組む `wifi/adapter/toppers_wifi_connect.c` は**両チップ共用の同一ファイル**
  - `wifi/config/{esp32,esp32s3}` の WPA3 / SAE / PMF / 11W / RSN 系 define は**全て同値**
  - `wifi/prebuilt/wpa2/{esp32,esp32s3}/libsupplicant.a` は同一構成のビルド（サイズも近い）

  ⇒ 残る差は**チップごとの Wi-Fi blob**か、S3 固有の初期化経路。

  **仮説 1「`pmf_cfg.capable=false` が原因」は実測で反証した（2026-09-18）。**
  `config.sta.pmf_cfg.capable = true` を 1 軸だけ立てて CoreS3 で測ったところ、
  像は `pmf_capable=1` と名乗ったうえで**やはり `reason=17`**。⇒ MFP capability は
  原因ではない。（そもそもこの仮説は「同じ設定で LX6 が通る理由」を説明できて
  いなかった。）

  **新しい事実: association は成功しており、落ちるのはその後である。**
  blob の状態遷移ログが採取に出ている:

  ```
  state: init -> auth (0xb0)
  state: auth -> assoc (0x0)
  state: assoc -> run (0x10)
  state: run -> init (0x1100)     <- 0x11 = 17
  ```

  ⇒ auth も assoc も通って `run` まで行き、**4-way で落ちている**。
  `IE_IN_4WAY_DIFFERS` の名前どおり、(Re)Assoc Request に載せた RSN IE と
  EAPOL-Key msg 2/3 の IE が食い違っている、という読みと整合する。

  **次の一手**: ここから先は supplicant 自身の診断が要る（`wpa_printf` 経由の
  出力は現状の採取に出ていない——出ているのは `esp_shim:` / `esp_event:` /
  `state:` / `wifi_adapter:` のみ）。blob と supplicant のログ水準を上げる計器を
  作るのが先で、それ無しに次の仮説を立てても当て推量になる。

  ### 原因を特定した（2026-09-19・supplicant の診断ビルド）

  出荷の `libsupplicant.a` には診断が **1 行も入っていない**（`wpa_printf` は
  `DEBUG_PRINT` で囲われており、未定義だと消える。`nm` で 0 件を実測）。
  ⇒ ログ入りで建て直す計器を作った（dev `build_wpa_libs_espidf_esp32{,s3}.sh`
  の `WPA_DEBUG_PRINT=1`、既定 OFF・出力先を `_dbg` に分離）。

  **同じスケッチ・同じ AP・同じ supplicant ソースで S3 と LX6 を比べた結果**:

  | | `set AP RSNXE`（association 時に記録） | `RSNXE in EAPOL-Key`（msg 3/4） | 結果 |
  |---|---|---|---|
  | **ESP32-S3** | **`len=0`（空）** | `len=3` | `reason=17` |
  | **ESP32 (LX6)** | **`len=3`: `f4 01 20`** | `len=3`: `f4 01 20` | `Key negotiation completed` |

  供給元は `esp_wifi_sta_get_rsnxe(bssid)`（両チップとも blob = `libnet80211.a`）。
  `-Wl,--wrap` で観測すると、**実在の BSSID で呼ばれて NULL が返る**
  （計器は `A1_WIFI_RSNXE_PROBE=ON`、既定 OFF）。⇒「鍵が違う」ではなく
  **「blob が保持していない」**で確定。

  supplicant 側の判定は `wpa.c:1170-1184` で**無条件**（`!sm->ap_rsnxe && ie->rsnxe`
  も不一致とする）。Kconfig にも公開 API にも RSNXE の口は無い（grep 0 件）。

  ⇒ **このポートでは回避できない。** 手当ての候補と問題:
  - `--wrap` で RSNXE を捏造 → `f4 01 20` は**この AP の値**。RSNXE は「AP が
    何に対応しているか」の宣言で、STA が勝手に作ってよいものではない
  - 自前 scan から取る → `wifi_ap_record_t` に生 IE は無く、raw beacon には
    promiscuous の層が要る（このポートに無い）
  - WPA2 専用 AP を使う → 回避ではなく**条件の限定**（未実測）

  **次の一手**: Espressif への報告（材料は揃っている）。制限は README と
  配布 README に明記した。

  ### F-3 の続き（2026-09-20）: 切断のあとスケッチが止まる

  all-in-one の試作を M5CoreS3 で回したときに見つけた。`reason=17` で切れた
  **あと**、`loop()` が呼ばれなくなり、コンソールも黙る（120 秒採取で
  `alive=3` まで、以後 114 秒間 1 行も出ない）。

  帰属は 3 本の対照で分けた:

  | 走らせたもの | 結果 |
  |---|---|
  | all-in-one + STA（CoreS3） | `alive=3` で停止、`reason=17` |
  | all-in-one・STA 無し（CoreS3） | **101 alive / 120 秒**、scan 12 回・89 AP、正常 |
  | **出荷の wifi-connect + STA**（CoreS3、M5Unified 無し） | **同じく `alive=3` で停止** |

  ⇒ **合成構成のせいではない。出荷している `WiFi` 構成でそのまま起きる。**

  **CPU は動いている。** 台本の JTAG プローブ（`S3_JTAG_FORCE=1`）で、
  2 秒あけた 2 回の halt で PC は動いている（`0x42003f71` → `0x42003ef0`、
  どちらも `esp_shim_timer_task`）のに、runtime が持つ `loop_calls` は
  **2 -> 2 で delta=0**。⇒ **止まっているのは Arduino タスクだけ**で、
  システム全体のハングではない。

  **2026-09-19 の採取にも同じものが写っていた**——M5AtomS3 Lite の
  `s3-capture-20260919-215207.log` は 212 行中 210 行目が `reason=17` で、
  そこで終わっている。当時は「繋がったか」だけを見ていたので気づかなかった。
  ⇒ **失敗を見るときは、失敗したあとどうなるかも見る。**

  **原因は同日に特定して直した**（branch `fix/xtensa-shim-timer-clamp`、
  `docs/xtensa-timer-starvation.md`）。タイマの ms->us 変換の 32bit 折り返しと、
  シムのタイマタスクが待ちを `TMAX_RELTIM` で頭打ちにしていなかったことの
  合わせ技で、優先度 2 のタイマタスクが空転して下位タスクを全部飢餓させて
  いた。dev では 2026-08-14 に直っており（`.steering/20260814-wifi-disconnect-hang/`）、
  arduino 側は RISC-V のコピーにだけ修正が入っていた。
  採取は `fmp3_esp_idf_dev/.steering/20260920-aio-probe/logs/s3-*`。

  ### この実験で踏んだ罠（次にやる人へ）

  **定義が届いたかを像に名乗らせること。** `A1_WIFI_PMF_CAPABLE` を渡す実験は
  **2 回空振りした**。`reason=17` という**期待どおりの失敗**が返るので、
  空振りに気づく手がかりが他に無い。

  1. `build_prebuilt_stages.py --cmake-define X=ON` は **CMake 変数を作るだけ**で、
     コンパイル定義にはならない。
  2. `option()` を宣言して `FMP3_COMPILE_DEFS` へ足しても、**`net_objects` には
     届かない**（アダプタはそこでコンパイルされる）。CMakeLists 239 行の
     `M5_USE_ESP_SHIM` についての警告と同じ罠。
     正しいのは `target_compile_definitions(net_objects PRIVATE X=1)`。
  3. 確認は `build/prebuilt-work/<chip>/<profile>/build.ninja` の当該 `.obj` の
     `DEFINES =` を見る。そのうえで**実行時のログに値を印字させる**。

## 6. 非退行

- X-check: 段1 は **11/11 MATCH**（C5 を含めた 11 stage の baseline を初めて取得）。
  段3 で `esp32s3/wifi-connect` のみ **DIFF**——増えたのは `objs/arduino_rgb_led.o` と、
  それを載せた `link-manifest.json`（objectCount / objectOrder）と `objects.rsp` だけで、
  **他の 80 ファイルはバイト同一**。残り 10 stage は MATCH。
- 既存 5 板の `boards.txt` 行・`platform.txt`・`programmers.txt` は不変（cmp 同一）。
- 負対照: `AtomS3LiteRgb` の板ガードを外すと M5Core / M5StampC5 で
  `undefined reference to 'rgbLedWrite'`。
- python テスト（`test_check_release_artifacts` ほか 3 本）すべて PASS。
- **導入済み platform に対する全 83 builds の compile: 76 PASS / 0 FAIL / 7 SKIP**
  （SKIP は `verify_package.py` が生成する `TwoFileSketch` で、直接 compile では
  作れないため）。`check_host_paths.py` は導入済み platform の 1,075 ファイルで
  PASS。**リリース形（Boards Manager 経由）の verify はこの機械では走らせられて
  いない**——`verify_package.py` は `~/.venvs/toppers-verify/bin/python`
  （PyInstaller 入り）で driver までは作れるが、`arduino-cli core install
  toppers:esp32@0.4.2` が "not found" で落ちる（ローカル index が読まれない。
  原因未解明）。

## 7. 残り

（RGB の点灯と色順は 2026-09-17 にユーザーが目視で確認しました。板には
`AtomS3LiteRgb` が入ったままです。）

- F-1 / F-2 / F-3 の扱い（いずれもユーザー判断）。
- **リリース形**（Boards Manager 経由）の verify と公開手順は未実施
  （83 builds 自体は導入済み platform に対して 76 PASS / 0 FAIL / 7 SKIP 済み。6 節）。
