#include <ToppersFMP3_ArduinoBridge.h>

extern "C" {
volatile uint32_t phase3_sketch_setup_count = 0;
volatile uint32_t phase3_sketch_loop_count = 0;
}

void setup()
{
    ++phase3_sketch_setup_count;
}

void loop()
{
    ++phase3_sketch_loop_count;
}
