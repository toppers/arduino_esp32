#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

// AtomLiteRgb: the on-board RGB LED (SK6812 3535 on G27) of the M5AtomLite
// (ESP32-PICO-D4; Tools > FMP3 Runtime > WiFi; the RMT driver behind
// rgbLedWrite() is linked into that runtime only).
//
// Board guard: only this board has the LED on G27, and rgbLedWrite() exists
// in this port's ESP32 runtime only for its sake. On every other board this
// sketch compiles to a no-op that says so (the M5AtomS3 Lite and the
// M5NanoC6 have their own examples, AtomS3LiteRgb and NanoC6Gpio).
//
// Runtime guard: this board also offers Minimal and Bluetooth Classic (SPP),
// neither of which links the driver. Say so at the #error rather than as an
// undefined reference to rgbLedWrite at the end of the link.
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

#if defined(ARDUINO_M5STACK_ATOM)

#if defined(TOPPERS_FMP3_RUNTIME_SELECTED) \
    && !defined(TOPPERS_FMP3_RUNTIME_WIFI_CONNECT)
#error "AtomLiteRgb needs the WiFi runtime. Select Tools > FMP3 Runtime > WiFi. rgbLedWrite() is linked into that runtime only, so Minimal and Bluetooth Classic (SPP) leave this sketch without it."
#endif

// Declared by Arduino.h (esp32-hal-rgb-led.h / esp32-hal-gpio.h) with the
// same C signatures the runtime defines: rgbLedWrite, pinMode,
// digitalWrite. Not re-declared here.

// The LED's data pin. M5Unified's board table (_pin_table_other0:
// board_M5AtomLite -> GPIO_NUM_27; the M5Stack documentation says the same).
// No power-enable pin on this board.
#define ATOMLITE_RGB_DATA_PIN 27

namespace {

uint32_t loopTicks;
uint32_t heartbeats;
uint32_t rgbTicks;
uint32_t rgbStep;           // 0..7: two cycles of red, green, blue, off
const uint32_t rgbSteps = 8;

}  // namespace

void setup()
{
    rgbLog("[AtomLiteRgb] start\n");
    rgbLog("[LX6-RGB] data pin G27 (SK6812), no power gate on this board\n");
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
        rgbLog("[LX6-RGB] write ");
        rgbLog(name);
        rgbLog("\n");
        rgbLedWrite(ATOMLITE_RGB_DATA_PIN, r, g, b);
        ++rgbStep;
    }
    // Heartbeat, so a run that never reaches the LED is still visible.
    if (++loopTicks >= 1000U) {
        loopTicks = 0;
        ++heartbeats;
        rgbLog("[AtomLiteRgb] heartbeat ");
        logUnsigned(heartbeats);
        rgbLog("\n");
    }
}

#else  /* not ARDUINO_M5STACK_ATOM */

void setup()
{
    rgbLog("[AtomLiteRgb] this example targets the M5AtomLite; nothing to do on this board\n");
}

void loop()
{
}

#endif
