#include <kernel.h>
#include <t_syslog.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "syssvc/logtask.h"

#include "esp_event.h"
#include "esp_shim.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "toppers_netif.h"
#include "toppers_wifi_core.h"

enum { TOPPERS_WL_IDLE = 0, TOPPERS_WL_NO_SSID = 1,
       TOPPERS_WL_CONNECTED = 3, TOPPERS_WL_CONNECT_FAILED = 4,
       TOPPERS_WL_CONNECTION_LOST = 5, TOPPERS_WL_DISCONNECTED = 6 };


/*
 * esp_wifi_init() calls esp_supplicant_init() internally. Wrapping that to a
 * no-op is what allows the auth backend to be chosen after the password is
 * known: toppers_wifi_core.c calls __real_esp_supplicant_init() only for a
 * protected network. Initializing the full supplicant unconditionally makes an
 * open AP fail with AUTH_EXPIRE, which is the failure this indirection exists
 * to avoid.
 *
 * The WPA callback table, the backend selection and the whole
 * driver bring-up into toppers_wifi_core.c, so the scan adapter shares them
 * rather than keeping a second copy.
 */
int __wrap_esp_supplicant_init(void)
{
    return ESP_OK;
}

/* lwIP is brought up once, here, because only this profile links it. */
static bool netif_started;
static bool address_logged;
static volatile uint8_t connection_status = TOPPERS_WL_DISCONNECTED;

static void stage_log(const char *message)
{
    syslog(LOG_NOTICE, "%s", message);
    (void)logtask_flush(0U);
}
static const char *disconnect_reason_name(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_TIMEOUT: return "TIMEOUT";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    case WIFI_REASON_CONNECTION_FAIL: return "CONNECTION_FAIL";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
    default: return "OTHER";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *event =
            (const wifi_event_sta_connected_t *)data;
        if (event != NULL) {
            syslog(LOG_NOTICE,
                   "[WiFiConnect] connected authmode=%u channel=%u",
                   (uint_t)event->authmode, (uint_t)event->channel);
        }
        stage_log("[WiFiConnect] event: station connected");
        connection_status = TOPPERS_WL_IDLE;
        toppers_netif_notify_link(true);
    }
    else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event =
            (const wifi_event_sta_disconnected_t *)data;
        if (event != NULL) {
            syslog(LOG_NOTICE,
                   "[WiFiConnect] disconnected reason=%u (%s) rssi=%d",
                   (uint_t)event->reason,
                   disconnect_reason_name(event->reason),
                   (int_t)event->rssi);
        }
        else {
            stage_log("[WiFiConnect] disconnected without event data");
        }
        stage_log("[WiFiConnect] event: station disconnected");
        if (connection_status != TOPPERS_WL_CONNECT_FAILED)
            connection_status = TOPPERS_WL_CONNECTION_LOST;
        toppers_netif_notify_link(false);
    }
}

/*
 * Bring-up is shared with the scan adapter now; this only adds the event
 * handler, because the two adapters listen for different events (this one for
 * STA_CONNECTED/DISCONNECTED, the scan adapter for SCAN_DONE) and esp_event
 * is happy to call both.
 */
static int initialize_wifi(bool protected_auth)
{
    return toppers_wifi_core_init(protected_auth, "[WiFiConnect]",
                                  wifi_event_handler);
}

uint8_t toppers_fmp3_wifi_begin(const char *ssid, const char *password)
{
    wifi_config_t config;
    esp_err_t error;
    size_t password_length = password != NULL ? strlen(password) : 0U;
    if (ssid == NULL || ssid[0] == '\0' ||
        (password_length > 0U && password_length < 8U) ||
        password_length > 63U ||
        initialize_wifi(password_length > 0U) != 0) {
        if (password_length > 0U && password_length < 8U)
            stage_log("[WiFiConnect] WiFi password must be at least 8 characters");
        else if (password_length > 63U)
            stage_log("[WiFiConnect] WiFi password must be at most 63 characters");
        connection_status = TOPPERS_WL_CONNECT_FAILED;
        stage_log("[WiFiConnect] begin: rejected or initialization failed");
        return connection_status;
    }
    memset(&config, 0, sizeof(config));
    strncpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid) - 1U);
    if (password != NULL)
        strncpy((char *)config.sta.password, password,
                sizeof(config.sta.password) - 1U);
    config.sta.threshold.authmode = password_length == 0U
        ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    syslog(LOG_NOTICE,
           "[WiFiConnect] config authmode=%u password_length=%u",
           (uint_t)config.sta.threshold.authmode, (uint_t)password_length);
    connection_status = TOPPERS_WL_IDLE;
    address_logged = false;
    /*
     * The station config has to be set before the driver is started. A scan
     * earlier in the same boot will have started it already (the scan adapter
     * cannot scan otherwise), and configuring a started driver produced
     * AUTH_EXPIRE on association - the same symptom as an always-initialized
     * supplicant, which makes it easy to misdiagnose. Cycle the driver so the
     * documented order holds either way.
     */
    if (toppers_wifi_core_started()) {
        (void)esp_wifi_disconnect();
        if (toppers_wifi_core_stop("[WiFiConnect]") != 0) {
            stage_log("[WiFiConnect] begin: could not restart the driver");
            connection_status = TOPPERS_WL_CONNECT_FAILED;
            return connection_status;
        }
    }
    stage_log("[WiFiConnect] begin: set_config begin");
    error = esp_wifi_set_config(WIFI_IF_STA, &config);
    syslog(LOG_NOTICE, "[WiFiConnect] esp_wifi_set_config=%d", (int_t)error);
    if (error != ESP_OK) {
        stage_log("[WiFiConnect] begin: set_config FAILED");
        connection_status = TOPPERS_WL_CONNECT_FAILED;
        return connection_status;
    }

    stage_log("[WiFiConnect] begin: set_config OK");
    error = (esp_err_t)toppers_wifi_core_start("[WiFiConnect]");
    if (error != ESP_OK) {
        connection_status = TOPPERS_WL_CONNECT_FAILED;
        return connection_status;
    }
    if (!netif_started) {
        stage_log("[WiFiConnect] init: tcpip begin");
        toppers_netif_start();
        stage_log("[WiFiConnect] init: tcpip OK");
        netif_started = true;
    }
    stage_log("[WiFiConnect] begin: connect request begin");
    error = esp_wifi_connect();
    syslog(LOG_NOTICE, "[WiFiConnect] esp_wifi_connect=%d", (int_t)error);
    if (error != ESP_OK) {
        stage_log("[WiFiConnect] begin: connect request FAILED");
        connection_status = TOPPERS_WL_CONNECT_FAILED;
    }
    else {
        stage_log("[WiFiConnect] begin: connect request accepted");
    }
    return connection_status;
}

uint8_t toppers_fmp3_wifi_status(void)
{
    uint32_t address = toppers_netif_local_ip();
    if (connection_status == TOPPERS_WL_IDLE && address != 0U) {
        connection_status = TOPPERS_WL_CONNECTED;
        if (!address_logged) {
            syslog(LOG_NOTICE, "[WiFiConnect] DHCP address=0x%08x",
                   (uint_t)address);
            stage_log("[WiFiConnect] DHCP completed");
            address_logged = true;
        }
    }
    return connection_status;
}

void toppers_fmp3_wifi_log_line(const char *message)
{
    if (message != NULL) stage_log(message);
}

void toppers_fmp3_wifi_disconnect(void)
{
    /*
     * Only meaningful once the driver is started; esp_wifi_disconnect() before
     * that returns an error and the status update below is what the caller
     * actually observes.
     */
    if (toppers_wifi_core_started()) (void)esp_wifi_disconnect();
    connection_status = TOPPERS_WL_DISCONNECTED;
}

uint32_t toppers_fmp3_wifi_local_ip(void) { return toppers_netif_local_ip(); }
uint32_t toppers_fmp3_wifi_gateway_ip(void) { return toppers_netif_gateway_ip(); }
uint32_t toppers_fmp3_wifi_subnet_mask(void) { return toppers_netif_subnet_mask(); }

int toppers_fmp3_wifi_host_by_name(const char *host, uint32_t *address)
{
    struct addrinfo hints, *result = NULL;
    int error;
    if (host == NULL || address == NULL) return 0;
    memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET;
    error = lwip_getaddrinfo(host, NULL, &hints, &result);
    if (error != 0 || result == NULL) {
        syslog(LOG_WARNING, "[WiFiConnect] DNS failed host=%s error=%d",
               host, (int_t)error);
        return 0;
    }
    *address = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
    lwip_freeaddrinfo(result);
    syslog(LOG_NOTICE, "[WiFiConnect] DNS resolved host=%s", host);
    return 1;
}

int toppers_fmp3_wifi_tcp_request(const char *host, uint16_t port,
                                  const char *request, char *response,
                                  uint32_t capacity, uint32_t timeout_ms)
{
    struct addrinfo hints, *result = NULL;
    struct sockaddr_in address;
    int socket_fd, received;
    uint32_t resolved;
    if (!toppers_fmp3_wifi_host_by_name(host, &resolved)) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = lwip_htons(port);
    address.sin_addr.s_addr = resolved;
    socket_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        syslog(LOG_WARNING, "[WiFiConnect] socket creation failed");
        return -2;
    }
    (void)lwip_setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                          &timeout_ms, sizeof(timeout_ms));
    if (lwip_connect(socket_fd, (struct sockaddr *)&address,
                     sizeof(address)) != 0) {
        syslog(LOG_WARNING, "[WiFiConnect] TCP connect failed host=%s port=%d",
               host, (int_t)port);
        lwip_close(socket_fd); return -3;
    }
    if (request != NULL && lwip_send(socket_fd, request, strlen(request), 0) < 0) {
        syslog(LOG_WARNING, "[WiFiConnect] TCP send failed");
        lwip_close(socket_fd); return -4;
    }
    received = (response != NULL && capacity > 1U)
        ? lwip_recv(socket_fd, response, capacity - 1U, 0) : 0;
    if (received >= 0 && response != NULL) response[received] = '\0';
    lwip_close(socket_fd);
    syslog(received >= 0 ? LOG_NOTICE : LOG_WARNING,
           "[WiFiConnect] TCP received=%d", (int_t)received);
    (void)hints; (void)result;
    return received;
}
