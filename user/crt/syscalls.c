/*
 * Project Tsukasa — SDK syscall stubs: flat-table syscalls for standalone
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

#include "tsukasa_sdk.h"

#include <stdint.h>

/* Syscall numbers: the Linux-convention flat table in syscall/syscall_table.c (SPEC-C01): read 0, write 1,... */
static inline long tsk_syscall3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return ret;
}

/* Explicit register variables bind the values since inline-asm constraint letters cannot name r8-r11 directly. */
static inline long tsk_syscall6(long nr, long a1, long a2, long a3,
                                long a4, long a5, long a6)
{
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2), "d"(a3),
                        "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return ret;
}

long read(int fd, void *buf, size_t count)
{
    return tsk_syscall3(0, fd, (long)(uintptr_t)buf, (long)count);
}

long write(int fd, const void *buf, size_t count)
{
    return tsk_syscall3(1, fd, (long)(uintptr_t)buf, (long)count);
}

int open(const char *path, int flags)
{
    return (int)tsk_syscall3(2, (long)(uintptr_t)path, flags, 0);
}

int close(int fd)
{
    return (int)tsk_syscall3(3, fd, 0, 0);
}

int dup2(int oldfd, int newfd)
{
    return (int)tsk_syscall3(33, oldfd, newfd, 0);
}

int getpid(void)
{
    return (int)tsk_syscall3(39, 0, 0, 0);
}

int getppid(void)
{
    return (int)tsk_syscall3(110, 0, 0, 0);
}

void exit(int code)
{
    tsk_syscall3(60, code, 0, 0);
    for (;;) {
    }
}

long futex(volatile unsigned int *uaddr, int op, int val)
{
    return tsk_syscall3(202, (long)(uintptr_t)uaddr, (long)op, (long)val);
}

int sched_yield(void)
{
    return (int)tsk_syscall3(24, 0, 0, 0);
}

int socket(int domain, int type, int protocol)
{
    return (int)tsk_syscall3(41, domain, type, protocol);
}

int connect(int fd, const void *addr, long addrlen)
{
    return (int)tsk_syscall3(42, fd, (long)(uintptr_t)addr, addrlen);
}

int accept(int fd)
{
    return (int)tsk_syscall3(43, fd, 0, 0);
}

int bind(int fd, const void *addr, long addrlen)
{
    return (int)tsk_syscall3(49, fd, (long)(uintptr_t)addr, addrlen);
}

int listen(int fd, int backlog)
{
    return (int)tsk_syscall3(50, fd, backlog, 0);
}

long send(int fd, const void *buf, size_t len, int flags)
{
    return tsk_syscall6(44, fd, (long)(uintptr_t)buf, (long)len, flags, 0, 0);
}

long recv(int fd, void *buf, size_t len, int flags)
{
    return tsk_syscall6(45, fd, (long)(uintptr_t)buf, (long)len, flags, 0, 0);
}

long sendto(int fd, const void *buf, size_t len, int flags,
            const void *dest_addr, long addrlen)
{
    return tsk_syscall6(44, fd, (long)(uintptr_t)buf, (long)len, flags,
                        (long)(uintptr_t)dest_addr, addrlen);
}

long recvfrom(int fd, void *buf, size_t len, int flags,
              void *src_addr, long *addrlen)
{
    return tsk_syscall6(45, fd, (long)(uintptr_t)buf, (long)len, flags,
                        (long)(uintptr_t)src_addr, (long)(uintptr_t)addrlen);
}

void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset)
{
    return (void *)(uintptr_t)tsk_syscall6(9, (long)(uintptr_t)addr,
                                           (long)length, (long)prot,
                                           (long)flags, (long)fd, offset);
}

int munmap(void *addr, unsigned long length)
{
    return (int)tsk_syscall3(11, (long)(uintptr_t)addr, (long)length, 0);
}

int ioctl(int fd, unsigned long request, long arg)
{
    return (int)tsk_syscall3(16, (long)fd, (long)request, arg);
}

int poll(vfs_pollfd_t *fds, long nfds, int timeout_ms)
{
    return (int)tsk_syscall3(7, (long)(uintptr_t)fds, nfds, (long)timeout_ms);
}

int shm_create(unsigned long size)
{
    return (int)tsk_syscall3(310, (long)size, 0, 0);
}

void *shm_attach(int shm_id)
{
    return (void *)(uintptr_t)tsk_syscall3(311, (long)shm_id, 0, 0);
}

void *shm_map(int shm_id)
{
    return shm_attach(shm_id);
}

int shm_detach(void *addr)
{
    return (int)tsk_syscall3(312, (long)(uintptr_t)addr, 0, 0);
}

int shm_unmap(void *addr)
{
    return shm_detach(addr);
}

int shm_destroy(int shm_id)
{
    return (int)tsk_syscall3(313, (long)shm_id, 0, 0);
}

unsigned long ticks(void)
{
    return (unsigned long)tsk_syscall3(320, 0, 0, 0);
}
