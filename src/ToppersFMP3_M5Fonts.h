#ifndef TOPPERS_FMP3_M5FONTS_H
#define TOPPERS_FMP3_M5FONTS_H

/*
 *  M5GFX のフォントをスケッチから選ぶためのID。
 *  scripts/gen_m5_fonts.py が M5GFX の lgfx_fonts.hpp から生成する。手で編集しない。
 *  生成元: M5GFX 0.2.27
 *
 *  M5GFX 本体は setFont(&fonts::名前) とポインタで指定するが、スケッチ側と
 *  ランタイム側で M5GFX のクラスレイアウトが一致しない（スケッチは -DARDUINO
 *  付きで Arduino の Print を、ランタイムは M5GFX 内蔵の Print を基底にする）。
 *  そのため境界は C に限り、フォントはIDで指定する。
 *
 *  CJK フォント（U8g2font）は含まない。1つで app パーティション(3MB)を超える
 *  （lgfx_efont_ja が 8.8MB、lgfx_efont_tw が 11.6MB）。
 *
 *  使い方:
 *      toppers_m5_set_font(TOPPERS_M5_FONT_FreeSans12pt7b);
 */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TOPPERS_M5_FONT_Font0 = 0,  /* lgfx::GLCDfont */
    TOPPERS_M5_FONT_Font2 = 1,  /* lgfx::BMPfont */
    TOPPERS_M5_FONT_Font4 = 2,  /* lgfx::RLEfont */
    TOPPERS_M5_FONT_Font6 = 3,  /* lgfx::RLEfont */
    TOPPERS_M5_FONT_Font7 = 4,  /* lgfx::RLEfont */
    TOPPERS_M5_FONT_Font8 = 5,  /* lgfx::RLEfont */
    TOPPERS_M5_FONT_Font8x8C64 = 6,  /* lgfx::GLCDfont */
    TOPPERS_M5_FONT_AsciiFont8x16 = 7,  /* lgfx::FixedBMPfont */
    TOPPERS_M5_FONT_AsciiFont24x48 = 8,  /* lgfx::FixedBMPfont */
    TOPPERS_M5_FONT_TomThumb = 9,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMono9pt7b = 10,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMono12pt7b = 11,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMono18pt7b = 12,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMono24pt7b = 13,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBold9pt7b = 14,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBold12pt7b = 15,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBold18pt7b = 16,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBold24pt7b = 17,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoOblique9pt7b = 18,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoOblique12pt7b = 19,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoOblique18pt7b = 20,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoOblique24pt7b = 21,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBoldOblique9pt7b = 22,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBoldOblique12pt7b = 23,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBoldOblique18pt7b = 24,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeMonoBoldOblique24pt7b = 25,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSans9pt7b = 26,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSans12pt7b = 27,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSans18pt7b = 28,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSans24pt7b = 29,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBold9pt7b = 30,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBold12pt7b = 31,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBold18pt7b = 32,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBold24pt7b = 33,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansOblique9pt7b = 34,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansOblique12pt7b = 35,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansOblique18pt7b = 36,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansOblique24pt7b = 37,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBoldOblique9pt7b = 38,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBoldOblique12pt7b = 39,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBoldOblique18pt7b = 40,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSansBoldOblique24pt7b = 41,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerif9pt7b = 42,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerif12pt7b = 43,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerif18pt7b = 44,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerif24pt7b = 45,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifItalic9pt7b = 46,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifItalic12pt7b = 47,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifItalic18pt7b = 48,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifItalic24pt7b = 49,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBold9pt7b = 50,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBold12pt7b = 51,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBold18pt7b = 52,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBold24pt7b = 53,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBoldItalic9pt7b = 54,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBoldItalic12pt7b = 55,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBoldItalic18pt7b = 56,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_FreeSerifBoldItalic24pt7b = 57,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_Orbitron_Light_24 = 58,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_Orbitron_Light_32 = 59,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_Roboto_Thin_24 = 60,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_Satisfy_24 = 61,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_Yellowtail_32 = 62,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu9 = 63,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu12 = 64,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu18 = 65,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu24 = 66,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu40 = 67,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu56 = 68,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_DejaVu72 = 69,  /* lgfx::GFXfont */
    TOPPERS_M5_FONT_COUNT = 70
};

/*
 *  フォントを選ぶ。成功で 0、未知のIDなら -1。
 *  ヘッダと同じリリースのランタイムに対してのみ有効（IDは生成時に決まる）。
 */
int32_t toppers_m5_set_font(int32_t font_id);

/*  ランタイムが持っているフォント数。ヘッダとの一致確認に使える。 */
int32_t toppers_m5_font_count(void);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_FMP3_M5FONTS_H */
