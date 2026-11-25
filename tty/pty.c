/*
 * Project Tsukasa — Pseudo-terminal (PTY) subsystem (/dev/ptmx and /dev/pts/N)
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

#include "pty.h"
#include "termios.h"

#include "../fs/devfs.h"
#include "../fs/vfs.h"
#include "../include/errno.h"
#include "../include/spinlock.h"
#include "../include/vfs_abi.h"
#include "../proc/process.h"
#include "../sys/wait_queue.h"

#define PTY_MAX      32
#define PTY_BUF_SIZE 4096U

typedef struct pty_instance {
    int id;
    int in_use;
    int locked;
    int master_refs;
    int slave_refs;
    int master_closed;
    int slave_ever_opened;
    int slave_closed;
    int fg_pgid;
    int session_id;
    struct tsk_winsize winsize;
    tty_ldisc_t ldisc;
    char m_rx_buf[PTY_BUF_SIZE];
    size_t m_rx_head;
    size_t m_rx_tail;
    size_t m_rx_count;
    wait_queue_head_t master_waitq;
    wait_queue_head_t slave_waitq;
} pty_instance_t;

static pty_instance_t g_ptys[PTY_MAX];
static spinlock_t g_pty_lock = SPINLOCK_INIT;

static const devfs_ops_t g_ptmx_ops;
static const devfs_ops_t g_pty_slave_ops;

static inline uint64_t pty_irq_save_disable(void)
{
    uint64_t flags = 0;
#ifdef __x86_64__
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
#endif
    return flags;
}

static inline void pty_irq_restore(uint64_t flags)
{
#ifdef __x86_64__
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
    else
        __asm__ volatile ("cli" : : : "memory");
#else
    (void)flags;
#endif
}

static void format_pts_name(int id, char *out, size_t cap)
{
    char tmp[16];
    size_t n = 0;
    unsigned int v = (unsigned int)id;
    if (cap < 8)
        return;
    out[0] = 'p';
    out[1] = 't';
    out[2] = 's';
    out[3] = '/';
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    size_t w = 4;
    while (n > 0 && w + 1 < cap)
        out[w++] = tmp[--n];
    out[w] = '\0';
}

static size_t m_rx_push_locked(pty_instance_t *p, const char *buf, size_t len)
{
    size_t w = 0;
    while (w < len && p->m_rx_count < PTY_BUF_SIZE) {
        p->m_rx_buf[p->m_rx_tail] = buf[w++];
        p->m_rx_tail = (p->m_rx_tail + 1U) % PTY_BUF_SIZE;
        p->m_rx_count++;
    }
    return w;
}

static void pty_echo_cb(void *ctx, const char *buf, size_t len)
{
    pty_instance_t *p = (pty_instance_t *)ctx;
    if (!p || !buf || len == 0)
        return;
    unsigned long flags = spin_lock_irqsave(&g_pty_lock);
    if (p->in_use && !p->master_closed)
        (void)m_rx_push_locked(p, buf, len);
    spin_unlock_irqrestore(&g_pty_lock, flags);
    wait_queue_wake_all(&p->master_waitq);
}

static void pty_sig_cb(void *ctx, int sig)
{
    pty_instance_t *p = (pty_instance_t *)ctx;
    int fg = -1;
    if (!p)
        return;
    unsigned long flags = spin_lock_irqsave(&g_pty_lock);
    if (p->in_use)
        fg = p->fg_pgid;
    spin_unlock_irqrestore(&g_pty_lock, flags);
    if (fg > 0)
        (void)process_kill_pgid(fg, sig);
}

static int pty_is_master_closed(void *ctx)
{
    pty_instance_t *p = (pty_instance_t *)ctx;
    int closed = 1;
    if (!p)
        return 1;
    unsigned long flags = spin_lock_irqsave(&g_pty_lock);
    closed = (!p->in_use || p->master_closed) ? 1 : 0;
    spin_unlock_irqrestore(&g_pty_lock, flags);
    return closed;
}

void pty_init(void)
{
    unsigned long flags = spin_lock_irqsave(&g_pty_lock);
    for (int i = 0; i < PTY_MAX; i++) {
        g_ptys[i].id = i;
        g_ptys[i].in_use = 0;
        g_ptys[i].locked = 1;
        g_ptys[i].master_refs = 0;
        g_ptys[i].slave_refs = 0;
        g_ptys[i].master_closed = 0;
        g_ptys[i].slave_ever_opened = 0;
        g_ptys[i].slave_closed = 0;
        g_ptys[i].fg_pgid = -1;
        g_ptys[i].session_id = -1;
        wait_queue_init(&g_ptys[i].master_waitq);
        wait_queue_init(&g_ptys[i].slave_waitq);
    }
    spin_unlock_irqrestore(&g_pty_lock, flags);
}

void *pty_lookup_by_id(int id)
{
    pty_instance_t *res = NULL;
    if (id < 0 || id >= PTY_MAX)
        return NULL;
    unsigned long flags = spin_lock_irqsave(&g_pty_lock);
    if (g_ptys[id].in_use && !g_ptys[id].master_closed)
        res = &g_ptys[id];
    spin_unlock_irqrestore(&g_pty_lock, flags);
    return res;
}

static int ptmx_open_instance(void *dev_priv, int flags, void **priv_out)
{
    (void)dev_priv;
    (void)flags;
    pty_instance_t *p = NULL;
    char pts_name[24];

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    for (int i = 0; i < PTY_MAX; i++) {
        if (!g_ptys[i].in_use) {
            p = &g_ptys[i];
            p->id = i;
            p->in_use = 1;
            p->locked = 1;
            p->master_refs = 1;
            p->slave_refs = 0;
            p->master_closed = 0;
            p->slave_ever_opened = 0;
            p->slave_closed = 0;
            p->fg_pgid = -1;
            p->session_id = -1;
            p->winsize.ws_row = 24;
            p->winsize.ws_col = 80;
            p->winsize.ws_xpixel = 0;
            p->winsize.ws_ypixel = 0;
            tty_ldisc_init(&p->ldisc);
            p->m_rx_head = 0;
            p->m_rx_tail = 0;
            p->m_rx_count = 0;
            wait_queue_init(&p->master_waitq);
            wait_queue_init(&p->slave_waitq);
            break;
        }
    }
    spin_unlock_irqrestore(&g_pty_lock, lflags);

    if (!p)
        return -ENOMEM;

    format_pts_name(p->id, pts_name, sizeof(pts_name));
    if (devfs_register_device(pts_name, VFS_TYPE_CHAR, &g_pty_slave_ops, p) != 0) {
        lflags = spin_lock_irqsave(&g_pty_lock);
        p->in_use = 0;
        p->master_refs = 0;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return -ENOMEM;
    }

    *priv_out = p;
    return 0;
}

static void ptmx_close(void *priv)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    int unregister_pts = 0;
    int fg = -1;
    int pts_id = -1;

    if (!p)
        return;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    if (!p->in_use) {
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return;
    }
    if (p->master_refs > 0)
        p->master_refs--;
    if (p->master_refs == 0) {
        p->master_closed = 1;
        unregister_pts = 1;
        pts_id = p->id;
        fg = p->fg_pgid;
        if (p->slave_refs == 0)
            p->in_use = 0;
    }
    spin_unlock_irqrestore(&g_pty_lock, lflags);

    if (unregister_pts) {
        char pts_name[24];
        format_pts_name(pts_id, pts_name, sizeof(pts_name));
        (void)devfs_unregister_device(pts_name);
        wait_queue_wake_all(&p->slave_waitq);
        if (fg > 0)
            (void)process_kill_pgid(fg, PROCESS_SIGHUP);
    }
}

static size_t ptmx_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)pos;
    pty_instance_t *p = (pty_instance_t *)priv;
    uint8_t *dst = (uint8_t *)buf;

    if (!p || !dst)
        return (size_t)-EINVAL;
    if (count == 0)
        return 0;

    for (;;) {
        process_t *cur = process_current();
        if (cur && (cur->signal_pending & ~cur->signal_mask))
            return (size_t)-EINTR;

        uint64_t irqf = pty_irq_save_disable();
        spin_lock(&g_pty_lock);
        if (!p->in_use) {
            spin_unlock(&g_pty_lock);
            pty_irq_restore(irqf);
            return 0;
        }
        if (p->m_rx_count > 0) {
            size_t got = 0;
            while (got < count && p->m_rx_count > 0) {
                dst[got++] = (uint8_t)p->m_rx_buf[p->m_rx_head];
                p->m_rx_head = (p->m_rx_head + 1U) % PTY_BUF_SIZE;
                p->m_rx_count--;
            }
            spin_unlock(&g_pty_lock);
            pty_irq_restore(irqf);
            wait_queue_wake_all(&p->slave_waitq);
            return got;
        }
        if (p->slave_closed) {
            spin_unlock(&g_pty_lock);
            pty_irq_restore(irqf);
            return 0;
        }
        if (flags & VFS_O_NONBLOCK) {
            spin_unlock(&g_pty_lock);
            pty_irq_restore(irqf);
            return (size_t)-EAGAIN;
        }
        if (!cur) {
            spin_unlock(&g_pty_lock);
            pty_irq_restore(irqf);
            return (size_t)-EAGAIN;
        }

        wait_queue_entry_t wqe;
        wqe.proc = cur;
        wqe.next = NULL;
        wait_queue_add(&p->master_waitq, &wqe);
        process_set_poll_blocked(0ULL);
        spin_unlock(&g_pty_lock);
        process_yield();
        pty_irq_restore(irqf);
        process_clear_poll_deadline();
        wait_queue_remove(&p->master_waitq, &wqe);
    }
}

static size_t ptmx_write_flags(void *priv, size_t pos, const void *buf, size_t count, int flags)
{
    (void)pos;
    (void)flags;
    pty_instance_t *p = (pty_instance_t *)priv;
    if (!p || !buf)
        return (size_t)-EINVAL;
    if (count == 0)
        return 0;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    int closed = (!p->in_use || p->master_closed || p->slave_closed);
    spin_unlock_irqrestore(&g_pty_lock, lflags);
    if (closed)
        return (size_t)-EIO;

    tty_ldisc_input_bytes(&p->ldisc, (const char *)buf, count,
                          pty_echo_cb, pty_sig_cb, p, &p->slave_waitq);
    return count;
}

static size_t ptmx_write(void *priv, size_t pos, const void *buf, size_t count)
{
    return ptmx_write_flags(priv, pos, buf, count, 0);
}

static int pty_common_ioctl(pty_instance_t *p, int is_master, unsigned long req, void *arg)
{
    switch (req) {
    case VFS_TIOCGPTN:
        if (!arg)
            return -EFAULT;
        *(unsigned int *)arg = (unsigned int)p->id;
        return 0;

    case VFS_TIOCSPTLCK: {
        if (!is_master)
            return -ENOTTY;
        if (!arg)
            return -EFAULT;
        int lock = *(const int *)arg;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        p->locked = lock ? 1 : 0;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return 0;
    }

    case VFS_TIOCGWINSZ: {
        if (!arg)
            return -EFAULT;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        *(struct tsk_winsize *)arg = p->winsize;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return 0;
    }

    case VFS_TIOCSWINSZ: {
        if (!arg)
            return -EFAULT;
        struct tsk_winsize ws = *(const struct tsk_winsize *)arg;
        int fg = -1;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        p->winsize = ws;
        fg = p->fg_pgid;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        if (fg > 0)
            (void)process_kill_pgid(fg, PROCESS_SIGWINCH);
        return 0;
    }

    case VFS_TIOCSCTTY: {
        if (is_master)
            return -ENOTTY;
        process_t *cur = process_current();
        int force = ((uintptr_t)arg == 1);
        if (!cur)
            return -EINVAL;
        if (cur->pid != cur->sid)
            return -EPERM;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        if (p->session_id > 0 && p->session_id != (int)cur->sid && !force) {
            spin_unlock_irqrestore(&g_pty_lock, lflags);
            return -EPERM;
        }
        p->session_id = (int)cur->sid;
        p->fg_pgid = (int)cur->pgid;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        process_set_ctty((int)cur->pid, 2, p->id);
        return 0;
    }

    case VFS_TIOCGPGRP: {
        if (!arg)
            return -EFAULT;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        int fg = p->fg_pgid;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        if (fg <= 0)
            return -ENOTTY;
        *(int *)arg = fg;
        return 0;
    }

    case VFS_TIOCSPGRP: {
        if (!arg)
            return -EFAULT;
        int pgid = *(const int *)arg;
        if (pgid <= 0)
            return -EINVAL;
        unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
        p->fg_pgid = pgid;
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return 0;
    }

    case VFS_FIONREAD: {
        if (!arg)
            return -EFAULT;
        if (is_master) {
            unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
            *(int *)arg = (int)p->m_rx_count;
            spin_unlock_irqrestore(&g_pty_lock, lflags);
            return 0;
        }
        return tty_ldisc_ioctl(&p->ldisc, req, arg);
    }

    case VFS_TCGETS:
    case VFS_TCSETS:
    case VFS_TCSETSW:
    case VFS_TCSETSF:
        return tty_ldisc_ioctl(&p->ldisc, req, arg);

    default:
        return -ENOTTY;
    }
}

static int ptmx_ioctl(void *priv, unsigned long request, void *arg)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    if (!p || !p->in_use)
        return -ENXIO;
    return pty_common_ioctl(p, 1, request, arg);
}

static int ptmx_poll(void *priv, int events)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    int revents = 0;
    if (!p)
        return VFS_POLLERR;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    if (!p->in_use) {
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return VFS_POLLHUP;
    }
    if (p->m_rx_count > 0)
        revents |= (events & VFS_POLLIN);
    if (!p->slave_closed)
        revents |= (events & VFS_POLLOUT);
    if (p->slave_closed)
        revents |= VFS_POLLHUP;
    spin_unlock_irqrestore(&g_pty_lock, lflags);
    return revents;
}

static wait_queue_head_t *ptmx_get_waitq(void *priv, int events)
{
    (void)events;
    pty_instance_t *p = (pty_instance_t *)priv;
    return p ? &p->master_waitq : NULL;
}

int pty_slave_open_existing(void *priv, int flags)
{
    (void)flags;
    pty_instance_t *p = (pty_instance_t *)priv;
    if (!p)
        return -ENOENT;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    if (!p->in_use || p->master_closed) {
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return -EIO;
    }
    if (p->locked) {
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return -EIO;
    }
    p->slave_refs++;
    p->slave_ever_opened = 1;
    p->slave_closed = 0;
    spin_unlock_irqrestore(&g_pty_lock, lflags);
    return 0;
}

static void pty_slave_close(void *priv)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    int notify_master = 0;
    if (!p)
        return;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    if (!p->in_use) {
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return;
    }
    if (p->slave_refs > 0)
        p->slave_refs--;
    if (p->slave_refs == 0 && p->slave_ever_opened) {
        p->slave_closed = 1;
        notify_master = 1;
        if (p->master_refs == 0)
            p->in_use = 0;
    }
    spin_unlock_irqrestore(&g_pty_lock, lflags);

    if (notify_master)
        wait_queue_wake_all(&p->master_waitq);
}

static size_t pty_slave_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)pos;
    pty_instance_t *p = (pty_instance_t *)priv;
    if (!p)
        return (size_t)-ENXIO;
    long r = tty_ldisc_read(&p->ldisc, buf, count, flags,
                            pty_is_master_closed, p, &p->slave_waitq);
    return (size_t)r;
}

static size_t pty_slave_write_flags(void *priv, size_t pos, const void *buf, size_t count, int flags)
{
    (void)pos;
    pty_instance_t *p = (pty_instance_t *)priv;
    const char *src = (const char *)buf;
    char out_buf[256];
    size_t sent = 0;

    if (!p || !src)
        return (size_t)-EINVAL;
    if (count == 0)
        return 0;

    while (sent < count) {
        size_t chunk = count - sent;
        if (chunk > 120)
            chunk = 120;
        size_t olen = tty_ldisc_process_output(&p->ldisc, src + sent, chunk,
                                               out_buf, sizeof(out_buf));
        size_t o_off = 0;
        while (o_off < olen) {
            process_t *cur = process_current();
            if (cur && (cur->signal_pending & ~cur->signal_mask))
                return (sent > 0) ? sent : (size_t)-EINTR;

            uint64_t irqf = pty_irq_save_disable();
            spin_lock(&g_pty_lock);
            if (!p->in_use || p->master_closed) {
                spin_unlock(&g_pty_lock);
                pty_irq_restore(irqf);
                return (sent > 0) ? sent : (size_t)-EIO;
            }
            size_t pushed = m_rx_push_locked(p, out_buf + o_off, olen - o_off);
            if (pushed > 0) {
                spin_unlock(&g_pty_lock);
                pty_irq_restore(irqf);
                wait_queue_wake_all(&p->master_waitq);
                o_off += pushed;
                continue;
            }
            if (flags & VFS_O_NONBLOCK) {
                spin_unlock(&g_pty_lock);
                pty_irq_restore(irqf);
                return (sent > 0) ? sent : (size_t)-EAGAIN;
            }
            if (!cur) {
                spin_unlock(&g_pty_lock);
                pty_irq_restore(irqf);
                return sent;
            }
            wait_queue_entry_t wqe;
            wqe.proc = cur;
            wqe.next = NULL;
            wait_queue_add(&p->slave_waitq, &wqe);
            process_set_poll_blocked(0ULL);
            spin_unlock(&g_pty_lock);
            process_yield();
            pty_irq_restore(irqf);
            process_clear_poll_deadline();
            wait_queue_remove(&p->slave_waitq, &wqe);
        }
        sent += chunk;
    }
    return sent;
}

static size_t pty_slave_write(void *priv, size_t pos, const void *buf, size_t count)
{
    return pty_slave_write_flags(priv, pos, buf, count, 0);
}

static int pty_slave_ioctl(void *priv, unsigned long request, void *arg)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    if (!p || !p->in_use)
        return -ENXIO;
    return pty_common_ioctl(p, 0, request, arg);
}

static int pty_slave_poll(void *priv, int events)
{
    pty_instance_t *p = (pty_instance_t *)priv;
    int revents = 0;
    if (!p)
        return VFS_POLLERR;

    unsigned long lflags = spin_lock_irqsave(&g_pty_lock);
    if (!p->in_use || p->master_closed) {
        revents |= VFS_POLLHUP;
        if (tty_ldisc_can_read(&p->ldisc))
            revents |= (events & VFS_POLLIN);
        spin_unlock_irqrestore(&g_pty_lock, lflags);
        return revents;
    }
    if (p->m_rx_count < PTY_BUF_SIZE)
        revents |= (events & VFS_POLLOUT);
    spin_unlock_irqrestore(&g_pty_lock, lflags);

    if (tty_ldisc_can_read(&p->ldisc))
        revents |= (events & VFS_POLLIN);
    return revents;
}

static wait_queue_head_t *pty_slave_get_waitq(void *priv, int events)
{
    (void)events;
    pty_instance_t *p = (pty_instance_t *)priv;
    return p ? &p->slave_waitq : NULL;
}

static const devfs_ops_t g_ptmx_ops = {
    .open = NULL,
    .open_instance = ptmx_open_instance,
    .close = ptmx_close,
    .read = ptmx_read,
    .write = ptmx_write,
    .write_flags = ptmx_write_flags,
    .ioctl = ptmx_ioctl,
    .mmap = NULL,
    .munmap = NULL,
    .poll = ptmx_poll,
    .size = NULL,
    .get_waitq = ptmx_get_waitq,
};

static const devfs_ops_t g_pty_slave_ops = {
    .open = pty_slave_open_existing,
    .open_instance = NULL,
    .close = pty_slave_close,
    .read = pty_slave_read,
    .write = pty_slave_write,
    .write_flags = pty_slave_write_flags,
    .ioctl = pty_slave_ioctl,
    .mmap = NULL,
    .munmap = NULL,
    .poll = pty_slave_poll,
    .size = NULL,
    .get_waitq = pty_slave_get_waitq,
};

const devfs_ops_t *pty_get_ptmx_ops(void)
{
    return &g_ptmx_ops;
}

const devfs_ops_t *pty_get_slave_ops(void)
{
    return &g_pty_slave_ops;
}
