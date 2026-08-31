#include "ToppersFMP3_ArduinoBridge.h"

extern void setup();
extern void loop();

extern "C" void esp_run_init_array(void);
extern "C" int dly_tsk(uint32_t delayTime);
extern "C" int get_pid(int32_t *processorId);
extern "C" int get_tid(int32_t *taskId);
extern "C" void target_fput_log(char character);

extern "C" {
volatile uint32_t toppers_arduino_setup_calls = 0;
volatile uint32_t toppers_arduino_loop_calls = 0;
volatile int32_t toppers_arduino_last_delay_result = 0;
}

extern "C" __attribute__((weak)) void toppers_arduino_runtime_init(void)
{
}

namespace {

void bridgeLog(const char *message)
{
    while (*message != '\0') {
        target_fput_log(*message++);
    }
}

void bridgeLogUnsigned(uint32_t value)
{
    uint32_t divisor = 1U;

    while ((value / divisor) >= 10U) {
        divisor *= 10U;
    }

    do {
        target_fput_log(static_cast<char>('0' + ((value / divisor) % 10U)));
        divisor /= 10U;
    } while (divisor != 0U);
}

}  // namespace

extern "C" void toppers_arduino_task(intptr_t exinf)
{
    int32_t processorId = 0;
    int32_t taskId = 0;

    (void)exinf;

    bridgeLog("[Arduino] task start\n");
    if ((get_tid(&taskId) == 0) && (get_pid(&processorId) == 0)) {
        bridgeLog("[Arduino] task=");
        bridgeLogUnsigned(static_cast<uint32_t>(taskId));
        bridgeLog(" processor=");
        bridgeLogUnsigned(static_cast<uint32_t>(processorId));
        bridgeLog("\n");
    }
    esp_run_init_array();
    toppers_arduino_runtime_init();

    setup();
    ++toppers_arduino_setup_calls;
    bridgeLog("[Arduino] setup complete\n");

    for (;;) {
        loop();
        ++toppers_arduino_loop_calls;
        if ((toppers_arduino_loop_calls % 1000U) == 0U) {
            bridgeLog("[Arduino] loop heartbeat\n");
        }

        /*
         * Give other FMP3 tasks a scheduling point even when loop() returns
         * immediately. FMP3 RELTIM is expressed in microseconds on this port,
         * so 1000 means one millisecond.
         */
        toppers_arduino_last_delay_result = dly_tsk(1000U);
    }
}
