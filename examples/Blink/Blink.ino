#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

extern "C" void target_fput_log(char character);

volatile bool blinkState = false;
volatile uint32_t blinkTransitions = 0;

namespace {

uint32_t loopTicks;

void blinkLog(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

}  // namespace

void setup()
{
    blinkLog("[Blink] serial indicator start\n");
}

void loop()
{
    /*
     * The Arduino bridge calls loop() every millisecond. CoreS3's
     * LED_BUILTIN is a virtual RGB pin that needs an additional driver, so
     * this minimal example uses the serial log as its portable indicator.
     */
    if (++loopTicks >= 1000U) {
        loopTicks = 0;
        blinkState = !blinkState;
        ++blinkTransitions;
        blinkLog(blinkState ? "[Blink] ON\n" : "[Blink] OFF\n");
    }
}
