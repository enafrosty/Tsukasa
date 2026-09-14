/*
 * Project Tsukasa — Device and I/O Multiplexing Syscall Wrappers
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

#include "syscall_internal.h"
#include <sys/ioctl.h>
#include <sys/poll.h>

int ioctl(int fd, unsigned long request, ...)
{
    void *arg = NULL;
    __builtin_va_list ap;
    __builtin_va_start(ap, request);
    arg = __builtin_va_arg(ap, void *);
    __builtin_va_end(ap);
    return (int)__syscall_check(__syscall3(SYS_ioctl, (int64_t)fd,
                                          (int64_t)request, (int64_t)(uintptr_t)arg));
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return (int)__syscall_check(__syscall3(SYS_poll, (int64_t)(uintptr_t)fds,
                                          (int64_t)nfds, (int64_t)timeout));
}

int isatty(int fd)
{
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}
