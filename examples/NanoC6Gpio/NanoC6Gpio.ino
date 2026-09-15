#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

// NanoC6Gpio: GPIO API, self-driven attachInterrupt test and the RGB LED
// on the M5NanoC6 (Tools > FMP3 Runtime > WiFi; the GPIO/RMT functions are
// linked into that runtime only).
//
// Board guard: pinMode/digitalWrite/digitalRead/rgbLedWrite exist in this
// port's ESP32-C6 runtime only. On the three Xtensa boards this sketch
// compiles to a no-op that says so, instead of failing at the linker with
// an undefined pinMode.
//
// Style as examples/Blink: the log is target_fput_log() char by char; no
// Serial, no delay(), no millis() (the M5Stack core is not linked). loop()
// is called about every 1 ms by the bridge, so pacing is loop-tick counts.

extern "C" void target_fput_log(char character);

namespace {

void gpioLog(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

}  // namespace

#if defined(ARDUINO_M5STACK_NANO_C6)

// Declared by Arduino.h (esp32-hal-gpio.h / esp32-hal-rgb-led.h) with the
// same C signatures the runtime defines: pinMode, digitalWrite, digitalRead,
// attachInterrupt, detachInterrupt, rgbLedWrite. Not re-declared here.

extern "C" void esp_rom_delay_us(uint32_t us);   // ROM (esp32c6.rom.api.ld)

// Diagnostic counters of the runtime's attachInterrupt
// (ports/m5stack_riscv/runtime/arduino/arduino_interrupt.c), exact types.
extern "C" volatile uint32_t ard_intr_n_dispatch;
extern "C" volatile uint32_t ard_intr_n_call;
extern "C" volatile uint32_t ard_intr_n_orphan;
extern "C" volatile int32_t ard_intr_acre_ercd;

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// G7: the blue LED, wired to nothing else on the NanoC6, so the pulses are
// visible and no external device sees them. G9 (the button) would fight
// the driver when pressed; the Grove/IR/RGB pins may have something
// attached.
#define NANOC6_TEST_PIN 7
// RGB LED: data on G20, power gate on G19 (M5 docs; verified by eye in
// stage 6: with 0 the LED stays dark). Keep 1; 0 exists for the "G19 axis" run.
#define NANOC6_RGB_POWER_ENABLE 1
#define NANOC6_RGB_POWER_PIN 19
#define NANOC6_RGB_DATA_PIN 20

#define PULSES_PER_PHASE 5
#define PULSE_GAP_US 200

namespace {

volatile uint32_t isrCount;

void IRAM_ATTR gpioIsr(void)
{
    ++isrCount;
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

void logSigned(int32_t value)
{
    if (value < 0) {
        target_fput_log('-');
        logUnsigned(static_cast<uint32_t>(-(value + 1)) + 1U);
    } else {
        logUnsigned(static_cast<uint32_t>(value));
    }
}

void logField(const char *name, uint32_t value)
{
    gpioLog(name);
    logUnsigned(value);
}

// LOW -> HIGH -> LOW, PULSES_PER_PHASE times, PULSE_GAP_US apart. G7 is
// left LOW.
void pulseTestPin(void)
{
    for (int i = 0; i < PULSES_PER_PHASE; ++i) {
        digitalWrite(NANOC6_TEST_PIN, HIGH);
        esp_rom_delay_us(PULSE_GAP_US);
        digitalWrite(NANOC6_TEST_PIN, LOW);
        esp_rom_delay_us(PULSE_GAP_US);
    }
}

// One phase: attach (mode >= 0) or leave detached (mode < 0), pulse, count.
uint32_t runPhase(const char *name, int mode, uint32_t want)
{
    isrCount = 0;
    if (mode >= 0) {
        attachInterrupt(NANOC6_TEST_PIN, gpioIsr, mode);
    }
    pulseTestPin();
    if (mode >= 0) {
        detachInterrupt(NANOC6_TEST_PIN);
    }
    uint32_t got = isrCount;
    gpioLog("[C6-INTR] phase=");
    gpioLog(name);
    logField(" got=", got);
    logField(" want=", want);
    gpioLog("\n");
    return got;
}

// 1. Readback on G7: OUTPUT also enables the input buffer, so the pin
//    reads what it drives.
void readbackTest(void)
{
    pinMode(NANOC6_TEST_PIN, OUTPUT);
    digitalWrite(NANOC6_TEST_PIN, HIGH);
    esp_rom_delay_us(10);
    int high = digitalRead(NANOC6_TEST_PIN);
    digitalWrite(NANOC6_TEST_PIN, LOW);
    esp_rom_delay_us(10);
    int low = digitalRead(NANOC6_TEST_PIN);
    if (high == HIGH && low == LOW) {
        gpioLog("[C6-GPIO] readback ok\n");
    } else {
        gpioLog("[C6-GPIO] readback FAIL high=");
        logSigned(high);
        gpioLog(" low=");
        logSigned(low);
        gpioLog("\n");
    }
}

// 2. Self-driven interrupt test on G7. No ONLOW/ONHIGH: a level trigger
//    on a self-driven pin re-enters until detached.
void interruptTest(void)
{
    uint32_t rising = runPhase("RISING", RISING, PULSES_PER_PHASE);
    uint32_t falling = runPhase("FALLING", FALLING, PULSES_PER_PHASE);
    uint32_t change = runPhase("CHANGE", CHANGE, 2 * PULSES_PER_PHASE);
    uint32_t detached = runPhase("DETACHED", -1, 0);
    digitalWrite(NANOC6_TEST_PIN, LOW);

    uint32_t dispatch = ard_intr_n_dispatch;
    uint32_t call = ard_intr_n_call;
    uint32_t orphan = ard_intr_n_orphan;
    int32_t acre = ard_intr_acre_ercd;
    const uint32_t expectedCalls = 4 * PULSES_PER_PHASE;
    bool pass = rising == PULSES_PER_PHASE && falling == PULSES_PER_PHASE
        && change == 2 * PULSES_PER_PHASE && detached == 0
        && dispatch == expectedCalls && call == expectedCalls && orphan == 0
        && acre > 0;

    gpioLog(pass ? "[C6-INTR] VERDICT PASS" : "[C6-INTR] VERDICT FAIL");
    logField(" rising=", rising);
    logField(" falling=", falling);
    logField(" change=", change);
    logField(" detached=", detached);
    logField(" dispatch=", dispatch);
    logField(" call=", call);
    logField(" orphan=", orphan);
    gpioLog(" acre=");
    logSigned(acre);
    gpioLog("\n");
}

uint32_t loopTicks;
uint32_t heartbeats;
uint32_t rgbTicks;
uint32_t rgbStep;           // 0..7: two cycles of red, green, blue, off
const uint32_t rgbSteps = 8;

}  // namespace

void setup()
{
    gpioLog("[NanoC6Gpio] start\n");
    readbackTest();
    interruptTest();

    // 3. RGB LED: power gate, then the colours are cycled from loop().
#if NANOC6_RGB_POWER_ENABLE
    pinMode(NANOC6_RGB_POWER_PIN, OUTPUT);
    digitalWrite(NANOC6_RGB_POWER_PIN, HIGH);
    gpioLog("[C6-RGB] power pin G19 HIGH\n");
#else
    gpioLog("[C6-RGB] power pin G19 left alone\n");
#endif
}

void loop()
{
    // 3. red -> green -> blue -> off every 1000 ticks, two full cycles.
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
        gpioLog("[C6-RGB] write ");
        gpioLog(name);
        gpioLog("\n");
        rgbLedWrite(NANOC6_RGB_DATA_PIN, r, g, b);
        ++rgbStep;
    }
    // 4. Heartbeat.
    if (++loopTicks >= 1000U) {
        loopTicks = 0;
        ++heartbeats;
        gpioLog("[NanoC6Gpio] heartbeat ");
        logUnsigned(heartbeats);
        gpioLog("\n");
    }
}

#else  /* not ARDUINO_M5STACK_NANO_C6 */

void setup()
{
    gpioLog("[NanoC6Gpio] this example targets the M5NanoC6; nothing to do on this board\n");
}

void loop()
{
}

#endif
