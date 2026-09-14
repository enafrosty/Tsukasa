/*
 * Project Tsukasa — x86_64 Fast Inline System Call Primitives
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

#ifndef _TSUKASA_SYS_SYSCALL_H
#define _TSUKASA_SYS_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* System call vector numbers (Linux-compatible flat table). */
#define SYS_read           0
#define SYS_write          1
#define SYS_open           2
#define SYS_close          3
#define SYS_stat           4
#define SYS_poll           7
#define SYS_lseek          8
#define SYS_mmap           9
#define SYS_munmap         11
#define SYS_rt_sigaction   13
#define SYS_rt_sigprocmask 14
#define SYS_ioctl          16
#define SYS_pipe           22
#define SYS_sched_yield    24
#define SYS_shm_create     29
#define SYS_shm_attach     30
#define SYS_shm_destroy    31
#define SYS_dup            32
#define SYS_dup2           33
#define SYS_nanosleep      35
#define SYS_getpid         39
#define SYS_socket         41
#define SYS_connect        42
#define SYS_accept         43
#define SYS_sendto         44
#define SYS_recvfrom       45
#define SYS_bind           49
#define SYS_listen         50
#define SYS_fork           57
#define SYS_execve         59
#define SYS_exit           60
#define SYS_wait4          61
#define SYS_kill           62
#define SYS_shm_detach     67
#define SYS_fcntl          72
#define SYS_rt_sigpending  73
#define SYS_getcwd         79
#define SYS_chdir          80
#define SYS_rename         82
#define SYS_mkdir          83
#define SYS_rmdir          84
#define SYS_unlink         87
#define SYS_getppid        110
#define SYS_reboot         169
#define SYS_time           201
#define SYS_futex          202
#define SYS_list           300
#define SYS_fsize          301
#define SYS_ftell          302
#define SYS_exists         303
#define SYS_spawn          317
#define SYS_spawn_ex       318
#define SYS_ticks          320
#define SYS_netcall        330

/* Shared memory aliases. */
#define SYS_SHM_CREATE     29
#define SYS_SHM_ATTACH     30
#define SYS_SHM_DESTROY    31
#define SYS_SHM_DETACH     67
#define SYS_SHM_MAP        SYS_SHM_ATTACH
#define SYS_SHM_UNMAP      SYS_SHM_DETACH

/* Fast x86_64 inline system call stubs. */
static inline int64_t __syscall0(int64_t n)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall1(int64_t n, int64_t a1)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall2(int64_t n, int64_t a1, int64_t a2)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall3(int64_t n, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall4(int64_t n, int64_t a1, int64_t a2, int64_t a3, int64_t a4)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall5(int64_t n, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t __syscall6(int64_t n, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6)
{
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* Convenience aliases. */
#define syscall0 __syscall0
#define syscall1 __syscall1
#define syscall2 __syscall2
#define syscall3 __syscall3
#define syscall4 __syscall4
#define syscall5 __syscall5
#define syscall6 __syscall6

#endif /* _TSUKASA_SYS_SYSCALL_H */
