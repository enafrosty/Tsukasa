/*
 * Project Tsukasa — Poll event notification header
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

#ifndef TSUKASA_SYS_POLL_H
#define TSUKASA_SYS_POLL_H

#include "types.h"
#include "../syscall_nums.h"

typedef unsigned long nfds_t;

#define POLLIN  TSUKASA_POLLIN
#define POLLOUT TSUKASA_POLLOUT
#define POLLERR TSUKASA_POLLERR
#define POLLHUP TSUKASA_POLLHUP

struct pollfd {
    int fd;
    short events;
    short revents;
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif /* TSUKASA_SYS_POLL_H */
