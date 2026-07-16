#ifndef TSUKASA_NIC_NETIF_H
#define TSUKASA_NIC_NETIF_H

#include "lwip/netif.h"

err_t nic_netif_init(struct netif *netif);
void nic_netif_poll(struct netif *netif);

#endif /* TSUKASA_NIC_NETIF_H */
