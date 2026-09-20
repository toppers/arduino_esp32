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

### 9. リリース形の verify（利用者と同じ経路・116 builds）

```
PASSED: 116 builds from the installed package on x86_64-pc-linux-gnu (116 planned)
```

package index を作り、loopback HTTP で配って Boards Manager からインストールし、
そのうえでスケッチを建てる経路です。**検証対象を一意にする**ため、手で入れた
`~/Arduino/hardware/toppers/esp32`（Boards Manager 版と同じ `toppers:esp32` を
名乗る）と `~/Arduino/libraries/ToppersFMP3-M5Stack`（同名のライブラリが
platform 内にも入る）の**両方を先に撤去**し、パッケージ版だけが存在する状態で
回しました。終了後に core を uninstall して手元の platform とライブラリを戻し、
`m5cores3_fmp3:FMP3Runtime=m5` の `M5Unified` が建つところまで確認しています。

M5Stack core / M5GFX / M5Unified の導入も skip せずこの経路で行っており、
ログに `Platform m5stack:esp32@3.3.9` / `M5GFX@0.2.29` / `M5Unified@0.2.22` が
出ます——**固定した版がそのまま導入できる**ことも同時に確かめています。

### 10. X-check のベースラインを採り直した（13 ステージ・5 チップ）

バンプ後のコミット（クリーンツリー）で `--force --clean` で採り直し、
**ESP32-P4 を初めてベースラインに入れた**（従来は 11 ステージ＝ 4 チップで、
P4 は `ignored (not in baseline)` だった）。`--clean` の完全再ビルドが
直前に建てたものとバイト一致し、自己照合も 13/13 MATCH。

---

## 2026-09-18: v0.6.0 として公開

上のバンプを含むリリース。`v0.6.0` タグ、
<https://github.com/toppers/arduino_esp32/releases/tag/v0.6.0>。

| アセット | |
|---|---|
| `toppers-esp32-0.6.0.zip` | 8.9 MB、sha256 `fc88281d564342e40454d53a45fcb61c2a80b37448ec129c66b6934770dd9273` |
| `package_toppers_index.json` | platform 6 版（`0.3.0`〜`0.6.0`）を保持 |
| `fmp3-link-{x86_64-pc-linux-gnu,x86_64-mingw32,arm64-apple-darwin}.zip` | `build-link-driver` の v0.6.0 タグビルドから |

検査:

- `check_release_artifacts.py` PASSED（過去 4 版のドライバが実 URL でまだ
  配信されていることまで確認＝ `--merge-into` が効いている証拠）
- `check_host_paths.py` PASSED（1234 ファイルにビルド機の絶対パス無し）
- `verify_package.py` **116/116**（loopback 経由。`toppers:esp32@0.6.0` が
  実際にインストールされたログ付き）
- **公開後に本物の URL からもう一度**: `releases/latest/download/package_toppers_index.json`
  を `--additional-urls` に渡して `core install toppers:esp32@0.6.0` し、
  `m5cores3_fmp3/m5/M5Unified`・`m5stampp4_fmp3/wificonnect/WiFiConnect`・
  `m5atomlite_fmp3/btclassic/BluetoothSPP` の 3 本が建つことを確認（手元の
  platform とライブラリ symlink は撤去したまま＝**公開物だけで**建てている）

利用者向け文言のうち、実測とずれていたものをこのリリースで直しました
（M5Stamp-P4 が「Minimal のみ・実機未確認」のままだった、M5Stack ATOM Lite が
「実機で一度も動かしていません」のままだった、配布 README の board 一覧が
8 板中 7 板しか載せていなかった）。

---

## 2026-09-19: v0.6.1 として公開（v0.6.0 の Upload 不能を直す）

`v0.6.1` タグ、<https://github.com/toppers/arduino_esp32/releases/tag/v0.6.1>。
`toppers-esp32-0.6.1.zip` は sha256
`8a4d61c1b9a55b7b48547be8863cba4227bdeef6d0c3e1939e451074cef32ec5`。

### なぜ出したか

**v0.6.0 は全 OS で Upload できなかった。** M5Stack core 3.3.9 が書き込み経路を
`tools/flasher.{py,exe}` 経由に変え、それが `{runtime.platform.path}`＝**本 platform**
から引かれる（ラッパは core 側にあるので届かない）。`platform_lines()` が core の
platform.txt を 1 行ずつ写す作りなので、そのまま入っていた。非 Windows 行は
`python3` を要求し、prebuilt stage が避けている当のものを持ち込んでいた。

### なぜ 116/116 の verify をすり抜けたか

**`verify_package.py` はコンパイルまでしか見ない。** Upload 経路には計器が無い。
v0.6.0 のとき 116/116 が通ったのは、壊れた場所を**誰も見ていなかった**からである。

### 実機で Upload を確かめた（2 系統）

今回は手で確かめた。`arduino-cli upload`＝**利用者と同じ経路**（`platform.txt` の
upload recipe）を、**公開された 0.6.1 を Boards Manager から入れた状態**で実行:

| 板 | 経路 | Upload | 書いた像の動作 |
|---|---|---|---|
| M5NanoC6（ESP32-C6） | USB Serial/JTAG | `Hash of data verified` | `banner=1 setup=1 heartbeat=29 blink=29 unexpected=0` |
| M5Stack ATOM Lite（ESP32 LX6） | **実 UART**（FTDI・115200） | `Hash of data verified` | `banner=1 setup=1 heartbeat=29 blink=29 unexpected=0` |

**2 系統にしたのは理由がある**——`flasher` の行は Windows 向けと非 Windows 向けで
別々にあり、USB-JTAG と実 UART では esptool の掴み方も違う。片方だけでは
「たまたま通った」を排除できない。また `Hash of data verified` は**書き込みの照合**で
あって**起動の証拠ではない**ので、採取まで回して像が走っていることを見た。

### その他の検査

`verify_package` **124/124**（`Fmp3Sample1` が増えて 116 -> 124）、
`check_release_artifacts` / `check_host_paths` とも PASSED、公開後に本物の URL から
`toppers:esp32@0.6.1` を入れ直して代表 3 本が建つこと、そして**配布される
`platform.txt` の `flasher` 参照が 0** であることを確認した。

### 残っている穴（塞がっていない）

**`verify_package` は依然として Upload 経路を見ない。** 今回は手で確かめたが、
同種の変更——`platform_lines()` が core の行を写す箇所——が次に入れば、同じように
すり抜ける。今日入った「`{runtime.platform.path}` 経由で参照されるのに同梱して
いないものを列挙する install 時監査」が最も近い防御だが、それは**ファイルの不在**を
見るものであって、**Upload が通るか**は見ていない。実機 Upload を検査へ組み込むには
板が要るので、やるかどうかは判断事項として残す。

### リリース後に埋めた確認（2026-09-19）

v0.6.1 を出した時点で、`Fmp3Sample1` の実機確認は **Xtensa の 2 板だけ**だった。
RISC-V 側（C6 / C5 / P4）は cfg もラッパも書いてビルドとリンクは通したが、
**一度も動かしていなかった**——`release-allowlist.json` のコメントにも
「M5Stack ATOM Lite で確認」とだけ書いてあった（正直ではあるが、覆いは狭い）。

板が戻ったので 6 板・全ポートで回して埋めた:

| ポート | 板 | 周期 | アラーム | `unexpected` |
|---|---|---|---|---|
| Xtensa LX6 | M5Stack ATOM Lite | 60 | 1 | 0 |
| Xtensa LX7 | M5CoreS3 | 60 | 1 | 0 |
| Xtensa LX7 | M5AtomS3 Lite | 61 | 1 | 0 |
| **RISC-V** | **M5Stamp-P4（SMP 2 コア）** | 60 | 1 | 0 |
| RISC-V | M5NanoC6 | 61 | 1 | 0 |
| RISC-V | M5Stamp-C5 | 61 | 1 | 0 |

周期の 60/61 は採取窓の開始位相の差で、動作の差ではない（1 秒周期・60 秒採取）。
**アラームはどの板でも 1** で、一度きりであることがそこで確かめられる。
P4 は同じ採取で `core2_alive=60 core2_full=60 prc2_start=1` も出ており、
PRC1 で周期通知と 3 タスクが回る間に PRC2 の 60 行が 1 行も壊れていない。

あわせて **F-3 の帰属に LX6 の 2 枚目を足した**（M5Stack Basic）。
詳細は `docs/atoms3lite-port.md`。


---

## 2026-09-21: v0.6.2 として公開（Xtensa のタイマ 3 件を直す）

`v0.6.2` タグ、<https://github.com/toppers/arduino_esp32/releases/tag/v0.6.2>。
`toppers-esp32-0.6.2.zip` は sha256
`33af8b25d39268551aca6873ba6f0f31daa3ceb0622565e917d08fb247f59082`。
**リンクドライバは v0.6.1 のものを再利用した**（`scripts/fmp3_link.py` が
不変なので `--reuse-driver-from 0.6.1`。このリリースに `fmp3-link-*.zip` は
添付していない）。v0.6.1 からの実体は **ESP32-S3 / ESP32（Xtensa）のタイマまわりの
欠陥 3 件**で、いずれも開発ツリー側では既に直っていたものが、arduino 側の複製に
残っていた（[`tree-sync-audit.md`](tree-sync-audit.md)）。

| # | 件 | 症状 | 実証 |
|---|---|---|---|
| A-3 | タイマ待ちを `TMAX_RELTIM` で頭打ちにせず、`twai_sem` の戻り値も捨てていた。`_timer_arm` の ms->us が 32bit | **Wi-Fi 切断後に全タスクが 296 秒止まる**（`loop()` が呼ばれず、コンソールも沈黙） | 修正前 120 秒で 3 周 -> 修正後 117 周（欠番なし） |
| A-1 | `esp_shim_time_us()` が 32bit HRT をゼロ拡張して返していた | **71.6 分で時刻が巻き戻る**。one-shot タイマが最大 71 分遅延、lwIP `sys_now()` が逆行 | 85 分 soak で 72.0 分に 2^32us を跨ぎ、171 サンプル全部で単調増加 |
| A-2 | `target_hrt_raise_event()` が `ccount+1` を 1 回置くだけ | CCOMPARE0 の厳密一致を取りこぼすと次の一致まで最大 17.9 秒（240MHz） | コード等価（dev・LX6 と同一形）。**単独の実機実証は無し** |

詳細は [`xtensa-timer-starvation.md`](xtensa-timer-starvation.md)。

**A-1・A-2 は ESP32-S3 だけの取り残しだった**——同じこのリポジトリの中で、
LX6 の `target_timer.h` には両方入っていた。複製は板ごとに別のタイミングで
取られており、古さも板ごとに違う。

### 配布バイト列が動いた範囲

X-check（v0.6.1 相当のステージを基準）:

```
esp32s3/minimal:      DIFF  objs/time_event.o
esp32s3/m5-unified:   DIFF  objs/time_event.o
esp32s3/wifi-connect: DIFF  objs/time_event.o objs/esp_shim.o objs/esp_wifi_adapter.o
esp32/wifi-connect:   DIFF  objs/esp_shim.o objs/esp_wifi_adapter.o
esp32/bt-classic:     DIFF  objs/esp_shim.o
esp32/minimal, esp32/m5-unified: MATCH
```

**LX6 の minimal と m5-unified が MATCH** なのは、LX6 のヘッダが元から直って
いたからで、診断と整合する。RISC-V 3 板（C6/C5/P4）は無関係（ステージ不変）。

### 同梱したが配布物には入らないもの

`all-in-one`（M5Unified + Dual Core + WiFi の合成）の試作が入っている。
**既定のステージ集合に無いので配布物には含まれない**——自分で
`--profiles all-in-one` を建てたときだけ `Tools > FMP3 Runtime` に出る
（画面のある 3 板のみ）。出荷の本数（124）も出荷バイト列も不変であることを
確認済み。[`all-in-one-prototype.md`](all-in-one-prototype.md)。

### リリース作業中に見つけた CI の欠陥（2026-09-20）

v0.6.2 のために `verify-package` を回したら `Build the platform` が落ちた。
**「M5StampP4 is not pinned to the esp32p4_es SDK」——だが boards.txt には
その行が在った。**

検査式が `grep -q "^m5stampp4_fmp3\.build\.chip_variant=esp32p4_es$"` で、
`install_platform.py` は boards.txt を **CRLF** で書く（初回コミットから一貫）。
`$` は CR に阻まれるので、**この検査は書かれた日から一度も通っていなかった**
（2026-09-17 の P4 追加で入り、以後 verify-package が回っていなかった）。
CR を落としたコピーへ照合する形に直した。

**手元で 30 分溶かした理由**: 対話シェルの `grep` が ugrep への委譲関数に
なっていて、`$` を CR の前で一致させる。だから「手元では在る、CI では無い」に
見えた。`/usr/bin/grep` では手元でも一致しない。
⇒ **検査式の成否を手元で確かめるときは、`type grep` で実体を見ること**
（`~/agents_playbook/rename-identifiers.md` に同じ罠の記録がある）。

⇒ 副産物として分かったこと: **v0.6.0 と v0.6.1 は、この CI が緑の状態では
出していない**（ローカルの `verify_package.py` 124/124 が根拠だった）。

### 公開後の確認（すべて実行済み）

- `latest` の index を取得して、検査を通したものと**バイト一致**。
  draft でも pre-release でもなく、アセットは 2 本とも添付されている。
- index が名指しする**全 29 アセット**（platform 8 版 + ドライバ 21 本）を
  実 URL から取得して sha256 照合 -> **29/29 一致**。引き継いだ過去の版も生きている。
- **公開物だけで**隔離環境へ導入（`toppers:esp32@0.6.2`）し、代表 3 本を建てた:
  `m5cores3_fmp3/m5/M5Unified`・`m5stampp4_fmp3/wificonnect/WiFiConnect`・
  `m5atomlite_fmp3/btclassic/BluetoothSPP` -> **3/3 PASS**。
- CI `verify-package` は 3 ホストとも **124/124**、`Compare images across hosts` も緑。
