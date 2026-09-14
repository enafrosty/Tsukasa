/*
 * Project Tsukasa — BSD socket ABI and prototypes
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

#ifndef TSUKASA_NET_SOCKET_H
#define TSUKASA_NET_SOCKET_H

#include "../socket_defs.h"
#include <stddef.h>
#include <stdint.h>

#define AF_UNIX     TSK_AF_UNIX
#define AF_LOCAL    TSK_AF_UNIX
#define SOCK_STREAM TSK_SOCK_STREAM
#define SOCK_DGRAM  2

typedef unsigned int socklen_t;
typedef uint16_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct sockaddr_un {
    sa_family_t sun_family;
    char        sun_path[TSK_UNIX_PATH_MAX];
};

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int fd, int backlog);
int accept(int fd);
int connect(int fd, const struct sockaddr *addr, socklen_t addrlen);
long send(int fd, const void *buf, size_t len, int flags);
long recv(int fd, void *buf, size_t len, int flags);
long sendto(int fd, const void *buf, size_t len, int flags,
            const struct sockaddr *dest_addr, socklen_t addrlen);
long recvfrom(int fd, void *buf, size_t len, int flags,
              struct sockaddr *src_addr, socklen_t *addrlen);

#endif /* TSUKASA_NET_SOCKET_H */
