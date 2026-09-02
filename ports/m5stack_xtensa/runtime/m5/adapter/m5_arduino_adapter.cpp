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
    config.internal_spk = false;
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

extern "C" void toppers_m5_update(void)
{
    M5.update();
    phase5TouchCount = M5.Touch.getCount();
    if (phase5TouchCount > 0) {
        const auto &detail = M5.Touch.getDetail(0);
        phase5TouchX = detail.x;
        phase5TouchY = detail.y;
        M5.Display.fillCircle(phase5TouchX, phase5TouchY, 4, TFT_CYAN);
    }
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
