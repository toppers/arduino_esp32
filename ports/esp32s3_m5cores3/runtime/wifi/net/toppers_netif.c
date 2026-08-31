#include <kernel.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"
#include "toppers_netif.h"

static struct netif wifi_netif;
static volatile bool netif_ready;
static bool dhcp_started;

static err_t low_level_output(struct netif *netif, struct pbuf *packet)
{
    static uint8_t frame[1600];
    struct pbuf *part;
    uint16_t length = 0;
    (void)netif;
    for (part = packet; part != NULL; part = part->next) {
        if ((size_t)(length + part->len) > sizeof(frame)) return ERR_BUF;
        memcpy(frame + length, part->payload, part->len);
        length += part->len;
    }
    return esp_wifi_internal_tx(WIFI_IF_STA, frame, length) == ESP_OK
        ? ERR_OK : ERR_IF;
}

static esp_err_t wifi_receive(void *buffer, uint16_t length, void *eb)
{
    struct pbuf *packet = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
    if (packet != NULL) {
        (void)pbuf_take(packet, buffer, length);
        if (tcpip_input(packet, &wifi_netif) != ERR_OK) pbuf_free(packet);
    }
    if (eb != NULL) esp_wifi_internal_free_rx_buffer(eb);
    return ESP_OK;
}

static err_t initialize_netif(struct netif *netif)
{
    uint8_t mac[6];
    (void)esp_read_mac(mac, ESP_MAC_WIFI_STA);
    memcpy(netif->hwaddr, mac, sizeof(mac));
    netif->hwaddr_len = sizeof(mac);
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    netif->name[0] = 'w'; netif->name[1] = 'l';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;
    return ERR_OK;
}

static void tcpip_ready(void *argument)
{
    ip4_addr_t any;
    (void)argument;
    IP4_ADDR(&any, 0, 0, 0, 0);
    (void)netif_add(&wifi_netif, &any, &any, &any, NULL,
                    initialize_netif, tcpip_input);
    netif_set_default(&wifi_netif);
    netif_ready = true;
}

static void link_up(void *argument)
{
    (void)argument;
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_receive);
    netif_set_link_up(&wifi_netif);
    netif_set_up(&wifi_netif);
    (void)dhcp_start(&wifi_netif);
    dhcp_started = true;
}

static void link_down(void *argument)
{
    (void)argument;
    if (dhcp_started) {
        dhcp_release_and_stop(&wifi_netif);
        dhcp_started = false;
    }
    netif_set_down(&wifi_netif);
    netif_set_link_down(&wifi_netif);
    (void)esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
}

void toppers_netif_start(void)
{
    if (!netif_ready) {
        tcpip_init(tcpip_ready, NULL);
        while (!netif_ready) (void)dly_tsk(1000U);
    }
}

void toppers_netif_notify_link(bool up)
{
    if (netif_ready) (void)tcpip_callback(up ? link_up : link_down, NULL);
}

uint32_t toppers_netif_local_ip(void)
{
    return ip4_addr_get_u32(netif_ip4_addr(&wifi_netif));
}
uint32_t toppers_netif_gateway_ip(void)
{
    return ip4_addr_get_u32(netif_ip4_gw(&wifi_netif));
}
uint32_t toppers_netif_subnet_mask(void)
{
    return ip4_addr_get_u32(netif_ip4_netmask(&wifi_netif));
}
