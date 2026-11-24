/*
 * Project Tsukasa — Memory Management System Call Wrappers
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
#include <sys/mman.h>
#include <unistd.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    int64_t ret = __syscall6(SYS_mmap, (int64_t)(uintptr_t)addr, (int64_t)length,
                             (int64_t)prot, (int64_t)flags, (int64_t)fd, (int64_t)offset);
    if (ret < 0) {
        errno = (int)(-ret);
        return MAP_FAILED;
    }
    return (void *)(uintptr_t)ret;
}

int munmap(void *addr, size_t length)
{
    return (int)__syscall_check(__syscall2(SYS_munmap, (int64_t)(uintptr_t)addr, (int64_t)length));
}

int mprotect(void *addr, size_t length, int prot)
{
    return (int)__syscall_check(__syscall3(SYS_mprotect, (int64_t)(uintptr_t)addr,
                                           (int64_t)length, (int64_t)prot));
}

int brk(void *addr)
{
    int64_t ret = __syscall1(SYS_brk, (int64_t)(uintptr_t)addr);
    if (ret < 0) {
        errno = (int)(-ret);
        return -1;
    }
    if ((uintptr_t)ret != (uintptr_t)addr) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int msync(void *addr, size_t length, int flags)
{
    return (int)__syscall_check(__syscall3(SYS_msync, (int64_t)(uintptr_t)addr,
                                           (int64_t)length, (int64_t)flags));
}

void *sbrk(intptr_t increment)
{
    int64_t cur_ret = __syscall1(SYS_brk, 0);
    if (cur_ret <= 0) {
        errno = (cur_ret < 0) ? (int)(-cur_ret) : ENOMEM;
        return (void *)-1;
    }

    uintptr_t cur = (uintptr_t)cur_ret;
    if (increment == 0)
        return (void *)cur;

    uintptr_t target;
    if (increment > 0) {
        uintptr_t inc = (uintptr_t)increment;
        if (cur + inc < cur) {
            errno = ENOMEM;
            return (void *)-1;
        }
        target = cur + inc;
    } else {
        uintptr_t dec = (uintptr_t)(-increment);
        if (dec > cur) {
            errno = ENOMEM;
            return (void *)-1;
        }
        target = cur - dec;
    }

    if (brk((void *)target) != 0)
        return (void *)-1;
    return (void *)cur;
}
