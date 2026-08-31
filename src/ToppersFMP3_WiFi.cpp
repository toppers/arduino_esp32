#include "ToppersFMP3_WiFi.h"

extern "C" {
int16_t toppers_fmp3_wifi_scan_networks(void);
const char *toppers_fmp3_wifi_ssid(uint8_t index);
int32_t toppers_fmp3_wifi_rssi(uint8_t index);
int32_t toppers_fmp3_wifi_channel(uint8_t index);
uint8_t toppers_fmp3_wifi_auth_mode(uint8_t index);
void toppers_fmp3_wifi_scan_delete(void);
uint8_t toppers_fmp3_wifi_begin(const char *ssid, const char *password);
uint8_t toppers_fmp3_wifi_status(void);
void toppers_fmp3_wifi_disconnect(void);
uint32_t toppers_fmp3_wifi_local_ip(void);
uint32_t toppers_fmp3_wifi_gateway_ip(void);
uint32_t toppers_fmp3_wifi_subnet_mask(void);
int toppers_fmp3_wifi_host_by_name(const char *host, uint32_t *address);
int toppers_fmp3_wifi_tcp_request(const char *host, uint16_t port,
                                  const char *request, char *response,
                                  uint32_t capacity, uint32_t timeout_ms);
}

ToppersFMP3WiFiClass WiFi;

uint8_t ToppersFMP3WiFiClass::begin(const char *ssid, const char *password)
{
    return toppers_fmp3_wifi_begin(ssid, password);
}

uint8_t ToppersFMP3WiFiClass::status() const
{
    return toppers_fmp3_wifi_status();
}

void ToppersFMP3WiFiClass::disconnect()
{
    toppers_fmp3_wifi_disconnect();
}

uint32_t ToppersFMP3WiFiClass::localIP() const { return toppers_fmp3_wifi_local_ip(); }
uint32_t ToppersFMP3WiFiClass::gatewayIP() const { return toppers_fmp3_wifi_gateway_ip(); }
uint32_t ToppersFMP3WiFiClass::subnetMask() const { return toppers_fmp3_wifi_subnet_mask(); }

int ToppersFMP3WiFiClass::hostByName(const char *host, uint32_t &address) const
{
    return toppers_fmp3_wifi_host_by_name(host, &address);
}

int ToppersFMP3WiFiClass::tcpRequest(const char *host, uint16_t port,
    const char *request, char *response, uint32_t capacity,
    uint32_t timeoutMs) const
{
    return toppers_fmp3_wifi_tcp_request(host, port, request, response,
                                         capacity, timeoutMs);
}

int16_t ToppersFMP3WiFiClass::scanNetworks()
{
    return toppers_fmp3_wifi_scan_networks();
}

const char *ToppersFMP3WiFiClass::SSID(uint8_t index) const
{
    return toppers_fmp3_wifi_ssid(index);
}

int32_t ToppersFMP3WiFiClass::RSSI(uint8_t index) const
{
    return toppers_fmp3_wifi_rssi(index);
}

int32_t ToppersFMP3WiFiClass::channel(uint8_t index) const
{
    return toppers_fmp3_wifi_channel(index);
}

uint8_t ToppersFMP3WiFiClass::encryptionType(uint8_t index) const
{
    return toppers_fmp3_wifi_auth_mode(index);
}

void ToppersFMP3WiFiClass::scanDelete()
{
    toppers_fmp3_wifi_scan_delete();
}
