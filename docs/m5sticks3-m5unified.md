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

## 次に見るべき場所

**M5GFX 自身に計装を入れるしかない段階。** `M5GFX.cpp:2837` の分岐に入ったか、
入ったとして `gpio::command` の戻り値と `chk_pm1.has_value()` が中で何になるかを
出力させる。スケッチブックのライブラリを一時的に書き換えることになるので、
戻す前提で行うこと。

自前で同じ 2 段を実行すると成立するのに、autodetect の中では成立しない——
この差がどこから来るのかが唯一の未解明点。
