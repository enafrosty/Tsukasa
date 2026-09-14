/*
 * Project Tsukasa — Minimal tty foreground control, Ctrl+C signalling, and the
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

#include "../drv/serial.h"
#include "../fs/vfs.h"
#include "../include/spinlock.h"
#include "../proc/process.h"
#include "../sys/kconsole.h"

#include <stddef.h>

#define TTY_MAX 8

struct tty_slot {
    int used;
    int fg_pgid;
};

static struct tty_slot g_ttys[TTY_MAX];
static int g_active_tty = 0;
static int g_ctrl_down = 0;
static spinlock_t g_tty_lock = SPINLOCK_INIT;

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

void tty_init(void)
{
    uint64_t flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    for (int i = 0; i < TTY_MAX; i++) {
        g_ttys[i].used = 0;
        g_ttys[i].fg_pgid = -1;
    }
    g_ttys[0].used = 1;
    g_ttys[0].fg_pgid = -1;
    g_active_tty = 0;
    g_ctrl_down = 0;
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
            g_ttys[i].used = 1;
            g_ttys[i].fg_pgid = -1;
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
    if (tty_id < 0 || tty_id >= TTY_MAX || tty_id == 0)
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
    if (tty_id < 0 || tty_id >= TTY_MAX)
        return -1;
    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (!g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return -1;
    }
    g_active_tty = tty_id;
    spin_unlock(&g_tty_lock);
    irq_restore(flags);
    return 0;
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

size_t tty_write(int tty_id, const void *buf, size_t count)
{
    const char *src = (const char *)buf;
    int mirror;
    uint64_t flags;

    if (!src || count == 0)
        return 0;

    flags = irq_save_disable();
    spin_lock(&g_tty_lock);
    if (tty_id < 0)
        tty_id = g_active_tty;
    if (tty_id >= TTY_MAX || !g_ttys[tty_id].used) {
        spin_unlock(&g_tty_lock);
        irq_restore(flags);
        return 0;
    }
    mirror = (tty_id == g_active_tty) && (vfs_kd_mode() == VFS_KD_TEXT);
    spin_unlock(&g_tty_lock);
    irq_restore(flags);

    /* Emit outside g_tty_lock: serial_putc busy-waits on the UART and kconsole_putc takes its own console lock. */
    for (size_t i = 0; i < count; i++) {
        serial_putc(src[i]);
        if (mirror)
            kconsole_putc(src[i]);
    }
    return count;
}

void tty_handle_scancode(uint8_t scancode, int pressed)
{
    int active_tty;
    if (scancode == 29) {
        g_ctrl_down = pressed ? 1 : 0;
        return;
    }

    if (!pressed)
        return;

    if (g_ctrl_down && scancode == 46) {
        active_tty = tty_get_active();
        tty_kill_foreground(active_tty, PROCESS_SIGINT);
    }
}
