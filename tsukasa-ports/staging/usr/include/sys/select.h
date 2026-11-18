/*
 * Project Tsukasa — Synchronous I/O Multiplexing (<sys/select.h>)
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

#ifndef _TSUKASA_SYS_SELECT_H
#define _TSUKASA_SYS_SELECT_H

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#define FD_SETSIZE 256

typedef struct {
    uint8_t fds_bits[FD_SETSIZE / 8];
} fd_set;

#define FD_ZERO(set) do { \
    __builtin_memset((set), 0, sizeof(fd_set)); \
} while (0)

#define FD_SET(fd, set) do { \
    int __fd = (fd); \
    if (__fd >= 0 && __fd < FD_SETSIZE) \
        ((fd_set *)(set))->fds_bits[__fd / 8] |= (uint8_t)(1U << (__fd % 8)); \
} while (0)

#define FD_CLR(fd, set) do { \
    int __fd = (fd); \
    if (__fd >= 0 && __fd < FD_SETSIZE) \
        ((fd_set *)(set))->fds_bits[__fd / 8] &= (uint8_t)~(1U << (__fd % 8)); \
} while (0)

#define FD_ISSET(fd, set) \
    ((fd) >= 0 && (fd) < FD_SETSIZE && \
     ((((fd_set *)(set))->fds_bits[(fd) / 8] & (1U << ((fd) % 8))) != 0))

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#endif /* _TSUKASA_SYS_SELECT_H */
