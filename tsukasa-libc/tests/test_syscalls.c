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
#include <sys/select.h>
#include <sys/epoll.h>
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
    int total = 8;

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

    /* Test 6: Synchronous I/O multiplexing (poll). */
    if (poll(NULL, 0, 10) != 0) {
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "poll(NULL, 0, 10) did not return 0");
        return 13;
    }
    errno = 0;
    if (poll(NULL, 1, 10) != -1 || errno != EFAULT) {
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "poll(NULL, 1, 10) did not return -1 EFAULT");
        return 13;
    }
    errno = 0;
    if (poll(NULL, 300, 10) != -1 || errno != EINVAL) {
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "poll(NULL, 300, 10) did not return -1 EINVAL");
        return 13;
    }

    int pfd[2];
    if (pipe(pfd) != 0) {
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "pipe creation failed");
        return 14;
    }

    struct pollfd pfds[2];
    pfds[0].fd = pfd[0];
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = pfd[1];
    pfds[1].events = POLLOUT;
    pfds[1].revents = 0;

    int poll_res = poll(pfds, 2, 0);
    if (poll_res != 1 || !(pfds[1].revents & POLLOUT) || (pfds[0].revents & POLLIN)) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "initial poll state mismatch");
        return 15;
    }

    if (write(pfd[1], "poll", 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "pipe write failed");
        return 16;
    }

    poll_res = poll(pfds, 2, 0);
    if (poll_res < 1 || !(pfds[0].revents & POLLIN)) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "poll after write did not flag POLLIN");
        return 17;
    }

    char pipe_buf[8];
    if (read(pfd[0], pipe_buf, 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "poll_multiplexing", "pipe read failed");
        return 18;
    }
    close(pfd[0]);
    close(pfd[1]);
    TSK_TEST_PASS("syscalls", "poll_multiplexing");
    passed++;

    /* Test 7: Synchronous I/O multiplexing (select). */
    if (pipe(pfd) != 0) {
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "pipe creation failed");
        return 19;
    }

    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(pfd[0], &rfds);
    FD_SET(pfd[1], &wfds);

    errno = 0;
    if (select(300, &rfds, NULL, NULL, NULL) != -1 || errno != EINVAL) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "select(300, ...) did not return -1 EINVAL");
        return 19;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    int maxfd = (pfd[0] > pfd[1] ? pfd[0] : pfd[1]) + 1;
    int sel_res = select(maxfd, &rfds, &wfds, NULL, &tv);
    if (sel_res <= 0 || !FD_ISSET(pfd[1], &wfds) || FD_ISSET(pfd[0], &rfds)) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "initial select state mismatch");
        return 20;
    }

    if (write(pfd[1], "selc", 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "pipe write failed");
        return 21;
    }

    FD_ZERO(&rfds);
    FD_SET(pfd[0], &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    sel_res = select(pfd[0] + 1, &rfds, NULL, NULL, &tv);
    if (sel_res != 1 || !FD_ISSET(pfd[0], &rfds)) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "select after write did not flag ready");
        return 22;
    }

    if (read(pfd[0], pipe_buf, 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        TSK_TEST_FAIL("syscalls", "select_multiplexing", "pipe read failed");
        return 23;
    }
    close(pfd[0]);
    close(pfd[1]);
    TSK_TEST_PASS("syscalls", "select_multiplexing");
    passed++;

    /* Test 8: Event notification facility (epoll). */
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_create1 failed");
        return 24;
    }

    if (pipe(pfd) != 0) {
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "pipe creation failed");
        return 25;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = pfd[0];

    errno = 0;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, epfd, &ev) != -1 || errno != EINVAL) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_ctl ADD epfd to itself did not return -1 EINVAL");
        return 26;
    }

    errno = 0;
    if (epoll_ctl(999, EPOLL_CTL_ADD, pfd[0], &ev) != -1 || errno != EBADF) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_ctl bad epfd did not return -1 EBADF");
        return 26;
    }

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, pfd[0], &ev) != 0) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_ctl ADD failed");
        return 26;
    }

    struct epoll_event events[2];
    int ep_res = epoll_wait(epfd, events, 2, 0);
    if (ep_res != 0) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_wait on empty pipe returned events");
        return 27;
    }

    if (write(pfd[1], "epol", 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "pipe write failed");
        return 28;
    }

    ep_res = epoll_wait(epfd, events, 2, 10);
    if (ep_res != 1 || events[0].data.fd != pfd[0] || !(events[0].events & EPOLLIN)) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_wait after write mismatch");
        return 29;
    }

    if (read(pfd[0], pipe_buf, 4) != 4) {
        close(pfd[0]);
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "pipe read failed");
        return 30;
    }

    /* Test closed fd unregistration from epoll */
    close(pfd[0]);
    int ep_closed = epoll_wait(epfd, events, 2, 0);
    if (ep_closed != 0) {
        close(pfd[1]);
        close(epfd);
        TSK_TEST_FAIL("syscalls", "epoll_facility", "epoll_wait on closed fd returned events");
        return 31;
    }

    close(pfd[1]);
    close(epfd);
    TSK_TEST_PASS("syscalls", "epoll_facility");
    passed++;

    TSK_TEST_DONE("syscalls", passed, total);
    return 0;
}
