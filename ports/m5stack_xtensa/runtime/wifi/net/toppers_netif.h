#ifndef TOPPERS_FMP3_NETIF_H
#define TOPPERS_FMP3_NETIF_H

#include <stdbool.h>
#include <stdint.h>

void toppers_netif_start(void);
void toppers_netif_notify_link(bool up);
uint32_t toppers_netif_local_ip(void);
uint32_t toppers_netif_gateway_ip(void);
uint32_t toppers_netif_subnet_mask(void);

#endif
