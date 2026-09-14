/*
 * Project Tsukasa — Flat POSIX-numbered syscall table (SPEC-C01)
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
#include <stddef.h>

#ifdef __x86_64__

#include "syscall.h"
#include "../fs/vfs.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../mm/vmm_x64.h"
#include "../proc/process.h"
#include "../ipc/shm.h"
#include "../sys/futex.h"
#include "../sys/panic.h"
#include "../drv/acpi.h"
#include "../include/kutils.h"

#define SYSCALL_TABLE_SIZE 351

typedef long (*syscall_fn_t)(long, long, long, long, long, long);

static int user_ok(long ptr, long len, int need_write)
{
    if (len < 0)
        return 0;
    return vmm_validate_user_ptr((const void *)(uintptr_t)ptr, (size_t)len, need_write);
}

static int user_str_ok(long ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    if (!p)
        return 0;
    for (size_t len = 0; len < VFS_PATH_MAX; len++) {
        if (!vmm_validate_user_ptr((const void *)(p + len), 1, 0))
            return 0;
        if (*(const char *)(p + len) == '\0')
            return 1;
    }
    return 0;
}

static long sys_futex(long uaddr, long op, long val,
                      long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (uaddr & 3)
        return -EINVAL;
    if (!user_ok(uaddr, 4, 0))
        return -EFAULT;
    if (op == FUTEX_WAIT)
        return kernel_futex_wait((volatile uint32_t *)(uintptr_t)uaddr,
                                 (uint32_t)val);
    if (op == FUTEX_WAKE)
        return kernel_futex_wake((volatile uint32_t *)(uintptr_t)uaddr,
                                 (int)val);
    return -ENOSYS;
}

/* Legacy handlers return (uintptr_t)-1 on error with no errno detail. */
static long ret_errno(uintptr_t r)
{
    if (r == (uintptr_t)-1)
        return -EINVAL;
    return (long)r;
}

static long mux_fs(long cmd, long a, long b, long c)
{
    return ret_errno(syscall_handler(SYS_FS, (uintptr_t)cmd, (uintptr_t)a,
                                     (uintptr_t)b, (uintptr_t)c, 0));
}

static long mux_sys(long cmd, long a, long b, long c)
{
    return ret_errno(syscall_handler(SYS_SYSTEM, (uintptr_t)cmd, (uintptr_t)a,
                                     (uintptr_t)b, (uintptr_t)c, 0));
}

static long sys_read(long fd, long buf, long count, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!user_ok(buf, count, 1)) return -EFAULT;
    return mux_fs(FS_CMD_READ, fd, buf, count);
}

static long sys_write(long fd, long buf, long count, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!user_ok(buf, count, 0)) return -EFAULT;
    return mux_fs(FS_CMD_WRITE, fd, buf, count);
}

static long sys_open(long path, long flags, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    long r = mux_fs(FS_CMD_OPEN, path, flags, 0);
    return (r < 0) ? -ENOENT : r;
}

static long sys_close(long fd, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_CLOSE, fd, 0, 0);
}

static long sys_stat(long path, long st, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    if (!user_ok(st, (long)sizeof(struct tsukasa_stat), 1)) return -EFAULT;
    long r = mux_fs(FS_CMD_STAT, path, st, 0);
    return (r < 0) ? -ENOENT : r;
}

static long sys_poll(long fds, long nfds, long timeout, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!user_ok(fds, nfds * (long)sizeof(struct tsukasa_pollfd), 1)) return -EFAULT;
    return mux_fs(FS_CMD_POLL, fds, nfds, timeout);
}

static long sys_lseek(long fd, long off, long whence, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_SEEK, fd, off, whence);
}

static long sys_mmap(long addr, long length, long prot, long flags, long fd, long off)
{
    vfs_mmap_request_t req;
    req.addr = (void *)(uintptr_t)addr;
    req.length = (size_t)length;
    req.prot = (int)prot;
    req.flags = (int)flags;
    req.fd = (int)fd;
    req.offset = (size_t)off;
    return ret_errno(syscall_handler(SYS_FS, FS_CMD_MMAP, (uintptr_t)&req, 0, 0, 0));
}

static long sys_munmap(long addr, long length, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_MUNMAP, addr, length, 0);
}

static long sys_ioctl(long fd, long req, long arg, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_IOCTL, fd, req, arg);
}

static long sys_pipe(long fds, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_ok(fds, 2 * (long)sizeof(int), 1)) return -EFAULT;
    return mux_fs(FS_CMD_PIPE, fds, 0, 0);
}

static long sys_sched_yield(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_yield();
    return 0;
}

static long sys_dup(long fd, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_DUP, fd, 0, 0);
}

static long sys_dup2(long oldfd, long newfd, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_DUP2, oldfd, newfd, 0);
}

static long sys_nanosleep(long ms, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    uint64_t target = process_ticks() + ((uint64_t)ms + 9) / 10;
    while (process_ticks() < target)
        process_yield();
    return 0;
}

static long sys_getpid(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (long)process_current_pid();
}

static long sys_getppid(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_current();
    return cur ? (long)cur->ppid : 0;
}

static long sys_exit(long code, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_exit((int)code);
    return 0;
}

static long sys_wait4(long pid, long status, long options, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (status && !user_ok(status, (long)sizeof(int), 1)) return -EFAULT;
    return mux_sys(SYSTEM_CMD_WAITPID, pid, status, options);
}

static long sys_kill(long pid, long sig, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_sys(SYSTEM_CMD_KILL, pid, sig, 0);
}

static long sys_fcntl(long fd, long cmd, long arg, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_FCNTL, fd, cmd, arg);
}

static long sys_getcwd(long buf, long size, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_ok(buf, size, 1)) return -EFAULT;
    return mux_fs(FS_CMD_GETCWD, buf, size, 0);
}

static long sys_chdir(long path, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    return mux_fs(FS_CMD_CHDIR, path, 0, 0);
}

static long sys_rename(long oldpath, long newpath, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(oldpath) || !user_str_ok(newpath)) return -EFAULT;
    return mux_fs(FS_CMD_RENAME, oldpath, newpath, 0);
}

static long sys_mkdir(long path, long mode, long a3, long a4, long a5, long a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    return mux_fs(FS_CMD_MKDIR, path, mode, 0);
}

static long sys_rmdir(long path, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    return mux_fs(FS_CMD_RMDIR, path, 0, 0);
}

static long sys_unlink(long path, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    return mux_fs(FS_CMD_UNLINK, path, 0, 0);
}

static int is_leap_year(int year)
{
    if ((year % 4) != 0) return 0;
    if ((year % 100) != 0) return 1;
    return (year % 400) == 0;
}

static int month_days(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2)
        return is_leap_year(year) ? 29 : 28;
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1];
}

static long sys_time(long tloc, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    struct tsukasa_time rt;
    long r = mux_sys(SYSTEM_CMD_TIME_GET, (long)(uintptr_t)&rt, 0, 0);
    if (r != 0)
        return -EINVAL;

    int64_t days = 0;
    for (int y = 1970; y < (int)rt.year; y++)
        days += is_leap_year(y) ? 366 : 365;
    for (int m = 1; m < (int)rt.month; m++)
        days += month_days((int)rt.year, m);
    days += (int64_t)rt.day - 1;

    int64_t epoch = days * 86400;
    epoch += (int64_t)rt.hour * 3600;
    epoch += (int64_t)rt.min * 60;
    epoch += (int64_t)rt.sec;

    if (tloc) {
        if (!user_ok(tloc, (long)sizeof(int64_t), 1))
            return -EFAULT;
        *(int64_t *)(uintptr_t)tloc = epoch;
    }
    return (long)epoch;
}

static long sys_reboot(long magic1, long magic2, long cmd, long a4, long a5, long a6)
{
    (void)magic1; (void)magic2; (void)a4; (void)a5; (void)a6;
    if (cmd == 0x4321fedc || cmd == 1) {
        acpi_power_off();
    } else if (cmd == 0x01234567 || cmd == 2) {
        k_reboot();
    }
    return 0;
}



static long sys_rt_sigaction(long sig, long act, long oldact, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (act && !user_ok(act, (long)sizeof(struct tsukasa_sigaction), 0))
        return -EFAULT;
    if (oldact && !user_ok(oldact, (long)sizeof(struct tsukasa_sigaction), 1))
        return -EFAULT;
    return mux_sys(SYSTEM_CMD_SIGACTION, sig, act, oldact);
}

static long sys_rt_sigprocmask(long how, long set, long oldset, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (set && !user_ok(set, (long)sizeof(uint64_t), 0))
        return -EFAULT;
    if (oldset && !user_ok(oldset, (long)sizeof(uint64_t), 1))
        return -EFAULT;
    return mux_sys(SYSTEM_CMD_SIGPROCMASK, how, set, oldset);
}

static long sys_rt_sigpending(long set, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_ok(set, (long)sizeof(uint64_t), 1))
        return -EFAULT;
    return mux_sys(SYSTEM_CMD_SIGPENDING, set, 0, 0);
}

static long sys_list(long dir, long names, long max, long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (!user_str_ok(dir)) return -EFAULT;
    if (max <= 0) return -EINVAL;
    if (!user_ok(names, max * (long)VFS_NAME_MAX, 1)) return -EFAULT;
    return mux_fs(FS_CMD_LIST, dir, names, max);
}

static long sys_fsize(long fd, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_SIZE, fd, 0, 0);
}

static long sys_ftell(long fd, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return mux_fs(FS_CMD_TELL, fd, 0, 0);
}

static long sys_exists(long path, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    struct tsukasa_stat st;
    if (!user_str_ok(path)) return -EFAULT;
    return (syscall_handler(SYS_FS, FS_CMD_STAT, (uintptr_t)path,
                            (uintptr_t)&st, 0, 0) == 0) ? 1 : 0;
}

static long sys_spawn(long path, long argv, long envp, long flags, long a5, long a6)
{
    (void)argv; (void)envp; (void)flags; (void)a5; (void)a6;
    if (!user_str_ok(path)) return -EFAULT;
    return mux_sys(SYSTEM_CMD_SPAWN, path, 0, 0);
}

static long sys_spawn_ex(long req_ptr, long a2, long a3, long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!user_ok(req_ptr, sizeof(struct tsukasa_spawn_request), 0))
        return -EFAULT;
    const struct tsukasa_spawn_request *req =
        (const struct tsukasa_spawn_request *)(uintptr_t)req_ptr;
    if (!user_str_ok((long)(uintptr_t)req->path))
        return -EFAULT;
    return mux_sys(SYSTEM_CMD_SPAWN_EX, req_ptr, 0, 0);
}

#include "../include/socket_defs.h"

static long copy_unix_addr(long addr, long addrlen, char *out)
{
    const struct tsk_sockaddr_un *ua;
    long plen;

    if (addrlen < 3 || addrlen > (long)sizeof(struct tsk_sockaddr_un))
        return -EINVAL;
    if (!user_ok(addr, addrlen, 0))
        return -EFAULT;
    ua = (const struct tsk_sockaddr_un *)(uintptr_t)addr;
    if (ua->sun_family != TSK_AF_UNIX)
        return -EAFNOSUPPORT;
    plen = addrlen - 2;
    if (plen >= TSK_UNIX_PATH_MAX)
        plen = TSK_UNIX_PATH_MAX - 1;
    for (long i = 0; i < plen; i++)
        out[i] = ua->sun_path[i];
    out[plen] = '\0';
    if (out[0] == '\0')
        return -EINVAL;
    return 0;
}

static long sys_socket(long domain, long type, long protocol,
                       long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    if (domain != TSK_AF_UNIX)
        return -EAFNOSUPPORT;
    if (type != TSK_SOCK_STREAM || protocol != 0)
        return -EINVAL;
    return vfs_socket_create();
}

static long sys_bind(long fd, long addr, long addrlen,
                     long a4, long a5, long a6)
{
    char path[TSK_UNIX_PATH_MAX];
    long rc;
    (void)a4; (void)a5; (void)a6;
    rc = copy_unix_addr(addr, addrlen, path);
    if (rc != 0)
        return rc;
    return vfs_socket_bind((int)fd, path);
}

static long sys_listen(long fd, long backlog, long a3,
                       long a4, long a5, long a6)
{
    (void)backlog; (void)a3; (void)a4; (void)a5; (void)a6;
    /* Backlog is fixed at UNIX_BACKLOG kernel-side; the argument is accepted for API shape and ignored, like... */
    return vfs_socket_listen((int)fd);
}

static long sys_connect(long fd, long addr, long addrlen,
                        long a4, long a5, long a6)
{
    char path[TSK_UNIX_PATH_MAX];
    long rc;
    (void)a4; (void)a5; (void)a6;
    rc = copy_unix_addr(addr, addrlen, path);
    if (rc != 0)
        return rc;
    return vfs_socket_connect((int)fd, path);
}

static long sys_accept(long fd, long addr, long addrlen,
                       long a4, long a5, long a6)
{
    (void)a4; (void)a5; (void)a6;
    long ret = vfs_socket_accept((int)fd);
    if (ret >= 0 && addr && addrlen) {
        if (user_ok(addrlen, sizeof(uint32_t), 1)) {
            uint32_t ulen = *(const uint32_t *)(uintptr_t)addrlen;
            if (ulen >= 2 && user_ok(addr, 2, 1)) {
                struct tsk_sockaddr_un *ua = (struct tsk_sockaddr_un *)(uintptr_t)addr;
                ua->sun_family = TSK_AF_UNIX;
                ua->sun_path[0] = '\0';
                *(uint32_t *)(uintptr_t)addrlen = 2;
            }
        }
    }
    return ret;
}

static long sys_sendto(long fd, long buf, long len, long flags,
                       long dest_addr, long addrlen)
{
    (void)flags; (void)dest_addr; (void)addrlen;
    if (len < 0)
        return -EINVAL;
    if (len == 0)
        return 0;
    if (!user_ok(buf, len, 0))
        return -EFAULT;
    size_t wr = vfs_write((int)fd, (const void *)(uintptr_t)buf, (size_t)len);
    if ((long)wr < 0)
        return (long)wr;
    if (wr == 0 && len > 0)
        return -EPIPE;
    return (long)wr;
}

static long sys_recvfrom(long fd, long buf, long len, long flags,
                         long src_addr, long addrlen)
{
    (void)flags; (void)src_addr; (void)addrlen;
    if (len < 0)
        return -EINVAL;
    if (len == 0)
        return 0;
    if (!user_ok(buf, len, 1))
        return -EFAULT;
    size_t rd = vfs_read((int)fd, (void *)(uintptr_t)buf, (size_t)len);
    return (long)rd;
}

static long sys_shm_create(long size, long a2, long a3,
                           long a4, long a5, long a6)
{
    int id;
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (size <= 0)
        return -EINVAL;
    id = shm_create((size_t)size);
    return (id < 0) ? -ENOMEM : (long)id;
}

static long sys_shm_attach(long id, long a2, long a3,
                           long a4, long a5, long a6)
{
    void *p;
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (id <= 0)
        return -EINVAL;
    p = shm_attach((int)id);
    return p ? (long)(uintptr_t)p : -ENOMEM;
}

static long sys_shm_detach(long addr, long a2, long a3,
                           long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    /* shm_detach only matches an attachment this process actually owns (keyed by pid + virt addr), so a bogus... */
    return (shm_detach((void *)(uintptr_t)addr) == 0) ? 0 : -EINVAL;
}

static long sys_shm_destroy(long id, long a2, long a3,
                            long a4, long a5, long a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (id <= 0)
        return -EINVAL;
    return (shm_destroy((int)id) == 0) ? 0 : -EINVAL;
}

static long sys_ticks(long a1, long a2, long a3, long a4, long a5, long a6)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (long)process_ticks();
}

static long sys_netcall(long cmd, long a1, long a2, long a3, long a4, long a5)
{
    (void)a5;
    switch (cmd) {
    case SYSTEM_CMD_NET_INIT:
    case SYSTEM_CMD_NET_IS_INIT:
    case SYSTEM_CMD_NET_HAS_IP:
    case SYSTEM_CMD_NET_DHCP:
    case SYSTEM_CMD_NET_TCP_CLOSE:
    case SYSTEM_CMD_NET_POLL:
        break;
    case SYSTEM_CMD_NET_GET_LINK:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_link_info), 1))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_GET_MAC:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_mac), 1))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_GET_IP:
    case SYSTEM_CMD_NET_GET_GATEWAY:
    case SYSTEM_CMD_NET_GET_DNS:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_ipv4), 1))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_GET_STATS:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_stats), 1))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_DNS_LOOKUP: {
        const struct tsukasa_net_dns_req *req =
            (const struct tsukasa_net_dns_req *)(uintptr_t)a1;
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_dns_req), 0))
            return -EFAULT;
        if (!user_str_ok((long)(uintptr_t)req->name))
            return -EFAULT;
        if (!user_ok((long)(uintptr_t)req->out_ip,
                     (long)sizeof(struct tsukasa_net_ipv4), 1))
            return -EFAULT;
        break;
    }
    case SYSTEM_CMD_NET_PING:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_ping_req), 0))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_TCP_CONNECT:
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_tcp_connect_req), 0))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_TCP_SEND:
        if (a2 > 0 && !user_ok(a1, a2, 0))
            return -EFAULT;
        break;
    case SYSTEM_CMD_NET_TCP_RECV: {
        const struct tsukasa_net_tcp_recv_req *req =
            (const struct tsukasa_net_tcp_recv_req *)(uintptr_t)a1;
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_tcp_recv_req), 0))
            return -EFAULT;
        if (req->max_len > 0 &&
            !user_ok((long)(uintptr_t)req->buffer, (long)req->max_len, 1))
            return -EFAULT;
        break;
    }
    case SYSTEM_CMD_NET_UDP_SEND: {
        const struct tsukasa_net_udp_send_req *req =
            (const struct tsukasa_net_udp_send_req *)(uintptr_t)a1;
        if (!user_ok(a1, (long)sizeof(struct tsukasa_net_udp_send_req), 0))
            return -EFAULT;
        if (req->length > 0 &&
            !user_ok((long)(uintptr_t)req->buffer, (long)req->length, 0))
            return -EFAULT;
        break;
    }
    default:
        return -EINVAL;
    }

    uintptr_t r = syscall_handler(SYS_SYSTEM, (uintptr_t)cmd, (uintptr_t)a1,
                                  (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return (long)r;
}

static const syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE] = {
    [0] = sys_read,
    [1] = sys_write,
    [2] = sys_open,
    [3] = sys_close,
    [4] = sys_stat,
    [7] = sys_poll,
    [8] = sys_lseek,
    [9] = sys_mmap,
    [11] = sys_munmap,
    [13] = sys_rt_sigaction,
    [14] = sys_rt_sigprocmask,
    [16] = sys_ioctl,
    [22] = sys_pipe,
    [24] = sys_sched_yield,
    [29] = sys_shm_create,
    [30] = sys_shm_attach,
    [31] = sys_shm_destroy,
    [32] = sys_dup,
    [33] = sys_dup2,
    [35] = sys_nanosleep,
    [39] = sys_getpid,
    [41] = sys_socket,
    [42] = sys_connect,
    [43] = sys_accept,
    [44] = sys_sendto,
    [45] = sys_recvfrom,
    [49] = sys_bind,
    [50] = sys_listen,
    [60] = sys_exit,
    [61] = sys_wait4,
    [62] = sys_kill,
    [67] = sys_shm_detach,
    [72] = sys_fcntl,
    [73] = sys_rt_sigpending,
    [79] = sys_getcwd,
    [80] = sys_chdir,
    [82] = sys_rename,
    [83] = sys_mkdir,
    [84] = sys_rmdir,
    [87] = sys_unlink,
    [110] = sys_getppid,
    [169] = sys_reboot,
    [201] = sys_time,
    [202] = sys_futex,
    [300] = sys_list,
    [301] = sys_fsize,
    [302] = sys_ftell,
    [303] = sys_exists,
    [310] = sys_shm_create,
    [311] = sys_shm_attach,
    [312] = sys_shm_detach,
    [313] = sys_shm_destroy,
    [317] = sys_spawn,
    [318] = sys_spawn_ex,
    [320] = sys_ticks,
    [330] = sys_netcall,
};

long syscall_dispatch(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
    if (nr < 0 || nr >= SYSCALL_TABLE_SIZE || !syscall_table[nr])
        return -ENOSYS;
    return syscall_table[nr](a1, a2, a3, a4, a5, a6);
}

/* Called from isr_x64_syscall (int 0x80, DPL=3). */
void syscall_gate_x64(interrupt_frame_t *f)
{
    f->rax = (uint64_t)syscall_dispatch((long)f->rax,
                                        (long)f->rdi, (long)f->rsi,
                                        (long)f->rdx, (long)f->r10,
                                        (long)f->r8, (long)f->r9);
}

#include "../loader/elf64.h"

static void c01_test_entry(void)
{
    char kbuf[8];

    if (syscall_dispatch(999, 0, 0, 0, 0, 0, 0) == -ENOSYS)
        kprintf("[parity][C01] unknown nr -ENOSYS PASS\n");
    else
        kprintf("[parity][C01] unknown nr -ENOSYS FAIL\n");

    if (syscall_dispatch(1, 1, (long)(uintptr_t)kbuf, 2, 0, 0, 0) == -EFAULT)
        kprintf("[parity][C01] bad user ptr -EFAULT PASS\n");
    else
        kprintf("[parity][C01] bad user ptr -EFAULT FAIL\n");

    /* elf64_spawn is a single-slot handoff; retry while another spawn (e.g. */
    int pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn("/fat12/hello.elf", "hello-elf");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[parity][C01] elf64 ring3 write FAIL (spawn)\n");
        process_exit(0);
    }
    int status = -1;
    /* wait_status packs the exit code in bits 8-15 (see process.c). */
    int code = (process_waitpid(process_current_pid(), pid, 0, &status) == pid)
                   ? ((status >> 8) & 0xFF) : -1;
    if (code == 42)
        kprintf("[parity][C01] elf64 ring3 write PASS code=%d\n", code);
    else
        kprintf("[parity][C01] elf64 ring3 write FAIL code=%d status=0x%x\n",
                code, (unsigned)status);

    pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn("/fat12/hello_sc.elf", "hello-syscall");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[guide02] SYSCALL ring3 write FAIL (spawn)\n");
        process_exit(0);
    }
    status = -1;
    code = (process_waitpid(process_current_pid(), pid, 0, &status) == pid)
               ? ((status >> 8) & 0xFF) : -1;
    if (code == 42)
        kprintf("[guide02] SYSCALL/SYSRET ring3 write PASS code=%d\n", code);
    else
        kprintf("[guide02] SYSCALL/SYSRET ring3 write FAIL code=%d status=0x%x\n",
                code, (unsigned)status);

    pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn("/fat12/hellosdk.elf", "hello-sdk");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[guide05] sdk hello FAIL (spawn)\n");
        process_exit(0);
    }
    status = -1;
    code = (process_waitpid(process_current_pid(), pid, 0, &status) == pid)
               ? ((status >> 8) & 0xFF) : -1;
    if (code == 42)
        kprintf("[guide05] sdk hello (crt0/main/argv) PASS code=%d\n", code);
    else
        kprintf("[guide05] sdk hello (crt0/main/argv) FAIL code=%d status=0x%x\n",
                code, (unsigned)status);

    pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn_cmdline("/fat12/argvchk.elf",
                                  "argvchk alpha beta gamma", "argvchk");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[guide05] argv round-trip FAIL (spawn)\n");
        process_exit(0);
    }
    status = -1;
    code = (process_waitpid(process_current_pid(), pid, 0, &status) == pid)
               ? ((status >> 8) & 0xFF) : -1;
    if (code == 42)
        kprintf("[guide05] argv round-trip PASS code=%d\n", code);
    else
        kprintf("[guide05] argv round-trip FAIL code=%d status=0x%x\n",
                code, (unsigned)status);

    pid = -1;
    for (int i = 0; i < 200 && pid < 0; i++) {
        pid = elf64_spawn("/fat12/echo.elf", "echo-sdk");
        if (pid < 0)
            process_yield();
    }
    if (pid < 0) {
        kprintf("[guide05] migrated echo FAIL (spawn)\n");
        process_exit(0);
    }
    status = -1;
    code = (process_waitpid(process_current_pid(), pid, 0, &status) == pid)
               ? ((status >> 8) & 0xFF) : -1;
    if (code == 0)
        kprintf("[guide05] migrated echo loads PASS code=%d\n", code);
    else
        kprintf("[guide05] migrated echo loads FAIL code=%d status=0x%x\n",
                code, (unsigned)status);
    process_exit(0);
}

void syscall_table_run_selftests(void)
{
    if (!process_spawn_kernel("c01-test", c01_test_entry))
        kprintf("[parity][C01] WARN: failed to spawn c01-test\n");
}

#endif /* __x86_64__ */
