/*
 * Project Tsukasa — Network Subsystem Definitions & Prototypes
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

#ifndef _TSUKASA_SYS_NET_H
#define _TSUKASA_SYS_NET_H

#include <stdint.h>
#include <stddef.h>

#define SYSTEM_CMD_NET_INIT        19
#define SYSTEM_CMD_NET_IS_INIT     20
#define SYSTEM_CMD_NET_HAS_IP      21
#define SYSTEM_CMD_NET_GET_LINK    22
#define SYSTEM_CMD_NET_GET_STATS   23
#define SYSTEM_CMD_NET_DHCP        24
#define SYSTEM_CMD_NET_DNS_LOOKUP  25
#define SYSTEM_CMD_NET_PING        26
#define SYSTEM_CMD_NET_TCP_CONNECT 27
#define SYSTEM_CMD_NET_TCP_SEND    28
#define SYSTEM_CMD_NET_TCP_RECV    29
#define SYSTEM_CMD_NET_TCP_CLOSE   30
#define SYSTEM_CMD_NET_UDP_SEND    31
#define SYSTEM_CMD_NET_POLL        32
#define SYSTEM_CMD_NET_GET_MAC     33
#define SYSTEM_CMD_NET_GET_IP      34
#define SYSTEM_CMD_NET_GET_GATEWAY 35
#define SYSTEM_CMD_NET_GET_DNS     36

struct tsukasa_net_ipv4 {
    uint8_t bytes[4];
};

struct tsukasa_net_mac {
    uint8_t bytes[6];
};

struct tsukasa_net_link_info {
    char nic_name[24];
    struct tsukasa_net_mac mac;
    struct tsukasa_net_ipv4 ip;
    struct tsukasa_net_ipv4 gateway;
    struct tsukasa_net_ipv4 dns;
    uint8_t link_up;
};

struct tsukasa_net_stats {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_dropped;
    uint64_t irq_count;
    uint64_t rx_poll_calls;
    uint8_t stack_initialized;
    uint8_t has_ip;
};

struct tsukasa_net_tcp_connect_req {
    struct tsukasa_net_ipv4 ip;
    uint16_t port;
};

struct tsukasa_net_udp_send_req {
    struct tsukasa_net_ipv4 ip;
    uint16_t src_port;
    uint16_t dst_port;
    const void *buffer;
    uint32_t length;
};

struct tsukasa_net_dns_req {
    const char *name;
    struct tsukasa_net_ipv4 *out_ip;
};

struct tsukasa_net_ping_req {
    struct tsukasa_net_ipv4 ip;
    uint32_t timeout_ms;
};

struct tsukasa_net_tcp_recv_req {
    void *buffer;
    uint32_t max_len;
    int wait;
};

int net_init(void);
int net_is_init(void);
int net_has_ip(void);
int net_get_link(struct tsukasa_net_link_info *out);
int net_get_mac(struct tsukasa_net_mac *out);
int net_get_ip(struct tsukasa_net_ipv4 *out);
int net_get_gateway(struct tsukasa_net_ipv4 *out);
int net_get_dns(struct tsukasa_net_ipv4 *out);
int net_get_stats(struct tsukasa_net_stats *out);
int net_dhcp(void);
int net_dns_lookup(const char *name, struct tsukasa_net_ipv4 *out);
int net_ping(const struct tsukasa_net_ipv4 *ip, uint32_t timeout_ms);
int net_tcp_connect(const struct tsukasa_net_tcp_connect_req *req);
int net_tcp_send(const void *buffer, size_t len);
int net_tcp_recv(void *buffer, size_t max_len, int wait);
int net_tcp_close(void);
int net_udp_send(const struct tsukasa_net_udp_send_req *req);
int net_poll(void);

#endif /* _TSUKASA_SYS_NET_H */
