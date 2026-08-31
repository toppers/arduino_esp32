#include <stdint.h>
#include <ToppersFMP3_ArduinoBridge.h>

extern "C" int get_pid(int32_t *processorId);

// The three counters below are self-test instrumentation, not something a
// sketch needs. The runtime's monitor task reads them to check that the
// Arduino task really stays on PRC1; the runtime defines them weakly, so your
// own sketch can leave them out and the monitor will skip those checks.
extern "C" {
volatile uint32_t phase6_sketch_setup_count;
volatile uint32_t phase6_sketch_loop_count;
volatile int32_t phase6_arduino_processor;
}

void setup()
{
    int32_t processorId = 0;

    if (get_pid(&processorId) == 0) {
        phase6_arduino_processor = processorId;
    }
    ++phase6_sketch_setup_count;
}

void loop()
{
    ++phase6_sketch_loop_count;
}
