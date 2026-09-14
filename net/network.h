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

#ifndef TSUKASA_NET_NETWORK_H
#define TSUKASA_NET_NETWORK_H

#include <stddef.h>
#include <stdint.h>

typedef struct net_mac {
    uint8_t bytes[6];
} net_mac_t;

typedef struct net_ipv4_addr {
    uint8_t bytes[4];
} net_ipv4_addr_t;

typedef struct net_link_info {
    char nic_name[32];
    uint32_t link_up;
    net_mac_t mac;
    net_ipv4_addr_t ip;
    net_ipv4_addr_t gateway;
    net_ipv4_addr_t dns;
} net_link_info_t;

typedef struct net_runtime_stats {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_dropped;
    uint64_t irq_count;
    uint64_t rx_poll_calls;
    uint64_t stack_initialized;
    uint64_t has_ip;
} net_runtime_stats_t;

typedef struct net_tcp_connect_req {
    net_ipv4_addr_t ip;
    uint16_t port;
} net_tcp_connect_req_t;

typedef struct net_udp_send_req {
    net_ipv4_addr_t ip;
    uint16_t src_port;
    uint16_t dst_port;
    const void *buffer;
    uint32_t length;
} net_udp_send_req_t;

/* Pre-scheduler: probe/bind NIC drivers (PCI must be scanned already). */
void network_init(void);

/* Post-scheduler: bring up lwIP + netif. Returns 0 on success, -1 if no NIC. */
int network_initialize_stack(void);

/* Blocking-ish DHCP acquisition (pumps the stack, yields). 0 on lease. */
int network_dhcp_acquire(void);

/* Drive rx + lwIP timers once. Safe to call from any kernel process. */
void network_pump(void);
void network_poll(void);

int network_is_initialized(void);
int network_has_ipv4(void);
int network_get_link_info(net_link_info_t *out);
int network_get_stats(net_runtime_stats_t *out);

int network_dns_lookup(const char *name, net_ipv4_addr_t *out_ip);
int network_ping(const net_ipv4_addr_t *ip, uint32_t timeout_ms);

/* Single-session kernel TCP client (legacy multiplexed syscall surface). */
int network_tcp_connect(const net_tcp_connect_req_t *req);
int network_tcp_send(const void *data, size_t len);
int network_tcp_recv(void *buf, size_t max_len, int wait);
int network_tcp_close(void);

int network_udp_send(const net_udp_send_req_t *req);

#endif /* TSUKASA_NET_NETWORK_H */
