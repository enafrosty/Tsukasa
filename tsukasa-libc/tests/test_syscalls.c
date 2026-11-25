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
#include <sys/file.h>
#include <sys/mount.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static int wait_child_ok(pid_t pid)
{
    int status = 0;
    for (int i = 0; i < 1000; i++) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
        }
        usleep(5000);
    }
    return -1;
}

static int run_worker_flock(void)
{
    for (int i = 3; i < 256; i++)
        close(i);
    int fd = open("/tmp/flock_test.dat", O_RDWR);
    if (fd < 0)
        return 1;
    errno = 0;
    if (flock(fd, LOCK_EX | LOCK_NB) != -1 || errno != EAGAIN) {
        close(fd);
        return 2;
    }
    int rfd = open("/tmp/flock_ready", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd >= 0)
        close(rfd);
    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return 3;
    }
    lseek(fd, 0, SEEK_SET);
    if (write(fd, "CHILD_OK", 8) != 8) {
        flock(fd, LOCK_UN);
        close(fd);
        return 4;
    }
    flock(fd, LOCK_UN);
    close(fd);
    return 0;
}

static int run_worker_vfs(const char *tag)
{
    char p1[64], p2[64], buf[32];
    snprintf(p1, sizeof(p1), "/tmp/conc_%s_a.txt", tag);
    snprintf(p2, sizeof(p2), "/tmp/conc_%s_b.txt", tag);

    for (int i = 0; i < 40; i++) {
        int fd = open(p1, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            return 1;
        if (write(fd, "DATA", 4) != 4) {
            close(fd);
            return 2;
        }
        close(fd);
        if (rename(p1, p2) != 0)
            return 3;
        fd = open(p2, O_RDONLY);
        if (fd < 0)
            return 4;
        memset(buf, 0, sizeof(buf));
        if (read(fd, buf, 4) != 4 || memcmp(buf, "DATA", 4) != 0) {
            close(fd);
            return 5;
        }
        close(fd);
        if (unlink(p2) != 0)
            return 6;
    }
    return 0;
}

static volatile sig_atomic_t g_sigusr1_seen;
static volatile sig_atomic_t g_sigusr1_signo;
static volatile sig_atomic_t g_sigusr1_masked_in_handler;
static volatile sig_atomic_t g_sigusr2_seen;
static volatile sig_atomic_t g_sigint_seen;
static volatile sig_atomic_t g_sigchld_seen;
static volatile sig_atomic_t g_sigpipe_seen;

static void sig_usr1_info_handler(int sig, siginfo_t *info, void *puc)
{
    sigset_t cur_mask = 0;
    uint64_t stack_probe = 0;
    uint64_t sp_val = (uint64_t)(uintptr_t)&stack_probe;
    (void)sp_val;

    /* Clobber all XMM registers inside the signal handler to test FXSAVE/FXRSTOR. */
    __asm__ volatile (
        "mov $0x1111222233334444, %%rax\n\t"
        "movq %%rax, %%xmm0\n\t"
        "movq %%rax, %%xmm1\n\t"
        "movq %%rax, %%xmm2\n\t"
        "movq %%rax, %%xmm3\n\t"
        "movq %%rax, %%xmm4\n\t"
        "movq %%rax, %%xmm5\n\t"
        "movq %%rax, %%xmm6\n\t"
        "movq %%rax, %%xmm7\n\t"
        "movq %%rax, %%xmm8\n\t"
        "movq %%rax, %%xmm9\n\t"
        "movq %%rax, %%xmm10\n\t"
        "movq %%rax, %%xmm11\n\t"
        "movq %%rax, %%xmm12\n\t"
        "movq %%rax, %%xmm13\n\t"
        "movq %%rax, %%xmm14\n\t"
        "movq %%rax, %%xmm15\n\t"
        : : : "rax",
              "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
    );

    if (sigprocmask(SIG_BLOCK, NULL, &cur_mask) == 0 && sigismember(&cur_mask, SIGUSR1) == 1)
        g_sigusr1_masked_in_handler = 1;
    if (info && info->si_signo == sig && puc != NULL)
        g_sigusr1_signo = sig;
    g_sigusr1_seen++;
}

static void sig_usr2_handler(int sig)
{
    (void)sig;
    g_sigusr2_seen++;
    if (g_sigusr2_seen == 1)
        raise(SIGUSR2); /* Allowed recursively under SA_NODEFER */
}

static void sig_int_handler(int sig)
{
    (void)sig;
    g_sigint_seen++;
}

static void sig_chld_handler(int sig)
{
    (void)sig;
    g_sigchld_seen++;
}

static void sig_pipe_handler(int sig)
{
    (void)sig;
    g_sigpipe_seen++;
}

static int run_worker_exit0(void)
{
    return 0;
}

static int run_worker_block_sigkill(void)
{
    sigset_t m;
    sigfillset(&m);
    sigprocmask(SIG_SETMASK, &m, NULL);
    for (int i = 0; i < 400; i++)
        usleep(10000);
    return 99;
}

static int run_worker_sig_ping(const char *pid_str)
{
    pid_t target = (pid_t)atoi(pid_str);
    usleep(20000);
    if (target > 0)
        kill(target, SIGUSR1);
    return 0;
}

static int run_worker_pipe_echo(void)
{
    for (int i = 3; i < 256; i++)
        close(i);
    char buf[64];
    size_t total = 0;
    for (;;) {
        ssize_t n = read(0, buf + total, sizeof(buf) - total);
        if (n < 0)
            return 2;
        if (n == 0)
            break;
        total += (size_t)n;
        if (total == sizeof(buf))
            break;
    }
    if (total == 0)
        return 3;
    if (write(1, buf, total) != (ssize_t)total)
        return 4;
    close(1);
    return 0;
}

static int run_worker_delayed_write(void)
{
    for (int i = 3; i < 256; i++)
        close(i);
    usleep(30000);
    if (write(1, "WAKE", 4) != 4)
        return 2;
    close(1);
    return 0;
}

static int run_worker_sigpipe_dfl(void)
{
    for (int i = 3; i < 256; i++)
        close(i);
    int pfd[2];
    if (pipe(pfd) != 0)
        return 2;
    close(pfd[0]);
    write(pfd[1], "X", 1); /* Must terminate process with SIGPIPE */
    close(pfd[1]);
    return 3;
}

static int run_worker_mprotect_segv(void)
{
    volatile uint32_t *p = (volatile uint32_t *)mmap(NULL, 4096,
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                                     -1, 0);
    if (p == MAP_FAILED)
        return 1;
    *p = 0x11223344u;
    if (mprotect((void *)p, 4096, PROT_READ) != 0)
        return 2;
    if (*p != 0x11223344u)
        return 3;
    *p = 0x55667788u; /* Must trigger Ring-3 #PF -> SIGSEGV */
    return 4;
}

__attribute__((noinline))
static int stack_autogrow_probe(int depth, uint8_t expected)
{
    volatile uint8_t buf[262144];
    buf[0] = (uint8_t)(expected + (uint8_t)depth);
    buf[sizeof(buf) / 2] = (uint8_t)(expected ^ (uint8_t)depth);
    buf[sizeof(buf) - 1] = (uint8_t)(expected + 7u);
    int rc = 0;
    if (depth > 1)
        rc = stack_autogrow_probe(depth - 1, expected);
    if (rc != 0)
        return rc;
    if (buf[0] != (uint8_t)(expected + (uint8_t)depth) ||
        buf[sizeof(buf) / 2] != (uint8_t)(expected ^ (uint8_t)depth) ||
        buf[sizeof(buf) - 1] != (uint8_t)(expected + 7u))
        return -1;
    return 0;
}

static int real_main(int argc, char **argv, char **envp)
{
    (void)envp;

    if (argc >= 2 && strcmp(argv[1], "--worker-flock") == 0)
        return run_worker_flock();
    if (argc >= 3 && strcmp(argv[1], "--worker-vfs") == 0)
        return run_worker_vfs(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "--worker-mprotect-segv") == 0)
        return run_worker_mprotect_segv();
    if (argc >= 2 && strcmp(argv[1], "--worker-exit0") == 0)
        return run_worker_exit0();
    if (argc >= 2 && strcmp(argv[1], "--worker-block-sigkill") == 0)
        return run_worker_block_sigkill();
    if (argc >= 3 && strcmp(argv[1], "--worker-sig-ping") == 0)
        return run_worker_sig_ping(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "--worker-pipe-echo") == 0)
        return run_worker_pipe_echo();
    if (argc >= 2 && strcmp(argv[1], "--worker-delayed-write") == 0)
        return run_worker_delayed_write();
    if (argc >= 2 && strcmp(argv[1], "--worker-sigpipe-dfl") == 0)
        return run_worker_sigpipe_dfl();

    int passed = 0;
    int total = 32;

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

    /* Test 9: Zero-length read/write and user pointer validation. */
    {
        const char *zpath = "/tmp/test_zlen.tmp";
        int zfd = open(zpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (zfd < 0) {
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "open temp file failed");
            return 32;
        }

        char zbuf[4] = {0};
        if (write(zfd, zbuf, 0) != 0 || read(zfd, zbuf, 0) != 0) {
            close(zfd);
            unlink(zpath);
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "zero-len read/write on file failed");
            return 33;
        }

        int zp[2];
        if (pipe(zp) != 0) {
            close(zfd);
            unlink(zpath);
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "pipe creation failed");
            return 34;
        }
        if (read(zp[0], zbuf, 0) != 0 || write(zp[1], zbuf, 0) != 0) {
            close(zp[0]);
            close(zp[1]);
            close(zfd);
            unlink(zpath);
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "zero-len read/write on pipe failed");
            return 35;
        }
        close(zp[0]);
        close(zp[1]);

        errno = 0;
        if (read(-1, zbuf, 0) != -1 || errno != EBADF) {
            close(zfd);
            unlink(zpath);
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "read(-1, buf, 0) did not fail EBADF");
            return 36;
        }
        errno = 0;
        if (write(-1, zbuf, 0) != -1 || errno != EBADF) {
            close(zfd);
            unlink(zpath);
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "write(-1, buf, 0) did not fail EBADF");
            return 37;
        }

        void *kptr = (void *)(uintptr_t)0xFFFFFFFF80000000ULL;
        void *unmapped_ptr = (void *)(uintptr_t)0x0000000050000000ULL;
        int ptr_ok = 1;

        errno = 0;
        if (read(zfd, kptr, 4) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (write(zfd, kptr, 4) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (read(zfd, kptr, 0) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (write(zfd, kptr, 0) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (read(zfd, NULL, 0) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (write(zfd, NULL, 0) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (read(zfd, unmapped_ptr, 4) != -1 || errno != EFAULT) ptr_ok = 0;
        errno = 0;
        if (write(zfd, unmapped_ptr, 4) != -1 || errno != EFAULT) ptr_ok = 0;

        close(zfd);
        unlink(zpath);

        if (!ptr_ok) {
            TSK_TEST_FAIL("syscalls", "zero_len_user_ptr", "bad user pointer did not return EFAULT");
            return 38;
        }
        TSK_TEST_PASS("syscalls", "zero_len_user_ptr");
        passed++;
    }

    /* Test 10: Symlink, readlink, lstat, O_NOFOLLOW, and ELOOP protection. */
    {
        const char *tgt = "/tmp/sym_target.txt";
        const char *lnk = "/tmp/sym_link.txt";
        int fd = open(tgt, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write(fd, "hello", 5) != 5) {
            if (fd >= 0) close(fd);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "target create failed");
            return 39;
        }
        close(fd);

        if (symlink(tgt, lnk) != 0) {
            unlink(tgt);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "symlink failed");
            return 40;
        }

        struct stat lst, st;
        if (lstat(lnk, &lst) != 0 || !S_ISLNK(lst.st_mode)) {
            unlink(lnk);
            unlink(tgt);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "lstat did not return S_IFLNK");
            return 41;
        }
        if (stat(lnk, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 5) {
            unlink(lnk);
            unlink(tgt);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "stat did not follow symlink");
            return 42;
        }

        char rbuf[64];
        memset(rbuf, 0, sizeof(rbuf));
        ssize_t rl = readlink(lnk, rbuf, sizeof(rbuf) - 1);
        if (rl != (ssize_t)strlen(tgt) || strcmp(rbuf, tgt) != 0) {
            unlink(lnk);
            unlink(tgt);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "readlink mismatch");
            return 43;
        }

        errno = 0;
        if (open(lnk, O_RDONLY | O_NOFOLLOW) != -1 || errno != ELOOP) {
            unlink(lnk);
            unlink(tgt);
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "O_NOFOLLOW did not return ELOOP");
            return 44;
        }

        unlink(lnk);
        unlink(tgt);

        if (symlink("/tmp/loop_b", "/tmp/loop_a") != 0 ||
            symlink("/tmp/loop_a", "/tmp/loop_b") != 0) {
            unlink("/tmp/loop_a");
            unlink("/tmp/loop_b");
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "loop symlink creation failed");
            return 45;
        }

        errno = 0;
        if (stat("/tmp/loop_a", &st) != -1 || errno != ELOOP) {
            unlink("/tmp/loop_a");
            unlink("/tmp/loop_b");
            TSK_TEST_FAIL("syscalls", "symlink_eloop", "symlink loop did not return ELOOP");
            return 46;
        }

        unlink("/tmp/loop_a");
        unlink("/tmp/loop_b");
        TSK_TEST_PASS("syscalls", "symlink_eloop");
        passed++;
    }

    /* Test 11: POSIX metadata (umask, chmod, fchmod, chown, fchown, access, truncate, ftruncate). */
    {
        const char *mpath = "/tmp/meta_test.txt";
        mode_t old_um = umask(0077);
        int fd = open(mpath, O_RDWR | O_CREAT | O_TRUNC, 0666);
        umask(old_um);
        if (fd < 0) {
            TSK_TEST_FAIL("syscalls", "posix_metadata", "open failed");
            return 47;
        }
        struct stat st;
        if (fstat(fd, &st) != 0 || (st.st_mode & 0777) != 0600) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "umask 0077 not applied");
            return 48;
        }

        if (chmod(mpath, 0755 | S_ISUID) != 0 || stat(mpath, &st) != 0 ||
            (st.st_mode & 07777) != (0755 | S_ISUID)) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "chmod failed");
            return 49;
        }

        if (fchmod(fd, 0640) != 0 || fstat(fd, &st) != 0 || (st.st_mode & 0777) != 0640) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "fchmod failed");
            return 50;
        }

        if (chown(mpath, 1000, 1001) != 0 || stat(mpath, &st) != 0 ||
            st.st_uid != 1000 || st.st_gid != 1001) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "chown failed");
            return 51;
        }

        if (fchown(fd, 0, 0) != 0 || fstat(fd, &st) != 0 ||
            st.st_uid != 0 || st.st_gid != 0) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "fchown failed");
            return 52;
        }

        if (access(mpath, F_OK | R_OK | W_OK) != 0) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "access failed");
            return 53;
        }

        if (write(fd, "12345678", 8) != 8 || ftruncate(fd, 3) != 0 ||
            fstat(fd, &st) != 0 || st.st_size != 3) {
            close(fd);
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "ftruncate shrink failed");
            return 54;
        }

        close(fd);
        if (truncate(mpath, 10) != 0 || stat(mpath, &st) != 0 || st.st_size != 10) {
            unlink(mpath);
            TSK_TEST_FAIL("syscalls", "posix_metadata", "truncate grow failed");
            return 55;
        }

        unlink(mpath);
        TSK_TEST_PASS("syscalls", "posix_metadata");
        passed++;
    }

    /* Test 12: getdents64 and opendir/readdir/closedir. */
    {
        mkdir("/tmp/dtest", 0755);
        int fd1 = open("/tmp/dtest/regfile", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd1 >= 0) close(fd1);
        mkdir("/tmp/dtest/subdir", 0755);
        symlink("/tmp/dtest/regfile", "/tmp/dtest/symfile");

        DIR *d = opendir("/tmp/dtest");
        if (!d) {
            TSK_TEST_FAIL("syscalls", "getdents64_dirent", "opendir failed");
            return 56;
        }
        int saw_dot = 0, saw_dotdot = 0, saw_reg = 0, saw_sub = 0, saw_sym = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 && de->d_type == DT_DIR) saw_dot = 1;
            else if (strcmp(de->d_name, "..") == 0 && de->d_type == DT_DIR) saw_dotdot = 1;
            else if (strcmp(de->d_name, "regfile") == 0 && de->d_type == DT_REG) saw_reg = 1;
            else if (strcmp(de->d_name, "subdir") == 0 && de->d_type == DT_DIR) saw_sub = 1;
            else if (strcmp(de->d_name, "symfile") == 0 && de->d_type == DT_LNK) saw_sym = 1;
        }
        closedir(d);

        unlink("/tmp/dtest/symfile");
        unlink("/tmp/dtest/regfile");
        rmdir("/tmp/dtest/subdir");
        rmdir("/tmp/dtest");

        if (!saw_dot || !saw_dotdot || !saw_reg || !saw_sub || !saw_sym) {
            TSK_TEST_FAIL("syscalls", "getdents64_dirent", "missing dirent entries or wrong d_type");
            return 57;
        }
        TSK_TEST_PASS("syscalls", "getdents64_dirent");
        passed++;
    }

    /* Test 13: >128 open FDs & >32 pipes/sockets decoupling. */
    {
        int fds[180];
        int opened = 0;
        for (int i = 0; i < 180; i++) {
            fds[i] = open("/dev/null", O_RDWR);
            if (fds[i] < 0)
                break;
            opened++;
        }
        for (int i = 0; i < opened; i++)
            close(fds[i]);

        if (opened < 150) {
            TSK_TEST_FAIL("syscalls", "fd_scaling_decouple", "could not open >=150 FDs");
            return 58;
        }

        int pipes[40][2];
        int p_opened = 0;
        for (int i = 0; i < 40; i++) {
            if (pipe(pipes[i]) != 0)
                break;
            p_opened++;
        }
        int p_ok = (p_opened == 40);
        if (p_ok) {
            char ch = 'X';
            if (write(pipes[39][1], &ch, 1) != 1 || read(pipes[39][0], &ch, 1) != 1 || ch != 'X')
                p_ok = 0;
        }
        for (int i = 0; i < p_opened; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        if (!p_ok) {
            TSK_TEST_FAIL("syscalls", "fd_scaling_decouple", "could not open 40 concurrent pipes");
            return 59;
        }

        /* Also test >32 AF_UNIX stream connections. */
        int srv = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un ua;
        memset(&ua, 0, sizeof(ua));
        ua.sun_family = AF_UNIX;
        strcpy(ua.sun_path, "/tmp/scale_sock");
        int s_ok = (srv >= 0 && bind(srv, (struct sockaddr *)&ua, sizeof(ua)) == 0 && listen(srv, 16) == 0);
        int cli_fds[36];
        int acc_fds[36];
        int s_cnt = 0;
        if (s_ok) {
            for (int i = 0; i < 36; i++) {
                cli_fds[i] = socket(AF_UNIX, SOCK_STREAM, 0);
                if (cli_fds[i] < 0 || connect(cli_fds[i], (struct sockaddr *)&ua, sizeof(ua)) != 0)
                    break;
                acc_fds[i] = accept(srv, NULL, NULL);
                if (acc_fds[i] < 0) {
                    close(cli_fds[i]);
                    break;
                }
                s_cnt++;
            }
        }
        if (s_cnt < 36)
            s_ok = 0;
        for (int i = 0; i < s_cnt; i++) {
            close(cli_fds[i]);
            close(acc_fds[i]);
        }
        if (srv >= 0)
            close(srv);
        if (!s_ok) {
            TSK_TEST_FAIL("syscalls", "fd_scaling_decouple", "could not open 36 concurrent unix sockets");
            return 60;
        }

        TSK_TEST_PASS("syscalls", "fd_scaling_decouple");
        passed++;
    }

    /* Test 14: Cross-process per-inode flock blocking & conflict resolution. */
    {
        int fd = open("/tmp/flock_test.dat", O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || write(fd, "INIT____", 8) != 8) {
            if (fd >= 0) close(fd);
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "init file failed");
            return 61;
        }
        if (flock(fd, LOCK_EX) != 0) {
            close(fd);
            unlink("/tmp/flock_test.dat");
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "parent LOCK_EX failed");
            return 62;
        }

        int dup_fd = dup(fd);
        unlink("/tmp/flock_ready");
        char *wargs[] = { "/bin/TSYSC.ELF", "--worker-flock", NULL };
        pid_t cpid = spawn("/bin/TSYSC.ELF", wargs, NULL);
        if (cpid < 0) {
            close(dup_fd);
            close(fd);
            unlink("/tmp/flock_test.dat");
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "spawn worker failed");
            return 63;
        }

        for (int wi = 0; wi < 400; wi++) {
            if (access("/tmp/flock_ready", F_OK) == 0)
                break;
            usleep(5000);
        }
        usleep(20000);
        /* Closing dup_fd must NOT release the flock held by the open file description! */
        close(dup_fd);
        usleep(20000);

        char rbuf[16];
        memset(rbuf, 0, sizeof(rbuf));
        lseek(fd, 0, SEEK_SET);
        read(fd, rbuf, 8);
        if (memcmp(rbuf, "INIT____", 8) != 0) {
            close(fd);
            wait_child_ok(cpid);
            unlink("/tmp/flock_ready");
            unlink("/tmp/flock_test.dat");
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "child wrote before final close/unlock");
            return 64;
        }

        /* Final close releases the flock so blocked child acquires LOCK_EX and writes CHILD_OK. */
        close(fd);
        if (wait_child_ok(cpid) != 0) {
            unlink("/tmp/flock_ready");
            unlink("/tmp/flock_test.dat");
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "worker failed");
            return 65;
        }
        unlink("/tmp/flock_ready");

        fd = open("/tmp/flock_test.dat", O_RDONLY);
        memset(rbuf, 0, sizeof(rbuf));
        if (fd < 0 || read(fd, rbuf, 8) != 8 || memcmp(rbuf, "CHILD_OK", 8) != 0) {
            if (fd >= 0) close(fd);
            unlink("/tmp/flock_test.dat");
            TSK_TEST_FAIL("syscalls", "flock_cross_process", "child update missing");
            return 66;
        }
        close(fd);
        unlink("/tmp/flock_test.dat");
        TSK_TEST_PASS("syscalls", "flock_cross_process");
        passed++;
    }

    /* Test 15: Concurrent open/read/write/unlink/rename + mount/umount2. */
    {
        mkdir("/tmp/mnt_test", 0755);
        if (mount("none", "/tmp/mnt_test", "tmpfs", 0, NULL) != 0) {
            rmdir("/tmp/mnt_test");
            TSK_TEST_FAIL("syscalls", "concurrent_vfs_mount", "mount tmpfs failed");
            return 67;
        }

        char *w1[] = { "/bin/TSYSC.ELF", "--worker-vfs", "w1", NULL };
        char *w2[] = { "/bin/TSYSC.ELF", "--worker-vfs", "w2", NULL };
        pid_t p1 = spawn("/bin/TSYSC.ELF", w1, NULL);
        pid_t p2 = spawn("/bin/TSYSC.ELF", w2, NULL);

        int mfd = open("/tmp/mnt_test/hello", O_RDWR | O_CREAT | O_TRUNC, 0644);
        int busy_ok = 0;
        if (mfd >= 0) {
            write(mfd, "MNT", 3);
            errno = 0;
            if (umount2("/tmp/mnt_test", 0) == -1 && errno == EBUSY)
                busy_ok = 1;
            close(mfd);
        }

        int u_ok = (busy_ok && umount2("/tmp/mnt_test", 0) == 0);
        int c1 = (p1 > 0) ? wait_child_ok(p1) : -1;
        int c2 = (p2 > 0) ? wait_child_ok(p2) : -1;
        rmdir("/tmp/mnt_test");

        if (!u_ok || c1 != 0 || c2 != 0) {
            TSK_TEST_FAIL("syscalls", "concurrent_vfs_mount", "concurrent vfs or mount/umount2 failed");
            return 68;
        }
        TSK_TEST_PASS("syscalls", "concurrent_vfs_mount");
        passed++;
    }

    /* Test 16: Negative user pointer tests across new syscalls (-EFAULT). */
    {
        void *kptr = (void *)(uintptr_t)0xFFFFFFFF80000000ULL;
        void *unmapped_ptr = (void *)(uintptr_t)0x0000000050000000ULL;
        char sbuf[16];
        struct stat st;
        int ok = 1;

        errno = 0; if (stat((const char *)kptr, &st) != -1 || errno != EFAULT) ok = 0;
        if (__syscall2(SYS_stat, (int64_t)(uintptr_t)"/tmp", (int64_t)(uintptr_t)kptr) != -EFAULT) ok = 0;
        errno = 0; if (lstat((const char *)unmapped_ptr, &st) != -1 || errno != EFAULT) ok = 0;
        if (__syscall2(SYS_lstat, (int64_t)(uintptr_t)"/tmp", (int64_t)(uintptr_t)unmapped_ptr) != -EFAULT) ok = 0;
        if (__syscall2(SYS_fstat, 0, (int64_t)(uintptr_t)kptr) != -EFAULT) ok = 0;
        errno = 0; if (readlink((const char *)kptr, sbuf, sizeof(sbuf)) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (readlink("/tmp", (char *)kptr, sizeof(sbuf)) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (readlink("/tmp", (char *)unmapped_ptr, 8) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (getdents64(0, kptr, 64) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (getdents64(0, unmapped_ptr, 64) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (chmod((const char *)kptr, 0644) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (chown((const char *)unmapped_ptr, 0, 0) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (symlink((const char *)kptr, "/tmp/x") != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (access((const char *)kptr, F_OK) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (truncate((const char *)kptr, 0) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (mount("none", (const char *)kptr, "tmpfs", 0, NULL) != -1 || errno != EFAULT) ok = 0;
        errno = 0; if (umount2((const char *)kptr, 0) != -1 || errno != EFAULT) ok = 0;

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "negative_user_ptrs", "syscall accepted invalid user pointer");
            return 69;
        }
        TSK_TEST_PASS("syscalls", "negative_user_ptrs");
        passed++;
    }

    /* Test 17: Page cache procfs telemetry check. */
    {
        int pfd_pc = open("/proc/pagecache", O_RDONLY);
        char pcbuf[512];
        ssize_t n = 0;
        if (pfd_pc >= 0) {
            n = read(pfd_pc, pcbuf, sizeof(pcbuf) - 1);
            close(pfd_pc);
        }
        if (n <= 0) {
            TSK_TEST_FAIL("syscalls", "pagecache_proc", "failed to read /proc/pagecache");
            return 70;
        }
        pcbuf[n] = '\0';
        if (!strstr(pcbuf, "max_pages ") || !strstr(pcbuf, "evictions ")) {
            TSK_TEST_FAIL("syscalls", "pagecache_proc", "missing fields in /proc/pagecache");
            return 71;
        }
        TSK_TEST_PASS("syscalls", "pagecache_proc");
        passed++;
    }

    /* Test 18: mprotect, MAP_FIXED, split/merge stress, and PROT_READ SIGSEGV isolation. */
    {
        int ok = 1;
        errno = 0;
        if (mprotect((void *)0x80000123ULL, 4096, PROT_READ) != -1 || errno != EINVAL)
            ok = 0;
        errno = 0;
        if (mprotect((void *)0x50000000ULL, 4096, PROT_READ) != -1 || errno != ENOMEM)
            ok = 0;
        errno = 0;
        if (mprotect((void *)0xFFFFFFFF80000000ULL, 4096, PROT_READ) != -1 || errno != ENOMEM)
            ok = 0;
        errno = 0;
        if (mmap((void *)0x80000123ULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED ||
            errno != EINVAL)
            ok = 0;
        errno = 0;
        if (mmap((void *)0xFFFFFFFF80000000ULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED ||
            errno != ENOMEM)
            ok = 0;

        /* MAP_FIXED replacement + rapid split/merge churn. */
        for (int iter = 0; iter < 50 && ok; iter++) {
            uint8_t *reg = (uint8_t *)mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (reg == MAP_FAILED) {
                ok = 0;
                break;
            }
            reg[0] = 0xAA;
            reg[4096] = 0xBB;
            reg[2 * 4096] = 0xCC;
            reg[3 * 4096] = 0xDD;

            if (mprotect(reg + 4096, 2 * 4096, PROT_READ) != 0) {
                ok = 0;
                munmap(reg, 4 * 4096);
                break;
            }
            if (reg[4096] != 0xBB || reg[2 * 4096] != 0xCC)
                ok = 0;
            if (mprotect(reg + 4096, 2 * 4096, PROT_READ | PROT_WRITE) != 0) {
                ok = 0;
                munmap(reg, 4 * 4096);
                break;
            }
            reg[4096] = 0xEE;

            void *rep = mmap(reg + 4096, 4096, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (rep != (void *)(reg + 4096) || reg[0] != 0xAA || reg[4096] != 0)
                ok = 0;

            if (munmap(reg, 4 * 4096) != 0)
                ok = 0;
        }

        if (ok) {
            char *wargs[] = { "/bin/TSYSC.ELF", "--worker-mprotect-segv", NULL };
            pid_t cpid = spawn("/bin/TSYSC.ELF", wargs, NULL);
            if (cpid < 0) {
                ok = 0;
            } else {
                int st = 0;
                int waited = 0;
                for (int i = 0; i < 1000; i++) {
                    if (waitpid(cpid, &st, WNOHANG) == cpid) {
                        waited = 1;
                        break;
                    }
                    usleep(5000);
                }
                if (!waited || !WIFSIGNALED(st) || WTERMSIG(st) != SIGSEGV)
                    ok = 0;
            }
        }

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "mprotect_map_fixed", "mprotect/MAP_FIXED/SIGSEGV check failed");
            return 72;
        }
        TSK_TEST_PASS("syscalls", "mprotect_map_fixed");
        passed++;
    }

    /* Test 19: brk / sbrk growth, shrink, and negative bounds checks. */
    {
        int ok = 1;
        void *b0 = sbrk(0);
        if (!b0 || b0 == (void *)-1)
            ok = 0;

        void *b1 = sbrk(8192);
        if (b1 != b0 || sbrk(0) != (void *)((char *)b0 + 8192))
            ok = 0;
        if (ok) {
            volatile uint32_t *w = (volatile uint32_t *)b1;
            w[0] = 0xCAFE1234u;
            w[2047] = 0xDEAD5678u;
            if (w[0] != 0xCAFE1234u || w[2047] != 0xDEAD5678u)
                ok = 0;
        }
        if (ok && (brk(b0) != 0 || sbrk(0) != b0))
            ok = 0;

        errno = 0;
        if (brk((void *)0x1000ULL) != -1 || errno != ENOMEM)
            ok = 0;
        errno = 0;
        if (brk((void *)0xFFFFFFFF80000000ULL) != -1 || errno != ENOMEM)
            ok = 0;

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "brk_sbrk_heap", "brk/sbrk test failed");
            return 73;
        }
        TSK_TEST_PASS("syscalls", "brk_sbrk_heap");
        passed++;
    }

    /* Test 20: 8 MiB auto-growing stack fault test (> 3 MiB depth). */
    {
        if (stack_autogrow_probe(14, 0x5Au) != 0) {
            TSK_TEST_FAIL("syscalls", "stack_autogrow_8mib", "deep stack fault failed");
            return 74;
        }
        TSK_TEST_PASS("syscalls", "stack_autogrow_8mib");
        passed++;
    }

    /* Test 21: /sys/memory breakdown keys check. */
    {
        int mfd = open("/sys/memory", O_RDONLY);
        char mbuf[1024];
        ssize_t mn = 0;
        if (mfd >= 0) {
            mn = read(mfd, mbuf, sizeof(mbuf) - 1);
            close(mfd);
        }
        if (mn <= 0) {
            TSK_TEST_FAIL("syscalls", "sysfs_memory_breakdown", "failed to read /sys/memory");
            return 75;
        }
        mbuf[mn] = '\0';
        if (!strstr(mbuf, "pmm_total_pages:") ||
            !strstr(mbuf, "pmm_dma_free_pages:") ||
            !strstr(mbuf, "pmm_dma32_free_pages:") ||
            !strstr(mbuf, "pmm_normal_free_pages:") ||
            !strstr(mbuf, "pagecache_pages:") ||
            !strstr(mbuf, "kernel_stack_pages:") ||
            !strstr(mbuf, "process_anon_pages:") ||
            !strstr(mbuf, "process_stack_pages:") ||
            !strstr(mbuf, "process_heap_pages:")) {
            TSK_TEST_FAIL("syscalls", "sysfs_memory_breakdown", "missing fields in /sys/memory");
            return 76;
        }
        TSK_TEST_PASS("syscalls", "sysfs_memory_breakdown");
        passed++;
    }

    /* Test 22: Demand-paged file mmap (close(fd), MAP_PRIVATE COW, MAP_SHARED + msync). */
    {
        const char *mpath = "/tmp/libc_mmap.bin";
        int fd = open(mpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
        int ok = 1;
        uint8_t seed[8192];
        for (size_t i = 0; i < sizeof(seed); i++)
            seed[i] = (uint8_t)((i * 29u + 7u) & 0xFFu);
        if (fd < 0 || write(fd, seed, sizeof(seed)) != (ssize_t)sizeof(seed)) {
            if (fd >= 0) close(fd);
            ok = 0;
        } else {
            close(fd);
        }

        if (ok) {
            fd = open(mpath, O_RDONLY);
            volatile uint8_t *pm = (volatile uint8_t *)mmap(
                NULL, sizeof(seed), PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
            if (fd >= 0)
                close(fd);
            if ((void *)pm == MAP_FAILED) {
                ok = 0;
            } else {
                for (size_t i = 0; i < sizeof(seed); i++) {
                    if (pm[i] != seed[i]) {
                        ok = 0;
                        break;
                    }
                }
                pm[0] = 0x11u;
                pm[4096] = 0x22u;
                if (munmap((void *)(uintptr_t)pm, sizeof(seed)) != 0)
                    ok = 0;
            }
        }

        if (ok) {
            fd = open(mpath, O_RDWR);
            volatile uint8_t *sm = (volatile uint8_t *)mmap(
                NULL, sizeof(seed), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (fd >= 0)
                close(fd);
            if ((void *)sm == MAP_FAILED) {
                ok = 0;
            } else {
                if (sm[0] != seed[0] || sm[4096] != seed[4096])
                    ok = 0;
                sm[5] = 0x99u;
                sm[4100] = 0x77u;
                if (msync((void *)(uintptr_t)sm, sizeof(seed), MS_SYNC) != 0)
                    ok = 0;
                if (munmap((void *)(uintptr_t)sm, sizeof(seed)) != 0)
                    ok = 0;
            }
        }

        if (ok) {
            uint8_t rb[8192];
            fd = open(mpath, O_RDONLY);
            if (fd < 0 || read(fd, rb, sizeof(rb)) != (ssize_t)sizeof(rb) ||
                rb[5] != 0x99u || rb[4100] != 0x77u) {
                ok = 0;
            }
            if (fd >= 0)
                close(fd);
        }
        unlink(mpath);
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "file_mmap_cow_shared_msync", "file mmap/msync check failed");
            return 77;
        }
        TSK_TEST_PASS("syscalls", "file_mmap_cow_shared_msync");
        passed++;
    }

    /* Test 23: Negative mmap/msync errno validation. */
    {
        int ok = 1;
        errno = 0;
        if (msync((void *)0x80000123ULL, 4096, MS_SYNC) != -1 || errno != EINVAL)
            ok = 0;
        errno = 0;
        if (msync((void *)0x80000000ULL, 4096, MS_ASYNC | MS_SYNC) != -1 || errno != EINVAL)
            ok = 0;
        errno = 0;
        if (msync((void *)0x50000000ULL, 4096, MS_SYNC) != -1 || errno != ENOMEM)
            ok = 0;
        errno = 0;
        if (mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 999, 0) != MAP_FAILED || errno != EBADF)
            ok = 0;
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "mmap_msync_negative", "negative errno mismatch");
            return 78;
        }
        TSK_TEST_PASS("syscalls", "mmap_msync_negative");
        passed++;
    }

    /* Test 24: COW fork parent/child isolation across stack, heap, anon mmap, and file mmap. */
    {
        int ok = 1;
        volatile uint32_t *heap_buf = (volatile uint32_t *)malloc(8192);
        volatile uint8_t *anon = (volatile uint8_t *)mmap(
            NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const char *cpath = "/tmp/cow_priv.dat";
        int pfd = open(cpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
        volatile char *pmap = MAP_FAILED;
        if (!heap_buf || (void *)anon == MAP_FAILED || pfd < 0 ||
            write(pfd, "ORIG_COW_PAGE_01", 16) != 16) {
            ok = 0;
        } else {
            pmap = (volatile char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, pfd, 0);
            close(pfd);
            pfd = -1;
            if ((void *)pmap == MAP_FAILED)
                ok = 0;
        }
        if (pfd >= 0)
            close(pfd);

        volatile uint64_t stack_var = 0x1111222233334444ULL;
        if (ok) {
            heap_buf[0] = 0xA1B2C3D4u;
            heap_buf[1024] = 0x55667788u;
            anon[0] = 0x33u;
            anon[4096] = 0x33u;
            pmap[0] = 'X';

            pid_t cpid = fork();
            if (cpid < 0) {
                ok = 0;
            } else if (cpid == 0) {
                if (heap_buf[0] != 0xA1B2C3D4u || heap_buf[1024] != 0x55667788u ||
                    anon[0] != 0x33u || anon[4096] != 0x33u ||
                    pmap[0] != 'X' || pmap[1] != 'R' ||
                    stack_var != 0x1111222233334444ULL) {
                    _exit(2);
                }
                heap_buf[0] = 0xDEADBEEFu;
                heap_buf[1024] = 0x0BADF00Du;
                anon[0] = 0x77u;
                anon[4096] = 0x88u;
                pmap[0] = 'Y';
                stack_var = 0x9999AAAABBBBCCCCULL;
                if (heap_buf[0] != 0xDEADBEEFu || anon[4096] != 0x88u || pmap[0] != 'Y' ||
                    stack_var != 0x9999AAAABBBBCCCCULL) {
                    _exit(3);
                }
                _exit(19);
            } else {
                int wst = 0;
                if (waitpid(cpid, &wst, 0) != cpid || !WIFEXITED(wst) || WEXITSTATUS(wst) != 19)
                    ok = 0;
                if (heap_buf[0] != 0xA1B2C3D4u || heap_buf[1024] != 0x55667788u ||
                    anon[0] != 0x33u || anon[4096] != 0x33u ||
                    pmap[0] != 'X' || pmap[1] != 'R' ||
                    stack_var != 0x1111222233334444ULL) {
                    ok = 0;
                }
            }
        }
        if ((void *)pmap != MAP_FAILED)
            munmap((void *)(uintptr_t)pmap, 4096);
        if ((void *)anon != MAP_FAILED)
            munmap((void *)(uintptr_t)anon, 8192);
        if (heap_buf)
            free((void *)(uintptr_t)heap_buf);
        unlink(cpath);
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "fork_cow_isolation", "COW fork isolation check failed");
            return 79;
        }
        TSK_TEST_PASS("syscalls", "fork_cow_isolation");
        passed++;
    }

    /* Test 25: fork preserves SHM attachments RW without COW splitting. */
    {
        int ok = 1;
        int sid = shm_create(4096);
        volatile uint32_t *shm_ptr = (sid >= 0) ? (volatile uint32_t *)shm_attach(sid) : NULL;
        if (!shm_ptr) {
            ok = 0;
        } else {
            *shm_ptr = 0x12340001u;
            pid_t cpid = fork();
            if (cpid < 0) {
                ok = 0;
            } else if (cpid == 0) {
                if (*shm_ptr != 0x12340001u)
                    _exit(2);
                *shm_ptr = 0xABCD9876u;
                _exit(0);
            } else {
                int wst = 0;
                if (waitpid(cpid, &wst, 0) != cpid || !WIFEXITED(wst) || WEXITSTATUS(wst) != 0)
                    ok = 0;
                if (*shm_ptr != 0xABCD9876u)
                    ok = 0;
            }
            shm_detach((void *)(uintptr_t)shm_ptr);
        }
        if (sid >= 0)
            shm_destroy(sid);
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "fork_shm_shared", "SHM state across fork was not shared");
            return 80;
        }
        TSK_TEST_PASS("syscalls", "fork_shm_shared");
        passed++;
    }

    /* Test 26: POSIX execve argv, envp, auxv, O_CLOEXEC, F_DUPFD_CLOEXEC, F_GETFD/F_SETFD. */
    {
        int ok = 1;
        int fail_stage = 0;
        int fail_val = 0;
        const char *fpath = "/tmp/exec_fd.txt";
        int fd = open(fpath, O_RDWR | O_CREAT | O_TRUNC, 0644);
        int keep_fd = -1, clo_fd = -1, dup_clo_fd = -1;
        if (fd < 0 || write(fd, "KEEP_ME", 7) != 7) {
            ok = 0;
            fail_stage = 1;
        } else {
            close(fd);
            keep_fd = open(fpath, O_RDONLY);
            clo_fd = open(fpath, O_RDONLY | O_CLOEXEC);
            dup_clo_fd = (keep_fd >= 0) ? fcntl(keep_fd, F_DUPFD_CLOEXEC, 20) : -1;
            if (keep_fd < 0 || clo_fd < 0 || dup_clo_fd < 20) {
                ok = 0;
                fail_stage = 2;
            }
            if (ok && (fcntl(keep_fd, F_GETFD, 0) != 0 ||
                       fcntl(clo_fd, F_GETFD, 0) != FD_CLOEXEC ||
                       fcntl(dup_clo_fd, F_GETFD, 0) != FD_CLOEXEC)) {
                ok = 0;
                fail_stage = 3;
            }
        }
        if (ok) {
            char kbuf[16], cbuf[16], dbuf[16];
            sprintf(kbuf, "%d", keep_fd);
            sprintf(cbuf, "%d", clo_fd);
            sprintf(dbuf, "%d", dup_clo_fd);
            pid_t cpid = fork();
            if (cpid < 0) {
                ok = 0;
                fail_stage = 4;
            } else if (cpid == 0) {
                char *const ex_argv[] = {
                    "/bin/exectest.elf", "--verify-exec", kbuf, cbuf, dbuf, NULL
                };
                char *const ex_envp[] = {
                    "TSK_TEST_ENV=cow_exec_ok", "TSK_EXTRA=1", NULL
                };
                execve("/bin/exectest.elf", ex_argv, ex_envp);
                _exit(99);
            } else {
                int wst = 0;
                if (waitpid(cpid, &wst, 0) != cpid || !WIFEXITED(wst) || WEXITSTATUS(wst) != 33) {
                    ok = 0;
                    fail_stage = 5;
                    fail_val = WIFEXITED(wst) ? WEXITSTATUS(wst) : -wst;
                }
            }
        }
        if (keep_fd >= 0) close(keep_fd);
        if (clo_fd >= 0) close(clo_fd);
        if (dup_clo_fd >= 0) close(dup_clo_fd);
        unlink(fpath);
        if (!ok) {
            printf("[DBG] execve_argv_envp_auxv_cloexec stage=%d val=%d\n", fail_stage, fail_val);
            TSK_TEST_FAIL("syscalls", "execve_argv_envp_auxv_cloexec", "execve argv/envp/auxv/cloexec failed");
            return 81;
        }
        TSK_TEST_PASS("syscalls", "execve_argv_envp_auxv_cloexec");
        passed++;
    }

    /* Test 27: Shebang script execution and -ELOOP depth limit. */
    {
        int ok = 1;
        const char *sp = "/tmp/test_shebang.sh";
        const char *lp = "/tmp/loop_shebang.sh";
        int sfd = open(sp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        int lfd = open(lp, O_WRONLY | O_CREAT | O_TRUNC, 0755);
        const char *sc = "#!/bin/exectest.elf shebang_arg\necho ignored\n";
        const char *lc = "#!/tmp/loop_shebang.sh\n";
        if (sfd < 0 || write(sfd, sc, strlen(sc)) != (ssize_t)strlen(sc) ||
            lfd < 0 || write(lfd, lc, strlen(lc)) != (ssize_t)strlen(lc)) {
            ok = 0;
        }
        if (sfd >= 0) close(sfd);
        if (lfd >= 0) close(lfd);

        if (ok) {
            pid_t cpid = fork();
            if (cpid < 0) {
                ok = 0;
            } else if (cpid == 0) {
                char *const sh_argv[] = { "/tmp/test_shebang.sh", "user_arg", NULL };
                char *const sh_envp[] = { "TSK_SHEBANG_ENV=shebang_ok", NULL };
                execve(sp, sh_argv, sh_envp);
                _exit(99);
            } else {
                int wst = 0;
                if (waitpid(cpid, &wst, 0) != cpid || !WIFEXITED(wst) || WEXITSTATUS(wst) != 55)
                    ok = 0;
            }
        }
        if (ok) {
            char *const lp_argv[] = { "/tmp/loop_shebang.sh", NULL };
            errno = 0;
            if (execve(lp, lp_argv, NULL) != -1 || errno != ELOOP)
                ok = 0;
        }
        unlink(sp);
        unlink(lp);
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "execve_shebang", "shebang exec or ELOOP check failed");
            return 82;
        }
        TSK_TEST_PASS("syscalls", "execve_shebang");
        passed++;
    }

    /* Test 28: PIE ET_DYN and PT_INTERP dynamic-linker execution. */
    {
        int ok = 1;
        pid_t c1 = fork();
        if (c1 < 0) {
            ok = 0;
        } else if (c1 == 0) {
            char *const argv1[] = { "/bin/pietest.elf", NULL };
            execve("/bin/pietest.elf", argv1, NULL);
            _exit(99);
        } else {
            int wst = 0;
            if (waitpid(c1, &wst, 0) != c1 || !WIFEXITED(wst) || WEXITSTATUS(wst) != 42)
                ok = 0;
        }

        if (ok) {
            pid_t c2 = fork();
            if (c2 < 0) {
                ok = 0;
            } else if (c2 == 0) {
                char *const argv2[] = { "/bin/interptest.elf", NULL };
                execve("/bin/interptest.elf", argv2, NULL);
                _exit(99);
            } else {
                int wst = 0;
                if (waitpid(c2, &wst, 0) != c2 || !WIFEXITED(wst) || WEXITSTATUS(wst) != 77)
                    ok = 0;
            }
        }
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "execve_pie_interp", "PIE ET_DYN or PT_INTERP execution failed");
            return 83;
        }
        TSK_TEST_PASS("syscalls", "execve_pie_interp");
        passed++;
    }

    /* Test 29: Negative execve error paths leave caller intact. */
    {
        int ok = 1;
        volatile uint64_t guard_val = 0x55AA123499887766ULL;
        const char *npath = "/tmp/not_elf.bin";
        int fd = open(npath, O_RDWR | O_CREAT | O_TRUNC, 0755);
        if (fd < 0 || write(fd, "NOT_ELF_DATA", 12) != 12) {
            ok = 0;
        } else {
            char *const bad_argv[] = { "bad", NULL };
            errno = 0;
            if (execve("/nonexistent_exec_target", bad_argv, NULL) != -1 || errno != ENOENT)
                ok = 0;
            errno = 0;
            if (execve("/bin", bad_argv, NULL) != -1 || errno != EISDIR)
                ok = 0;
            errno = 0;
            if (execve(npath, bad_argv, NULL) != -1 || errno != ENOEXEC)
                ok = 0;
            errno = 0;
            if (execve((const char *)(uintptr_t)0x1234, bad_argv, NULL) != -1 || errno != EFAULT)
                ok = 0;
            char chk[16];
            memset(chk, 0, sizeof(chk));
            if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, chk, 12) != 12 ||
                memcmp(chk, "NOT_ELF_DATA", 12) != 0)
                ok = 0;
        }
        if (fd >= 0) close(fd);
        unlink(npath);
        if (guard_val != 0x55AA123499887766ULL)
            ok = 0;
        if (!ok) {
            TSK_TEST_FAIL("syscalls", "execve_negative_intact", "negative execve error or caller state check failed");
            return 84;
        }
        TSK_TEST_PASS("syscalls", "execve_negative_intact");
        passed++;
    }

    /* Test 30: Real signal trampolines (rt_sigreturn), Red Zone, GPRs, XMM, and masks. */
    {
        int ok = 1;
        g_sigusr1_seen = 0;
        g_sigusr1_signo = 0;
        g_sigusr1_masked_in_handler = 0;
        g_sigusr2_seen = 0;
        g_sigint_seen = 0;

        struct sigaction act, oldact;
        memset(&act, 0, sizeof(act));
        act.sa_sigaction = sig_usr1_info_handler;
        act.sa_flags = SA_SIGINFO;
        sigemptyset(&act.sa_mask);
        if (sigaction(SIGUSR1, &act, &oldact) != 0 || oldact.sa_handler != SIG_DFL)
            ok = 0;

        /* Preserve GPRs, XMM0/XMM1, and 128-byte Red Zone across syscall-exit delivery + rt_sigreturn. */
        if (ok) {
            uint64_t out_xmm0 = 0, out_xmm1 = 0, out_rz1 = 0, out_rz2 = 0;
            uint64_t out_rbx = 0, out_r12 = 0;
            uint64_t exp_xmm0 = 0x123456789ABCDEF0ULL;
            uint64_t exp_xmm1 = 0x0FEDCBA987654321ULL;
            uint64_t exp_rz1 = 0xAAAABBBBCCCCDDDDULL;
            uint64_t exp_rz2 = 0x5555666677778888ULL;
            pid_t self_pid = getpid();

            __asm__ volatile (
                "pushq %%rbx\n\t"
                "pushq %%r12\n\t"
                "movq %[in_rz1], -16(%%rsp)\n\t"
                "movq %[in_rz2], -128(%%rsp)\n\t"
                "movq %[in_x0], %%xmm0\n\t"
                "movq %[in_x1], %%xmm1\n\t"
                "movq $0xCAFEBABE11223344, %%rbx\n\t"
                "movq $0x778899AABBCCDDEE, %%r12\n\t"
                "movq $62, %%rax\n\t"
                "movq %[pid], %%rdi\n\t"
                "movq $10, %%rsi\n\t"
                "syscall\n\t"
                "movq %%rbx, %[out_rbx]\n\t"
                "movq %%r12, %[out_r12]\n\t"
                "movq -16(%%rsp), %[out_rz1]\n\t"
                "movq -128(%%rsp), %[out_rz2]\n\t"
                "popq %%r12\n\t"
                "popq %%rbx\n\t"
                "movq %%xmm0, %[out_x0]\n\t"
                "movq %%xmm1, %[out_x1]\n\t"
                : [out_x0] "=r"(out_xmm0),
                  [out_x1] "=r"(out_xmm1),
                  [out_rz1] "=r"(out_rz1),
                  [out_rz2] "=r"(out_rz2),
                  [out_rbx] "=r"(out_rbx),
                  [out_r12] "=r"(out_r12)
                : [in_x0] "r"(exp_xmm0),
                  [in_x1] "r"(exp_xmm1),
                  [in_rz1] "r"(exp_rz1),
                  [in_rz2] "r"(exp_rz2),
                  [pid] "r"((uint64_t)self_pid)
                : "rax", "rcx", "rdi", "rsi", "r11", "xmm0", "xmm1", "memory"
            );

            if (g_sigusr1_seen != 1 || g_sigusr1_signo != SIGUSR1 ||
                !g_sigusr1_masked_in_handler ||
                out_xmm0 != exp_xmm0 || out_xmm1 != exp_xmm1 ||
                out_rz1 != exp_rz1 || out_rz2 != exp_rz2 ||
                out_rbx != 0xCAFEBABE11223344ULL ||
                out_r12 != 0x778899AABBCCDDEEULL) {
                ok = 0;
            }
        }

        /* Async IRQ-exit signal delivery while spinning in userland loop with live XMM/RCX/R11. */
        if (ok) {
            char pidbuf[32];
            snprintf(pidbuf, sizeof(pidbuf), "%d", (int)getpid());
            char *pargs[] = { "/bin/TSYSC.ELF", "--worker-sig-ping", pidbuf, NULL };
            pid_t pinger = spawn("/bin/TSYSC.ELF", pargs, NULL);
            if (pinger < 0) {
                ok = 0;
            } else {
                uint64_t loop_xmm2 = 0, loop_rcx = 0, loop_r11 = 0;
                uint64_t exp_xmm2 = 0x13579BDF2468ACE0ULL;
                uint64_t exp_rcx = 0x2222333344445555ULL;
                uint64_t exp_r11 = 0x6666777788889999ULL;
                volatile sig_atomic_t *flag_ptr = &g_sigusr1_seen;

                __asm__ volatile (
                    "movq %[in_x2], %%xmm2\n\t"
                    "movq %[in_rcx], %%rcx\n\t"
                    "movq %[in_r11], %%r11\n\t"
                    "movl $0x2000000, %%edx\n\t"
                    "1:\n\t"
                    "cmpl $2, (%[fp])\n\t"
                    "jge 2f\n\t"
                    "pause\n\t"
                    "decl %%edx\n\t"
                    "jnz 1b\n\t"
                    "2:\n\t"
                    "movq %%xmm2, %[out_x2]\n\t"
                    "movq %%rcx, %[out_rcx]\n\t"
                    "movq %%r11, %[out_r11]\n\t"
                    : [out_x2] "=r"(loop_xmm2),
                      [out_rcx] "=r"(loop_rcx),
                      [out_r11] "=r"(loop_r11)
                    : [in_x2] "r"(exp_xmm2),
                      [in_rcx] "r"(exp_rcx),
                      [in_r11] "r"(exp_r11),
                      [fp] "r"(flag_ptr)
                    : "rcx", "rdx", "r11", "xmm2", "cc", "memory"
                );

                wait_child_ok(pinger);
                if (g_sigusr1_seen != 2 ||
                    loop_xmm2 != exp_xmm2 ||
                    loop_rcx != exp_rcx ||
                    loop_r11 != exp_r11) {
                    ok = 0;
                }
            }
        }

        /* Test sigprocmask blocking, sigpending, SA_NODEFER, and SA_RESETHAND. */
        if (ok) {
            sigset_t bset, oldset, pend;
            sigemptyset(&bset);
            sigaddset(&bset, SIGUSR1);
            if (sigprocmask(SIG_BLOCK, &bset, &oldset) != 0)
                ok = 0;
            raise(SIGUSR1);
            if (g_sigusr1_seen != 2)
                ok = 0;
            if (sigpending(&pend) != 0 || sigismember(&pend, SIGUSR1) != 1)
                ok = 0;
            if (sigprocmask(SIG_UNBLOCK, &bset, NULL) != 0)
                ok = 0;
            if (g_sigusr1_seen != 3)
                ok = 0;

            memset(&act, 0, sizeof(act));
            act.sa_handler = sig_usr2_handler;
            act.sa_flags = SA_NODEFER;
            sigemptyset(&act.sa_mask);
            if (sigaction(SIGUSR2, &act, NULL) != 0)
                ok = 0;
            raise(SIGUSR2);
            if (g_sigusr2_seen != 2)
                ok = 0;

            memset(&act, 0, sizeof(act));
            act.sa_handler = sig_int_handler;
            act.sa_flags = SA_RESETHAND;
            sigemptyset(&act.sa_mask);
            if (sigaction(SIGINT, &act, NULL) != 0)
                ok = 0;
            raise(SIGINT);
            if (g_sigint_seen != 1)
                ok = 0;
            if (sigaction(SIGINT, NULL, &oldact) != 0 || oldact.sa_handler != SIG_DFL)
                ok = 0;
        }

        /* Negative checks: SIGKILL/SIGSTOP cannot be caught, and bad rt_sigreturn returns EFAULT. */
        if (ok) {
            memset(&act, 0, sizeof(act));
            act.sa_handler = sig_int_handler;
            errno = 0;
            if (sigaction(SIGKILL, &act, NULL) != -1 || errno != EINVAL)
                ok = 0;
            errno = 0;
            if (sigaction(SIGSTOP, &act, NULL) != -1 || errno != EINVAL)
                ok = 0;
            int64_t bad_null = 0;
            int64_t bad_kern = 0;
            __asm__ volatile (
                "movq %%rsp, %%r12\n\t"
                "xorq %%rsp, %%rsp\n\t"
                "movq $15, %%rax\n\t"
                "syscall\n\t"
                "movq %%r12, %%rsp\n\t"
                : "=a"(bad_null)
                :
                : "rcx", "r11", "r12", "memory"
            );
            __asm__ volatile (
                "movq %%rsp, %%r12\n\t"
                "movq $0xFFFFFFFF80000000, %%rsp\n\t"
                "movq $15, %%rax\n\t"
                "syscall\n\t"
                "movq %%r12, %%rsp\n\t"
                : "=a"(bad_kern)
                :
                : "rcx", "r11", "r12", "memory"
            );
            if (bad_null != -EFAULT || bad_kern != -EFAULT)
                ok = 0;
        }

        signal(SIGUSR1, SIG_DFL);
        signal(SIGUSR2, SIG_DFL);
        signal(SIGINT, SIG_DFL);

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "signal_trampoline_rt_sigreturn", "signal trampoline or state restore failed");
            return 79;
        }
        TSK_TEST_PASS("syscalls", "signal_trampoline_rt_sigreturn");
        passed++;
    }

    /* Test 25: SIGCHLD delivery on child exit and unblockable SIGKILL. */
    {
        int ok = 1;
        g_sigchld_seen = 0;
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_handler = sig_chld_handler;
        sigemptyset(&act.sa_mask);
        if (sigaction(SIGCHLD, &act, NULL) != 0)
            ok = 0;

        if (ok) {
            char *wargs[] = { "/bin/TSYSC.ELF", "--worker-exit0", NULL };
            pid_t cpid = spawn("/bin/TSYSC.ELF", wargs, NULL);
            if (cpid < 0) {
                ok = 0;
            } else {
                int st = 0;
                pid_t wr;
                do {
                    wr = waitpid(cpid, &st, 0);
                } while (wr == -1 && errno == EINTR);
                if (wr != cpid || !WIFEXITED(st) || WEXITSTATUS(st) != 0 || g_sigchld_seen < 1)
                    ok = 0;
            }
        }
        signal(SIGCHLD, SIG_DFL);

        if (ok) {
            char *kargs[] = { "/bin/TSYSC.ELF", "--worker-block-sigkill", NULL };
            pid_t kpid = spawn("/bin/TSYSC.ELF", kargs, NULL);
            if (kpid < 0) {
                ok = 0;
            } else {
                usleep(20000);
                if (kill(kpid, SIGKILL) != 0)
                    ok = 0;
                int st = 0;
                pid_t wr = waitpid(kpid, &st, 0);
                if (wr != kpid || !WIFSIGNALED(st) || WTERMSIG(st) != SIGKILL)
                    ok = 0;
            }
        }

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "sigchld_sigkill_unblockable", "SIGCHLD or SIGKILL check failed");
            return 80;
        }
        TSK_TEST_PASS("syscalls", "sigchld_sigkill_unblockable");
        passed++;
    }

    /* Test 26: Blocking pipes, poll wakeup, SIGPIPE + EPIPE. */
    {
        int ok = 1;
        int p_p2c[2];
        int p_c2p[2];
        if (pipe(p_p2c) != 0 || pipe(p_c2p) != 0) {
            ok = 0;
        } else {
            struct tsukasa_spawn_request req;
            req.path = "/bin/TSYSC.ELF";
            req.args = "/bin/TSYSC.ELF --worker-pipe-echo";
            req.stdin_fd = p_p2c[0];
            req.stdout_fd = p_c2p[1];
            req.stderr_fd = 2;
            req.tty_id = -1;
            pid_t cpid = spawn_ex(&req);
            close(p_p2c[0]);
            close(p_c2p[1]);
            if (cpid < 0) {
                close(p_p2c[1]);
                close(p_c2p[0]);
                ok = 0;
            } else {
                /* Wait briefly so child genuinely blocks in pipe read(0). */
                usleep(25000);
                const char *msg = "BLOCK_PIPE_OK";
                if (write(p_p2c[1], msg, strlen(msg)) != (ssize_t)strlen(msg))
                    ok = 0;
                close(p_p2c[1]);

                struct pollfd pfd;
                pfd.fd = p_c2p[0];
                pfd.events = POLLIN;
                pfd.revents = 0;
                if (poll(&pfd, 1, 2000) <= 0 || !(pfd.revents & POLLIN))
                    ok = 0;

                char rbuf[32];
                memset(rbuf, 0, sizeof(rbuf));
                ssize_t rn = read(p_c2p[0], rbuf, sizeof(rbuf));
                ssize_t reof = read(p_c2p[0], rbuf, sizeof(rbuf));
                close(p_c2p[0]);

                int st = 0;
                pid_t wr = waitpid(cpid, &st, 0);
                if (rn != (ssize_t)strlen(msg) || strcmp(rbuf, msg) != 0 ||
                    reof != 0 || wr != cpid || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
                    ok = 0;
            }
        }

        /* Poll wakeup while blocked waiting on pipe writer. */
        if (ok) {
            int dp[2];
            if (pipe(dp) != 0) {
                ok = 0;
            } else {
                struct tsukasa_spawn_request req;
                req.path = "/bin/TSYSC.ELF";
                req.args = "/bin/TSYSC.ELF --worker-delayed-write";
                req.stdin_fd = 0;
                req.stdout_fd = dp[1];
                req.stderr_fd = 2;
                req.tty_id = -1;
                pid_t cpid = spawn_ex(&req);
                close(dp[1]);
                if (cpid < 0) {
                    close(dp[0]);
                    ok = 0;
                } else {
                    struct pollfd pfd;
                    pfd.fd = dp[0];
                    pfd.events = POLLIN;
                    pfd.revents = 0;
                    int pr = poll(&pfd, 1, 2000);
                    char wbuf[8] = {0};
                    ssize_t n = read(dp[0], wbuf, sizeof(wbuf));
                    close(dp[0]);
                    int st = 0;
                    pid_t wr = waitpid(cpid, &st, 0);
                    if (pr <= 0 || !(pfd.revents & POLLIN) || n != 4 ||
                        memcmp(wbuf, "WAKE", 4) != 0 || wr != cpid ||
                        !WIFEXITED(st) || WEXITSTATUS(st) != 0)
                        ok = 0;
                }
            }
        }

        /* SIGPIPE raised + EPIPE returned when reader closes, plus SIG_DFL SIGPIPE termination. */
        if (ok) {
            int sp[2];
            g_sigpipe_seen = 0;
            signal(SIGPIPE, sig_pipe_handler);
            if (pipe(sp) != 0) {
                ok = 0;
            } else {
                close(sp[0]);
                errno = 0;
                ssize_t wrc = write(sp[1], "Z", 1);
                close(sp[1]);
                if (wrc != -1 || errno != EPIPE || g_sigpipe_seen != 1)
                    ok = 0;
            }
            signal(SIGPIPE, SIG_DFL);

            char *sargs[] = { "/bin/TSYSC.ELF", "--worker-sigpipe-dfl", NULL };
            pid_t spid = spawn("/bin/TSYSC.ELF", sargs, NULL);
            if (spid < 0) {
                ok = 0;
            } else {
                int st = 0;
                pid_t wr = waitpid(spid, &st, 0);
                if (wr != spid || !WIFSIGNALED(st) || WTERMSIG(st) != SIGPIPE)
                    ok = 0;
            }
        }

        if (!ok) {
            TSK_TEST_FAIL("syscalls", "blocking_pipes_sigpipe_poll", "blocking pipe/poll/SIGPIPE check failed");
            return 81;
        }
        TSK_TEST_PASS("syscalls", "blocking_pipes_sigpipe_poll");
        passed++;
    }

    TSK_TEST_DONE("syscalls", passed, total);
    return 0;
}
