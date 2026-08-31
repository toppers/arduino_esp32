#include <ToppersFMP3_M5CoreS3.h>

using toppers::fmp3::m5cores3::LibraryInfo;
using toppers::fmp3::m5cores3::libraryInfo;

void setup()
{
    Serial.begin(115200);

    const LibraryInfo info = libraryInfo();
    Serial.println();
    Serial.println(info.name);
    Serial.print("version: ");
    Serial.println(info.version);
    Serial.print("description: ");
    Serial.println(info.description);
    Serial.print("FMP3 kernel linked: ");
    Serial.println(info.kernelLinked ? "yes" : "no");
}

void loop()
{
    delay(1000);
}
