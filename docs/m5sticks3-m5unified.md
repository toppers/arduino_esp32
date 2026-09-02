# M5StickS3 で M5Unified が動かない件（調査記録・2026-09-02）

`minimal` と `wifi-connect` は M5StickS3 実機で動く。`m5-unified` は**動かない**。
ここまでで分かったことと、次に見るべき場所を残す。

## 症状

```
[M5] M5.begin returned
[M5] LCD SPI bus was not available
[M5] M5.begin or initial LCD draw FAILED
[M5] Display.board=0 M5.board=137 w=0 h=0 begin=-1
```

- `Display.board=0` — **M5GFX の autodetect が何も検出できていない**（`board_unknown`）
- `M5.board=137` — `board_M5AtomS3Lite`。M5Unified が「画面を持たない S3」用の
  フォールバック（`M5Unified.cpp:1630` の GPIO パターン表）で決めた値
- 画面が無いので SPI バスも無く、`M5.begin` が失敗する

## 潰した仮説

**どれも原因ではなかった。** 同じ道を辿らないよう全部残す。

| 疑い | 結果 |
| --- | --- |
| StickS3 検出の GPIO47/48 が読めない | **読める。** 同じ手順を自前で実行すると `0x03`（M5GFX が求める値そのもの） |
| PMIC(M5PM1) が I2C で読めない | **読める。** `readRegister8(I2C_NUM_1, 0x6E, 0x00)` が `0x50` を返す |
| `M5.begin` の途中で条件が壊れる | **壊れない。** begin の**後**に測っても `0x03` / `READ OK` のまま |
| `periph_module_enable` が空で I2C にクロックが来ない | **無関係。** ESP-IDF 5.x では M5GFX は `i2c_ll_enable_bus_clock()` 等の LL 関数で直接クロックを入れる |
| S3 用の検出コードがコンパイルされていない | **されている。** `CONFIG_IDF_TARGET_ESP32S3` は `wifi/config/esp32s3/sdkconfig.h:329` から届く（コマンドライン定義を見て「無い」と誤判定しかけた）。CoreS3 の検出も同じ `#elif` 分岐内なので、CoreS3 が壊れているわけでもない |

## 分かっている事実

- autodetect は走っている（`[BEGIN] -> P1 M5GFX::init_impl` / `<- ... = 0`）
- その間に `spi_bus_initialize` が **60 回**呼ばれ、`i2c_new_master_bus` は 0 回
  （StickS3 分岐は IDF API ではなく `lgfx::i2c` を直接使うので、これは整合する）
- M5GFX の `[Autodetect] board_XXX` ログは 1 行も出ない＝どの分岐も成立していない
- StickS3 分岐は `M5GFX.cpp:2837`、直前のブロックは不成立で抜けているように読める

## 結論（解決済み・実機で確認）

原因は **2 つ**あり、1 つ目を直すと 2 つ目が現れた。どちらも「CoreS3 だけを
見て書いたスタブが、CoreS3 でだけ偶然正しい」という同じ形をしている。

### 原因 1：`esp_efuse_get_pkg_ver()` が 0 固定のスタブだった

`m5/shim/m5_idf_stubs.c` に

```c
/*  チップパッケージ版数：CoreS3 の autodetect 判定には未使用（0 で無害）。 */
uint32_t esp_efuse_get_pkg_ver(void) { return(0U); }
```

とあった。**このコメントの前提が誤り**で、M5GFX の S3 用 autodetect は本体が
丸ごと `switch (m5gfx::get_pkg_ver())`（`M5GFX.cpp:1708`）である。

| case | 意味 | そこで検出される機種 |
| --- | --- | --- |
| `0` | QFN56 | CoreS3 など |
| `1` | LGA56（ESP32-S3-PICO） | **StickS3** |
| `default` | — | 何も検出せず `board_unknown` |

0 固定なので **CoreS3 だけが偶然通り、StickS3 は `case 0` に落ちて必ず
`board_unknown`（`Display` の幅 0）になっていた**。「S3 の検出コードは
コンパイルされている」「GPIO も I2C も読める」という調査結果と矛盾しないのは
このためで、StickS3 の判定コードは**そもそも実行されていなかった**。

`esp_efuse_read_field_blob()` 一式を持ち込まず、読み出しレジスタを直接見る形で
実装した。ビット位置は ESP-IDF の `esp_efuse_table.c` と `soc/efuse_reg.h` の
双方で一致を確認している。

- S3 : `PKG_VERSION` = BLK1 bit117 幅3 ＝ `EFUSE_RD_MAC_SPI_SYS_3_REG` の `[23:21]`
- LX6: `CHIP_PACKAGE` = BLK0 bit105 幅3 ＝ `EFUSE_BLK0_RDATA3_REG` の `[11:9]`、
  上位 1bit が `CHIP_PACKAGE_4BIT` ＝ 同レジスタの bit2

M5Stack Basic（ESP32-D0WDQ6）は `CHIP_PACKAGE=0` なので、LX6 側の実値は
従来のスタブの戻り値と一致し、M5Core の挙動は変わらない。

実機での確認：`[A1] pkg_ver=0x00000001` → StickS3 分岐に入り
`gpio47/48 result=0x03`・`pm1 has_value=1` → `[Autodetect] board_M5StickS3`。

### 原因 2：`spi_bus_initialize()` の S3 分岐が GPSPI2 決め打ちだった

原因 1 を直すと、検出は通るが `M5.begin` が返らなくなった。ROM が出す
`Saved PC:0x42001fbf` を `addr2line` にかけると
`lgfx::v1::Bus_SPI::writeData` ＝ `while (*spi_cmd_reg & SPI_USR)` の完了待ちで
無限ループしていた。

M5GFX の StickS3 は `bus_cfg.spi_host = SPI3_HOST`（MOSI=39 / SCLK=40 / DC=45 /
CS=41 / RST=21）を要求するのに、`m5/shim/m5_spi_bus_stub.c` の S3 分岐は
`host_id` を捨てて GPSPI2 固定だった。よって

- クロック／リセットが SPI2 側にしか入らない（`SYSTEM_SPI3_CLK_EN` が立たない）
- マスタクロックゲート `SPI_CLK_GATE_REG(2)` も GPSPI2 のもの
- GPIO マトリクスが `FSPID`/`FSPICLK`（SPI2 の信号）へ張られる

一方 `Bus_SPI` はレジスタベースを `spi_host` から決めるので **GPSPI3 のレジスタを
叩く**。クロックの来ていない GPSPI3 では `SPI_USR` が永久に落ちない。

LX6 分岐は既に `host_id` で選び分けていた（M5Stack Basic が VSPI＝SPI3 を使う
ため）。S3 分岐を同じ形に揃えた。`spi_bus_add_device()` の CS 信号も同様。

### 実機での最終確認

```
I (0) M5GFX: [Autodetect] board_M5StickS3
[BEGIN] <- P2 M5Unified::_check_boardtype = 26
[BEGIN] <- P6a Power_Class::begin = 1
[BEGIN] <- P9b IMU_Class::begin (BMI270/BMM150) = 1
[M5] M5.begin and initial LCD draw PASS
[M5] board=26 w=240 h=135 imu=1 power=6 batt_mv=4006
```

`26 = 0x1a = board_M5StickS3`。`240x135` は StickS3 の 1.14 インチ ST7789 の
実寸で一致する。

### この調査から持ち帰るもの

**「CoreS3 では使われていない」と書かれたスタブのコメントを根拠にしない。**
2 件とも、CoreS3 で動いていることが「正しさ」ではなく「たまたま既定値と
一致していること」の証明にすぎなかった。別基板を足すときは、
**定数を返しているスタブを機種差の候補として最初に洗う**のが速い。

## 当時「次に見るべき場所」として書いた内容（記録として残す）


**M5GFX 自身に計装を入れるしかない段階。** `M5GFX.cpp:2837` の分岐に入ったか、
入ったとして `gpio::command` の戻り値と `chk_pm1.has_value()` が中で何になるかを
出力させる。スケッチブックのライブラリを一時的に書き換えることになるので、
戻す前提で行うこと。

自前で同じ 2 段を実行すると成立するのに、autodetect の中では成立しない——
この差がどこから来るのかが唯一の未解明点。
