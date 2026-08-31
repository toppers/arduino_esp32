#ifndef TOPPERS_FMP3_WIFI_H
#define TOPPERS_FMP3_WIFI_H

#include <stdint.h>

class ToppersFMP3WiFiClass {
public:
    static constexpr uint8_t WL_IDLE_STATUS = 0;
    static constexpr uint8_t WL_NO_SSID_AVAIL = 1;
    static constexpr uint8_t WL_CONNECTED = 3;
    static constexpr uint8_t WL_CONNECT_FAILED = 4;
    static constexpr uint8_t WL_CONNECTION_LOST = 5;
    static constexpr uint8_t WL_DISCONNECTED = 6;
    static constexpr int16_t ScanFailed = -2;
    static constexpr int16_t ScanTimeout = -3;

    uint8_t begin(const char *ssid, const char *password);
    uint8_t status() const;
    void disconnect();
    uint32_t localIP() const;
    uint32_t gatewayIP() const;
    uint32_t subnetMask() const;
    int hostByName(const char *host, uint32_t &address) const;
    int tcpRequest(const char *host, uint16_t port, const char *request,
                   char *response, uint32_t capacity,
                   uint32_t timeoutMs = 5000U) const;

    int16_t scanNetworks();
    const char *SSID(uint8_t index) const;
    int32_t RSSI(uint8_t index) const;
    int32_t channel(uint8_t index) const;
    uint8_t encryptionType(uint8_t index) const;
    void scanDelete();
};

extern ToppersFMP3WiFiClass WiFi;

#endif  // TOPPERS_FMP3_WIFI_H
