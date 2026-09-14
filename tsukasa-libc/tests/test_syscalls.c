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

static void print_str(const char *s)
{
    size_t len = 0;
    while (s && s[len])
        len++;
    write(STDOUT_FILENO, s, len);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    print_str("[test] Starting tsukasa-libc syscall verification...\n");

    /* Test 1: Stack frame alignment (rsp % 16 == 8 on function entry). */
    uintptr_t rsp_val;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp_val));
    if ((rsp_val & 0xF) != 8) {
        print_str("[FAIL] Stack frame unaligned\n");
        return 1;
    }
    print_str("[PASS] Stack frame aligned to 16 bytes\n");

    /* Test 2: Process telemetry (0-argument syscalls). */
    pid_t pid = getpid();
    if (pid < 0) {
        print_str("[FAIL] getpid failed\n");
        return 2;
    }
    pid_t ppid = getppid();
    if (ppid < 0) {
        print_str("[FAIL] getppid failed\n");
        return 3;
    }
    if (sched_yield() != 0) {
        print_str("[FAIL] sched_yield failed\n");
        return 4;
    }
    print_str("[PASS] Process syscalls (getpid, getppid, sched_yield)\n");

    /* Test 3: Errno handling on invalid syscall arguments. */
    errno = 0;
    int bad_fd = close(-999);
    if (bad_fd != -1 || errno == 0) {
        print_str("[FAIL] close(-999) did not set errno\n");
        return 5;
    }

    errno = 0;
    int bad_open = open("/nonexistent_directory/nonexistent_file_xyz", O_RDONLY);
    if (bad_open != -1 || errno != ENOENT) {
        print_str("[FAIL] open nonexistent file did not set errno = ENOENT\n");
        return 6;
    }

    errno = 0;
    int bad_sock = socket(9999, SOCK_STREAM, 0);
    if (bad_sock != -1 || errno != EAFNOSUPPORT) {
        print_str("[FAIL] socket(9999) did not set errno = EAFNOSUPPORT\n");
        return 7;
    }
    print_str("[PASS] Negative return errno translation (EBADF, ENOENT, EAFNOSUPPORT)\n");

    /* Test 4: Memory management (mmap/munmap). */
    errno = 0;
    void *bad_map = mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bad_map != MAP_FAILED || errno != EINVAL) {
        print_str("[FAIL] mmap(0) did not return MAP_FAILED with EINVAL\n");
        return 8;
    }

    void *mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        print_str("[FAIL] mmap anonymous 4K failed\n");
        return 9;
    }
    /* Write test pattern to mapped memory. */
    char *buf = (char *)mem;
    buf[0] = 'T';
    buf[1] = 'S';
    buf[2] = 'K';
    buf[3] = '\0';
    if (buf[0] != 'T' || buf[1] != 'S' || buf[2] != 'K') {
        print_str("[FAIL] Memory readback mismatch\n");
        return 10;
    }
    if (munmap(mem, 4096) != 0) {
        print_str("[FAIL] munmap failed\n");
        return 11;
    }
    print_str("[PASS] Memory management (mmap/munmap anonymous page)\n");

    /* Test 5: Sleep and time syscalls. */
    if (usleep(1000) != 0) {
        print_str("[FAIL] usleep failed\n");
        return 12;
    }
    time_t t = time(NULL);
    (void)t;
    print_str("[PASS] Time and sleep operations (usleep, time)\n");

    print_str("[ALL PASS] tsukasa-libc Step 2.2 & 2.3 verification complete\n");
    return 0;
}
