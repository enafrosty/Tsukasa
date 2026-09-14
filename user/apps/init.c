/*
 * Project Tsukasa — Standard System Init Daemon (PID 1)
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

#include "user/include/app_runtime.h"
#include "user/include/stdio.h"
#include "user/include/unistd.h"
#include "user/include/fcntl.h"
#include "user/include/time.h"
#include "user/include/sys/stat.h"
#include "user/include/sys/wait.h"
#include "user/include/shell.h"
#include "user/lib/syscall.h"
#include "../../loader/exec.h"

static void init_open_fallback_descriptor(int target_fd, int flags)
{
    if (fcntl(target_fd, F_GETFL) >= 0)
        return;

    int fd = -1;
    /* Primary: Virtual console /dev/tty0 */
    fd = open("/dev/tty0", flags);
    /* Fallback 1: Serial console /dev/ttyS0 */
    if (fd < 0)
        fd = open("/dev/ttyS0", flags);
    /* Fallback 2: /dev/console */
    if (fd < 0)
        fd = open("/dev/console", flags);
    /* Fallback 3: /dev/null */
    if (fd < 0)
        fd = open("/dev/null", flags);

    if (fd >= 0 && fd != target_fd) {
        dup2(fd, target_fd);
        close(fd);
    }
}

static void init_setup_descriptors(void)
{
    init_open_fallback_descriptor(0, O_RDONLY);
    init_open_fallback_descriptor(1, O_WRONLY);
    init_open_fallback_descriptor(2, O_WRONLY);
}

static int init_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    init_setup_descriptors();
    dprintf(1, "[init] PID 1 active (Tsukasa OS System Init)\n");

    /* Execute system startup configuration scripts */
    shell_run_rc_file("/etc/tsukasa.rc", 1, 2);
    shell_run_rc_file("/tmp/.tsukasarc", 1, 2);

    /* Spawn Project Vanilla display server */
    int vsrv_pid = spawn("/bin/vsrv.elf");
    if (vsrv_pid <= 0)
        vsrv_pid = spawn("/fat12/vsrv.elf");
    if (vsrv_pid <= 0)
        vsrv_pid = spawn("/bin/VSRV.ELF");
    if (vsrv_pid <= 0)
        vsrv_pid = spawn("/fat12/VSRV.ELF");

    if (vsrv_pid > 0)
        dprintf(1, "[init] spawned Vanilla display server (pid=%d)\n", vsrv_pid);

    /* Yield briefly to allow display server socket initialization */
    struct timespec ts_init;
    ts_init.tv_sec = 0;
    ts_init.tv_nsec = 150000000; /* 150ms */
    nanosleep(&ts_init, NULL);

    int fm_pid = spawn("/bin/filemgr.elf");
    if (fm_pid <= 0)
        fm_pid = spawn("/fat12/filemgr.elf");
    if (fm_pid > 0)
        dprintf(1, "[init] spawned File Manager (pid=%d)\n", fm_pid);

    ts_init.tv_sec = 0;
    ts_init.tv_nsec = 50000000;
    nanosleep(&ts_init, NULL);

    int tm_pid = spawn("/bin/taskmgr.elf");
    if (tm_pid <= 0)
        tm_pid = spawn("/fat12/taskmgr.elf");
    if (tm_pid > 0)
        dprintf(1, "[init] spawned Task Manager (pid=%d)\n", tm_pid);

    ts_init.tv_sec = 0;
    ts_init.tv_nsec = 50000000;
    nanosleep(&ts_init, NULL);

    int term_pid = spawn("/bin/terminal.elf");
    if (term_pid <= 0)
        term_pid = spawn("/fat12/terminal.elf");
    if (term_pid > 0)
        dprintf(1, "[init] spawned Terminal (pid=%d)\n", term_pid);

    /* Continuous PID 1 zombie reaping loop */
    for (;;) {
        int status = 0;
        int reaped_pid = 0;

        /* Drain all terminated child processes currently available */
        while ((reaped_pid = waitpid(-1, &status, WNOHANG)) > 0)
            dprintf(1, "[init] reaped child pid=%d status=%d\n", reaped_pid, status);

        /* Sleep when idle to avoid 100% CPU spinning */
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50000000; /* 50ms */
        nanosleep(&ts, NULL);
    }

    return 0;
}

void app_init_entry(void)
{
    _exit(app_run_main(init_main));
}
