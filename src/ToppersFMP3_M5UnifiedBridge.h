#ifndef TOPPERS_FMP3_M5UNIFIED_BRIDGE_H
#define TOPPERS_FMP3_M5UNIFIED_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t toppers_m5_begin(void);
/*  タッチ状態を更新し、触れている位置へ診断用の水色の印を描く。 */
void toppers_m5_update(void);
/*  タッチ状態を更新する。画面へ診断用の印を描かない。 */
void toppers_m5_poll_touch(void);
void toppers_m5_draw_liveness(uint32_t seconds);

/*
 *  M5GFX の描画処理はランタイム側で実行し、スケッチとは C ABI で接続する
 *  （スケッチから M5.Display を直接触れない理由は ToppersFMP3_M5Unified.h）。
 *
 *  draw_begin / draw_end は一連の描画を 1 回の SPI トランザクションに
 *  まとめる（startWrite / endWrite）。表示が初期化されていない、または
 *  引数が無効なら何もしない。
 *
 *  push_rgb565 は行優先の RGB565 配列（width * height 個）を 1 回で転送する。
 *  転送が終わって戻るまで、呼び出し元は pixels を書き換えないこと。
 */
void toppers_m5_draw_begin(void);
void toppers_m5_draw_end(void);
void toppers_m5_fill_rect(int32_t x, int32_t y, int32_t width,
                          int32_t height, uint16_t rgb565);
void toppers_m5_draw_text(int32_t x, int32_t y, const char *text,
                          uint16_t foreground, uint16_t background);
void toppers_m5_push_rgb565(int32_t x, int32_t y, int32_t width,
                            int32_t height, const uint16_t *pixels);
/*  バックライトOFF＋パネルsleep（CoreS3はAXP2101経由。PMICの状態は保持される） */
void toppers_m5_display_off(void);

/*
 *  フォント選択。IDは ToppersFMP3_M5Fonts.h（生成物）を参照する。
 *  M5GFXのsetFont(&fonts::名前)を直接呼べない理由はそのヘッダに書いてある。
 */
int32_t toppers_m5_set_font(int32_t font_id);
int32_t toppers_m5_font_count(void);

int32_t toppers_m5_board(void);
int32_t toppers_m5_display_width(void);
int32_t toppers_m5_display_height(void);
int32_t toppers_m5_touch_enabled(void);
int32_t toppers_m5_touch_count(void);
int32_t toppers_m5_touch_x(void);
int32_t toppers_m5_touch_y(void);
int32_t toppers_m5_imu_enabled(void);
int32_t toppers_m5_rtc_enabled(void);
int32_t toppers_m5_power_type(void);
int32_t toppers_m5_battery_mv(void);
uint32_t toppers_m5_trace_enters(void);
uint32_t toppers_m5_trace_leaves(void);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_FMP3_M5UNIFIED_BRIDGE_H */
