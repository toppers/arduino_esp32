#include <ToppersFMP3_ArduinoBridge.h>
#include <ToppersFMP3_WiFi.h>

// Enter local credentials before uploading. Leave WIFI_PASSWORD empty only
// for an open access point. Wi-Fi passphrases must contain 8-63 characters.
char WIFI_SSID[33] = "";
char WIFI_PASSWORD[65] = "";
const char TEST_HOST[] = "example.com";

extern "C" void toppers_fmp3_wifi_log_line(const char *message);

static void logLine(const char *text)
{
    toppers_fmp3_wifi_log_line(text);
}

volatile int wifiConnectResult = -99;
volatile int wifiDnsResult = -99;
volatile int wifiTcpResult = -99;
static uint32_t waitLoops;
static bool testFinished;

void setup()
{
    if (WIFI_SSID[0] == '\0') {
        logLine("[WiFiConnect] Set WIFI_SSID before uploading");
        return;
    }

    wifiConnectResult = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    logLine("[WiFiConnect] WiFi.begin returned");
}

void loop()
{
    if (testFinished || WIFI_SSID[0] == '\0') return;
    if (WiFi.status() != ToppersFMP3WiFiClass::WL_CONNECTED) {
        if (++waitLoops < 30000U) return;
        wifiConnectResult = WiFi.status();
        logLine("[WiFiConnect] connection timeout");
        testFinished = true;
        return;
    }
    wifiConnectResult = ToppersFMP3WiFiClass::WL_CONNECTED;
    logLine("[WiFiConnect] connected and DHCP completed");

    (void)WiFi.localIP();

    uint32_t resolved = 0;
    if (!WiFi.hostByName(TEST_HOST, resolved)) {
        wifiDnsResult = 0;
        logLine("[WiFiConnect] DNS failed");
        testFinished = true;
        return;
    }
    wifiDnsResult = 1;
    logLine("[WiFiConnect] DNS completed");
    (void)resolved;

    char response[256];
    const char request[] =
        "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    int received = WiFi.tcpRequest(TEST_HOST, 80, request,
                                   response, sizeof(response));
    wifiTcpResult = received;
    logLine(received >= 0
        ? "[WiFiConnect] TCP request completed"
        : "[WiFiConnect] TCP request failed");
    testFinished = true;
}
