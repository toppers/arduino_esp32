#include <stdint.h>
#include <ToppersFMP3_M5CoreS3.h>

using toppers::fmp3::m5cores3::LibraryInfo;
using toppers::fmp3::m5cores3::libraryInfo;

/*
 * This port does not bring the M5Stack core's Serial with it: Print, HWCDC
 * and delay() all reach into FreeRTOS, which FMP3 replaces. The kernel's own
 * log port is what every other example writes to, so this one does too.
 */
extern "C" void target_fput_log(char character);

namespace {

void log(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

void logLine(const char *label, const char *value)
{
    log(label);
    log(value);
    log("\n");
}

void report()
{
    const LibraryInfo info = libraryInfo();

    log("\n");
    logLine("", info.name);
    logLine("version: ", info.version);
    logLine("description: ", info.description);
    logLine("FMP3 kernel linked: ", info.kernelLinked ? "yes" : "no");
}

uint32_t loopTicks;

}  // namespace

void setup()
{
    report();
}

void loop()
{
    /*
     * Repeated rather than printed once. CoreS3 talks to the host over the
     * ESP32-S3's own USB, which re-enumerates when the board resets, so a
     * serial monitor is never attached in time to see what setup() wrote.
     * The Arduino bridge calls loop() every millisecond.
     */
    if (++loopTicks >= 5000U) {
        loopTicks = 0;
        report();
    }
}
