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
#include <sys/select.h>
#include <sys/epoll.h>

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

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    return (int)__syscall_check(__syscall5(SYS_select, (int64_t)nfds,
                                          (int64_t)(uintptr_t)readfds,
                                          (int64_t)(uintptr_t)writefds,
                                          (int64_t)(uintptr_t)exceptfds,
                                          (int64_t)(uintptr_t)timeout));
}

int epoll_create(int size)
{
    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }
    return epoll_create1(0);
}

int epoll_create1(int flags)
{
    return (int)__syscall_check(__syscall1(SYS_epoll_create1, (int64_t)flags));
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return (int)__syscall_check(__syscall4(SYS_epoll_ctl, (int64_t)epfd,
                                          (int64_t)op, (int64_t)fd,
                                          (int64_t)(uintptr_t)event));
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return (int)__syscall_check(__syscall4(SYS_epoll_wait, (int64_t)epfd,
                                          (int64_t)(uintptr_t)events,
                                          (int64_t)maxevents,
                                          (int64_t)timeout));
}

int isatty(int fd)
{
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}
