/*
 * Project Tsukasa — Tsukasa lwIP configuration (NO_SYS mainloop mode)
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

#ifndef TSUKASA_LWIPOPTS_H
#define TSUKASA_LWIPOPTS_H

/* Freestanding build: no hosted libc headers; cc.h supplies types/format macros. */
#define LWIP_NO_INTTYPES_H          1
#define LWIP_NO_CTYPE_H             1
#define LWIP_NO_UNISTD_H            1

#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

#define LWIP_TIMEVAL_PRIVATE        1
#define LWIP_PROVIDE_ERRNO          1

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_IGMP                   0

#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_LOOPBACK         0

#define MEM_ALIGNMENT               8
#define MEM_SIZE                    (512 * 1024)
#define MEMP_MEM_MALLOC             0
#define MEM_LIBC_MALLOC             0
#define PBUF_POOL_SIZE              64
#define MEMP_NUM_PBUF               64
#define MEMP_NUM_TCP_PCB            16
#define MEMP_NUM_TCP_SEG            64

#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            (4 * (TCP_SND_BUF / TCP_MSS))

#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          0

#endif /* TSUKASA_LWIPOPTS_H */
