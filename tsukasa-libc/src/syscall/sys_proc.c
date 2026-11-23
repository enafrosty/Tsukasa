/*
 * Project Tsukasa — Process Management System Call Wrappers
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
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

pid_t fork(void)
{
    return (pid_t)__syscall_check(__syscall0(SYS_fork));
}

pid_t spawn(const char *path, char *const argv[], char *const envp[])
{
    char args_buf[512];
    int64_t args_ptr = 0;

    if (argv && argv[0] && argv[1]) {
        size_t pos = 0;
        for (int i = 0; argv[i]; i++) {
            const char *s = argv[i];
            int quote = (*s == '\0');
            size_t slen = 0;
            while (s[slen]) {
                if (s[slen] == ' ' || s[slen] == '\t')
                    quote = 1;
                slen++;
            }
            size_t need = (i > 0 ? 1u : 0u) + (quote ? 2u : 0u) + slen;
            if (pos + need >= sizeof(args_buf))
                break;
            if (i > 0)
                args_buf[pos++] = ' ';
            if (quote)
                args_buf[pos++] = '"';
            for (size_t j = 0; j < slen; j++)
                args_buf[pos++] = s[j];
            if (quote)
                args_buf[pos++] = '"';
        }
        args_buf[pos] = '\0';
        if (pos > 0)
            args_ptr = (int64_t)(uintptr_t)args_buf;
    }

    return (pid_t)__syscall_check(__syscall4(SYS_spawn, (int64_t)(uintptr_t)path,
                                             args_ptr, (int64_t)(uintptr_t)envp, 0));
}

int spawn_ex(const struct tsukasa_spawn_request *req)
{
    return (int)__syscall_check(__syscall1(SYS_spawn_ex, (int64_t)(uintptr_t)req));
}

int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return (int)__syscall_check(__syscall3(SYS_execve, (int64_t)(uintptr_t)pathname,
                                          (int64_t)(uintptr_t)argv, (int64_t)(uintptr_t)envp));
}

int execvp(const char *file, char *const argv[])
{
    if (!file || !file[0]) {
        errno = ENOENT;
        return -1;
    }

    if (strchr(file, '/')) {
        return execve(file, argv, environ);
    }

    char pathbuf[256];
    snprintf(pathbuf, sizeof(pathbuf), "/bin/%s", file);
    execve(pathbuf, argv, environ);

    snprintf(pathbuf, sizeof(pathbuf), "/fat12/%s", file);
    return execve(pathbuf, argv, environ);
}

pid_t wait4(pid_t pid, int *wstatus, int options, void *rusage)
{
    (void)rusage;
    return (pid_t)__syscall_check(__syscall3(SYS_wait4, (int64_t)pid,
                                             (int64_t)(uintptr_t)wstatus, (int64_t)options));
}

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
    return wait4(pid, wstatus, options, NULL);
}

pid_t wait(int *wstatus)
{
    return waitpid(-1, wstatus, 0);
}

int kill(pid_t pid, int sig)
{
    return (int)__syscall_check(__syscall2(SYS_kill, (int64_t)pid, (int64_t)sig));
}

int raise(int sig)
{
    return kill(getpid(), sig);
}

static sighandler_t g_signal_handlers[32];

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (signum <= 0 || signum >= 32) {
        errno = EINVAL;
        return -1;
    }

    sighandler_t prev = g_signal_handlers[signum];
    int64_t rc = __syscall4(SYS_rt_sigaction,
                            (int64_t)signum,
                            (int64_t)(uintptr_t)act,
                            (int64_t)(uintptr_t)oldact,
                            (int64_t)sizeof(sigset_t));
    if (__syscall_check(rc) < 0) {
        return -1;
    }

    if (oldact) {
        oldact->sa_handler = prev;
        oldact->sa_mask = 0;
        oldact->sa_flags = 0;
    }
    if (act) {
        g_signal_handlers[signum] = act->sa_handler;
    }
    return 0;
}

sighandler_t signal(int signum, sighandler_t handler)
{
    struct sigaction act;
    struct sigaction oldact;

    act.sa_handler = handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    if (sigaction(signum, &act, &oldact) < 0) {
        return SIG_ERR;
    }
    return oldact.sa_handler;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    (void)how;
    (void)set;
    (void)oldset;
    return 0;
}

int sigemptyset(sigset_t *set)
{
    if (!set) {
        errno = EFAULT;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set)
{
    if (!set) {
        errno = EFAULT;
        return -1;
    }
    *set = ~0ULL;
    return 0;
}

int sigaddset(sigset_t *set, int signum)
{
    if (!set || signum <= 0 || signum >= 64) {
        errno = EINVAL;
        return -1;
    }
    *set |= (1ULL << (signum - 1));
    return 0;
}

int sigdelset(sigset_t *set, int signum)
{
    if (!set || signum <= 0 || signum >= 64) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1ULL << (signum - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signum)
{
    if (!set || signum <= 0 || signum >= 64) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1ULL << (signum - 1))) ? 1 : 0;
}

pid_t getpid(void)
{
    return (pid_t)__syscall_check(__syscall0(SYS_getpid));
}

pid_t getppid(void)
{
    return (pid_t)__syscall_check(__syscall0(SYS_getppid));
}

int sched_yield(void)
{
    return (int)__syscall_check(__syscall0(SYS_sched_yield));
}

int reboot(int cmd)
{
    return (int)__syscall_check(__syscall4(SYS_reboot, 0xfee1dead, 672274793, (int64_t)cmd, 0));
}

#define ATEXIT_MAX 32

static void (*g_atexit_funcs[ATEXIT_MAX])(void);
static size_t g_atexit_count = 0;
static int g_exiting = 0;
static int g_flushed = 0;

int atexit(void (*func)(void))
{
    if (!func || g_atexit_count >= ATEXIT_MAX)
        return -1;
    g_atexit_funcs[g_atexit_count++] = func;
    return 0;
}

void _exit(int status)
{
    __syscall1(SYS_exit, status);
    for (;;) {
    }
}

void exit(int status)
{
    if (!g_exiting) {
        g_exiting = 1;
        while (g_atexit_count > 0) {
            void (*fn)(void) = g_atexit_funcs[--g_atexit_count];
            if (fn)
                fn();
        }
    }
    if (!g_flushed) {
        g_flushed = 1;
        fflush(NULL);
    }
    _exit(status);
}
