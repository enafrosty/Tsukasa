/*
 * Project Tsukasa — Standard BSD socket library implementation
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

#include "include/net/socket.h"

#ifdef TSUKASA_USERLIB_KERNEL
#include "fs/vfs.h"
#include "include/errno.h"

int socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain != AF_UNIX)
        return -EAFNOSUPPORT;
    if (type != SOCK_STREAM)
        return -EINVAL;
    return vfs_socket_create();
}

int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    const struct sockaddr_un *ua;
    (void)addrlen;
    if (!addr)
        return -EINVAL;
    ua = (const struct sockaddr_un *)addr;
    if (ua->sun_family != AF_UNIX)
        return -EAFNOSUPPORT;
    return vfs_socket_bind(fd, ua->sun_path);
}

int listen(int fd, int backlog)
{
    (void)backlog;
    return vfs_socket_listen(fd);
}

int accept(int fd)
{
    return vfs_socket_accept(fd);
}

int connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    const struct sockaddr_un *ua;
    (void)addrlen;
    if (!addr)
        return -EINVAL;
    ua = (const struct sockaddr_un *)addr;
    if (ua->sun_family != AF_UNIX)
        return -EAFNOSUPPORT;
    return vfs_socket_connect(fd, ua->sun_path);
}

long send(int fd, const void *buf, size_t len, int flags)
{
    (void)flags;
    size_t wr = vfs_write(fd, buf, len);
    if ((long)wr < 0)
        return (long)wr;
    if (wr == 0 && len > 0)
        return -EPIPE;
    return (long)wr;
}

long recv(int fd, void *buf, size_t len, int flags)
{
    (void)flags;
    return (long)vfs_read(fd, buf, len);
}

long sendto(int fd, const void *buf, size_t len, int flags,
            const struct sockaddr *dest_addr, socklen_t addrlen)
{
    (void)dest_addr;
    (void)addrlen;
    return send(fd, buf, len, flags);
}

long recvfrom(int fd, void *buf, size_t len, int flags,
              struct sockaddr *src_addr, socklen_t *addrlen)
{
    (void)src_addr;
    (void)addrlen;
    return recv(fd, buf, len, flags);
}

#else

/* Standalone userspace assembly stubs are provided in user/crt/syscalls.c */

#endif
