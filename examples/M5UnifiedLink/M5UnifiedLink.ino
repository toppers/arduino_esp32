#include <M5Unified.h>
#include <ToppersFMP3_ArduinoBridge.h>

extern "C" {
volatile uintptr_t phase4_m5unified_address = 0;
volatile uint32_t phase4_m5unified_loop_count = 0;
}

void setup()
{
    /*
     * This sketch deliberately does not call M5.begin(). Taking the address of the
     * global object proves the Arduino sketch ABI reaches the M5Unified object
     * compiled by the FMP3 build without starting hardware initialization.
     */
    phase4_m5unified_address = reinterpret_cast<uintptr_t>(&M5);
}

void loop()
{
    ++phase4_m5unified_loop_count;
}
