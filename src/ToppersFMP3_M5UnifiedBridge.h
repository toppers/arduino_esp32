#ifndef TOPPERS_FMP3_M5UNIFIED_BRIDGE_H
#define TOPPERS_FMP3_M5UNIFIED_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t toppers_m5_begin(void);
void toppers_m5_update(void);
void toppers_m5_draw_liveness(uint32_t seconds);
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
