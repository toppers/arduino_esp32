#include <ToppersFMP3_ArduinoBridge.h>

// See WiFiConnect for why this guard exists: the wrong runtime otherwise
// fails at the linker, naming Wi-Fi symbols rather than the menu option.
#if defined(TOPPERS_FMP3_RUNTIME_SELECTED) \
    && !defined(TOPPERS_FMP3_RUNTIME_WIFI_CONNECT) \
    && !defined(TOPPERS_FMP3_RUNTIME_ALL_IN_ONE)
#error "WiFiScan needs a runtime with Wi-Fi. Select Tools > FMP3 Runtime > WiFi. The Wi-Fi stack is linked into that runtime only, so any other option leaves this sketch without one."
#endif

#include <ToppersFMP3_WiFi.h>

volatile int16_t wifiScanResult = ToppersFMP3WiFiClass::ScanFailed;

void setup()
{
    // No SSID or password is needed. Results are also written to the
    // TOPPERS/FMP3 serial log with RSSI, channel, and authmode. On boards
    // whose runtime redacts SSIDs from that log (ESP32-C6 as of 2026-09-15),
    // the log line carries a `<SSID-N>` placeholder instead of the SSID
    // text; WiFi.SSID(i) below still returns the real SSID either way.
    wifiScanResult = WiFi.scanNetworks();
}

void loop()
{
}
