/*
 * Project Tsukasa - mmapflt: Ring-3 mmap protection and NX fault test binary
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

#include <stdint.h>
#include "tsukasa_sdk.h"

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 10;

    if (str_eq(argv[1], "ro_write")) {
        int fd = open("/tmp/mmap_ro.dat", TSK_O_RDONLY);
        if (fd < 0)
            return 11;
        volatile uint8_t *p = (volatile uint8_t *)mmap(
            NULL, 4096, VFS_PROT_READ, VFS_MAP_PRIVATE, fd, 0);
        close(fd);
        if (!p || (long)(uintptr_t)p < 0)
            return 12;
        volatile uint8_t first = p[0];
        (void)first;
        p[0] = 0x55u;
        return 99;
    }

    if (str_eq(argv[1], "nx_exec")) {
        volatile uint8_t *p = (volatile uint8_t *)mmap(
            NULL, 4096, VFS_PROT_READ | VFS_PROT_WRITE,
            VFS_MAP_PRIVATE | VFS_MAP_ANONYMOUS, -1, 0);
        if (!p || (long)(uintptr_t)p < 0)
            return 21;
        p[0] = 0xC3u; /* ret */
        void (*fn)(void) = (void (*)(void))(uintptr_t)p;
        fn();
        return 98;
    }

    if (str_eq(argv[1], "prot_none")) {
        volatile uint8_t *p = (volatile uint8_t *)mmap(
            NULL, 4096, VFS_PROT_NONE,
            VFS_MAP_PRIVATE | VFS_MAP_ANONYMOUS, -1, 0);
        if (!p || (long)(uintptr_t)p < 0)
            return 31;
        volatile uint8_t v = p[0];
        (void)v;
        return 97;
    }

    if (str_eq(argv[1], "exec_ok")) {
        volatile uint8_t *p = (volatile uint8_t *)mmap(
            NULL, 4096, VFS_PROT_READ | VFS_PROT_WRITE | VFS_PROT_EXEC,
            VFS_MAP_PRIVATE | VFS_MAP_ANONYMOUS, -1, 0);
        if (!p || (long)(uintptr_t)p < 0)
            return 41;
        /* mov eax, 42; ret */
        p[0] = 0xB8u;
        p[1] = 0x2Au;
        p[2] = 0x00u;
        p[3] = 0x00u;
        p[4] = 0x00u;
        p[5] = 0xC3u;
        int (*fn)(void) = (int (*)(void))(uintptr_t)p;
        int rc = fn();
        (void)munmap((void *)(uintptr_t)p, 4096);
        return rc;
    }

    return 15;
}
