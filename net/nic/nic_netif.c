/*
 * nic_netif.c - lwIP netif bridge to unified NIC abstraction.
 */

#include "nic_netif.h"

#include "nic.h"

#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"

static err_t nic_low_level_output(struct netif *netif, struct pbuf *p)
{
    int rc;
    (void)netif;
    if (!p)
        return ERR_ARG;

    if (!p->next) {
        rc = nic_send(p->payload, p->len);
    } else {
        uint8_t frame[2048];
        u16_t copied = pbuf_copy_partial(p, frame, sizeof(frame), 0);
        rc = nic_send(frame, copied);
    }

    if (rc != 0)
        return ERR_IF;
    LINK_STATS_INC(link.xmit);
    return ERR_OK;
}

static void nic_rx_to_lwip(const uint8_t *frame, size_t len, void *ctx)
{
    struct netif *netif = (struct netif *)ctx;
    struct pbuf *p;
    if (!netif || !frame || len == 0)
        return;

    p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!p) {
        LINK_STATS_INC(link.drop);
        return;
    }

    if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
        pbuf_free(p);
        LINK_STATS_INC(link.drop);
        return;
    }

    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
        LINK_STATS_INC(link.drop);
        return;
    }

    LINK_STATS_INC(link.recv);
}

err_t nic_netif_init(struct netif *netif)
{
    if (!netif || !nic_ready())
        return ERR_IF;

    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = nic_low_level_output;
    netif->hwaddr_len = 6;

    if (nic_get_mac(netif->hwaddr) != 0)
        return ERR_IF;

    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
    if (nic_link_up())
        netif->flags |= NETIF_FLAG_LINK_UP;

    nic_set_rx_callback(nic_rx_to_lwip, netif);
    return ERR_OK;
}

void nic_netif_poll(struct netif *netif)
{
    (void)netif;
    (void)nic_poll_rx();
}
