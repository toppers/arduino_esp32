/*
 *  GpioInterrupt: self-driven attachInterrupt test through the Arduino
 *  GPIO API (pinMode / digitalWrite / digitalRead), for every board of
 *  this package. One pin is made OUTPUT (the input buffer stays enabled,
 *  as with the M5Stack core's OUTPUT), pulses are generated on it by the
 *  sketch itself, and the interrupt attached to the same pin is counted:
 *  no wiring, a fixed expected count per phase, and a detach phase as
 *  the negative control. Prints one grep-friendly VERDICT line.
 *
 *  Pins (all free Grove pins, the same the dev repo's T3 probe used):
 *    M5NanoC6      G7  (the blue LED, so the pulses are visible)
 *    M5Stack CoreS3 G8 (Grove Port B p1)
 *    M5Stack Basic  G16 (Grove Port C p1; G8-11 are the flash)
 *    M5StickS3      G9  (Grove port SDA; M5Unified's Ex_I2C SDA for this
 *                       board)
 *    M5StampC5     G1  (a free pad, Grove-class pin; the blue LED G28 is
 *                       BOOT and is not used as a test pin: C5 plan A10.
 *                       Link only until C5 plan stage 4 runs it)
 *    other boards   no pin assigned: the sketch links and reports it
 *  Runs in every profile that offers attachInterrupt (m5 / wificonnect /
 *  btclassic); minimal has neither attachInterrupt nor pinMode.
 *  delay()/Serial are not linked in this package; loop() is called about
 *  every millisecond and esp_rom_delay_us() (ROM) paces the pulses.
 */
#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

extern "C" void target_fput_log(char character);
extern "C" void esp_rom_delay_us(uint32_t us);
extern "C" volatile uint32_t ard_intr_n_dispatch;
extern "C" volatile uint32_t ard_intr_n_call;
extern "C" volatile uint32_t ard_intr_n_orphan;
extern "C" volatile int32_t ard_intr_acre_ercd;

#if defined(ARDUINO_M5STACK_NANO_C6)
#define PROBE_PIN 7
#elif defined(ARDUINO_M5STACK_CORES3)
#define PROBE_PIN 8
#elif defined(ARDUINO_M5STACK_CORE)
#define PROBE_PIN 16
#elif defined(ARDUINO_M5STACK_STICKS3)
#define PROBE_PIN 9
#elif defined(ARDUINO_M5STACK_STAMP_C5)
#define PROBE_PIN 1
#elif defined(ARDUINO_M5STACK_ATOMS3LITE)
/*  M5AtomS3Lite: G7 is one of the six pins on the bottom header
 *  (G5/G6/G7/G8/G38/G39) and is not wired to anything on the board. G38 is
 *  avoided because M5Unified reads it while detecting the board. */
#define PROBE_PIN 7
#else
#define PROBE_PIN -1   /* unknown boards: link only */
#endif

namespace {

volatile uint32_t isrCount;
void probeIsr(void) { ++isrCount; }

void logStr(const char *s) { while (*s != '\0') target_fput_log(*s++); }
void logU(uint32_t v)
{
    /* static, not a stack array: the SDK compiles sketches with
     * -fstack-protector, and a stack char array pulls __stack_chk_fail ->
     * _exit; packages before 2026-09-15 lacked _exit in the ESP32's m5
     * profile, and keeping this example buildable there costs nothing. */
    static char b[12];
    int i = 11; b[i] = '\0';
    do { b[--i] = static_cast<char>('0' + v % 10U); v /= 10U; } while (v != 0U);
    logStr(&b[i]);
}

#if PROBE_PIN >= 0
void pulse(void)
{
    digitalWrite(PROBE_PIN, LOW);  esp_rom_delay_us(200);
    digitalWrite(PROBE_PIN, HIGH); esp_rom_delay_us(200);
    digitalWrite(PROBE_PIN, LOW);  esp_rom_delay_us(200);
}

uint32_t phase(const char *name, int mode, uint32_t want)
{
    isrCount = 0;
    if (mode != 0) attachInterrupt(PROBE_PIN, probeIsr, mode);
    for (int i = 0; i < 5; i++) pulse();
    esp_rom_delay_us(2000);
    if (mode != 0) detachInterrupt(PROBE_PIN);
    uint32_t got = isrCount;
    logStr("[GPIO-INTR] phase="); logStr(name); logStr(" got="); logU(got);
    logStr(" want="); logU(want); logStr("\n");
    return got;
}

void runTest(void)
{
    pinMode(PROBE_PIN, OUTPUT);
    digitalWrite(PROBE_PIN, HIGH);
    int hi = digitalRead(PROBE_PIN);
    digitalWrite(PROBE_PIN, LOW);
    int lo = digitalRead(PROBE_PIN);
    logStr(hi == HIGH && lo == LOW ? "[GPIO-INTR] readback ok\n" : "[GPIO-INTR] readback FAIL\n");
    uint32_t r = phase("RISING", RISING, 5);
    uint32_t f = phase("FALLING", FALLING, 5);
    uint32_t c = phase("CHANGE", CHANGE, 10);
    uint32_t d = phase("DETACHED", 0, 0);
    bool pass = r == 5 && f == 5 && c == 10 && d == 0
             && ard_intr_n_dispatch == 20 && ard_intr_n_call == 20 && ard_intr_n_orphan == 0
             && ard_intr_acre_ercd > 0 && hi == HIGH && lo == LOW;
    logStr(pass ? "[GPIO-INTR] VERDICT PASS" : "[GPIO-INTR] VERDICT FAIL");
    logStr(" pin="); logU(PROBE_PIN);
    logStr(" rising="); logU(r); logStr(" falling="); logU(f); logStr(" change="); logU(c);
    logStr(" detached="); logU(d); logStr(" dispatch="); logU(ard_intr_n_dispatch);
    logStr(" call="); logU(ard_intr_n_call); logStr(" orphan="); logU(ard_intr_n_orphan);
    logStr(" acre="); logU(static_cast<uint32_t>(ard_intr_acre_ercd)); logStr("\n");
}
#endif

uint32_t ticks, heartbeats;
bool done;

}  // namespace

void setup()
{
    logStr("[GpioInterrupt] start\n");
#if PROBE_PIN < 0
    logStr("[GPIO-INTR] no probe pin assigned for this board; nothing to test\n");
    done = true;
#endif
}

void loop()
{
    ++ticks;
#if PROBE_PIN >= 0
    if (!done && ticks == 3000U) {   /* give a serial capture 3 s to attach */
        done = true;
        runTest();
    }
#endif
    if (ticks % 1000U == 0U) {
        ++heartbeats;
        logStr("[GpioInterrupt] heartbeat "); logU(heartbeats); logStr("\n");
    }
}
