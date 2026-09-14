/*
 * Project Tsukasa — include "../include/app_runtime.h"
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

#include <stddef.h>
#include <stdint.h>
#include "user/include/fcntl.h"
#include "user/include/stdio.h"
#include "user/include/stdlib.h"
#include "user/include/string.h"
#include "user/include/unistd.h"
#include "user/include/signal.h"
#include "user/include/time.h"
#include "user/include/sys/stat.h"
#include "user/include/sys/poll.h"
#include "user/include/sys/ioctl.h"
#include "user/include/sys/input.h"
#include "user/include/sys/socket.h"
#include "user/include/sys/un.h"
#include "user/include/sys/shm.h"
#include "user/include/sys/mman.h"
#include "user/include/app_runtime.h"

static int cmd_abi_test_main(int argc, char **argv)
{
    int fail = 0;
    int fd;
    char buf[32];
    struct stat st;
    time_t now;
    int pipefd[2];
    sigset_t mask = 0;
    sigset_t oldmask = 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    struct pollfd pfd;
    void *fb_map;
    (void)argc;
    (void)argv;

    fd = open("/tmp/abi_smoke.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0 || write(fd, "phase7-abi", 10) != 10 || close(fd) != 0)
        fail = 1;

    fd = open("/tmp/abi_smoke.txt", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    if (fd < 0 || read(fd, buf, 10) != 10 || strcmp(buf, "phase7-abi") != 0)
        fail = 1;
    if (fd >= 0)
        close(fd);

    if (stat("/tmp/abi_smoke.txt", &st) != 0 || st.st_size != 10)
        fail = 1;

    if (pipe(pipefd) != 0)
        fail = 1;
    else {
        if (write(pipefd[1], "ok", 2) != 2)
            fail = 1;
        memset(buf, 0, sizeof(buf));
        if (read(pipefd[0], buf, 2) != 2 || strcmp(buf, "ok") != 0)
            fail = 1;
        close(pipefd[0]);
        close(pipefd[1]);
    }

    mask |= (1ULL << SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) != 0)
        fail = 1;
    if (sigprocmask(SIG_SETMASK, &oldmask, 0) != 0)
        fail = 1;

    now = time(0);
    if (now < 0)
        fail = 1;

    fd = open("/dev/fb0", O_RDWR, 0);
    if (fd < 0)
        fail = 1;
    else {
        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0 ||
            ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0 ||
            vinfo.xres == 0 || vinfo.yres == 0 || finfo.line_length == 0)
            fail = 1;
        pfd.fd = fd;
        pfd.events = POLLIN | POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) <= 0 || (pfd.revents & POLLOUT) == 0)
            fail = 1;
        fb_map = mmap(0, finfo.smem_len > 16 ? 16 : finfo.smem_len,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (fb_map == MAP_FAILED)
            fail = 1;
        else if (munmap(fb_map, finfo.smem_len > 16 ? 16 : finfo.smem_len) != 0)
            fail = 1;
        if (ioctl(fd, KDSETMODE, (void *)KD_GRAPHICS) != 0 ||
            ioctl(fd, KDSETMODE, (void *)KD_TEXT) != 0)
            fail = 1;
        close(fd);
    }

    /* Verify /dev/input/events and /dev/events */
    fd = open("/dev/input/events", O_RDONLY | O_NONBLOCK, 0);
    if (fd < 0) {
        fail = 1;
    } else {
        struct input_event ev;
        if (sizeof(struct input_event) != 24)
            fail = 1;
        /* Non-blocking read on empty queue should return 0 */
        if (read(fd, &ev, sizeof(ev)) != 0)
            fail = 1;
        pfd.fd = fd;
        pfd.events = POLLIN | POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) < 0 || (pfd.revents & POLLOUT) == 0)
            fail = 1;
        close(fd);
    }

    fd = open("/dev/events", O_RDONLY | O_NONBLOCK, 0);
    if (fd < 0) {
        fail = 1;
    } else {
        close(fd);
    }

    /* Verify AF_UNIX Local IPC sockets */
    {
        int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (lfd < 0) {
            fail = 1;
        } else {
            struct sockaddr_un sun;
            sun.sun_family = AF_UNIX;
            strncpy(sun.sun_path, "/tmp/test.sock", sizeof(sun.sun_path));
            if (bind(lfd, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
                fail = 1;
            } else if (listen(lfd, 5) != 0) {
                fail = 1;
            } else {
                struct stat sst;
                if (stat("/tmp/test.sock", &sst) != 0 || !S_ISSOCK(sst.st_mode))
                    fail = 1;

                int cfd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (cfd < 0) {
                    fail = 1;
                } else if (connect(cfd, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
                    fail = 1;
                    close(cfd);
                } else {
                    int afd = accept(lfd);
                    if (afd < 0) {
                        fail = 1;
                    } else {
                        char msg_buf[16];
                        if (send(cfd, "PING", 4, 0) != 4)
                            fail = 1;
                        if (recv(afd, msg_buf, 4, 0) != 4 || memcmp(msg_buf, "PING", 4) != 0)
                            fail = 1;
                        if (send(afd, "PONG", 4, 0) != 4)
                            fail = 1;
                        if (recv(cfd, msg_buf, 4, 0) != 4 || memcmp(msg_buf, "PONG", 4) != 0)
                            fail = 1;

                        /* Peer disconnection propagation: close client, read on server must return EOF (0) */
                        close(cfd);
                        if (read(afd, msg_buf, sizeof(msg_buf)) != 0)
                            fail = 1;
                        /* Write on disconnected peer must fail with -EPIPE or error */
                        if (write(afd, "FAIL", 4) > 0)
                            fail = 1;

                        close(afd);
                    }
                }
            }
            close(lfd);
        }
    }

    /* Verify Zero-Copy Shared Memory (SYS_SHM_*) */
    {
        /* Size 0 must fail */
        if (shm_create(0) > 0)
            fail = 1;

        /* Allocate 1 page */
        int shm_id = shm_create(4096);
        if (shm_id <= 0) {
            fail = 1;
        } else {
            /* Map into address space */
            uint64_t *ptr1 = (uint64_t *)shm_map(shm_id);
            if (!ptr1) {
                fail = 1;
            } else {
                /* Verify page is zero-initialized */
                if (*ptr1 != 0)
                    fail = 1;

                /* Write test pattern */
                *ptr1 = 0xCAFEBABE12345678ULL;

                /* Map/attach second descriptor to the same SHM ID */
                uint64_t *ptr2 = (uint64_t *)shm_attach(shm_id);
                if (!ptr2) {
                    fail = 1;
                } else {
                    /* Verify zero-copy access to the same physical memory */
                    if (*ptr2 != 0xCAFEBABE12345678ULL)
                        fail = 1;

                    /* Mutate via second mapping */
                    *ptr2 = 0x55AA55AA11223344ULL;
                    if (*ptr1 != 0x55AA55AA11223344ULL)
                        fail = 1;

                    /* Destroy while still attached should return non-zero (ref_count > 0) */
                    if (shm_destroy(shm_id) == 0)
                        fail = 1;

                    /* Detach second mapping */
                    if (shm_detach(ptr2) != 0)
                        fail = 1;

                    /* Unmap first mapping */
                    if (shm_unmap(ptr1) != 0)
                        fail = 1;

                    /* Destroy when ref_count == 0 must succeed */
                    if (shm_destroy(shm_id) != 0)
                        fail = 1;

                    /* Subsequent attach to destroyed region must fail */
                    if (shm_attach(shm_id) != NULL)
                        fail = 1;
                }
            }
        }
    }

    /* Verify PID, PPID, and PID 1 immunity */
    {
        int my_pid = getpid();
        int my_ppid = getppid();
        if (my_pid <= 0)
            fail = 1;
        if (my_ppid < 0)
            fail = 1;

        /* Verify PID 1 cannot be killed */
        if (kill(1, 9) == 0)
            fail = 1;
    }

    if (fail) {
        dprintf(2, "abi-test: FAIL\n");
        return 1;
    }
    dprintf(1, "abi-test: PASS\n");
    return 0;
}

void app_cmd_abi_test_entry(void)
{
    _exit(app_run_main(cmd_abi_test_main));
}
