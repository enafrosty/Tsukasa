/*
 * Project Tsukasa — In-Guest Automated Test Suite Runner Daemon
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

static inline long sys_spawn(const char *path, char *const argv[], char *const envp[])
{
    register long r10 __asm__("r10") = 0;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(317L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)argv), "d"((long)(uintptr_t)envp), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_wait4(long pid, int *wstatus, long options)
{
    register long r10 __asm__("r10") = 0;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(61L), "D"(pid), "S"((long)(uintptr_t)wstatus), "d"(options), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_poweroff(void)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(169L), "D"(0L), "S"(0L), "d"(1L)
                      : "rcx", "r11", "memory");
    (void)ret;
}

static inline long sys_stat(const char *path, void *st)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(4L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)st), "d"(0L)
                      : "rcx", "r11", "memory");
    return ret;
}

static int is_vanilla_test(const char *bin_path)
{
    const char * const targets[] = {
        "/fat12/TESTIPC.ELF",
        "/fat12/TESTCOMP.ELF",
        "/fat12/TESTTYPO.ELF",
        "/fat12/TSHELL.ELF",
        0
    };

    for (int i = 0; targets[i] != 0; i++) {
        const char *p = bin_path;
        const char *t = targets[i];
        while (*p && *t && *p == *t) {
            p++;
            t++;
        }
        if (*p == '\0' && *t == '\0')
            return 1;
    }
    return 0;
}

static const char * const g_test_binaries[] = {
    "/fat12/TCRT0.ELF",
    "/fat12/TSYSC.ELF",
    "/fat12/TLIBC.ELF",
    "/fat12/TMATH.ELF",
    "/fat12/TSTDIO.ELF",
    "/fat12/TESTIPC.ELF",
    "/fat12/TESTCOMP.ELF",
    "/fat12/TESTTYPO.ELF",
    "/fat12/TSHELL.ELF",
    NULL
};

static void setup_stdio(void)
{
    int fd = open("/dev/tty0", TSK_O_RDWR);
    if (fd < 0)
        fd = open("/dev/tty0", TSK_O_WRONLY);
    if (fd < 0)
        fd = open("/dev/ttyS0", TSK_O_RDWR);
    if (fd < 0)
        fd = open("/dev/console", TSK_O_RDWR);

    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    setup_stdio();

    dprintf(1, "[testdrv] Starting in-guest test suite runner...\n");

    int vanilla_ready = 0;

    for (int i = 0; g_test_binaries[i] != NULL; i++) {
        const char *bin_path = g_test_binaries[i];

        if (!vanilla_ready && is_vanilla_test(bin_path)) {
            char stat_buf[128];
            for (int wait_sock = 0; wait_sock < 500; wait_sock++) {
                if (sys_stat("/tmp/vanilla.sock", stat_buf) == 0)
                    break;
                sched_yield();
            }
            vanilla_ready = 1;
        }

        dprintf(1, "[testdrv] Executing %s\n", bin_path);

        long pid = -1;
        for (int retry = 0; retry < 50 && pid < 0; retry++) {
            pid = sys_spawn(bin_path, NULL, NULL);
            if (pid < 0)
                sched_yield();
        }

        if (pid <= 0) {
            dprintf(1, "[TEST] %s.spawn FAIL: could not spawn process (rc=%ld)\n", bin_path, pid);
            continue;
        }

        int status = 0;
        int completed = 0;
        for (int wait_loop = 0; wait_loop < 200000; wait_loop++) {
            long ret = sys_wait4(pid, &status, 1 /* WNOHANG */);
            if (ret == pid) {
                completed = 1;
                break;
            }
            sched_yield();
        }

        if (!completed) {
            dprintf(1, "[TEST] %s.execution FAIL: timeout waiting for process pid=%ld\n", bin_path, pid);
        }
    }

    dprintf(1, "[TEST] ALL DONE\n");
    sys_poweroff();

    for (;;) {
        sched_yield();
    }

    return 0;
}
