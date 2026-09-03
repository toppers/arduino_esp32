/*
 *  TOPPERS/FMP3 Arduino — M5Unified をスケッチから安全に使うための入口
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ★スケッチが M5Unified を使うときは <M5Unified.h> ではなく**このヘッダ**を
 *    include すること。理由は ABI にある。
 *
 *  M5GFX の LGFXBase は ARDUINO が定義されているときだけ Print を継承する
 *  （M5GFX/src/lgfx/v1/LGFXBase.hpp:64）。
 *
 *      class LGFXBase
 *      #if defined (ARDUINO)
 *        : public Print
 *      #endif
 *
 *  プリビルトステージは M5GFX / M5Unified を **ARDUINO 未定義**でビルドする。
 *  これは単なる都合ではなく必然で、ARDUINO を定義すると M5GFX は SPI の
 *  トランザクションを Arduino-ESP32 の HAL（spiSimpleTransaction / spiEndTransaction を
 *  spi_t* に対して）で行う経路に切り替わる。本移植はその HAL を積んでおらず、
 *  ESP-IDF 側の経路（spi_device_acquire_bus）を自前 shim で受けている。
 *
 *  一方 arduino-cli はスケッチを必ず -DARDUINO=xxxxx でコンパイルする。
 *  そのため素朴に <M5Unified.h> を include すると、同じ 1 個のオブジェクト
 *  （ELF 上で M5 はただ 1 つ）をスケッチとステージが**異なるレイアウト**で
 *  見ることになる：
 *
 *      sizeof(lgfx::v1::LovyanGFX)      ステージ 0x100 / スケッチ 0x104
 *      sizeof(m5::M5Unified)            ステージ 0x804 / スケッチ 0x808
 *      offsetof(m5::M5Unified, Power)   ステージ 0x27c / スケッチ 0x280
 *
 *  Display 以降のメンバが 4 バイトずれるので、スケッチから M5. に触ると
 *  壊れた値を読む。2026-09-02 に CoreS3 実機で観測した実際の症状：
 *
 *      M5.getBoard()          -> 256        （正しくは 10）
 *      M5.Power.getType()     -> 16711680   （正しくは 4）
 *      M5.Display.width()     -> Guru Meditation Error: LoadProhibited
 *
 *  このヘッダは include の間だけ ARDUINO を外すことでスケッチ側の見え方を
 *  ステージに揃える。ARDUINO 未定義でも表示系の機能は失われない——
 *  LGFXBase は !ARDUINO のとき print/println を自前で持つ（LGFXBase.hpp:859）。
 */

#ifndef TOPPERS_FMP3_M5UNIFIED_H
#define TOPPERS_FMP3_M5UNIFIED_H

#pragma push_macro("ARDUINO")
#undef ARDUINO
/*
 *  ARDUINO を外すと M5GFX の lgfx/utility/pgmspace.h が
 *  `#ifndef ARDUINO` の側に入り、memcpy_P / memcmp_P を static inline で
 *  自前定義する。ところがスケッチでは Arduino コアの pgmspace.h が先に
 *  `#define memcpy_P memcpy` を張っているので、その定義が
 *  `static inline void* memcpy(...)` に化けて string.h の extern 宣言と
 *  衝突する。include の間だけマクロを外し、後で戻す。
 */
#pragma push_macro("memcpy_P")
#pragma push_macro("memcmp_P")
#undef memcpy_P
#undef memcmp_P

#include <M5Unified.h>

#pragma pop_macro("memcmp_P")
#pragma pop_macro("memcpy_P")
#pragma pop_macro("ARDUINO")

#include <ToppersFMP3_M5UnifiedBridge.h>

/*
 *  ガード：このヘッダより前に <M5Unified.h> が（ARDUINO 定義下で）読まれて
 *  いたら、上の include はインクルードガードで no-op になり、レイアウトは
 *  ずれたままになる。それを**ビルド時に**落とす。
 *
 *  判定は「LGFXBase が Print を継承しているか」で行う。継承していれば
 *  ARDUINO 定義下で解釈された証拠であり、実機で静かに壊れる代わりに
 *  ここでコンパイルエラーになる。
 */
#if defined(__cplusplus) && defined(ARDUINO) && __has_include(<Print.h>)
#include <Print.h>
#include <type_traits>
static_assert(
    !std::is_base_of<Print, lgfx::v1::LGFXBase>::value,
    "M5Unified.h was parsed with ARDUINO defined, so the sketch's view of "
    "m5::M5Unified does not match the prebuilt FMP3 runtime (4-byte shift). "
    "Include <ToppersFMP3_M5Unified.h> before any <M5Unified.h> / <M5GFX.h>.");
#endif

#endif /* TOPPERS_FMP3_M5UNIFIED_H */
