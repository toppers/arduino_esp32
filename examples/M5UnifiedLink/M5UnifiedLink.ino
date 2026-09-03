#include <ToppersFMP3_M5Unified.h>
#include <ToppersFMP3_ArduinoBridge.h>

extern "C" {
volatile uintptr_t phase4_m5unified_address = 0;
volatile uint32_t phase4_m5unified_loop_count = 0;
}

void setup()
{
    /*
     * This sketch deliberately does not call M5.begin(). Taking the address of the
     * global object proves the sketch links against the same M5Unified instance the
     * FMP3 build compiled, without starting hardware initialization.
     *
     * Linking is all it proves. It says nothing about the ABI: the address is the
     * same either way, and the sketch and the prebuilt runtime once disagreed by
     * four bytes on where every member after Display lives. That is what
     * <ToppersFMP3_M5Unified.h> exists to fix - see the comment at its top - and
     * what its static_assert now catches at build time.
     */
    phase4_m5unified_address = reinterpret_cast<uintptr_t>(&M5);
}

void loop()
{
    ++phase4_m5unified_loop_count;
}
