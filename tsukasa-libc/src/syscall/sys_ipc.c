/*
 * Project Tsukasa — Inter-Process Communication & Socket Syscall Wrappers
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
#include <sys/socket.h>
#include <sys/shm.h>

int socket(int domain, int type, int protocol)
{
    return (int)__syscall_check(__syscall3(SYS_socket, (int64_t)domain, (int64_t)type, (int64_t)protocol));
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)__syscall_check(__syscall3(SYS_bind, (int64_t)sockfd,
                                           (int64_t)(uintptr_t)addr, (int64_t)addrlen));
}

int listen(int sockfd, int backlog)
{
    return (int)__syscall_check(__syscall2(SYS_listen, (int64_t)sockfd, (int64_t)backlog));
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    return (int)__syscall_check(__syscall3(SYS_connect, (int64_t)sockfd,
                                           (int64_t)(uintptr_t)addr, (int64_t)addrlen));
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return (int)__syscall_check(__syscall3(SYS_accept, (int64_t)sockfd,
                                           (int64_t)(uintptr_t)addr, (int64_t)(uintptr_t)addrlen));
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return (ssize_t)__syscall_check(__syscall6(SYS_sendto, (int64_t)sockfd,
                                               (int64_t)(uintptr_t)buf, (int64_t)len,
                                               (int64_t)flags, (int64_t)(uintptr_t)dest_addr,
                                               (int64_t)addrlen));
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen)
{
    return (ssize_t)__syscall_check(__syscall6(SYS_recvfrom, (int64_t)sockfd,
                                               (int64_t)(uintptr_t)buf, (int64_t)len,
                                               (int64_t)flags, (int64_t)(uintptr_t)src_addr,
                                               (int64_t)(uintptr_t)addrlen));
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

int shm_create(size_t size)
{
    return (int)__syscall_check(__syscall1(SYS_shm_create, (int64_t)size));
}

void *shm_attach(int shm_id)
{
    int64_t ret = __syscall1(SYS_shm_attach, (int64_t)shm_id);
    if (ret < 0) {
        errno = (int)(-ret);
        return NULL;
    }
    return (void *)(uintptr_t)ret;
}

int shm_detach(void *addr)
{
    return (int)__syscall_check(__syscall1(SYS_shm_detach, (int64_t)(uintptr_t)addr));
}

int shm_destroy(int shm_id)
{
    return (int)__syscall_check(__syscall1(SYS_shm_destroy, (int64_t)shm_id));
}
