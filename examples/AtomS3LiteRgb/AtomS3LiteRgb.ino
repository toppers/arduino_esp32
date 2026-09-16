#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

// AtomS3LiteRgb: the on-board RGB LED (WS2812B-2020 on G35) of the
// M5AtomS3 Lite (Tools > FMP3 Runtime > WiFi; the RMT driver behind
// rgbLedWrite() is linked into that runtime only).
//
// Board guard: rgbLedWrite() exists in this port's ESP32-S3 runtime only,
// and only this board has the LED. On every other board this sketch
// compiles to a no-op that says so, instead of failing at the linker with
// an undefined rgbLedWrite (the M5NanoC6 has its own example,
// examples/NanoC6Gpio, for its own LED).
//
// Style as examples/Blink: the log is target_fput_log() char by char; no
// Serial, no delay(), no millis() (the M5Stack core is not linked). loop()
// is called about every 1 ms by the bridge, so pacing is loop-tick counts.

extern "C" void target_fput_log(char character);

namespace {

void rgbLog(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

void logUnsigned(uint32_t value)
{
    char buffer[11];
    int index = 0;
    do {
        buffer[index++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    while (index > 0) {
        target_fput_log(buffer[--index]);
    }
}

}  // namespace

#if defined(ARDUINO_M5STACK_ATOMS3LITE)

// Declared by Arduino.h (esp32-hal-rgb-led.h / esp32-hal-gpio.h) with the
// same C signatures the runtime defines: rgbLedWrite, pinMode,
// digitalWrite. Not re-declared here.

// The LED's data pin. M5Unified's board table (_pin_table_other0:
// board_M5AtomS3Lite -> GPIO_NUM_35). Unlike the M5NanoC6's LED there is no
// power-enable pin to raise.
#define ATOMS3LITE_RGB_DATA_PIN 35

namespace {

uint32_t loopTicks;
uint32_t heartbeats;
uint32_t rgbTicks;
uint32_t rgbStep;           // 0..7: two cycles of red, green, blue, off
const uint32_t rgbSteps = 8;

}  // namespace

void setup()
{
    rgbLog("[AtomS3LiteRgb] start\n");
    rgbLog("[S3-RGB] data pin G35, no power gate on this board\n");
}

void loop()
{
    // red -> green -> blue -> off every 1000 ticks, two full cycles.
    if (rgbStep < rgbSteps && ++rgbTicks >= 1000U) {
        rgbTicks = 0;
        uint8_t r = 0, g = 0, b = 0;
        const char *name = "off";
        switch (rgbStep % 4U) {
        case 0: r = 32; name = "red"; break;
        case 1: g = 32; name = "green"; break;
        case 2: b = 32; name = "blue"; break;
        default: break;
        }
        rgbLog("[S3-RGB] write ");
        rgbLog(name);
        rgbLog("\n");
        rgbLedWrite(ATOMS3LITE_RGB_DATA_PIN, r, g, b);
        ++rgbStep;
    }
    // Heartbeat, so a run that never reaches the LED is still visible.
    if (++loopTicks >= 1000U) {
        loopTicks = 0;
        ++heartbeats;
        rgbLog("[AtomS3LiteRgb] heartbeat ");
        logUnsigned(heartbeats);
        rgbLog("\n");
    }
}

#else  /* not ARDUINO_M5STACK_ATOMS3LITE */

void setup()
{
    rgbLog("[AtomS3LiteRgb] this example targets the M5AtomS3 Lite; nothing to do on this board\n");
}

void loop()
{
}

#endif
