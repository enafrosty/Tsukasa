/*
 * Project Tsukasa — Multi-VT TTY subsystem, termios, and job control
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

#include "tty.h"
#include "termios.h"

#include "../drv/fb.h"
#include "../drv/serial.h"
#include "../fs/vfs.h"
#include "../include/errno.h"
#include "../include/spinlock.h"
#include "../include/vfs_abi.h"
#include "../proc/process.h"
#include "../sys/kconsole.h"
#include "../sys/wait_queue.h"

#include <stddef.h>

struct tty_slot {
    int used;
    int fg_pgid;
    int session_id;
    int kd_mode;
    struct tsk_winsize winsize;
    tty_ldisc_t ldisc;
    wait_queue_head_t waitq;
};

static struct tty_slot g_ttys[TTY_MAX];
static int g_active_tty = 0;
static int g_ctrl_down = 0;
static int g_shift_down = 0;
static int g_alt_down = 0;
static int g_caps_lock = 0;
static spinlock_t g_tty_lock = SPINLOCK_INIT;
static wait_queue_head_t g_vt_waitq;

static inline uint64_t irq_save_disable(void)
{
    uint64_t flags = 0;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags)
{
    if (flags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
    else
        __asm__ volatile ("cli" : : : "memory");
}

static int resolve_tty_index(int tty_id)
{
    if (tty_id >= 0 && tty_id < TTY_MAX)
        return tty_id;
    process_t *cur = process_current();
    if (cur && cur->tty_id >= 0 && cur->tty_id < TTY_MAX)
        return cur->tty_id;
    return g_active_tty;
}

static void tty_slot_reset(int idx)
{
    g_ttys[idx].used = 1;
    g_ttys[idx].fg_pgid = -1;
    g_ttys[idx].session_id = -1;
    g_ttys[idx].kd_mode = VFS_KD_TEXT;
    g_ttys[idx].winsize.ws_row = 25;
    g_ttys[idx].winsize.ws_col = 80;
    g_ttys[idx].winsize.ws_xpixel = (uint16_t)fb_info.width;
    g_ttys[idx].winsize.ws_ypixel = (uint16_t)fb_info.height;
    if (fb_info.width >= 80 && fb_info.height >= 100) {
        g_ttys[idx].winsize.ws_col = (uint16_t)(fb_info.width / 8u);
        g_ttys[idx].winsize.ws_row = (uint16_t)(fb_info.height / 10u);
    }
    tty_ldisc_init(&g_ttys[idx].ldisc);
    wait_queue_init(&g_ttys[idx].waitq);
}

void tty_init(void)
{
    uint64_t flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    wait_queue_init(&g_vt_waitq);
    for (int i = 0; i < TTY_MAX; i++)
        tty_slot_reset(i);
    g_active_tty = 0;
    g_ctrl_down = 0;
    g_shift_down = 0;
    g_alt_down = 0;
    g_caps_lock = 0;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
}

int tty_create(void)
{
    int id = -1;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    for (int i = 0; i < TTY_MAX; i++) {
        if (!g_ttys[i].used) {
            tty_slot_reset(i);
            id = i;
            break;
        }
    }
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return id;
}

int tty_destroy(int tty_id)
{
    uint64_t flags;
    if (tty_id <= 0 || tty_id >= TTY_MAX)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (!g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -1;
    }
    g_ttys[tty_id].used = 0;
    g_ttys[tty_id].fg_pgid = -1;
    g_ttys[tty_id].session_id = -1;
    if (g_active_tty == tty_id)
        g_active_tty = 0;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return 0;
}

int tty_get_active(void)
{
    int tty;
    uint64_t flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    tty = g_active_tty;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return tty;
}

int tty_set_active(int tty_id)
{
    uint64_t flags;
    int kd_mode;
    if (tty_id < 0 || tty_id >= TTY_MAX)
        return -EINVAL;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (!g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -ENXIO;
    }
    g_active_tty = tty_id;
    kd_mode = g_ttys[tty_id].kd_mode;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);

    fb_set_kd_mode(kd_mode);
    kconsole_set_active(kd_mode == VFS_KD_TEXT);
    wait_queue_wake_all(&g_vt_waitq);
    wait_queue_wake_all(&g_ttys[tty_id].waitq);
    return 0;
}

int tty_set_kd_mode(int tty_id, int mode)
{
    uint64_t flags;
    int is_active = 0;

    if (mode != VFS_KD_TEXT && mode != VFS_KD_GRAPHICS)
        return -EINVAL;

    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    int idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -ENXIO;
    }
    g_ttys[idx].kd_mode = mode;
    is_active = (idx == g_active_tty);
    spin_unlock(&g_tty_lock);
    irq_restore(flags);

    if (is_active) {
        fb_set_kd_mode(mode);
        kconsole_set_active(mode == VFS_KD_TEXT);
    }
    return 0;
}

int tty_get_kd_mode(int tty_id)
{
    uint64_t flags;
    int mode;

    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    int idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -ENXIO;
    }
    mode = g_ttys[idx].kd_mode;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return mode;
}

int tty_set_foreground_pgid(int tty_id, int pgid)
{
    uint64_t flags;
    if (tty_id < 0 || tty_id >= TTY_MAX)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (!g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -1;
    }
    g_ttys[tty_id].fg_pgid = pgid;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return 0;
}

int tty_get_foreground_pgid(int tty_id)
{
    int pgid = -1;
    uint64_t flags;
    if (tty_id < 0 || tty_id >= TTY_MAX)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (g_ttys[tty_id].used)
        pgid = g_ttys[tty_id].fg_pgid;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return pgid;
}

int tty_kill_foreground(int tty_id, int sig)
{
    int pgid;
    uint64_t flags;
    if (tty_id < 0 || tty_id >= TTY_MAX)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (!g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -1;
    }
    pgid = g_ttys[tty_id].fg_pgid;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    if (pgid <= 0)
        return 0;
    return process_kill_pgid(pgid, sig);
}

static void tty_raw_emit(int tty_id, const char *buf, size_t len)
{
    int mirror;
    uint64_t flags;

    if (!buf || len == 0)
        return;

    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (tty_id < 0 || tty_id >= TTY_MAX || !g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return;
    }
    mirror = (tty_id == g_active_tty) && (g_ttys[tty_id].kd_mode == VFS_KD_TEXT);
    spin_unlock(&g_tty_lock);
    irq_restore(flags);

    for (size_t i = 0; i < len; i++) {
        serial_putc(buf[i]);
        if (mirror)
            kconsole_putc(buf[i]);
    }
}

static void tty_echo_cb(void *ctx, const char *buf, size_t len)
{
    int tty_id = (int)(intptr_t)ctx;
    tty_raw_emit(tty_id, buf, len);
}

static void tty_sig_cb(void *ctx, int sig)
{
    int tty_id = (int)(intptr_t)ctx;
    (void)tty_kill_foreground(tty_id, sig);
}

static void tty_drain_serial_to_active(int tty_id)
{
    char sbuf[64];
    size_t n = 0;
    while (n < sizeof(sbuf) && serial_has_rx_port(COM1_BASE))
        sbuf[n++] = (char)serial_getc_port(COM1_BASE);
    if (n > 0) {
        tty_ldisc_input_bytes(&g_ttys[tty_id].ldisc, sbuf, n,
                              tty_echo_cb, tty_sig_cb,
                              (void *)(intptr_t)tty_id,
                              &g_ttys[tty_id].waitq);
    }
}

long tty_read(int tty_id, void *buf, size_t count, int flags)
{
    int idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used)
        return -ENXIO;
    if (idx == tty_get_active())
        tty_drain_serial_to_active(idx);
    return tty_ldisc_read(&g_ttys[idx].ldisc, buf, count, flags,
                          NULL, NULL, &g_ttys[idx].waitq);
}

size_t tty_write(int tty_id, const void *buf, size_t count)
{
    const char *src = (const char *)buf;
    int idx;
    char out_buf[256];

    if (!src || count == 0)
        return 0;

    idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used)
        return 0;

    size_t off = 0;
    while (off < count) {
        size_t chunk = count - off;
        if (chunk > 120)
            chunk = 120;
        size_t olen = tty_ldisc_process_output(&g_ttys[idx].ldisc,
                                               src + off, chunk,
                                               out_buf, sizeof(out_buf));
        tty_raw_emit(idx, out_buf, olen);
        off += chunk;
    }
    return count;
}

int tty_poll(int tty_id, int events)
{
    int idx = resolve_tty_index(tty_id);
    int revents = events & VFS_POLLOUT;
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used)
        return VFS_POLLERR;
    if (idx == tty_get_active())
        tty_drain_serial_to_active(idx);
    if (tty_ldisc_can_read(&g_ttys[idx].ldisc))
        revents |= (events & VFS_POLLIN);
    return revents;
}

wait_queue_head_t *tty_get_waitq(int tty_id, int events)
{
    (void)events;
    int idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used)
        return NULL;
    return &g_ttys[idx].waitq;
}

int tty_ioctl(int tty_id, unsigned long request, void *arg)
{
    int idx = resolve_tty_index(tty_id);
    if (idx < 0 || idx >= TTY_MAX || !g_ttys[idx].used)
        return -ENXIO;

    switch (request) {
    case VFS_TCGETS:
    case VFS_TCSETS:
    case VFS_TCSETSW:
    case VFS_TCSETSF:
    case VFS_FIONREAD:
        if (request == VFS_FIONREAD && idx == tty_get_active())
            tty_drain_serial_to_active(idx);
        return tty_ldisc_ioctl(&g_ttys[idx].ldisc, request, arg);

    case VFS_TIOCGWINSZ: {
        if (!arg)
            return -EFAULT;
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        *(struct tsk_winsize *)arg = g_ttys[idx].winsize;
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return 0;
    }
    case VFS_TIOCSWINSZ: {
        if (!arg)
            return -EFAULT;
        struct tsk_winsize ws = *(const struct tsk_winsize *)arg;
        int fg = -1;
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        g_ttys[idx].winsize = ws;
        fg = g_ttys[idx].fg_pgid;
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        if (fg > 0)
            (void)process_kill_pgid(fg, PROCESS_SIGWINCH);
        return 0;
    }
    case VFS_TIOCSCTTY: {
        process_t *cur = process_current();
        int force = ((uintptr_t)arg == 1);
        if (!cur)
            return -EINVAL;
        if (cur->pid != cur->sid)
            return -EPERM;
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        if (g_ttys[idx].session_id > 0 &&
            g_ttys[idx].session_id != (int)cur->sid && !force) {
            spin_unlock(&g_tty_lock);
            irq_restore(flags);
            return -EPERM;
        }
        g_ttys[idx].session_id = (int)cur->sid;
        g_ttys[idx].fg_pgid = (int)cur->pgid;
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        process_set_ctty((int)cur->pid, 1, idx);
        return 0;
    }
    case VFS_TIOCGPGRP: {
        if (!arg)
            return -EFAULT;
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        int fg = g_ttys[idx].fg_pgid;
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        if (fg <= 0) {
            process_t *cur = process_current();
            fg = cur ? (int)cur->pgid : 0;
        }
        *(int *)arg = fg;
        return 0;
    }
    case VFS_TIOCSPGRP: {
        if (!arg)
            return -EFAULT;
        int pgid = *(const int *)arg;
        if (pgid <= 0)
            return -EINVAL;
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        g_ttys[idx].fg_pgid = pgid;
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return 0;
    }
    case VFS_KDSETMODE: {
        uintptr_t raw = (uintptr_t)arg;
        int mode = (raw <= 0xFFFFu) ? (int)raw : (arg ? *(const int *)arg : -1);
        return tty_set_kd_mode(idx, mode);
    }
    case VFS_KDGETMODE: {
        if (!arg)
            return -EFAULT;
        int mode = tty_get_kd_mode(idx);
        if (mode < 0)
            return mode;
        *(int *)arg = mode;
        return 0;
    }
    case VFS_VT_GETSTATE: {
        if (!arg)
            return -EFAULT;
        struct vfs_vt_stat st = { 0, 0, 0 };
        uint64_t flags = irq_save_disable();
        spin_lock(&g_tty_lock);
        st.v_active = (uint16_t)g_active_tty;
        for (int i = 0; i < TTY_MAX; i++) {
            if (g_ttys[i].used)
                st.v_state |= (uint16_t)(1U << (unsigned)i);
        }
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        *(struct vfs_vt_stat *)arg = st;
        return 0;
    }
    case VFS_VT_ACTIVATE: {
        int vt = (int)(intptr_t)arg;
        return tty_set_active(vt);
    }
    case VFS_VT_WAITACTIVE: {
        int vt = (int)(intptr_t)arg;
        if (vt < 0 || vt >= TTY_MAX || !g_ttys[vt].used)
            return -EINVAL;
        for (;;) {
            if (tty_get_active() == vt)
                return 0;
            process_t *cur = process_current();
            if (!cur)
                return -EINTR;
            if (cur->signal_pending & ~cur->signal_mask)
                return -EINTR;
            wait_queue_entry_t wqe;
            wqe.proc = cur;
            wqe.next = NULL;
            uint64_t irqf = irq_save_disable();
            wait_queue_add(&g_vt_waitq, &wqe);
            if (tty_get_active() == vt) {
                wait_queue_remove(&g_vt_waitq, &wqe);
                irq_restore(irqf);
                return 0;
            }
            process_set_poll_blocked(0ULL);
            process_yield();
            irq_restore(irqf);
            process_clear_poll_deadline();
            wait_queue_remove(&g_vt_waitq, &wqe);
        }
    }
    default:
        return -ENOTTY;
    }
}

void tty_handle_scancode(uint8_t scancode, int pressed)
{
    static const char normal_map[58] = {
        [0x01] = 0x1B, [0x02] = '1', [0x03] = '2', [0x04] = '3',
        [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7',
        [0x09] = '8', [0x0A] = '9', [0x0B] = '0', [0x0C] = '-',
        [0x0D] = '=', [0x0E] = 127,  [0x0F] = '\t',
        [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
        [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
        [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
        [0x1C] = '\n',
        [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
        [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
        [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
        [0x2B] = '\\',
        [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
        [0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' '
    };
    static const char shift_map[58] = {
        [0x01] = 0x1B, [0x02] = '!', [0x03] = '@', [0x04] = '#',
        [0x05] = '$', [0x06] = '%', [0x07] = '^', [0x08] = '&',
        [0x09] = '*', [0x0A] = '(', [0x0B] = ')', [0x0C] = '_',
        [0x0D] = '+', [0x0E] = 127,  [0x0F] = '\t',
        [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
        [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
        [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
        [0x1C] = '\n',
        [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
        [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
        [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
        [0x2B] = '|',
        [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
        [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
        [0x34] = '>', [0x35] = '?', [0x37] = '*', [0x39] = ' '
    };

    if (scancode == 29) {
        g_ctrl_down = pressed ? 1 : 0;
        return;
    }
    if (scancode == 42 || scancode == 54) {
        g_shift_down = pressed ? 1 : 0;
        return;
    }
    if (scancode == 56) {
        g_alt_down = pressed ? 1 : 0;
        return;
    }
    if (scancode == 58) {
        if (pressed)
            g_caps_lock ^= 1;
        return;
    }

    if (!pressed)
        return;

    /* Ctrl+Alt+F1..F6 switches to /dev/tty1..6; F7 returns to /dev/tty0; F8 -> /dev/tty7. */
    if (g_ctrl_down && g_alt_down) {
        if (scancode >= 0x3B && scancode <= 0x40) {
            (void)tty_set_active(1 + (int)(scancode - 0x3B));
            return;
        }
        if (scancode == 0x41) {
            (void)tty_set_active(0);
            return;
        }
        if (scancode == 0x42) {
            (void)tty_set_active(7);
            return;
        }
    }

    if (scancode >= 58u)
        return;

    char ch = normal_map[scancode];
    if (!ch)
        return;

    if (g_ctrl_down) {
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 1);
        else if (ch == '\\')
            ch = 28;
        else if (ch == '[')
            ch = 27;
        else if (ch == ']')
            ch = 29;
    } else if (ch >= 'a' && ch <= 'z') {
        if ((g_shift_down != 0) ^ (g_caps_lock != 0))
            ch = shift_map[scancode];
    } else if (g_shift_down != 0) {
        ch = shift_map[scancode];
    }

    int active = tty_get_active();
    if (active >= 0 && active < TTY_MAX && g_ttys[active].used) {
        tty_ldisc_input_bytes(&g_ttys[active].ldisc, &ch, 1,
                              tty_echo_cb, tty_sig_cb,
                              (void *)(intptr_t)active,
                              &g_ttys[active].waitq);
    }
}
