/*
 * Project Tsukasa — Networking System Call Wrappers and POSIX Helpers
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

#include <sys/net.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>

int net_init(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_INIT);
}

int net_is_init(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_IS_INIT);
}

int net_has_ip(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_HAS_IP);
}

int net_get_link(struct tsukasa_net_link_info *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_LINK, (int64_t)(uintptr_t)out);
}

int net_get_mac(struct tsukasa_net_mac *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_MAC, (int64_t)(uintptr_t)out);
}

int net_get_ip(struct tsukasa_net_ipv4 *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_IP, (int64_t)(uintptr_t)out);
}

int net_get_gateway(struct tsukasa_net_ipv4 *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_GATEWAY, (int64_t)(uintptr_t)out);
}

int net_get_dns(struct tsukasa_net_ipv4 *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_DNS, (int64_t)(uintptr_t)out);
}

int net_get_stats(struct tsukasa_net_stats *out)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_GET_STATS, (int64_t)(uintptr_t)out);
}

int net_dhcp(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_DHCP);
}

int net_dns_lookup(const char *name, struct tsukasa_net_ipv4 *out)
{
    struct tsukasa_net_dns_req req;
    req.name = name;
    req.out_ip = out;
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_DNS_LOOKUP, (int64_t)(uintptr_t)&req);
}

int net_ping(const struct tsukasa_net_ipv4 *ip, uint32_t timeout_ms)
{
    struct tsukasa_net_ping_req req;
    if (!ip)
        return -1;
    req.ip = *ip;
    req.timeout_ms = timeout_ms;
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_PING, (int64_t)(uintptr_t)&req);
}

int net_tcp_connect(const struct tsukasa_net_tcp_connect_req *req)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_TCP_CONNECT, (int64_t)(uintptr_t)req);
}

int net_tcp_send(const void *buffer, size_t len)
{
    return (int)__syscall3(SYS_netcall, SYSTEM_CMD_NET_TCP_SEND, (int64_t)(uintptr_t)buffer, (int64_t)len);
}

int net_tcp_recv(void *buffer, size_t max_len, int wait)
{
    struct tsukasa_net_tcp_recv_req req;
    req.buffer = buffer;
    req.max_len = (uint32_t)max_len;
    req.wait = wait;
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_TCP_RECV, (int64_t)(uintptr_t)&req);
}

int net_tcp_close(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_TCP_CLOSE);
}

int net_udp_send(const struct tsukasa_net_udp_send_req *req)
{
    return (int)__syscall2(SYS_netcall, SYSTEM_CMD_NET_UDP_SEND, (int64_t)(uintptr_t)req);
}

int net_poll(void)
{
    return (int)__syscall1(SYS_netcall, SYSTEM_CMD_NET_POLL);
}

uint16_t htons(uint16_t hostshort)
{
    return (uint16_t)(((hostshort & 0x00FFU) << 8) | ((hostshort & 0xFF00U) >> 8));
}

uint16_t ntohs(uint16_t netshort)
{
    return htons(netshort);
}

uint32_t htonl(uint32_t hostlong)
{
    return ((hostlong & 0x000000FFU) << 24) |
           ((hostlong & 0x0000FF00U) << 8) |
           ((hostlong & 0x00FF0000U) >> 8) |
           ((hostlong & 0xFF000000U) >> 24);
}

uint32_t ntohl(uint32_t netlong)
{
    return htonl(netlong);
}

int inet_aton(const char *cp, struct in_addr *inp)
{
    if (!cp)
        return 0;

    uint32_t parts[4] = {0, 0, 0, 0};
    int part_idx = 0;
    int digits_in_part = 0;

    while (*cp) {
        if (*cp >= '0' && *cp <= '9') {
            parts[part_idx] = parts[part_idx] * 10 + (uint32_t)(*cp - '0');
            if (parts[part_idx] > 255)
                return 0;
            digits_in_part++;
        } else if (*cp == '.') {
            if (digits_in_part == 0)
                return 0;
            part_idx++;
            if (part_idx > 3)
                return 0;
            digits_in_part = 0;
        } else {
            return 0;
        }
        cp++;
    }

    if (part_idx != 3 || digits_in_part == 0)
        return 0;

    if (inp)
        inp->s_addr = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);

    return 1;
}

in_addr_t inet_addr(const char *cp)
{
    struct in_addr addr;
    if (!inet_aton(cp, &addr))
        return INADDR_NONE;
    return addr.s_addr;
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[18];
    const uint8_t *b = (const uint8_t *)&in.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
    return buf;
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent ent;
    static char s_name[64];
    static struct in_addr s_addr;
    static char *s_addr_list[2];
    static char *s_aliases[1];

    if (!name || name[0] == '\0')
        return NULL;

    if (inet_aton(name, &s_addr)) {
        strncpy(s_name, name, sizeof(s_name) - 1);
        s_name[sizeof(s_name) - 1] = '\0';
        s_addr_list[0] = (char *)&s_addr;
        s_addr_list[1] = NULL;
        s_aliases[0] = NULL;
        ent.h_name = s_name;
        ent.h_aliases = s_aliases;
        ent.h_addrtype = AF_INET;
        ent.h_length = sizeof(struct in_addr);
        ent.h_addr_list = s_addr_list;
        return &ent;
    }

    struct tsukasa_net_ipv4 out_ip;
    if (net_dns_lookup(name, &out_ip) != 0)
        return NULL;

    memcpy(&s_addr, out_ip.bytes, 4);
    strncpy(s_name, name, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    s_addr_list[0] = (char *)&s_addr;
    s_addr_list[1] = NULL;
    s_aliases[0] = NULL;
    ent.h_name = s_name;
    ent.h_aliases = s_aliases;
    ent.h_addrtype = AF_INET;
    ent.h_length = sizeof(struct in_addr);
    ent.h_addr_list = s_addr_list;
    return &ent;
}
