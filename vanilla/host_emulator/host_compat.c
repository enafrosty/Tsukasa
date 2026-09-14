/*
 * Project Tsukasa — Vanilla Display Server Host Compatibility Layer Implementation
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

#include "host_compat.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>


#define HOST_MAX_SHM_SLOTS 256
static void *g_host_shm_slots[HOST_MAX_SHM_SLOTS];
static int   g_host_shm_next = 1;

int shm_create(size_t size)
{
    if (g_host_shm_next >= HOST_MAX_SHM_SLOTS)
        return -1;

    void *ptr = malloc(size);
    if (!ptr)
        return -1;

    memset(ptr, 0, size);
    int id = g_host_shm_next++;
    g_host_shm_slots[id] = ptr;
    return id;
}

void *shm_attach(int shm_id)
{
    if (shm_id <= 0 || shm_id >= HOST_MAX_SHM_SLOTS)
        return (void *)-1;

    return g_host_shm_slots[shm_id] ? g_host_shm_slots[shm_id] : (void *)-1;
}

int shm_detach(const void *addr)
{
    (void)addr;
    return 0;
}

int shm_destroy(int shm_id)
{
    if (shm_id <= 0 || shm_id >= HOST_MAX_SHM_SLOTS)
        return -1;

    if (g_host_shm_slots[shm_id]) {
        free(g_host_shm_slots[shm_id]);
        g_host_shm_slots[shm_id] = NULL;
    }
    return 0;
}

struct tm *gmtime_r(const time_t *timer, struct tm *result)
{
    if (!timer || !result)
        return NULL;

    struct tm *t = gmtime(timer);
    if (t) {
        *result = *t;
        return result;
    }
    return NULL;
}

pid_t spawn(const char *path, char *const argv[], char *const envp[])
{
    (void)argv;
    (void)envp;
    printf("[Host Emulator] Spawn requested for application: %s\n", path ? path : "(null)");
    return 1;
}

/* Stubs for POSIX networking and IPC calls unused in host prototype mode */
int socket(int domain, int type, int protocol)
{
    (void)domain;
    (void)type;
    (void)protocol;
    return -1;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1;
}

int listen(int sockfd, int backlog)
{
    (void)sockfd;
    (void)backlog;
    return -1;
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    (void)sockfd;
    (void)addr;
    (void)addrlen;
    return -1;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    (void)fds;
    (void)nfds;
    (void)timeout;
    return 0;
}

int ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    (void)request;
    return -1;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)addr;
    (void)length;
    (void)prot;
    (void)flags;
    (void)fd;
    (void)offset;
    return (void *)-1;
}

int munmap(void *addr, size_t length)
{
    (void)addr;
    (void)length;
    return 0;
}

int sched_yield(void)
{
    return 0;
}
