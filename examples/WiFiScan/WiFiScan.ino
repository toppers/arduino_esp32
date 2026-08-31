#include <ToppersFMP3_ArduinoBridge.h>
#include <ToppersFMP3_WiFi.h>

volatile int16_t wifiScanResult = ToppersFMP3WiFiClass::ScanFailed;

void setup()
{
    // No SSID or password is needed. Results are also written to the
    // TOPPERS/FMP3 serial log as SSID, RSSI, and channel.
    wifiScanResult = WiFi.scanNetworks();
}

void loop()
{
}
