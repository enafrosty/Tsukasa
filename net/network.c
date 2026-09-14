/*
 * Project Tsukasa — Kernel network stack facade over lwIP + unified NIC layer
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "network.h"

#ifdef __x86_64__

#include "nic/nic.h"
#include "nic/nic_netif.h"
#include "nic/e1000.h"
#include "nic/virtio_net.h"

#include "../include/kprintf.h"
#include "../drv/pit.h"
#include "../proc/process.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/icmp.h"
#include "lwip/prot/ip.h"
#include "lwip/timeouts.h"

#define NET_TICK_HZ            100u
#define DHCP_TIMEOUT_TICKS     (15u * NET_TICK_HZ)
#define TCP_CONNECT_TIMEOUT    (5u * NET_TICK_HZ)
#define TCP_RECV_TIMEOUT       (5u * NET_TICK_HZ)
#define DNS_TIMEOUT_TICKS      (5u * NET_TICK_HZ)
#define TCP_RX_RING_SIZE       8192u
#define PING_ID                0x54554Bu /* "TUK" */

static struct netif g_netif;
static int g_stack_ready;

/* ------------------------------------------------------------------ core */

void network_init(void)
{
    nic_init();
    virtio_net_register_pci_driver();
    e1000_register_pci_driver();

    if (nic_ready()) {
        const nic_device_t *dev = nic_get_active();
        kprintf("[net] nic bound: %s (%s)\n",
                dev->driver_name ? dev->driver_name : "?",
                dev->model_name ? dev->model_name : "?");
    } else {
        kprintf("[net] no supported nic found\n");
    }
}

int network_initialize_stack(void)
{
    ip4_addr_t ip, mask, gw;

    if (g_stack_ready)
        return 0;
    if (!nic_ready())
        return -1;

    lwip_init();

    ip4_addr_set_zero(&ip);
    ip4_addr_set_zero(&mask);
    ip4_addr_set_zero(&gw);

    if (!netif_add(&g_netif, &ip, &mask, &gw, NULL, nic_netif_init, netif_input)) {
        kprintf("[net] netif_add failed\n");
        return -1;
    }

    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    g_stack_ready = 1;
    return 0;
}

void network_pump(void)
{
    if (!g_stack_ready)
        return;
    nic_netif_poll(&g_netif);
    sys_check_timeouts();
}

void network_poll(void)
{
    network_pump();
}

static void net_wait_tick(void)
{
    network_pump();
    process_yield();
}

int network_dhcp_acquire(void)
{
    uint64_t deadline;

    if (!g_stack_ready)
        return -1;

    if (dhcp_start(&g_netif) != ERR_OK)
        return -1;

    deadline = pit_ticks() + DHCP_TIMEOUT_TICKS;
    while (pit_ticks() < deadline) {
        if (dhcp_supplied_address(&g_netif))
            return 0;
        net_wait_tick();
    }
    {
        nic_stats_t st;
        nic_get_stats(&st);
        kprintf("[net] dhcp timeout: tx=%u rx=%u polls=%u drop=%u link=%d\n",
                (uint32_t)st.tx_packets, (uint32_t)st.rx_packets,
                (uint32_t)st.rx_poll_calls, (uint32_t)st.rx_dropped,
                nic_link_up());
        e1000_debug_dump();
    }
    return -1;
}

int network_is_initialized(void)
{
    return g_stack_ready;
}

int network_has_ipv4(void)
{
    if (!g_stack_ready)
        return 0;
    return !ip4_addr_isany_val(*netif_ip4_addr(&g_netif));
}

/* ------------------------------------------------------------------ info */

static void copy_ip4(net_ipv4_addr_t *dst, const ip4_addr_t *src)
{
    uint32_t v = src ? ip4_addr_get_u32(src) : 0;
    dst->bytes[0] = (uint8_t)(v & 0xFF);
    dst->bytes[1] = (uint8_t)((v >> 8) & 0xFF);
    dst->bytes[2] = (uint8_t)((v >> 16) & 0xFF);
    dst->bytes[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void addr_to_ip4(ip4_addr_t *dst, const net_ipv4_addr_t *src)
{
    IP4_ADDR(dst, src->bytes[0], src->bytes[1], src->bytes[2], src->bytes[3]);
}

int network_get_link_info(net_link_info_t *out)
{
    const nic_device_t *dev;
    const ip_addr_t *dns;
    int i;

    if (!out || !nic_ready())
        return -1;

    dev = nic_get_active();
    for (i = 0; i < (int)sizeof(out->nic_name) - 1 && dev->driver_name && dev->driver_name[i]; i++)
        out->nic_name[i] = dev->driver_name[i];
    out->nic_name[i] = '\0';

    out->link_up = nic_link_up() ? 1u : 0u;
    if (nic_get_mac(out->mac.bytes) != 0)
        return -1;

    if (g_stack_ready) {
        copy_ip4(&out->ip, netif_ip4_addr(&g_netif));
        copy_ip4(&out->gateway, netif_ip4_gw(&g_netif));
        dns = dns_getserver(0);
        copy_ip4(&out->dns, dns ? ip_2_ip4(dns) : NULL);
    } else {
        copy_ip4(&out->ip, NULL);
        copy_ip4(&out->gateway, NULL);
        copy_ip4(&out->dns, NULL);
    }
    return 0;
}

int network_get_stats(net_runtime_stats_t *out)
{
    nic_stats_t st;

    if (!out)
        return -1;

    nic_get_stats(&st);
    out->tx_packets = st.tx_packets;
    out->tx_bytes = st.tx_bytes;
    out->rx_packets = st.rx_packets;
    out->rx_bytes = st.rx_bytes;
    out->rx_dropped = st.rx_dropped;
    out->irq_count = st.irq_count;
    out->rx_poll_calls = st.rx_poll_calls;
    out->stack_initialized = (uint64_t)g_stack_ready;
    out->has_ip = (uint64_t)network_has_ipv4();
    return 0;
}

/* ------------------------------------------------------------------- dns */

typedef struct dns_wait {
    volatile int done;
    volatile int ok;
    ip_addr_t addr;
} dns_wait_t;

static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    dns_wait_t *w = (dns_wait_t *)arg;
    (void)name;
    if (ipaddr) {
        w->addr = *ipaddr;
        w->ok = 1;
    }
    w->done = 1;
}

int network_dns_lookup(const char *name, net_ipv4_addr_t *out_ip)
{
    dns_wait_t w;
    err_t err;
    uint64_t deadline;

    if (!name || !out_ip || !g_stack_ready)
        return -1;

    w.done = 0;
    w.ok = 0;

    err = dns_gethostbyname(name, &w.addr, dns_found_cb, &w);
    if (err == ERR_OK) {
        copy_ip4(out_ip, ip_2_ip4(&w.addr));
        return 0;
    }
    if (err != ERR_INPROGRESS)
        return -1;

    deadline = pit_ticks() + DNS_TIMEOUT_TICKS;
    while (pit_ticks() < deadline && !w.done)
        net_wait_tick();

    if (w.done && w.ok) {
        copy_ip4(out_ip, ip_2_ip4(&w.addr));
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ ping */

static volatile int g_ping_reply;

static u8_t ping_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr)
{
    struct icmp_echo_hdr *hdr;
    (void)arg;
    (void)pcb;
    (void)addr;

    if (p->tot_len >= IP_HLEN + sizeof(struct icmp_echo_hdr) &&
        pbuf_remove_header(p, IP_HLEN) == 0) {
        hdr = (struct icmp_echo_hdr *)p->payload;
        if (hdr->type == ICMP_ER && lwip_ntohs(hdr->id) == (PING_ID & 0xFFFF)) {
            g_ping_reply = 1;
            pbuf_free(p);
            return 1;
        }
        pbuf_add_header(p, IP_HLEN);
    }
    return 0;
}

int network_ping(const net_ipv4_addr_t *ip, uint32_t timeout_ms)
{
    struct raw_pcb *pcb;
    struct pbuf *p;
    struct icmp_echo_hdr *hdr;
    ip_addr_t dest;
    ip4_addr_t dest4;
    uint64_t deadline;
    int rc = -1;

    if (!ip || !g_stack_ready)
        return -1;
    if (timeout_ms == 0)
        timeout_ms = 3000;

    pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb)
        return -1;
    raw_recv(pcb, ping_recv_cb, NULL);
    raw_bind(pcb, IP_ADDR_ANY);

    p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 8, PBUF_RAM);
    if (!p) {
        raw_remove(pcb);
        return -1;
    }

    hdr = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(hdr, ICMP_ECHO);
    ICMPH_CODE_SET(hdr, 0);
    hdr->id = lwip_htons(PING_ID & 0xFFFF);
    hdr->seqno = lwip_htons(1);
    hdr->chksum = 0;
    for (int i = 0; i < 8; i++)
        ((uint8_t *)p->payload)[sizeof(struct icmp_echo_hdr) + i] = (uint8_t)i;
    hdr->chksum = inet_chksum(p->payload, p->len);

    addr_to_ip4(&dest4, ip);
    ip_addr_copy_from_ip4(dest, dest4);

    g_ping_reply = 0;
    if (raw_sendto(pcb, p, &dest) == ERR_OK) {
        deadline = pit_ticks() + ((uint64_t)timeout_ms * NET_TICK_HZ) / 1000u;
        while (pit_ticks() < deadline && !g_ping_reply)
            net_wait_tick();
        rc = g_ping_reply ? 0 : -1;
    }

    pbuf_free(p);
    raw_remove(pcb);
    return rc;
}

/* ------------------------------------------------- single-session tcp */

typedef struct tcp_session {
    struct tcp_pcb *pcb;
    volatile int connected;
    volatile int remote_closed;
    volatile int aborted;
    uint8_t ring[TCP_RX_RING_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} tcp_session_t;

static tcp_session_t g_tcp;

static uint32_t tcp_ring_used(void)
{
    return g_tcp.head - g_tcp.tail;
}

static void tcp_ring_push(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (tcp_ring_used() >= TCP_RX_RING_SIZE)
            return;
        g_tcp.ring[g_tcp.head % TCP_RX_RING_SIZE] = data[i];
        g_tcp.head++;
    }
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    (void)arg;
    (void)tpcb;
    if (err == ERR_OK)
        g_tcp.connected = 1;
    return ERR_OK;
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;
    (void)arg;
    (void)err;

    if (!p) {
        g_tcp.remote_closed = 1;
        return ERR_OK;
    }
    for (q = p; q; q = q->next)
        tcp_ring_push((const uint8_t *)q->payload, q->len);
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    g_tcp.pcb = NULL;
    g_tcp.connected = 0;
    g_tcp.aborted = 1;
}

int network_tcp_close(void)
{
    if (g_tcp.pcb) {
        tcp_arg(g_tcp.pcb, NULL);
        tcp_recv(g_tcp.pcb, NULL);
        tcp_err(g_tcp.pcb, NULL);
        if (tcp_close(g_tcp.pcb) != ERR_OK)
            tcp_abort(g_tcp.pcb);
        g_tcp.pcb = NULL;
    }
    g_tcp.connected = 0;
    g_tcp.remote_closed = 0;
    g_tcp.aborted = 0;
    g_tcp.head = 0;
    g_tcp.tail = 0;
    return 0;
}

int network_tcp_connect(const net_tcp_connect_req_t *req)
{
    ip_addr_t dest;
    ip4_addr_t dest4;
    uint64_t deadline;

    if (!req || !g_stack_ready)
        return -1;

    network_tcp_close();

    g_tcp.pcb = tcp_new();
    if (!g_tcp.pcb)
        return -1;

    tcp_arg(g_tcp.pcb, &g_tcp);
    tcp_recv(g_tcp.pcb, tcp_recv_cb);
    tcp_err(g_tcp.pcb, tcp_err_cb);

    addr_to_ip4(&dest4, &req->ip);
    ip_addr_copy_from_ip4(dest, dest4);

    if (tcp_connect(g_tcp.pcb, &dest, req->port, tcp_connected_cb) != ERR_OK) {
        network_tcp_close();
        return -1;
    }

    deadline = pit_ticks() + TCP_CONNECT_TIMEOUT;
    while (pit_ticks() < deadline) {
        if (g_tcp.connected)
            return 0;
        if (g_tcp.aborted)
            return -1;
        net_wait_tick();
    }
    network_tcp_close();
    return -1;
}

int network_tcp_send(const void *data, size_t len)
{
    if (!data || !g_tcp.pcb || !g_tcp.connected)
        return -1;
    if (len == 0)
        return 0;
    if (len > 0xFFFF)
        len = 0xFFFF;

    if (tcp_write(g_tcp.pcb, data, (u16_t)len, TCP_WRITE_FLAG_COPY) != ERR_OK)
        return -1;
    tcp_output(g_tcp.pcb);
    network_pump();
    return (int)len;
}

int network_tcp_recv(void *buf, size_t max_len, int wait)
{
    uint8_t *out = (uint8_t *)buf;
    uint32_t n = 0;
    uint64_t deadline;

    if (!out || max_len == 0)
        return -1;

    deadline = pit_ticks() + TCP_RECV_TIMEOUT;
    for (;;) {
        while (n < max_len && tcp_ring_used() > 0) {
            out[n++] = g_tcp.ring[g_tcp.tail % TCP_RX_RING_SIZE];
            g_tcp.tail++;
        }
        if (n > 0 || !wait)
            return (int)n;
        if (g_tcp.aborted || (g_tcp.remote_closed && tcp_ring_used() == 0))
            return -1;
        if (pit_ticks() >= deadline)
            return 0;
        net_wait_tick();
    }
}

/* ------------------------------------------------------------------- udp */

int network_udp_send(const net_udp_send_req_t *req)
{
    struct udp_pcb *pcb;
    struct pbuf *p;
    ip_addr_t dest;
    ip4_addr_t dest4;
    int rc = -1;

    if (!req || !req->buffer || req->length == 0 || !g_stack_ready)
        return -1;

    pcb = udp_new();
    if (!pcb)
        return -1;

    if (req->src_port != 0 && udp_bind(pcb, IP_ADDR_ANY, req->src_port) != ERR_OK) {
        udp_remove(pcb);
        return -1;
    }

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)req->length, PBUF_RAM);
    if (p) {
        pbuf_take(p, req->buffer, (u16_t)req->length);
        addr_to_ip4(&dest4, &req->ip);
        ip_addr_copy_from_ip4(dest, dest4);
        if (udp_sendto(pcb, p, &dest, req->dst_port) == ERR_OK)
            rc = (int)req->length;
        pbuf_free(p);
    }
    udp_remove(pcb);
    network_pump();
    return rc;
}

#else /* !__x86_64__ */

void network_init(void) {}
int network_initialize_stack(void) { return -1; }
int network_dhcp_acquire(void) { return -1; }
void network_pump(void) {}
void network_poll(void) {}
int network_is_initialized(void) { return 0; }
int network_has_ipv4(void) { return 0; }
int network_get_link_info(net_link_info_t *out) { (void)out; return -1; }
int network_get_stats(net_runtime_stats_t *out) { (void)out; return -1; }
int network_dns_lookup(const char *name, net_ipv4_addr_t *out_ip) { (void)name; (void)out_ip; return -1; }
int network_ping(const net_ipv4_addr_t *ip, uint32_t timeout_ms) { (void)ip; (void)timeout_ms; return -1; }
int network_tcp_connect(const net_tcp_connect_req_t *req) { (void)req; return -1; }
int network_tcp_send(const void *data, size_t len) { (void)data; (void)len; return -1; }
int network_tcp_recv(void *buf, size_t max_len, int wait) { (void)buf; (void)max_len; (void)wait; return -1; }
int network_tcp_close(void) { return 0; }
int network_udp_send(const net_udp_send_req_t *req) { (void)req; return -1; }

#endif /* __x86_64__ */
