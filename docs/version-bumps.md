# 依存バージョンの更新記録

本ポートが固定している外部依存——M5Stack Arduino core（SDK とツールチェーン）と
`M5GFX` / `M5Unified`——を動かしたときの、**何を測って何が動いたか**の記録です。
契約としての現在値は `README.md` / `BUILDING.md` / `packaging/README.release.md` に
あります（ここは履歴で、腐らせないために日付つきで書き足すだけにします）。

---

## 2026-09-18: core 3.3.8 → **3.3.9**、M5GFX 0.2.27 → **0.2.29**、M5Unified 0.2.20 → **0.2.22**

### 0. 2 軸を分けて測った

上流が同時期に両方を上げたので、最初は**両方いっぺんに**動かしてしまいました。
その状態で X-check を回すと 11 ステージ中 2 つ（`esp32s3/m5-unified` と
`esp32/m5-unified`）が DIFF になり、**そのままでは「SDK が変えたのか
ライブラリが変えたのか」が言えません**。⇒ ライブラリを 0.2.27 / 0.2.20 に
戻して（スクラッチの別ツリーへ入れ、`--m5gfx-source` / `--m5unified-source` で
指す）**SDK だけを動かした状態**で測り直しました。

### 1. SDK の総入れ替えは、配布バイト列を 1 バイトも動かさなかった

```
expected=11 compared=11 match=11 diff=0
PASSED: 11 stage(s) match the baseline
```

`build/xcheck-baseline`（core 3.3.8 で採った 11 ステージ＝ esp32s3 3 + esp32 4 +
esp32c6 2 + esp32c5 2）に対し、**core 3.3.9 で建て直した 11 ステージが全一致**。

- **always-pass ではありません**: 同じ比較器が、その 20 分前に同じ 11 ステージを
  「9 MATCH / 2 DIFF」と報告しています（ライブラリが新しかった状態）。差が出る
  条件で実際に差が出ることは実演済みです。
- **3.3.8 は機械から完全に消えている**ので（`~/.arduino15/packages/m5stack/
  {hardware/esp32,tools/*}` に 3.3.9 しか無い）、ビルドが古い SDK を掴んだ可能性は
  ありません。
- 一致する理由は測れます: **両版の同梱 ESP-IDF が同一**です
  （全 SDK の `versions.txt` が `esp-idf: v5.5.4 735507283d`、`lib-builder: master 43a8f6d`）。
  ステージが SDK から取り込むのはヘッダの内容だけ（アーカイブは利用者側の最終
  リンクで解決する）なので、ヘッダが動かなければ `.o` も動きません。

### 2. 動いたのは `m5-unified` の 2 ステージだけで、原因はライブラリ

ライブラリを 0.2.29 / 0.2.22 にすると同じ 2 ステージが DIFF になります
（`M5GFX.o` / `M5Unified.o` / `common.o` / 各 `*_Class.o` ほか、+ 本ポートの
`m5_idf_stubs.o`）。他の 9 ステージ（minimal / wifi-connect / bt-classic、全チップ）は
**MATCH のまま**——これらは M5 のソースを 1 本も含まないので、当然そうなるべき
形です。

### 3. core 3.3.8 の M5StampP4 欠陥は 3.3.9 で直っている

3.3.8 では上流の `m5stack_stamp_p4` が**自分の板をコンパイルできません**でした
（`cores/esp32/esp32-hal-spi.c:299` が `SOC_SDMMC_IO_POWER_EXTERNAL` の下で
`BOARD_SDMMC_POWER_CHANNEL` を読むが、variant の `pins_arduino.h` が定義しない）。
本ポートは板行に `-DBOARD_SDMMC_POWER_CHANNEL=4` を足して回避していました。

3.3.9 で測り直した結果:

- `cores/` 全体から当該識別子が消えている（`SD_MMC.cpp` は読むが
  `#if defined(...)` の下だけ）。
- `arduino-cli compile -b m5stack:esp32:m5stack_stamp_p4` が空スケッチを通す
  （314144 bytes）。

⇒ **回避を外しました**。値 4 は Tab5 の LDO チャネルで本板のものとは限らず、
ガードが消えた今は `SD_MMC` に誤ったチャネルを渡しかねないためです
（`install_platform.BOARD_BUILD_OVERRIDES`）。

### 4. M5GFX 0.2.28 以降が要求する 4 シンボルと、その扱い

0.2.29 のステージを建てて例題をリンクすると、まず 4 本が未定義になります:

| シンボル | 供給元アーカイブ | 本ポートの扱い |
|---|---|---|
| `esp_log_level_get` / `esp_log_level_set` | `lib/liblog.a` | スタブ（`m5_idf_stubs.c`） |
| `i2c_master_get_bus_handle` | `lib/libesp_driver_i2c.a` | スタブ（常に `ESP_ERR_NOT_FOUND`） |
| `rtc_clk_xtal_freq_get` | `<memory_type>/libesp_hw_support.a` | 自前実装（SDK のインラインで RTC レジスタを読む） |

**SDK のアーカイブを引き込む道は採りませんでした**。実際に
`-L<sdk>/lib -L<sdk>/<memory_type> -llog -lesp_driver_i2c -lesp_hw_support` を
足して測ると、今度は FreeRTOS の 4 本（`xQueueCreateMutex` / `xQueueGenericSend` /
`xQueueSemaphoreTake` / `xTaskGetSchedulerState`）が未定義になります——
`liblog.a` の `tag_log_level.c.obj` が `log_lock.c.obj` を引き、そこが
FreeRTOS を要求するためです。FMP3 は FreeRTOS ではないので、ここから先は
シムを足す話になり、**「ログのタグ別レベル」という使っていない機能のために
カーネル ABI を 4 本生やす**ことになります。

`rtc_clk_xtal_freq_get` だけは**推測値を返してはいけません**（M5GFX の
`getSpiClockFrequency()` が SPI 分周に使う）。本物の `rtc_clk.c.obj` は
`esp_sleep_sub_mode_*` / `regi2c_ctrl_*` / dbias テーブルを芋づるで要求するので、
**読み出しだけを SDK のインライン**（`hal/clk_tree_ll.h` の
`clk_ll_xtal_load_freq_mhz`、S3 と LX6 で同一実装）で行い、値を書くのは
ESP-IDF 2nd-stage bootloader＝本ポートの起動元、という関係を使っています。

### 5. ツールも 1 つ動いた

`esptool_py` **5.2.0 → 5.3.0**（M5Stack の package index が 3.3.9 でそう宣言する）。
`make_package_index.M5STACK_TOOL_DEPENDENCIES` を合わせました。
`esp-x32` / `esp-rv32` は 2601 のまま。

### 6. 動かなかったことを確かめた値

`fmp3_link.FIXED_VMA_LAYOUTS` が持つ RISC-V 3 チップの定数は、3.3.9 の SDK で
**すべて同じ**でした（変わっていればリンク時の画像検査が静かに間違った基準で
通ります）:

| | C6 | C5 | P4(es) |
|---|---|---|---|
| `CONFIG_MMU_PAGE_SIZE` | 0x10000 | 0x10000 | 0x10000 |
| bootloader `.iram_loader.text` | 0x4086e610 | 0x4084e5a0 | 0x4ff2cbd0 |

（`.iram_loader.text` は 4 つの `bootloader_*.elf` すべてで同値。）
`arduino_sdk.ARCH_HEADERS` が指す arch ヘッダも 5 SDK すべてに在ります。

### 7. リンク行列（8 板 x profile x 例題）

```
link matrix: ok=106 fail=0 skip=10 (skip = generated TwoFileSketch)
```

`verify_package.BOARD_PROFILES x PROFILES` の 116 本のうち、`TwoFileSketch`
（`verify_package.py` が作業ツリーへ生成する多ファイル・スケッチ。直接 compile では
作れない）10 本を除く **106 本すべて**が、導入済み platform から建ちます。

### 8. 実機回帰（6 板・10 走）

`fmp3_core` を使うチップ全種類。採取は dev の
`.steering/20260918-core339-m5libs/logs/`。

| profile / 例題 | 板 | 結果 |
|---|---|---|
| minimal / Blink | M5AtomS3Lite (S3・USJ) | `banner=1 setup=1 heartbeat=59 unexpected=0 blink=59` |
| minimal / Blink | M5Stack ATOM Lite (LX6・**実 UART**) | `banner=1 setup=1 heartbeat=59 unexpected=0 blink=59` |
| minimal / Blink | M5NanoC6 (C6) | `banner=1 setup=1 heartbeat=59 unexpected=0 smark=1 blink=59` |
| minimal / Blink | M5Stamp-C5 (C5) | `banner=1 setup=1 heartbeat=59 unexpected=0 smark=1 blink=59` |
| minimal / Blink | M5Stamp-P4 (P4・SMP) | `heartbeat=29 core2_alive=30 core2_full=30 prc2_start=1 unexpected=0` **VERDICT PASS** |
| wificonnect / WiFiScan | M5NanoC6 | `scan=11 scanap=11`・`heartbeat=56 unexpected=0` |
| wificonnect / WiFiScan | M5Stamp-C5 | `scan=20 scanap=20`・`heartbeat=48 unexpected=0` |
| wificonnect / WiFiScan | M5Stamp-P4 | `companion=1 ready=1 hosted_err=0 scan=14 scanap=14` **VERDICT PASS** |
| btclassic / BluetoothSPP | M5Stack ATOM Lite | `heartbeat=57 unexpected=0`・`[BluetoothSPP] discoverable as=1` |
| m5 / M5Unified | **M5CoreS3** | `heartbeat=58 unexpected=0`・`[M5] M5.begin and initial LCD draw PASS=1` |

`m5` は**このバンプで実際にバイト列が動いた唯一の profile** なので、そこが
実機で動くことが今回いちばん確かめたかった点です。

#### `rtc_clk_xtal_freq_get()` が読む値を独立に確かめた

自前実装が読むのは `RTC_XTAL_FREQ_REG`（= `RTC_CNTL_STORE4_REG`
= `0x600080C0`）です。スケッチと無関係に esptool で直接読むと:

```
CoreS3     0x600080c0 = 0x00280028
AtomS3Lite 0x600080c0 = 0x00280028
```

上下 16bit とも `0x28` = 40。⇒ `clk_ll_xtal_load_freq_mhz()` は 40 を返し、
esptool 自身が別経路で報告する `Crystal frequency: 40MHz` と一致します。
**フォールバック（レジスタ不正時の 40）は踏んでいません**——踏んでいれば
`M5_STUB_HIT` が 1 行出ます。

> **目視が要る 1 点**: 「`M5.begin()` が true を返し描画呼出しが成功した」は
> スケッチ自身の判定で、**画面に出ていることまでは自動では言えません**。
> 前回（2026-09-18・0.2.27）はユーザーが目視で確認しています。0.2.29 では
> SPI クロックの元になる XTAL 値が上のとおり正しいところまでを機械で確認しました。
