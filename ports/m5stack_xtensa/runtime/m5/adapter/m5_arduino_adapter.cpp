#include <M5Unified.h>

#include <ToppersFMP3_M5UnifiedBridge.h>

extern "C" void m5_begin_trace_quiet(void);
extern "C" unsigned int m5_begin_trace_nenters(void);
extern "C" unsigned int m5_begin_trace_nleaves(void);
extern "C" void target_fput_log(char character);

namespace {

int32_t phase5Board;
int32_t phase5Width;
int32_t phase5Height;
int32_t phase5TouchEnabled;
int32_t phase5TouchCount;
int32_t phase5TouchX;
int32_t phase5TouchY;
int32_t phase5ImuEnabled;
int32_t phase5RtcEnabled;
int32_t phase5PowerType;
int32_t phase5BatteryMv;

void phase5AdapterLog(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

void phase5DisableDisplayDma()
{
    auto *panel = M5.Display.getPanel();
    auto *bus = (panel != nullptr) ? panel->getBus() : nullptr;

    if ((bus != nullptr) &&
        (bus->busType() == lgfx::v1::bus_type_t::bus_spi)) {
        auto *spiBus = static_cast<lgfx::v1::Bus_SPI *>(bus);
        auto config = spiBus->config();
        config.dma_channel = 0;
        spiBus->config(config);
        phase5AdapterLog("[M5] LCD SPI DMA disabled\n");
    }
    else {
        phase5AdapterLog("[M5] LCD SPI bus was not available\n");
    }
}

}  // namespace

extern "C" int32_t toppers_m5_begin(void)
{
    auto config = M5.config();

    config.clear_display = false;
    config.external_display_value = 0;
    config.external_speaker_value = 0;
    config.internal_mic = false;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    /*  Configures the speaker only: M5Unified starts it on first use, and
     *  toppers_m5_speaker_* below refuses every board but the CoreS3 / SE,
     *  the ones whose path (I2S1 -> AW88298) has been run here. The I2S
     *  channel behind it is m5/audio/m5_i2s_std_tx.c. */
    config.internal_spk = true;
#else
    config.internal_spk = false;
#endif
    config.internal_imu = true;
    config.internal_rtc = true;

    phase5AdapterLog("[M5] entering M5.begin\n");
    M5.begin(config);
    phase5AdapterLog("[M5] M5.begin returned\n");

    phase5Board = static_cast<int32_t>(M5.getBoard());
    phase5Width = M5.Display.width();
    phase5Height = M5.Display.height();
    phase5TouchEnabled = M5.Touch.isEnabled() ? 1 : 0;
    phase5ImuEnabled = M5.Imu.isEnabled() ? 1 : 0;
    phase5RtcEnabled = M5.Rtc.isEnabled() ? 1 : 0;
    phase5PowerType = static_cast<int32_t>(M5.Power.getType());
    phase5BatteryMv = M5.Power.getBatteryVoltage();

    phase5DisableDisplayDma();
    if ((phase5Width <= 0) || (phase5Height <= 0)) {
        return -1;
    }

    M5.Display.setRotation(1);
    phase5Width = M5.Display.width();
    phase5Height = M5.Display.height();
    M5.Display.startWrite();
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.fillRect(8, 8, phase5Width - 16, 42, TFT_BLUE);
    M5.Display.drawRect(4, 4, phase5Width - 8, phase5Height - 8, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(18, 20);
    M5.Display.print("TOPPERS/FMP3 + Arduino");
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(18, 62);
    M5.Display.print("M5Unified on FMP3");
    M5.Display.setCursor(18, 80);
    M5.Display.print("Touch the screen to test input");
    M5.Display.fillCircle(60, phase5Height - 60, 24, TFT_RED);
    M5.Display.fillCircle(140, phase5Height - 60, 24, TFT_GREEN);
    M5.Display.fillCircle(220, phase5Height - 60, 24, TFT_YELLOW);
    M5.Display.endWrite();

    m5_begin_trace_quiet();
    /*
     *  ここまで来たなら M5.begin も初期描画も終わっている。呼び出し元の
     *  メッセージ（"M5.begin and initial LCD draw PASS"）が問うているのは
     *  それであって、機種ではない。
     *
     *  以前は board が 10（board_M5StackCoreS3）か 17
     *  （board_M5StackCoreS3SE）かだけを見ていた。CoreS3 しか対象が無い間は
     *  同じ答えになるが、実際には「CoreS3 か？」を聞いており、M5Stack Basic
     *  （board_M5Stack = 1）では**画面が 320x240 で認識され描画も済んでいる
     *  のに FAILED**になった（2026-09-02 実機）。
     *
     *  判定は「機種が判別できた（board_unknown = 0 ではない）」かつ
     *  「画面の大きさが取れている」に置き換える。CoreS3 はどちらも満たすので
     *  従来と同じ答えになる。
     */
    return ((phase5Board != 0) && (phase5Width > 0) && (phase5Height > 0))
               ? 1 : -1;
}

/*
 *  入力の取得と診断描画を分ける。toppers_m5_update は従来の診断スケッチ
 *  向けで、触れた位置へ印を描く。ゲームは toppers_m5_poll_touch を呼び、
 *  入力を読むたびに画面を書き換えない（書き換えるとスクロール中の一括転送と
 *  重なってチラつく）。
 */
extern "C" void toppers_m5_poll_touch(void)
{
    M5.update();
    phase5TouchCount = M5.Touch.getCount();
    if (phase5TouchCount > 0) {
        const auto &detail = M5.Touch.getDetail(0);
        phase5TouchX = detail.x;
        phase5TouchY = detail.y;
    }
}

extern "C" void toppers_m5_update(void)
{
    toppers_m5_poll_touch();
    if (phase5TouchCount > 0) {
        M5.Display.fillCircle(phase5TouchX, phase5TouchY, 4, TFT_CYAN);
    }
}

namespace {

bool phase5DisplayReady()
{
    return (phase5Width > 0) && (phase5Height > 0);
}

}  // namespace

extern "C" void toppers_m5_draw_begin(void)
{
    if (phase5DisplayReady()) {
        M5.Display.startWrite();
    }
}

extern "C" void toppers_m5_draw_end(void)
{
    if (phase5DisplayReady()) {
        M5.Display.endWrite();
    }
}

extern "C" void toppers_m5_fill_rect(int32_t x, int32_t y,
                                     int32_t width, int32_t height,
                                     uint16_t rgb565)
{
    if (!phase5DisplayReady() || (width <= 0) || (height <= 0)) {
        return;
    }
    M5.Display.fillRect(x, y, width, height, rgb565);
}

extern "C" void toppers_m5_draw_text(int32_t x, int32_t y, const char *text,
                                     uint16_t foreground, uint16_t background)
{
    if (!phase5DisplayReady() || (text == nullptr)) {
        return;
    }
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(foreground, background);
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
}

/*
 *  LCD の SPI DMA は塞いである（phase5DisableDisplayDma、理由は
 *  m5_idf_prelude.h）ので、pushImage は CPU が pixels を SPI の FIFO へ
 *  写し終えてから戻る（最後の FIFO 分がまだ送出中でも、pixels はもう
 *  読まれない）。戻った時点で pixels を再利用してよいのはこのためで、
 *  DMA を有効にすると DMA が後から pixels を読むので、この約束が崩れる。
 */
extern "C" void toppers_m5_push_rgb565(int32_t x, int32_t y,
                                       int32_t width, int32_t height,
                                       const uint16_t *pixels)
{
    if (!phase5DisplayReady() || (width <= 0) || (height <= 0) ||
        (pixels == nullptr)) {
        return;
    }
    M5.Display.pushImage(x, y, width, height,
                         reinterpret_cast<const lgfx::rgb565_t *>(pixels));
}

extern "C" void toppers_m5_draw_liveness(uint32_t seconds)
{
    if ((phase5Width <= 0) || (phase5Height <= 0)) {
        return;
    }
    M5.Display.startWrite();
    M5.Display.fillRect(8, phase5Height - 24, 190, 16, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(10, phase5Height - 22);
    M5.Display.print("alive ");
    M5.Display.print(seconds);
    M5.Display.print("s");
    M5.Display.endWrite();
}

/*
 *  フォント選択。
 *
 *  M5GFX 本体は setFont(&fonts::名前) とポインタで指定するが、スケッチ側は
 *  Arduino が -DARDUINO 付きでコンパイルするためクラスレイアウトがこちらと
 *  一致せず、M5GFX の API を直接呼べない（呼ぶと Print::print が未解決になる。
 *  仮にリンクが通っても基底クラスが違うので静かに壊れる）。
 *  よって境界は C に限り、IDで指定する。
 *
 *  対応表 m5_arduino_fonts.inc は scripts/gen_m5_fonts.py が M5GFX の
 *  lgfx_fonts.hpp から生成する。CJK（U8g2font）は含まない——1つで app
 *  パーティション(3MB)を超えるため（lgfx_efont_ja 8.8MB / lgfx_efont_tw 11.6MB）。
 *  ★この表が参照するフォントはすべてイメージに載る（--gc-sections が効かない）。
 */
#include "m5_arduino_fonts.inc"

extern "C" int32_t toppers_m5_set_font(int32_t font_id)
{
    const int32_t count =
        (int32_t)(sizeof(toppers_m5_font_table) / sizeof(toppers_m5_font_table[0]));

    if ((font_id < 0) || (font_id >= count)) {
        return -1;
    }
    M5.Display.setFont(toppers_m5_font_table[font_id]);
    return 0;
}

extern "C" int32_t toppers_m5_font_count(void)
{
    return (int32_t)(sizeof(toppers_m5_font_table) / sizeof(toppers_m5_font_table[0]));
}

/*
 *  画面を消す（バックライトOFF＋パネルsleep）。
 *  CoreS3のバックライトはAXP2101（PMIC）経由なので、setBrightness(0)で電源を落とす。
 *  PMICはCPUリセットでは戻らないため、この状態は次に明るさを上げるまで保たれる。
 */
extern "C" void toppers_m5_display_off(void)
{
    if ((phase5Width > 0) && (phase5Height > 0)) {
        M5.Display.startWrite();
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.endWrite();
    }
    M5.Display.setBrightness(0);
    M5.Display.sleep();
    phase5AdapterLog("[M5] display off (brightness 0 + panel sleep)\n");
}

/*
 *  内蔵スピーカー（CoreS3 / CoreS3-SE）。
 *
 *  M5.Speaker（M5Unified の Speaker_Class）をそのまま使う。I2S の送信は
 *  m5/audio/m5_i2s_std_tx.c、キューは m5/shim/m5_freertos_abi.c。
 *
 *  ★begin はコア 0（PRC1）のタスクから呼ぶこと。I2S の DMA 割込みを配線する
 *    esp_intr_alloc_intrstatus（esp_shim_intr.c）が PRC1 のタスク文脈専用で、
 *    Speaker の begin の中でそれが呼ばれる。setup() / loop() は PRC1 で動く。
 */
#if defined(CONFIG_IDF_TARGET_ESP32S3)
namespace {

constexpr int32_t kSpeakerUnavailable = -1;	/* この板では使えない／開始に失敗 */
constexpr int32_t kSpeakerBadArgument = -2;
constexpr int32_t kSpeakerWrongCore = -3;		/* PRC1 以外から開始しようとした */

/*  トーンを交互に載せる M5 のチャンネル数。短い効果音が重なっても前の音を
 *  切らないように 4 本を順に使う（M5 は 8 本持つ）。 */
constexpr uint8_t kToneChannels = 4;
uint8_t speakerNextChannel;
int32_t speakerBeginResult;			/* 0 = 未開始、1 = 開始済み、負 = 失敗 */

bool speakerBoardSupported()
{
    const auto board = M5.getBoard();
    return (board == m5::board_t::board_M5StackCoreS3)
        || (board == m5::board_t::board_M5StackCoreS3SE);
}

bool onPrc1()
{
    uint32_t prid;
    __asm__ __volatile__("rsr %0, prid" : "=r"(prid));
    return ((prid >> 13) & 1U) == 0U;
}

}  // namespace

extern "C" int32_t toppers_m5_speaker_begin(void)
{
    if (M5.Speaker.isRunning()) {
        speakerBeginResult = 1;
        return 1;
    }
    if (!speakerBoardSupported()) {
        speakerBeginResult = kSpeakerUnavailable;
        return kSpeakerUnavailable;
    }
    if (!onPrc1()) {
        return kSpeakerWrongCore;			/* 状態は変えない：PRC1 から再試行できる */
    }
    speakerBeginResult = M5.Speaker.begin() ? 1 : kSpeakerUnavailable;
    if (speakerBeginResult == 1) {
        phase5AdapterLog("[M5] speaker started\n");
    }
    else {
        phase5AdapterLog("[M5] speaker failed to start\n");
    }
    return speakerBeginResult;
}

extern "C" int32_t toppers_m5_speaker_ready(void)
{
    if (M5.Speaker.isRunning()) {
        return 1;
    }
    return speakerBeginResult;
}

extern "C" int32_t toppers_m5_speaker_tone(uint32_t frequency_hz,
                                           uint32_t duration_ms,
                                           uint8_t volume)
{
    if ((frequency_hz < 20U) || (frequency_hz > 20000U)
        || (duration_ms == 0U) || (duration_ms > 60000U)) {
        return kSpeakerBadArgument;
    }
    if (!M5.Speaker.isRunning()) {
        const int32_t begun = toppers_m5_speaker_begin();
        if (begun != 1) {
            return begun;
        }
    }
    const uint8_t channel = speakerNextChannel;
    speakerNextChannel = static_cast<uint8_t>((channel + 1U) % kToneChannels);
    M5.Speaker.setChannelVolume(channel, volume);
    /*  非同期：M5 の spk_task が鳴らす。stop_current_sound=true なので、同じ
     *  チャンネルに前の音が残っていれば置き換える。M5 が受け付けなかった
     *  （そのチャンネルに書き込み中の要求がある）ときは 0 を返す。 */
    return M5.Speaker.tone(static_cast<float>(frequency_hz), duration_ms,
                           channel, true) ? 1 : 0;
}

extern "C" void toppers_m5_speaker_stop(void)
{
    if (M5.Speaker.isRunning()) {
        M5.Speaker.stop();
    }
}

/*
 *  全体音量（M5.Speaker.setVolume）。M5Unified の既定は 64 で、ここでは
 *  変えない。振幅は「全体音量の二乗 × トーンの音量の二乗」に比例する
 *  （Speaker_Class::spk_task）。開始前でも設定でき、開始後に効く。
 */
extern "C" int32_t toppers_m5_speaker_set_volume(uint8_t master_volume)
{
    if (!speakerBoardSupported()) {
        return kSpeakerUnavailable;
    }
    M5.Speaker.setVolume(master_volume);
    return 1;
}

/*
 *  AW88298 のレジスタを読む（診断用。耳の代わりにアンプの状態を確かめる）。
 *  I2C 上は 16 ビットのビッグエンディアン。読めなければ -1。
 */
extern "C" int32_t toppers_m5_speaker_amp_reg(uint8_t reg)
{
    if (!speakerBoardSupported()) {
        return -1;
    }
    uint8_t value[2] = { 0, 0 };
    if (!M5.In_I2C.readRegister(0x36, reg, value, 2, 400000)) {
        return -1;
    }
    return (static_cast<int32_t>(value[0]) << 8) | value[1];
}

#else  /* !CONFIG_IDF_TARGET_ESP32S3 */
/*  Speaker_Class is not built for this chip (see CMakeLists.txt): the
 *  bridge exists so that a sketch links everywhere, and says so. */
extern "C" int32_t toppers_m5_speaker_begin(void) { return -1; }
extern "C" int32_t toppers_m5_speaker_ready(void) { return -1; }
extern "C" int32_t toppers_m5_speaker_tone(uint32_t, uint32_t, uint8_t) { return -1; }
extern "C" void toppers_m5_speaker_stop(void) { }
extern "C" int32_t toppers_m5_speaker_set_volume(uint8_t) { return -1; }
extern "C" int32_t toppers_m5_speaker_amp_reg(uint8_t) { return -1; }
#endif /* CONFIG_IDF_TARGET_ESP32S3 */

extern "C" int32_t toppers_m5_board(void) { return phase5Board; }
extern "C" int32_t toppers_m5_display_width(void) { return phase5Width; }
extern "C" int32_t toppers_m5_display_height(void) { return phase5Height; }
extern "C" int32_t toppers_m5_touch_enabled(void) { return phase5TouchEnabled; }
extern "C" int32_t toppers_m5_touch_count(void) { return phase5TouchCount; }
extern "C" int32_t toppers_m5_touch_x(void) { return phase5TouchX; }
extern "C" int32_t toppers_m5_touch_y(void) { return phase5TouchY; }
extern "C" int32_t toppers_m5_imu_enabled(void) { return phase5ImuEnabled; }
extern "C" int32_t toppers_m5_rtc_enabled(void) { return phase5RtcEnabled; }
extern "C" int32_t toppers_m5_power_type(void) { return phase5PowerType; }
extern "C" int32_t toppers_m5_battery_mv(void) { return phase5BatteryMv; }
extern "C" uint32_t toppers_m5_trace_enters(void)
{
    return m5_begin_trace_nenters();
}
extern "C" uint32_t toppers_m5_trace_leaves(void)
{
    return m5_begin_trace_nleaves();
}
