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

/*
 *  内蔵スピーカー。**CoreS3 / CoreS3-SE だけ**（AW88298 アンプ、I2S1）。
 *  ほかの板では begin / ready / tone / tone_channel が -1 を返し、何も鳴らさない。
 *
 *  toppers_m5_speaker_begin: 開始する。setup() / loop() から呼ぶこと
 *    （I2S の割込みを配線する処理がコア 0 のタスクでしか動かないため）。
 *    1 = 鳴らせる、-1 = この板では使えない／開始に失敗、
 *    -3 = コア 0 以外から呼んだ（状態は変えないので、コア 0 から再試行できる）。
 *  toppers_m5_speaker_ready: 1 = 開始済み、0 = 未開始、負 = begin の失敗理由。
 *  toppers_m5_speaker_tone: 単音を非同期に鳴らす（すぐ戻る）。未開始なら
 *    先に begin する。frequency_hz は 20..20000、duration_ms は 1..60000、
 *    volume は 0..255（0 は無音）。この音のチャンネル音量になる。
 *    1 = 受け付けた、0 = 受け付けなかった（直前の要求を M5 がまだ取り込んで
 *    いない。捨ててよい）、-2 = 引数が範囲外、-1 / -3 は begin と同じ。
 *    短い音が重なっても前の音を切らないよう、M5 のチャンネル 0..3 を順に
 *    使う（4..7 には載せない）。
 *  toppers_m5_speaker_tone_channel: チャンネルを指定して鳴らす（BGM 用）。
 *    channel は 0..7、ほかの引数と戻り値は tone と同じ（channel が範囲外でも
 *    -2）。同じチャンネルに残っている音は新しい音に置き換える。0..3 は tone の
 *    順送りと共有するので、tone と混ぜて使う音は 4..7 に載せること。
 *  toppers_m5_speaker_stop: 鳴っている音をすべて止める（全チャンネル）。
 *  toppers_m5_speaker_stop_channel: そのチャンネルの音だけを止める。
 *    範囲外または未開始なら何もしない。
 *  toppers_m5_speaker_set_volume: 全体音量 0..255（M5.Speaker.setVolume）。
 *    既定は M5Unified と同じ 64。1 = 設定した、-1 = この板では使えない。
 *    ★振幅は「全体音量の二乗 × tone の volume の二乗」に比例する
 *    （M5Unified の Speaker_Class の計算そのまま）。1 kHz のトーンで書かれた
 *    PCM の最大振幅（最大 32767）を CoreS3 で測った値:
 *        全体 64 × volume 64 → 64     全体 64 × volume 255 → 1009
 *        全体 255 × volume 64 → 1009  全体 255 × volume 255 → 16004
 *    既定の全体音量 64 のまま小さい volume で鳴らすと、とても小さい。
 *  toppers_m5_speaker_amp_reg: AW88298 のレジスタ値（16 ビット）を読む。
 *    診断用。読めなければ -1。
 */
int32_t toppers_m5_speaker_begin(void);
int32_t toppers_m5_speaker_ready(void);
int32_t toppers_m5_speaker_tone(uint32_t frequency_hz, uint32_t duration_ms,
                                uint8_t volume);
int32_t toppers_m5_speaker_tone_channel(uint32_t frequency_hz,
                                        uint32_t duration_ms,
                                        uint8_t volume, uint8_t channel);
void toppers_m5_speaker_stop(void);
void toppers_m5_speaker_stop_channel(uint8_t channel);
int32_t toppers_m5_speaker_set_volume(uint8_t master_volume);
int32_t toppers_m5_speaker_amp_reg(uint8_t reg);
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
