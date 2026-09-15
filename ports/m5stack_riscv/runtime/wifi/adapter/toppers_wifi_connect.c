/*
 * Arduino-facing station connect adapter for ESP32-C6 / ESP32-C5 FMP3.
 *
 * ESP32-C6 (ports/m5stack_riscv) version of
 * ports/m5stack_xtensa/runtime/wifi/adapter/toppers_wifi_connect.c. Two
 * things differ, both decided in docs/c6-port.md:
 *
 *   D6  No __wrap_esp_supplicant_init(): the supplicant is initialized by
 *       esp_wifi_init() (toppers_wifi_core.c). Open AP is unverified on
 *       the C6 until stage 4.
 *   D7  lwIP is the development repository's own build (liblwip.a with
 *       wifi/net/port/include/lwipopts.h) glued by wifi/net/netif_esp32s3.c
 *       and port/sys_arch.c, not the Xtensa port's toppers_netif.c. The
 *       toppers_fmp3_wifi_* API is connected to that netif here.
 *       Since stage 4 Task 0 that liblwip.a is this repository's own build
 *       with LWIP_DNS 1 (wifi/prebuilt/lwip/README.md), and hostByName()
 *       resolves names through lwIP's resolver
 *       (see toppers_fmp3_wifi_host_by_name).
 */
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
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "netif_esp32s3.h"
#include "toppers_wifi_core.h"

/*
 * ESP32-C5 (C5 plan stage 3): the same file. The connect path (set_config
 * -> start -> connect -> netif/DHCP -> DNS -> TCP) is what the dev C5
 * station demo runs at 1d96bcba; nothing here names a chip register.
 * The open-AP NOTICE below still says "ESP32-C6": the D6 status (open AP
 * unverified) is the same on the C5, and the string is kept so the C6
 * object stays byte-identical.
 */
#if !defined(TOPPERS_ESP32C6) && !defined(TOPPERS_ESP32C5)
#error "toppers_wifi_connect.c (ports/m5stack_riscv) is the ESP32-C6 / ESP32-C5 version"
#endif

enum { TOPPERS_WL_IDLE = 0, TOPPERS_WL_NO_SSID = 1,
       TOPPERS_WL_CONNECTED = 3, TOPPERS_WL_CONNECT_FAILED = 4,
       TOPPERS_WL_CONNECTION_LOST = 5, TOPPERS_WL_DISCONNECTED = 6 };

/* lwIP is brought up once, here, because only this profile links it. */
static bool netif_started;
static bool address_logged;
static volatile uint8_t connection_status = TOPPERS_WL_DISCONNECTED;

static void stage_log(const char *message)
{
    syslog(LOG_NOTICE, "%s", message);
    (void)logtask_flush(0U);
}

/*
 * Link notifications must not reach lwIP before tcpip_init() has run.
 * The vendored netif_esp32s3_notify_link() goes straight to
 * tcpip_callback(), whose LWIP_ASSERT("Invalid mbox") on the not yet
 * created tcpip mailbox ends in the port's assert handler (an endless
 * loop). That loop parks the POSTER's context, not a separate "event
 * task": esp_event_shim's esp_event_post() calls registered handlers
 * synchronously, in the caller's own context, and the caller here is the
 * Wi-Fi blob's internal task. The only event that can actually arrive
 * before netif_esp32s3_start() is STA_DISCONNECTED, from the driver
 * cycle a scan-first begin() runs (esp_wifi_disconnect() ahead of the
 * restart, see toppers_fmp3_wifi_begin()); STA_CONNECTED cannot precede
 * it, because begin() only calls esp_wifi_connect() after
 * netif_esp32s3_start() (the start -> netif -> connect order there).
 * The gate below still covers both events - defensive, not just for the
 * one that is currently reachable - and the vendored netif stays
 * untouched.
 */
static void notify_link_if_started(bool up)
{
    if (!netif_started) {
        syslog(LOG_NOTICE,
               "[WiFiConnect] link %s before tcpip start: not forwarded to lwIP",
               up ? "up" : "down");
        return;
    }
    netif_esp32s3_notify_link(up);
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
        /* L2 up -> lwIP link up + DHCP start, in the tcpip_thread context. */
        notify_link_if_started(true);
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
        notify_link_if_started(false);
    }
}

/*
 * Bring-up is shared with the scan adapter; this only adds the event
 * handler, because the two adapters listen for different events (this one
 * for STA_CONNECTED/DISCONNECTED, the scan adapter for SCAN_DONE) and
 * esp_event is happy to call both.
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
    if (password_length == 0U) {
        /*
         * D6: said here, at every open request, rather than only on the
         * first driver init - a scan-first sketch initializes the driver
         * from the scan adapter and would otherwise never see it.
         */
        stage_log("[WiFiConnect] begin: open AP requested - unverified on "
                  "ESP32-C6 (supplicant is initialized regardless; stage 4)");
    }
    syslog(LOG_NOTICE,
           "[WiFiConnect] config authmode=%u password_length=%u",
           (uint_t)config.sta.threshold.authmode, (uint_t)password_length);
    connection_status = TOPPERS_WL_IDLE;
    address_logged = false;
    /*
     * The station config has to be set before the driver is started (the
     * dev C6 demo does set_config, then esp_wifi_start). A scan earlier in
     * the same boot will have started it already, so cycle the driver.
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
        /*
         * After esp_wifi_start(), as in the dev demo: netif_add() reads
         * the station MAC. tcpip_init() starts the NET_TSK (net.cfg) and
         * runs netif_add/netif_set_default in that context.
         */
        stage_log("[WiFiConnect] init: tcpip begin");
        netif_esp32s3_start();
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
    uint32_t address = netif_esp32s3_get_ipaddr();
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

/*
 * The netif glue exposes the address only (netif_esp32s3_get_ipaddr).
 * Gateway and netmask are read from lwIP's default netif, which
 * netif_esp32s3.c registers with netif_set_default() in tcpip_init_done;
 * before that (or before DHCP binds) they read as 0, like the address.
 * Single-word reads, the same polling contract as netif_esp32s3_get_ipaddr.
 */
uint32_t toppers_fmp3_wifi_local_ip(void) { return netif_esp32s3_get_ipaddr(); }

uint32_t toppers_fmp3_wifi_gateway_ip(void)
{
    struct netif *netif = netif_default;
    if (!netif_started || netif == NULL) return 0U;
    return ip4_addr_get_u32(netif_ip4_gw(netif));
}

uint32_t toppers_fmp3_wifi_subnet_mask(void)
{
    struct netif *netif = netif_default;
    if (!netif_started || netif == NULL) return 0U;
    return ip4_addr_get_u32(netif_ip4_netmask(netif));
}

/*
 * Name resolution (stage 4 Task 0; replaces the stage 3 Low#1 stand-in).
 *
 * lwIP's resolver is not thread safe: dns_gethostbyname() must run in the
 * tcpip thread. This runtime uses the raw/callback netif glue with
 * LWIP_TCPIP_CORE_LOCKING 0, so the request is handed to that thread with
 * tcpip_callback() and the answer comes back through the dns_found_callback,
 * also in the tcpip thread. The caller (the sketch task) polls a flag with
 * dly_tsk(), the same pattern as the scan adapter, with a bounded wait of
 * TOPPERS_DNS_TIMEOUT_MS. There is no lwIP semaphore involved: the port's
 * sys_sem pool (NET_SEM1..8) is shared with every netconn, and a resolver
 * must not be the thing that empties it.
 *
 * Ordering: NET_TSK (priority 4) preempts the sketch task (priority 10) and
 * is never preempted by it, so the callback's writes to dns_request are
 * complete before the poller can observe the state change; the single core
 * keeps the volatile stores in program order. A callback that arrives after
 * the caller gave up (timeout) finds a different generation and is ignored;
 * a request that is still pending in lwIP when the next one is issued is
 * likewise stale. Requests are serialized: one at a time, callers from two
 * tasks at once get a failure, not a mixed answer.
 *
 * The DNS server list is filled by the DHCP client (LWIP_DHCP_PROVIDE_DNS_
 * SERVERS, dhcp.c) when the lease is bound; before that, or if the lease
 * carried no option 6, lwIP fails the query at once (ERR_VAL from
 * dns_gethostbyname) and that is reported as such - no fallback server is
 * invented here.
 */
#ifndef TOPPERS_DNS_TIMEOUT_MS
#define TOPPERS_DNS_TIMEOUT_MS 5000U
#endif
#define TOPPERS_DNS_POLL_US 10000U

enum { DNS_REQUEST_IDLE = 0, DNS_REQUEST_WAITING = 1,
       DNS_REQUEST_DONE = 2, DNS_REQUEST_FAILED = 3 };

static struct {
    volatile uint32_t generation;   /* the request the callback belongs to */
    volatile uint8_t state;         /* DNS_REQUEST_* */
    volatile err_t error;           /* lwIP err_t when FAILED */
    ip_addr_t result;               /* valid when DONE */
    char name[DNS_MAX_NAME_LENGTH];
} dns_request;

/* tcpip thread: lwIP's answer, immediate or after the query round trips. */
static void dns_found(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    if ((uint32_t)(uintptr_t)arg != dns_request.generation ||
        dns_request.state != DNS_REQUEST_WAITING)
        return;                     /* stale: the caller already gave up */
    if (ipaddr != NULL && !ip_addr_isany(ipaddr)) {
        ip_addr_copy(dns_request.result, *ipaddr);
        dns_request.state = DNS_REQUEST_DONE;
    }
    else {
        dns_request.error = ERR_VAL;   /* NXDOMAIN or retries exhausted */
        dns_request.state = DNS_REQUEST_FAILED;
    }
}

/* tcpip thread: issue the query (tcpip_callback target). */
static void dns_request_start(void *arg)
{
    ip_addr_t address;
    err_t error;

    if ((uint32_t)(uintptr_t)arg != dns_request.generation ||
        dns_request.state != DNS_REQUEST_WAITING)
        return;
    error = dns_gethostbyname(dns_request.name, &address, dns_found, arg);
    if (error == ERR_OK) {          /* cached, or a numeric name */
        ip_addr_copy(dns_request.result, address);
        dns_request.state = DNS_REQUEST_DONE;
    }
    else if (error != ERR_INPROGRESS) {  /* ERR_VAL: no server / bad name */
        dns_request.error = error;
        dns_request.state = DNS_REQUEST_FAILED;
    }
    /* ERR_INPROGRESS: dns_found() will finish it. */
}

int toppers_fmp3_wifi_host_by_name(const char *host, uint32_t *address)
{
    ip4_addr_t numeric;
    uint32_t generation, waited_ms;
    const char *reason;
    int error;

    if (host == NULL || address == NULL) return 0;
    /* A dotted quad is answered locally, without a query. */
    if (ip4addr_aton(host, &numeric) != 0) {
        *address = ip4_addr_get_u32(&numeric);
        return 1;
    }
    if (!netif_started) {
        /* tcpip_callback() before tcpip_init() asserts on the mailbox. */
        syslog(LOG_WARNING,
               "[WiFiConnect] DNS failed host=%s error=%d (not connected)",
               host, (int_t)ERR_CONN);
        return 0;
    }
    if (strlen(host) >= sizeof(dns_request.name)) {
        syslog(LOG_WARNING,
               "[WiFiConnect] DNS failed host=%s error=%d (name too long)",
               host, (int_t)ERR_ARG);
        return 0;
    }
    if (dns_request.state == DNS_REQUEST_WAITING) {
        syslog(LOG_WARNING,
               "[WiFiConnect] DNS failed host=%s error=%d (request in progress)",
               host, (int_t)ERR_INPROGRESS);
        return 0;
    }
    generation = dns_request.generation + 1U;
    dns_request.generation = generation;
    dns_request.error = ERR_OK;
    strncpy(dns_request.name, host, sizeof(dns_request.name) - 1U);
    dns_request.name[sizeof(dns_request.name) - 1U] = '\0';
    dns_request.state = DNS_REQUEST_WAITING;
    if (tcpip_callback(dns_request_start, (void *)(uintptr_t)generation)
        != ERR_OK) {
        dns_request.state = DNS_REQUEST_IDLE;
        syslog(LOG_WARNING,
               "[WiFiConnect] DNS failed host=%s error=%d (tcpip mailbox)",
               host, (int_t)ERR_MEM);
        return 0;
    }
    for (waited_ms = 0U; waited_ms < TOPPERS_DNS_TIMEOUT_MS;
         waited_ms += TOPPERS_DNS_POLL_US / 1000U) {
        if (dns_request.state != DNS_REQUEST_WAITING) break;
        (void)dly_tsk(TOPPERS_DNS_POLL_US);
    }
    if (dns_request.state == DNS_REQUEST_DONE) {
        *address = ip4_addr_get_u32(ip_2_ip4(&dns_request.result));
        dns_request.state = DNS_REQUEST_IDLE;
        syslog(LOG_NOTICE, "[WiFiConnect] DNS resolved host=%s address=0x%08x",
               host, (uint_t)*address);
        return 1;
    }
    if (dns_request.state == DNS_REQUEST_WAITING) {
        /* Give up; a late dns_found() sees the new generation and drops it. */
        dns_request.generation = generation + 1U;
        reason = "timeout";
        error = ERR_TIMEOUT;
    }
    else {
        reason = "unresolved";
        error = dns_request.error;
    }
    dns_request.state = DNS_REQUEST_IDLE;
    syslog(LOG_WARNING, "[WiFiConnect] DNS failed host=%s error=%d (%s)",
           host, (int_t)error, reason);
    return 0;
}

int toppers_fmp3_wifi_tcp_request(const char *host, uint16_t port,
                                  const char *request, char *response,
                                  uint32_t capacity, uint32_t timeout_ms)
{
    struct sockaddr_in address;
    int socket_fd, received;
    uint32_t resolved;
    /*
     * LWIP_SO_SNDRCVTIMEO_NONSTANDARD is 1 in this liblwip.a's lwipopts.h:
     * SO_RCVTIMEO takes an int in milliseconds, which is what is passed.
     */
    int receive_timeout_ms = (int)timeout_ms;
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
                          &receive_timeout_ms, sizeof(receive_timeout_ms));
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
    return received;
}
