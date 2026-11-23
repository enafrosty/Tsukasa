/*
 * Project Tsukasa — Multiplexed syscall dispatcher for Phase 2
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

#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#include "../fs/vfs.h"
#include "../drv/rtc.h"
#include "../include/kprintf.h"
#include "../ipc/shm.h"
#include "../loader/exec.h"
#include "../loader/elf64.h"
#include "../mm/vmm_x64.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../net/network.h"
#include "../proc/process.h"
#include "../proc/signal.h"
#include "../tty/tty.h"

static uintptr_t handle_fs(uintptr_t cmd,
                           uintptr_t arg2,
                           uintptr_t arg3,
                           uintptr_t arg4,
                           uintptr_t arg5)
{
    (void)arg5;
    switch (cmd) {
    case FS_CMD_OPEN:
        return (uintptr_t)vfs_open_flags((const char *)(uintptr_t)arg2, (int)arg3);
    case FS_CMD_READ:
        return (uintptr_t)vfs_read((int)arg2, (void *)(uintptr_t)arg3, (size_t)arg4);
    case FS_CMD_WRITE:
        return (uintptr_t)vfs_write((int)arg2, (const void *)(uintptr_t)arg3, (size_t)arg4);
    case FS_CMD_CLOSE:
        vfs_close((int)arg2);
        return 0;
    case FS_CMD_SEEK:
        return (uintptr_t)vfs_seek((int)arg2, (size_t)arg3, (int)arg4);
    case FS_CMD_SIZE:
        return (uintptr_t)vfs_size((int)arg2);
    case FS_CMD_CREATE:
        return (uintptr_t)vfs_create((const char *)(uintptr_t)arg2);
    case FS_CMD_LIST:
    {
        int max = (int)arg4;
        if (max <= 0)
            return (uintptr_t)-1;
        if (!vmm_validate_user_ptr((void *)(uintptr_t)arg3, (size_t)max * VFS_NAME_MAX, 1))
            return (uintptr_t)-1;
        return (uintptr_t)vfs_list((const char *)(uintptr_t)arg2,
                                   (char (*)[VFS_NAME_MAX])(uintptr_t)arg3,
                                   max);
    }
    case FS_CMD_TELL:
        return (uintptr_t)vfs_tell((int)arg2);
    case FS_CMD_STAT:
        return (uintptr_t)vfs_stat((const char *)(uintptr_t)arg2,
                                   (vfs_stat_t *)(uintptr_t)arg3);
    case FS_CMD_FSTAT:
        return (uintptr_t)vfs_fstat((int)arg2, (vfs_stat_t *)(uintptr_t)arg3);
    case FS_CMD_DUP:
        return (uintptr_t)vfs_dup((int)arg2);
    case FS_CMD_DUP2:
        return (uintptr_t)vfs_dup2((int)arg2, (int)arg3);
    case FS_CMD_PIPE:
        return (uintptr_t)vfs_pipe((int *)(uintptr_t)arg2);
    case FS_CMD_FCNTL:
        return (uintptr_t)vfs_fcntl((int)arg2, (int)arg3, (int)arg4);
    case FS_CMD_GETCWD:
        return (uintptr_t)vfs_getcwd((char *)(uintptr_t)arg2, (size_t)arg3);
    case FS_CMD_CHDIR:
        return (uintptr_t)vfs_chdir((const char *)(uintptr_t)arg2);
    case FS_CMD_IOCTL:
        return (uintptr_t)vfs_ioctl((int)arg2,
                                    (unsigned long)arg3,
                                    (void *)(uintptr_t)arg4);
    case FS_CMD_MMAP:
    {
        vfs_mmap_request_t *req = (vfs_mmap_request_t *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)vfs_mmap(req->addr,
                                   req->length,
                                   req->prot,
                                   req->flags,
                                   req->fd,
                                   req->offset);
    }
    case FS_CMD_MUNMAP:
        return (uintptr_t)vfs_munmap((void *)(uintptr_t)arg2, (size_t)arg3);
    case FS_CMD_POLL:
        return (uintptr_t)vfs_poll((vfs_pollfd_t *)(uintptr_t)arg2,
                                   (size_t)arg3,
                                   (int)arg4);
    case FS_CMD_MKDIR:
        return (uintptr_t)vfs_mkdir((const char *)(uintptr_t)arg2, (int)arg3);
    case FS_CMD_UNLINK:
        return (uintptr_t)vfs_unlink((const char *)(uintptr_t)arg2);
    case FS_CMD_RMDIR:
        return (uintptr_t)vfs_rmdir((const char *)(uintptr_t)arg2);
    case FS_CMD_RENAME:
        return (uintptr_t)vfs_rename((const char *)(uintptr_t)arg2,
                                     (const char *)(uintptr_t)arg3);
    default:
        return (uintptr_t)-1;
    }
}


#else

#include "../ipc/shm.h"
#include "../proc/task.h"

uintptr_t syscall_handler(uintptr_t num,
                          uintptr_t arg1,
                          uintptr_t arg2,
                          uintptr_t arg3,
                          uintptr_t arg4,
                          uintptr_t arg5)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    switch (num) {
    case SYS_YIELD:
        task_yield();
        return 0;
    case SYS_EXIT:
        if (task_current())
            task_current()->state = TASK_DEAD;
        task_yield();
        return 0;
    case SYS_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg1);
    case SYS_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg1);
    case SYS_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg1);
    case SYS_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg1);
    default:
        return (uintptr_t)-1;
    }
}

#endif

#ifdef __x86_64__

static uintptr_t handle_gui(uintptr_t cmd,
                            uintptr_t arg2,
                            uintptr_t arg3,
                            uintptr_t arg4,
                            uintptr_t arg5)
{
    (void)cmd;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    return (uintptr_t)GUI_ERR_INVALID;
}

static uintptr_t handle_system(uintptr_t cmd,
                               uintptr_t arg2,
                               uintptr_t arg3,
                               uintptr_t arg4,
                               uintptr_t arg5)
{
    (void)arg5;
    switch (cmd) {
    case SYSTEM_CMD_YIELD:
        process_yield();
        return 0;

    case SYSTEM_CMD_SPAWN:
    {
        const char *path = (const char *)(uintptr_t)arg2;
        const char *args = (const char *)(uintptr_t)arg3;
        exec_entry_t entry = NULL;
        process_t *child;
        if (!path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(path, &entry) == 0 && entry) {
            child = process_spawn_kernel(path, entry);
            if (child)
                process_set_cmdline((int)child->pid, (args && args[0]) ? args : path);
            return child ? (uintptr_t)child->pid : (uintptr_t)-1;
        }
        /* Not a compiled-in builtin: load an ELF64 binary from the VFS into a fresh private address space and run it. */
        {
            int parent_pid = process_current_pid();
            int pid = (args && args[0])
                          ? elf64_spawn_cmdline(path, args, path)
                          : elf64_spawn(path, path);
            if (pid >= 0) {
                process_set_cmdline(pid, (args && args[0]) ? args : path);
                if (parent_pid > 0) {
                    process_clone_fd(pid, 0, parent_pid, 0);
                    process_clone_fd(pid, 1, parent_pid, 1);
                    process_clone_fd(pid, 2, parent_pid, 2);
                }
            }
            return (uintptr_t)pid;
        }
    }

    case SYSTEM_CMD_SPAWN_EX:
    {
        const struct tsukasa_spawn_request *req =
            (const struct tsukasa_spawn_request *)(uintptr_t)arg2;
        exec_entry_t entry = NULL;
        int parent_pid = process_current_pid();
        int child_pid;
        if (!req || !req->path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(req->path, &entry) == 0 && entry) {
            process_t *child = process_spawn_kernel(req->path, entry);
            if (!child)
                return (uintptr_t)-1;
            child_pid = (int)child->pid;
        } else {
            /* Not a compiled-in builtin: load an ELF64 binary from the VFS. */
            child_pid = elf64_spawn_cmdline(req->path, req->args, req->path);
            if (child_pid < 0)
                return (uintptr_t)-1;
        }

        process_set_cmdline(child_pid, (req->args && req->args[0]) ? req->args : req->path);
        if (req->tty_id >= 0)
            process_set_tty(child_pid, req->tty_id);

        if (parent_pid > 0) {
            if (req->stdin_fd >= 0)
                process_clone_fd(child_pid, 0, parent_pid, req->stdin_fd);
            if (req->stdout_fd >= 0)
                process_clone_fd(child_pid, 1, parent_pid, req->stdout_fd);
            if (req->stderr_fd >= 0)
                process_clone_fd(child_pid, 2, parent_pid, req->stderr_fd);
        }
        return (uintptr_t)child_pid;
    }

    case SYSTEM_CMD_EXEC:
    {
        int pid = (int)arg2;
        const char *path = (const char *)(uintptr_t)arg3;
        exec_entry_t entry = NULL;
        if (!path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(path, &entry) != 0 || !entry)
            return (uintptr_t)-1;
        process_set_cmdline(pid, path);
        return (uintptr_t)process_exec(pid, entry, path);
    }

    case SYSTEM_CMD_GET_CMDLINE:
        return (uintptr_t)process_get_cmdline(process_current_pid(),
                                              (char *)(uintptr_t)arg2,
                                              (size_t)arg3);

    case SYSTEM_CMD_WAITPID:
    {
        int caller = process_current_pid();
        int target = (int)arg2;
        int *status = (int *)(uintptr_t)arg3;
        int options = (int)arg4;
        return (uintptr_t)process_waitpid(caller, target, options, status);
    }

    case SYSTEM_CMD_KILL:
        return (uintptr_t)process_kill((int)arg2, (int)arg3);

    case SYSTEM_CMD_SIGACTION:
    {
        int sig = (int)arg2;
        const struct tsukasa_sigaction *act = (const struct tsukasa_sigaction *)(uintptr_t)arg3;
        struct tsukasa_sigaction *old = (struct tsukasa_sigaction *)(uintptr_t)arg4;
        int pid = process_current_pid();
        if (old) {
            if (!vmm_validate_user_ptr(old, sizeof(struct tsukasa_sigaction), 1))
                return (uintptr_t)-1;
            uint64_t pending = 0;
            process_signal_pending(pid, &pending);
            old->sa_handler = PROCESS_SIG_DFL;
            old->sa_mask = pending;
            old->sa_flags = 0;
        }
        if (act) {
            if (!vmm_validate_user_ptr(act, sizeof(struct tsukasa_sigaction), 0))
                return (uintptr_t)-1;
            if (process_signal_register(pid, sig,
                    (process_signal_handler_t)(uintptr_t)act->sa_handler) != 0)
                return (uintptr_t)-1;
            if (process_signal_mask(pid, SIG_SETMASK, act->sa_mask, NULL) != 0)
                return (uintptr_t)-1;
        }
        return 0;
    }

    case SYSTEM_CMD_SIGPROCMASK:
    {
        uint64_t set = 0;
        if (arg3) {
            if (!vmm_validate_user_ptr((const void *)(uintptr_t)arg3, sizeof(uint64_t), 0))
                return (uintptr_t)-1;
            set = *(const uint64_t *)(uintptr_t)arg3;
        }
        if (arg4 && !vmm_validate_user_ptr((void *)(uintptr_t)arg4, sizeof(uint64_t), 1))
            return (uintptr_t)-1;
        return (uintptr_t)process_signal_mask(process_current_pid(),
                                              (int)arg2,
                                              set,
                                              (uint64_t *)(uintptr_t)arg4);
    }

    case SYSTEM_CMD_SIGPENDING:
        if (!vmm_validate_user_ptr((void *)(uintptr_t)arg2, sizeof(uint64_t), 1))
            return (uintptr_t)-1;
        return (uintptr_t)process_signal_pending(process_current_pid(),
                                                 (uint64_t *)(uintptr_t)arg2);

    case SYSTEM_CMD_TTY_CREATE:
        return (uintptr_t)tty_create();
    case SYSTEM_CMD_TTY_SET_FG:
        return (uintptr_t)tty_set_foreground_pgid((int)arg2, (int)arg3);
    case SYSTEM_CMD_TTY_GET_FG:
        return (uintptr_t)tty_get_foreground_pgid((int)arg2);
    case SYSTEM_CMD_TTY_KILL_FG:
        return (uintptr_t)tty_kill_foreground((int)arg2, (int)arg3);

    case SYSTEM_CMD_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg2);
    case SYSTEM_CMD_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg2);
    case SYSTEM_CMD_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg2);
    case SYSTEM_CMD_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg2);
    case SYSTEM_CMD_MEM_STATS:
    {
        struct tsukasa_mem_stats *out = (struct tsukasa_mem_stats *)(uintptr_t)arg2;
        heap_stats_t heap_stats = {0};
        struct shm_stats shm_stats = {0};
        size_t proc_count = 0;
        size_t proc_maps = 0;
        size_t proc_shm_pages = 0;
        size_t proc_shm_attachments = 0;

        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;

        heap_get_stats(&heap_stats);
        shm_get_stats(&shm_stats);
        process_get_memory_totals(&proc_count,
                                  &proc_maps,
                                  &proc_shm_pages,
                                  &proc_shm_attachments);

        out->total_pages = pmm_total_page_count();
        out->used_pages = pmm_used_page_count();
        out->free_pages = pmm_free_page_count();
        out->heap_pool_bytes = heap_stats.pool_bytes;
        out->heap_used_bytes = heap_stats.allocated_bytes;
        out->heap_peak_bytes = heap_stats.peak_allocated_bytes;
        out->process_count = proc_count;
        out->process_mapped_pages = proc_maps;
        out->process_shm_pages = proc_shm_pages;
        out->process_shm_attachments = proc_shm_attachments;
        out->shm_regions = shm_stats.region_count;
        out->shm_attachments = shm_stats.attachment_count;
        out->shm_reserved_pages = shm_stats.reserved_pages;
        return 0;
    }
    case SYSTEM_CMD_MEM_DUMP:
    {
        struct tsukasa_mem_stats stats = {0};
        heap_stats_t heap_stats = {0};
        struct shm_stats shm_stats = {0};
        size_t proc_count = 0;
        size_t proc_maps = 0;
        size_t proc_shm_pages = 0;
        size_t proc_shm_attachments = 0;

        heap_get_stats(&heap_stats);
        shm_get_stats(&shm_stats);
        process_get_memory_totals(&proc_count,
                                  &proc_maps,
                                  &proc_shm_pages,
                                  &proc_shm_attachments);

        stats.total_pages = pmm_total_page_count();
        stats.used_pages = pmm_used_page_count();
        stats.free_pages = pmm_free_page_count();
        stats.heap_pool_bytes = heap_stats.pool_bytes;
        stats.heap_used_bytes = heap_stats.allocated_bytes;
        stats.heap_peak_bytes = heap_stats.peak_allocated_bytes;
        stats.process_count = proc_count;
        stats.process_mapped_pages = proc_maps;
        stats.process_shm_pages = proc_shm_pages;
        stats.process_shm_attachments = proc_shm_attachments;
        stats.shm_regions = shm_stats.region_count;
        stats.shm_attachments = shm_stats.attachment_count;
        stats.shm_reserved_pages = shm_stats.reserved_pages;

        kprintf("[mem] pages total=%u used=%u free=%u\n",
                (uint32_t)stats.total_pages,
                (uint32_t)stats.used_pages,
                (uint32_t)stats.free_pages);
        kprintf("[mem] heap pool=%u used=%u peak=%u\n",
                (uint32_t)stats.heap_pool_bytes,
                (uint32_t)stats.heap_used_bytes,
                (uint32_t)stats.heap_peak_bytes);
        kprintf("[mem] proc count=%u maps=%u shm_pages=%u shm_atts=%u\n",
                (uint32_t)stats.process_count,
                (uint32_t)stats.process_mapped_pages,
                (uint32_t)stats.process_shm_pages,
                (uint32_t)stats.process_shm_attachments);
        process_dump_memory_state();
        shm_dump_state();
        return 0;
    }

    case SYSTEM_CMD_NET_INIT:
        return (uintptr_t)network_initialize_stack();
    case SYSTEM_CMD_NET_IS_INIT:
        return (uintptr_t)network_is_initialized();
    case SYSTEM_CMD_NET_HAS_IP:
        return (uintptr_t)network_has_ipv4();
    case SYSTEM_CMD_NET_GET_LINK:
        return (uintptr_t)network_get_link_info((net_link_info_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_GET_MAC:
    {
        net_link_info_t info;
        struct tsukasa_net_mac *out = (struct tsukasa_net_mac *)(uintptr_t)arg2;
        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 6; i++)
            out->bytes[i] = info.mac.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_IP:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.ip.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_GATEWAY:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.gateway.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_DNS:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.dns.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_STATS:
        return (uintptr_t)network_get_stats((net_runtime_stats_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_DHCP:
        return (uintptr_t)network_dhcp_acquire();
    case SYSTEM_CMD_NET_DNS_LOOKUP:
    {
        const struct tsukasa_net_dns_req *req = (const struct tsukasa_net_dns_req *)(uintptr_t)arg2;
        if (!req || !req->name || !req->out_ip)
            return (uintptr_t)-1;
        return (uintptr_t)network_dns_lookup(req->name, (net_ipv4_addr_t *)req->out_ip);
    }
    case SYSTEM_CMD_NET_PING:
    {
        const struct tsukasa_net_ping_req *req = (const struct tsukasa_net_ping_req *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)network_ping((const net_ipv4_addr_t *)&req->ip, req->timeout_ms);
    }
    case SYSTEM_CMD_NET_TCP_CONNECT:
        return (uintptr_t)network_tcp_connect((const net_tcp_connect_req_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_TCP_SEND:
        return (uintptr_t)network_tcp_send((const void *)(uintptr_t)arg2, (size_t)arg3);
    case SYSTEM_CMD_NET_TCP_RECV:
    {
        const struct tsukasa_net_tcp_recv_req *req =
            (const struct tsukasa_net_tcp_recv_req *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)network_tcp_recv(req->buffer, req->max_len, req->wait);
    }
    case SYSTEM_CMD_NET_TCP_CLOSE:
        return (uintptr_t)network_tcp_close();
    case SYSTEM_CMD_NET_UDP_SEND:
        return (uintptr_t)network_udp_send((const net_udp_send_req_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_POLL:
        network_poll();
        return 0;

    case SYSTEM_CMD_THEME_SET_ACCENT:
    case SYSTEM_CMD_THEME_SET_BG_MODE:
    case SYSTEM_CMD_THEME_SET_WALLPAPER:
    case SYSTEM_CMD_THEME_GET_STATE:
        return (uintptr_t)-1;

    case SYSTEM_CMD_TIME_GET:
    {
        rtc_time_t now;
        struct tsukasa_time *out = (struct tsukasa_time *)(uintptr_t)arg2;
        if (!out || !vmm_validate_user_ptr(out, sizeof(*out), 1))
            return (uintptr_t)-1;
        rtc_read(&now);
        out->sec = now.sec;
        out->min = now.min;
        out->hour = now.hour;
        out->day = now.day;
        out->month = now.month;
        out->year = now.year;
        return 0;
    }

    default:
        return (uintptr_t)-1;
    }
}

uintptr_t syscall_handler(uintptr_t num,
                          uintptr_t arg1,
                          uintptr_t arg2,
                          uintptr_t arg3,
                          uintptr_t arg4,
                          uintptr_t arg5)
{
    switch (num) {
    case SYS_YIELD:
        process_yield();
        return 0;
    case SYS_EXIT:
        process_exit((int)arg1);
        return 0;
    case SYS_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg1);
    case SYS_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg1);
    case SYS_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg1);
    case SYS_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg1);
    case SYS_GUI:
        return handle_gui(arg1, arg2, arg3, arg4, arg5);
    case SYS_FS:
        return handle_fs(arg1, arg2, arg3, arg4, arg5);
    case SYS_SYSTEM:
        return handle_system(arg1, arg2, arg3, arg4, arg5);
    default:
        return (uintptr_t)-1;
    }
}

#endif /* __x86_64__ */
