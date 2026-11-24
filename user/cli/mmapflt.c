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

static inline long mf_syscall3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long mf_syscall4(long nr, long a1, long a2, long a3, long a4)
{
    register long r10 __asm__("r10") = a4;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static int ext4_persist_write(const char *dev)
{
    (void)mf_syscall3(83L, (long)(uintptr_t)"/mnt", 0755L, 0L);
    (void)mf_syscall3(83L, (long)(uintptr_t)"/mnt/e4p", 0755L, 0L);
    long mrc = mf_syscall4(165L, (long)(uintptr_t)dev, (long)(uintptr_t)"/mnt/e4p",
                           (long)(uintptr_t)"ext4", 0L);
    if (mrc != 0) {
        dprintf(1, "[mmapflt] ext4_write FAIL: mount %s rc=%ld\n", dev, mrc);
        return 51;
    }

    int fd = open("/mnt/e4p/persist.bin", TSK_O_RDWR | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0) {
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_write FAIL: open\n");
        return 52;
    }

    uint8_t zbuf[4096];
    for (int i = 0; i < 4096; i++)
        zbuf[i] = (uint8_t)(i & 0xFF);
    if (write(fd, zbuf, 4096) != 4096 || write(fd, zbuf, 4096) != 4096) {
        close(fd);
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_write FAIL: seed write\n");
        return 53;
    }
    close(fd);

    fd = open("/mnt/e4p/persist.bin", TSK_O_RDWR);
    if (fd < 0) {
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        return 54;
    }
    volatile uint8_t *p = (volatile uint8_t *)mmap(
        NULL, 8192, VFS_PROT_READ | VFS_PROT_WRITE, VFS_MAP_SHARED, fd, 0);
    close(fd);
    if (!p || (long)(uintptr_t)p < 0) {
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_write FAIL: mmap\n");
        return 55;
    }

    const uint64_t magic0 = 0xDEADBEEFCAFEBABEULL;
    const uint64_t magic1 = 0x123456789ABCDEF0ULL;
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)((magic0 >> (i * 8)) & 0xFFu);
        p[4100 + i] = (uint8_t)((magic1 >> (i * 8)) & 0xFFu);
    }

    if (msync((void *)(uintptr_t)p, 8192, VFS_MS_SYNC) != 0) {
        (void)munmap((void *)(uintptr_t)p, 8192);
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_write FAIL: msync\n");
        return 56;
    }
    (void)munmap((void *)(uintptr_t)p, 8192);

    long urc = mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
    if (urc != 0) {
        dprintf(1, "[mmapflt] ext4_write FAIL: umount rc=%ld\n", urc);
        return 57;
    }
    dprintf(1, "[mmapflt] ext4_write PASS\n");
    return 0;
}

static int ext4_persist_verify(const char *dev)
{
    (void)mf_syscall3(83L, (long)(uintptr_t)"/mnt", 0755L, 0L);
    (void)mf_syscall3(83L, (long)(uintptr_t)"/mnt/e4p", 0755L, 0L);
    long mrc = mf_syscall4(165L, (long)(uintptr_t)dev, (long)(uintptr_t)"/mnt/e4p",
                           (long)(uintptr_t)"ext4", 0L);
    if (mrc != 0) {
        dprintf(1, "[mmapflt] ext4_verify FAIL: mount %s rc=%ld\n", dev, mrc);
        return 61;
    }

    int fd = open("/mnt/e4p/persist.bin", TSK_O_RDONLY);
    if (fd < 0) {
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_verify FAIL: open\n");
        return 62;
    }
    volatile uint8_t *p = (volatile uint8_t *)mmap(
        NULL, 8192, VFS_PROT_READ, VFS_MAP_PRIVATE, fd, 0);
    close(fd);
    if (!p || (long)(uintptr_t)p < 0) {
        (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
        dprintf(1, "[mmapflt] ext4_verify FAIL: mmap\n");
        return 63;
    }

    const uint64_t magic0 = 0xDEADBEEFCAFEBABEULL;
    const uint64_t magic1 = 0x123456789ABCDEF0ULL;
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        if (p[i] != (uint8_t)((magic0 >> (i * 8)) & 0xFFu) ||
            p[4100 + i] != (uint8_t)((magic1 >> (i * 8)) & 0xFFu)) {
            ok = 0;
            break;
        }
    }
    if (p[200] != (uint8_t)(200 & 0xFF) || p[5000] != (uint8_t)(5000 & 0xFF))
        ok = 0;

    (void)munmap((void *)(uintptr_t)p, 8192);
    (void)mf_syscall3(166L, (long)(uintptr_t)"/mnt/e4p", 0L, 0L);
    if (!ok) {
        dprintf(1, "[mmapflt] ext4_verify FAIL: persisted bytes mismatch\n");
        return 64;
    }
    dprintf(1, "[mmapflt] ext4_verify PASS\n");
    return 0;
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

    if (str_eq(argv[1], "ext4_write") && argc >= 3)
        return ext4_persist_write(argv[2]);

    if (str_eq(argv[1], "ext4_verify") && argc >= 3)
        return ext4_persist_verify(argv[2]);

    return 15;
}
