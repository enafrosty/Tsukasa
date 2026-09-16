/*
 * Project Tsukasa — Syscall Wrappers and Errno Verification Suite
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

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include "../../scripts/test/tsk_test.h"

static uintptr_t s_entry_rsp;

static int real_main(int argc, char **argv, char **envp) __attribute__((used));

__attribute__((naked)) int main(void)
{
    __asm__ volatile (
        "movq %%rsp, %0\n\t"
        "jmp real_main\n\t"
        : "=m"(s_entry_rsp)
    );
}

static int real_main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    int passed = 0;
    int total = 5;

    /* Test 1: Stack frame alignment (rsp % 16 == 8 on function entry). */
    if ((s_entry_rsp & 0xF) != 8) {
        TSK_TEST_FAIL("syscalls", "stack_alignment", "stack frame unaligned");
        return 1;
    }
    TSK_TEST_PASS("syscalls", "stack_alignment");
    passed++;

    /* Test 2: Process telemetry (0-argument syscalls). */
    pid_t pid = getpid();
    if (pid < 0) {
        TSK_TEST_FAIL("syscalls", "process_telemetry", "getpid failed");
        return 2;
    }
    pid_t ppid = getppid();
    if (ppid < 0) {
        TSK_TEST_FAIL("syscalls", "process_telemetry", "getppid failed");
        return 3;
    }
    if (sched_yield() != 0) {
        TSK_TEST_FAIL("syscalls", "process_telemetry", "sched_yield failed");
        return 4;
    }
    TSK_TEST_PASS("syscalls", "process_telemetry");
    passed++;

    /* Test 3: Errno handling on invalid syscall arguments. */
    errno = 0;
    int bad_fd = close(-999);
    if (bad_fd != -1 || errno == 0) {
        TSK_TEST_FAIL("syscalls", "errno_translation", "close(-999) did not set errno");
        return 5;
    }

    errno = 0;
    int bad_open = open("/nonexistent_directory/nonexistent_file_xyz", O_RDONLY);
    if (bad_open != -1 || errno != ENOENT) {
        TSK_TEST_FAIL("syscalls", "errno_translation", "open nonexistent file did not set errno = ENOENT");
        return 6;
    }

    errno = 0;
    int bad_sock = socket(9999, SOCK_STREAM, 0);
    if (bad_sock != -1 || errno != EAFNOSUPPORT) {
        TSK_TEST_FAIL("syscalls", "errno_translation", "socket(9999) did not set errno = EAFNOSUPPORT");
        return 7;
    }
    TSK_TEST_PASS("syscalls", "errno_translation");
    passed++;

    /* Test 4: Memory management (mmap/munmap). */
    errno = 0;
    void *bad_map = mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bad_map != MAP_FAILED || errno != EINVAL) {
        TSK_TEST_FAIL("syscalls", "mmap_munmap", "mmap(0) did not return MAP_FAILED with EINVAL");
        return 8;
    }

    void *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        TSK_TEST_FAIL("syscalls", "mmap_munmap", "mmap anonymous 4K failed");
        return 9;
    }
    /* Write test pattern to mapped memory. */
    char *buf = (char *)mem;
    buf[0] = 'T';
    buf[1] = 'S';
    buf[2] = 'K';
    buf[3] = '\0';
    if (buf[0] != 'T' || buf[1] != 'S' || buf[2] != 'K') {
        TSK_TEST_FAIL("syscalls", "mmap_munmap", "memory readback mismatch");
        return 10;
    }
    if (munmap(mem, 4096) != 0) {
        TSK_TEST_FAIL("syscalls", "mmap_munmap", "munmap failed");
        return 11;
    }
    TSK_TEST_PASS("syscalls", "mmap_munmap");
    passed++;

    /* Test 5: Sleep and time syscalls. */
    if (usleep(1000) != 0) {
        TSK_TEST_FAIL("syscalls", "time_sleep", "usleep failed");
        return 12;
    }
    time_t t = time(NULL);
    (void)t;
    TSK_TEST_PASS("syscalls", "time_sleep");
    passed++;

    TSK_TEST_DONE("syscalls", passed, total);
    return 0;
}
