#include <M5Unified.h>
#include <ToppersFMP3_M5UnifiedBridge.h>

extern "C" int64_t esp_shim_time_us(void);
extern "C" void target_fput_log(char character);

// The block below is self-test instrumentation, not something a sketch needs.
// The runtime's monitor task reads these to check the 60-second M5Unified
// integration; the runtime defines them weakly, so your own sketch can simply
// leave them out and the monitor will report the self-test as skipped.
extern "C" {
volatile uintptr_t phase5_m5unified_address;
volatile int32_t phase5_begin_result;
volatile int32_t phase5_board;
volatile int32_t phase5_display_width;
volatile int32_t phase5_display_height;
volatile int32_t phase5_touch_enabled;
volatile int32_t phase5_imu_enabled;
volatile int32_t phase5_rtc_enabled;
volatile int32_t phase5_power_type;
volatile int32_t phase5_battery_mv;
volatile uint32_t phase5_updates;
volatile uint32_t phase5_touch_events;
volatile int32_t phase5_touch_x;
volatile int32_t phase5_touch_y;
volatile uint32_t phase5_liveness_seconds;
volatile uint32_t phase5_trace_enters;
volatile uint32_t phase5_trace_leaves;
}

namespace {

int64_t phase5_started_us;
int64_t phase5_next_update_us;
uint32_t phase5_last_drawn_second;

void phase5Log(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

void phase5LogSigned(int32_t value)
{
    uint32_t magnitude;
    uint32_t divisor = 1U;

    if (value < 0) {
        target_fput_log('-');
        magnitude = static_cast<uint32_t>(-(static_cast<int64_t>(value)));
    }
    else {
        magnitude = static_cast<uint32_t>(value);
    }
    while ((magnitude / divisor) >= 10U) {
        divisor *= 10U;
    }
    do {
        target_fput_log(
            static_cast<char>('0' + ((magnitude / divisor) % 10U)));
        divisor /= 10U;
    } while (divisor != 0U);
}

}  // namespace

void setup()
{
    phase5_m5unified_address = reinterpret_cast<uintptr_t>(&M5);
    phase5Log("[M5] Arduino M5Unified adapter start\n");

    phase5_begin_result = toppers_m5_begin();
    phase5_board = toppers_m5_board();
    phase5_display_width = toppers_m5_display_width();
    phase5_display_height = toppers_m5_display_height();
    phase5_touch_enabled = toppers_m5_touch_enabled();
    phase5_imu_enabled = toppers_m5_imu_enabled();
    phase5_rtc_enabled = toppers_m5_rtc_enabled();
    phase5_power_type = toppers_m5_power_type();
    phase5_battery_mv = toppers_m5_battery_mv();
    phase5_trace_enters = toppers_m5_trace_enters();
    phase5_trace_leaves = toppers_m5_trace_leaves();

    phase5_started_us = esp_shim_time_us();
    phase5_next_update_us = phase5_started_us;
    phase5_last_drawn_second = 0;

    if (phase5_begin_result > 0) {
        toppers_m5_draw_liveness(0);
        phase5Log("[M5] M5.begin and initial LCD draw PASS\n");
    }
    else {
        phase5Log("[M5] M5.begin or initial LCD draw FAILED\n");
    }
}

void loop()
{
    const int64_t now = esp_shim_time_us();

    if ((phase5_begin_result > 0) && (now >= phase5_next_update_us)) {
        phase5_next_update_us = now + 100000;
        toppers_m5_update();
        ++phase5_updates;

        const int32_t touchCount = toppers_m5_touch_count();
        if (touchCount > 0) {
            phase5_touch_x = toppers_m5_touch_x();
            phase5_touch_y = toppers_m5_touch_y();
            if (phase5_touch_events == 0U) {
                phase5Log("[M5] first touch x=");
                phase5LogSigned(phase5_touch_x);
                phase5Log(" y=");
                phase5LogSigned(phase5_touch_y);
                phase5Log("\n");
            }
            ++phase5_touch_events;
        }

        phase5_trace_enters = toppers_m5_trace_enters();
        phase5_trace_leaves = toppers_m5_trace_leaves();
    }

    if (phase5_begin_result > 0) {
        const uint32_t seconds =
            static_cast<uint32_t>((now - phase5_started_us) / 1000000);
        phase5_liveness_seconds = seconds;
        if ((seconds != phase5_last_drawn_second) && (seconds <= 60U)) {
            phase5_last_drawn_second = seconds;
            toppers_m5_draw_liveness(seconds);
        }
    }
}
