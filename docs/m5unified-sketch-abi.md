# スケッチとプリビルトランタイムの M5Unified ABI

2026-09-02、CoreS3 実機で発覚。**スケッチから `M5.` のメンバに触ると壊れた値を
読み、`M5.Display.width()` では落ちていた。**

## 症状

`M5Unified` の例は `toppers_m5_*` ブリッジ経由で値を取っており、そちらは正しい。
スケッチから live に読むと次のようになった。

```
[LIVE] board=256 pmic=16711680 w=Guru Meditation Error: Core 0 panic'ed (LoadProhibited)
```

正しくは `board=10`（`board_M5StackCoreS3`）、`pmic=4`（AXP2101）、`w=320`。
`256 = 0x100`、`16711680 = 0xFF0000` というバイトのずれた値が手がかりだった。

## 原因

M5GFX の `LGFXBase` は **`ARDUINO` が定義されているときだけ `Print` を継承する**。

```cpp
// M5GFX/src/lgfx/v1/LGFXBase.hpp:64
class LGFXBase
#if defined (ARDUINO)
  : public Print
#endif
```

プリビルトステージは `ARDUINO` **未定義**でビルドし、arduino-cli はスケッチを
**必ず `-DARDUINO=xxxxx`** でコンパイルする。同じ 1 個のオブジェクト（ELF 上で
`M5` は `0x3fc98648` にただ 1 つ）を、両者が異なるレイアウトで見ていた。

両方のフラグで同じヘッダをコンパイルして測った値（推測ではない）:

| | ステージ | スケッチ |
| --- | --- | --- |
| `sizeof(lgfx::v1::LovyanGFX)` | `0x100` | `0x104` |
| `sizeof(m5::M5Unified)` | `0x804` | `0x808` |
| `offsetof(m5::M5Unified, Power)` | `0x27c` | `0x280` |

`Display` は先頭近く（offset 4）にあるので、**それ以降のメンバが全部 4 バイトずれる**。

## なぜステージ側で `ARDUINO` を定義して揃えないのか

最初にその方向を試して、**誤りだと分かった**。M5GFX にとって `ARDUINO` は
「`Print` が使える」ではなく **「Arduino-ESP32 の SPI HAL を使う」**という意味である。

```cpp
// M5GFX/src/lgfx/v1/platforms/esp32/common.cpp:871
#if defined (ARDUINO) // Arduino ESP32
    spiSimpleTransaction(_spi_handle[spi_host]);
#else // ESP-IDF
    spi_device_acquire_bus(_spi_dev_handle[spi_host], portMAX_DELAY);
```

本移植は Arduino の SPI HAL を積んでおらず、ESP-IDF 側の経路を自前 shim で
受けている。ステージで `ARDUINO` を定義すると `_spi_handle`（常に nullptr）を
Arduino HAL に渡す経路に切り替わる。加えて、コアの `Arduino.h` は `esp32-hal.h`
経由で FreeRTOS・lwIP・USB・touch ドライバまで引き連れてきて、m5-unified
プロファイルはそのどれも積んでいない（lwIP の `CONFIG_TCP_OVERSIZE_*` 未定義で
止まるところまで確認した）。

**したがって、揃える向きは逆でなければならない。**

## 対処

`src/ToppersFMP3_M5Unified.h` を新設した。include の間だけ `ARDUINO` を外して
`<M5Unified.h>` を読むので、スケッチ側の見え方がステージに一致する
（実測で `0x100` / `0x804`）。`ARDUINO` 未定義でも表示機能は失われない——
`LGFXBase` は `!ARDUINO` のとき `print` / `println` を自前で持つ
（`LGFXBase.hpp:859`）。

**スケッチは `<M5Unified.h>` ではなくこのヘッダを include すること。**

副作用が 1 つあり、これも同ヘッダで処理している。`ARDUINO` を外すと M5GFX の
`lgfx/utility/pgmspace.h` が `memcpy_P` / `memcmp_P` を `static inline` で自前
定義するが、スケッチでは Arduino コアの `pgmspace.h` が先に
`#define memcpy_P memcpy` を張っているため、その定義が
`static inline void* memcpy(...)` に化けて `string.h` の extern 宣言と衝突する。
include の間だけこの 2 つのマクロも外す。

## ガード

同ヘッダに `static_assert` を置いた。判定は「`LGFXBase` が `Print` を継承して
いるか」で行う。継承していれば `ARDUINO` 定義下で解釈された証拠なので、
**実機で静かに壊れる代わりにビルドが止まる**。

先に `<M5Unified.h>` を読んでしまった場合、こちらの include はインクルード
ガードで no-op になりレイアウトはずれたままになる——それがこのガードの捕まえる
ケースである。動作は両方向で確認済み（正しい順序は通り `0x804`、
誤った順序は `static_assert` で停止）。

なお、このヘッダを一切 include しないスケッチは検出できない。そこは
ドキュメントと同梱例に頼っている。

## 実機での確認（CoreS3、2026-09-02）

```
[LIVE] board=10 pmic=4 w=320 sda=12 scl=11
[CTRL] id(0x03)=74 expect=74 st(0x00)=32 adcen(0x30)=15 pct(0xA4)=0
```

`id(0x03)` は AXP2101 のチップ ID で `0x4A` が規約値。これが読めることが
**この計測自体の対照**であり、以前はスケッチ側から 0 しか返らなかった。
`st` / `adcen` はステージ側から読んだ値と一致する。

## 持ち帰るもの

`examples/M5UnifiedLink/M5UnifiedLink.ino` は `&M5` を取ることが
「Arduino sketch ABI reaches the M5Unified object」を証明すると書いていた。
**証明していたのはリンクが通ることだけで、ABI は一致していなかった。**
アドレスは片方のレイアウトが壊れていても同じ値になる。
コメントは実際の保証内容に書き直した。
