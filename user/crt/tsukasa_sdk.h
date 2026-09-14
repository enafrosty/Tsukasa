/*
 * Project Tsukasa — minimal SDK header: syscall stub prototypes for
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

#ifndef TSUKASA_SDK_H
#define TSUKASA_SDK_H

#include <stddef.h>

#include "../../include/vfs_abi.h"

/* VFS open flags — values match the kernel ABI header syscall/syscall.h (TSUKASA_O_*). */
#define TSK_O_RDONLY 0x0001
#define TSK_O_WRONLY 0x0002
#define TSK_O_RDWR   (TSK_O_RDONLY | TSK_O_WRONLY)
#define TSK_O_APPEND 0x0004
#define TSK_O_CREAT  0x0008
#define TSK_O_TRUNC  0x0010

/* Thin wrappers over the flat Linux-numbered syscall table (syscall/syscall_table.c), entered via the... */
long read(int fd, void *buf, size_t count);
long write(int fd, const void *buf, size_t count);
int  open(const char *path, int flags);
int  close(int fd);
int  dup2(int oldfd, int newfd);
int  getpid(void);
int  getppid(void);
void exit(int code) __attribute__((noreturn));

/* Minimal stdio (user/crt/stdio_lite.c): unbuffered, write()-backed. */
int  putchar(int c);
int  dprintf(int fd, const char *fmt, ...);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
long futex(volatile unsigned int *uaddr, int op, int val);

int sched_yield(void);

int socket(int domain, int type, int protocol);
int bind(int fd, const void *addr, long addrlen);
int listen(int fd, int backlog);
int accept(int fd);
int connect(int fd, const void *addr, long addrlen);
long send(int fd, const void *buf, size_t len, int flags);
long recv(int fd, void *buf, size_t len, int flags);
long sendto(int fd, const void *buf, size_t len, int flags,
            const void *dest_addr, long addrlen);
long recvfrom(int fd, void *buf, size_t len, int flags,
              void *src_addr, long *addrlen);

void *mmap(void *addr, unsigned long length, int prot, int flags, int fd, long offset);
int   munmap(void *addr, unsigned long length);
int   ioctl(int fd, unsigned long request, long arg);
int   poll(vfs_pollfd_t *fds, long nfds, int timeout_ms);
int   shm_create(unsigned long size);
void *shm_attach(int shm_id);
void *shm_map(int shm_id);
int   shm_detach(void *addr);
int   shm_unmap(void *addr);
int   shm_destroy(int shm_id);

unsigned long ticks(void);

#endif /* TSUKASA_SDK_H */
