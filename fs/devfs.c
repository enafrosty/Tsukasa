/*
 * Project Tsukasa — Device filesystem (DevFS)
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

#include "devfs.h"
#include "../drv/fb.h"
#include "../drv/input_dev.h"
#include "../tty/tty.h"
#include "../include/errno.h"
#include "../include/vfs_abi.h"
#include <stddef.h>
#include <stdint.h>

#define DEVFS_MAX_DEVICES 32

typedef struct devfs_node_entry {
    char name[VFS_NAME_MAX];
    uint32_t type;
    const devfs_ops_t *ops;
    void *priv;
    int used;
} devfs_node_entry_t;

static devfs_node_entry_t g_dev_nodes[DEVFS_MAX_DEVICES];

static int dev_streq(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void dev_strncpy(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    if (!dst || max == 0)
        return;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/* /dev/fb0 operations */

static size_t dev_fb_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)flags;
    return fb_read(pos, buf, count);
}

static size_t dev_fb_write(void *priv, size_t pos, const void *buf, size_t count)
{
    (void)priv;
    return fb_write(pos, buf, count);
}

static int dev_fb_ioctl(void *priv, unsigned long request, void *arg)
{
    (void)priv;
    return fb_ioctl(request, arg);
}

static void *dev_fb_mmap(void *priv, void *addr, size_t length, int prot, int flags, size_t offset)
{
    (void)priv;
    return fb_mmap(addr, length, prot, flags, offset);
}

static int dev_fb_munmap(void *priv, void *addr, size_t length)
{
    (void)priv;
    return fb_munmap(addr, length);
}

static int dev_fb_poll(void *priv, int events)
{
    (void)priv;
    return fb_poll(events);
}

static size_t dev_fb_size(void *priv)
{
    (void)priv;
    return fb_byte_size();
}

static const devfs_ops_t g_fb_ops = {
    .open = NULL,
    .close = NULL,
    .read = dev_fb_read,
    .write = dev_fb_write,
    .ioctl = dev_fb_ioctl,
    .mmap = dev_fb_mmap,
    .munmap = dev_fb_munmap,
    .poll = dev_fb_poll,
    .size = dev_fb_size,
};

/* /dev/null operations */

static size_t dev_null_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    (void)buf;
    (void)count;
    (void)flags;
    return 0;
}

static size_t dev_null_write(void *priv, size_t pos, const void *buf, size_t count)
{
    (void)priv;
    (void)pos;
    (void)buf;
    return count;
}

static int dev_null_poll(void *priv, int events)
{
    (void)priv;
    return events & (VFS_POLLIN | VFS_POLLOUT);
}

static const devfs_ops_t g_null_ops = {
    .open = NULL,
    .close = NULL,
    .read = dev_null_read,
    .write = dev_null_write,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_null_poll,
    .size = NULL,
};

/* /dev/zero operations */

static size_t dev_zero_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    (void)flags;
    if (!buf)
        return 0;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = 0;
    return count;
}

static size_t dev_zero_write(void *priv, size_t pos, const void *buf, size_t count)
{
    (void)priv;
    (void)pos;
    (void)buf;
    return count;
}

static int dev_zero_poll(void *priv, int events)
{
    (void)priv;
    return events & (VFS_POLLIN | VFS_POLLOUT);
}

static const devfs_ops_t g_zero_ops = {
    .open = NULL,
    .close = NULL,
    .read = dev_zero_read,
    .write = dev_zero_write,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_zero_poll,
    .size = NULL,
};

/* /dev/urandom operations */

static uint32_t g_urandom_state = 0x853c49e6;

static size_t dev_urandom_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    (void)flags;
    if (!buf)
        return 0;
    uint8_t *b = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        g_urandom_state = g_urandom_state * 1664525u + 1013904223u;
        b[i] = (uint8_t)(g_urandom_state >> 16);
    }
    return count;
}

static size_t dev_urandom_write(void *priv, size_t pos, const void *buf, size_t count)
{
    (void)priv;
    (void)pos;
    (void)buf;
    return count;
}

static int dev_urandom_poll(void *priv, int events)
{
    (void)priv;
    return events & (VFS_POLLIN | VFS_POLLOUT);
}

static const devfs_ops_t g_urandom_ops = {
    .open = NULL,
    .close = NULL,
    .read = dev_urandom_read,
    .write = dev_urandom_write,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_urandom_poll,
    .size = NULL,
};

/* /dev/tty operations */

static size_t dev_tty_write(void *priv, size_t pos, const void *buf, size_t count)
{
    (void)pos;
    int index = (int)(intptr_t)priv;
#ifdef __x86_64__
    return tty_write(index, buf, count);
#else
    (void)index;
    (void)buf;
    (void)count;
    return 0;
#endif
}

static int dev_tty_poll(void *priv, int events)
{
    (void)priv;
    return events & (VFS_POLLIN | VFS_POLLOUT);
}

static const devfs_ops_t g_tty_ops = {
    .open = NULL,
    .close = NULL,
    .read = NULL,
    .write = dev_tty_write,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_tty_poll,
    .size = NULL,
};

/* /dev/keyboard operations */

static int dev_kbd_open(void *priv, int flags)
{
    (void)priv;
    (void)flags;
    input_dev_open(INPUT_DEV_KEYBOARD);
    return 0;
}

static void dev_kbd_close(void *priv)
{
    (void)priv;
    input_dev_close(INPUT_DEV_KEYBOARD);
}

static size_t dev_kbd_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    (void)flags;
    return input_dev_read(INPUT_DEV_KEYBOARD, buf, count);
}

static int dev_kbd_poll(void *priv, int events)
{
    (void)priv;
    int revents = events & VFS_POLLOUT;
    if (input_dev_poll_readable(INPUT_DEV_KEYBOARD))
        revents |= (events & VFS_POLLIN);
    return revents;
}

static const devfs_ops_t g_kbd_ops = {
    .open = dev_kbd_open,
    .close = dev_kbd_close,
    .read = dev_kbd_read,
    .write = NULL,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_kbd_poll,
    .size = NULL,
};

/* /dev/mouse operations */

static int dev_mouse_open(void *priv, int flags)
{
    (void)priv;
    (void)flags;
    input_dev_open(INPUT_DEV_MOUSE);
    return 0;
}

static void dev_mouse_close(void *priv)
{
    (void)priv;
    input_dev_close(INPUT_DEV_MOUSE);
}

static size_t dev_mouse_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    (void)flags;
    return input_dev_read(INPUT_DEV_MOUSE, buf, count);
}

static int dev_mouse_poll(void *priv, int events)
{
    (void)priv;
    int revents = events & VFS_POLLOUT;
    if (input_dev_poll_readable(INPUT_DEV_MOUSE))
        revents |= (events & VFS_POLLIN);
    return revents;
}

static const devfs_ops_t g_mouse_ops = {
    .open = dev_mouse_open,
    .close = dev_mouse_close,
    .read = dev_mouse_read,
    .write = NULL,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_mouse_poll,
    .size = NULL,
};

/* /dev/input/events operations */

static int dev_input_open(void *priv, int flags)
{
    (void)priv;
    (void)flags;
    input_dev_open(INPUT_DEV_UNIFIED);
    return 0;
}

static void dev_input_close(void *priv)
{
    (void)priv;
    input_dev_close(INPUT_DEV_UNIFIED);
}

static size_t dev_input_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)priv;
    (void)pos;
    return input_dev_read_events(buf, count, flags);
}

static int dev_input_poll(void *priv, int events)
{
    (void)priv;
    return input_dev_poll_events(events);
}

static const devfs_ops_t g_input_events_ops = {
    .open = dev_input_open,
    .close = dev_input_close,
    .read = dev_input_read,
    .write = NULL,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = dev_input_poll,
    .size = NULL,
};

/* DevFS subsystem API */

int devfs_register_device(const char *name, uint32_t type, const devfs_ops_t *ops, void *priv)
{
    if (!name || !ops)
        return -1;

    for (size_t i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (g_dev_nodes[i].used && dev_streq(g_dev_nodes[i].name, name)) {
            g_dev_nodes[i].type = type;
            g_dev_nodes[i].ops = ops;
            g_dev_nodes[i].priv = priv;
            return 0;
        }
    }

    for (size_t i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (!g_dev_nodes[i].used) {
            dev_strncpy(g_dev_nodes[i].name, name, VFS_NAME_MAX);
            g_dev_nodes[i].type = type;
            g_dev_nodes[i].ops = ops;
            g_dev_nodes[i].priv = priv;
            g_dev_nodes[i].used = 1;
            return 0;
        }
    }
    return -1;
}

void devfs_init(void)
{
    input_dev_init();

    for (size_t i = 0; i < DEVFS_MAX_DEVICES; i++)
        g_dev_nodes[i].used = 0;

    devfs_register_device("fb0", VFS_TYPE_CHAR, &g_fb_ops, NULL);
    devfs_register_device("null", VFS_TYPE_CHAR, &g_null_ops, NULL);
    devfs_register_device("zero", VFS_TYPE_CHAR, &g_zero_ops, NULL);
    devfs_register_device("urandom", VFS_TYPE_CHAR, &g_urandom_ops, NULL);
    devfs_register_device("tty", VFS_TYPE_CHAR, &g_tty_ops, (void *)(intptr_t)-1);

    char tty_name[8];
    for (int i = 0; i <= 9; i++) {
        tty_name[0] = 't';
        tty_name[1] = 't';
        tty_name[2] = 'y';
        tty_name[3] = (char)('0' + i);
        tty_name[4] = '\0';
        devfs_register_device(tty_name, VFS_TYPE_CHAR, &g_tty_ops, (void *)(intptr_t)i);
    }

    devfs_register_device("keyboard", VFS_TYPE_CHAR, &g_kbd_ops, NULL);
    devfs_register_device("mouse", VFS_TYPE_CHAR, &g_mouse_ops, NULL);

    devfs_register_device("input/events", VFS_TYPE_CHAR, &g_input_events_ops, NULL);
    devfs_register_device("events", VFS_TYPE_CHAR, &g_input_events_ops, NULL);
}

int devfs_lookup(const char *subpath, const devfs_ops_t **ops_out, void **priv_out, uint32_t *type_out)
{
    const char *name = subpath;
    if (!name)
        return -1;
    while (*name == '/')
        name++;

    for (size_t i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (g_dev_nodes[i].used && dev_streq(g_dev_nodes[i].name, name)) {
            if (ops_out)
                *ops_out = g_dev_nodes[i].ops;
            if (priv_out)
                *priv_out = g_dev_nodes[i].priv;
            if (type_out)
                *type_out = g_dev_nodes[i].type;
            return 0;
        }
    }
    return -1;
}

int devfs_stat(const char *subpath, vfs_stat_t *out)
{
    if (!subpath || !out)
        return -1;

    const char *p = subpath;
    while (*p == '/')
        p++;

    if (dev_streq(p, "") || dev_streq(p, "input")) {
        out->size = 0;
        out->blocks = 0;
        out->type = VFS_TYPE_DIR;
        out->mode = VFS_MODE_READ;
        return 0;
    }

    const devfs_ops_t *ops = NULL;
    void *priv = NULL;
    uint32_t type = VFS_TYPE_CHAR;
    if (devfs_lookup(subpath, &ops, &priv, &type) != 0)
        return -1;

    size_t sz = 0;
    if (ops && ops->size)
        sz = ops->size(priv);

    out->size = sz;
    out->blocks = (sz + 511) / 512;
    out->type = type;
    out->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    return 0;
}

int devfs_list(const char *subpath, char names[][VFS_NAME_MAX], int max)
{
    int count = 0;
    if (!subpath || !names || max <= 0)
        return -1;
    while (*subpath == '/')
        subpath++;

    if (*subpath == '\0') {
        for (size_t i = 0; i < DEVFS_MAX_DEVICES && count < max; i++) {
            if (g_dev_nodes[i].used && !dev_streq(g_dev_nodes[i].name, "input/events")) {
                dev_strncpy(names[count], g_dev_nodes[i].name, VFS_NAME_MAX);
                count++;
            }
        }
        if (count < max) {
            dev_strncpy(names[count], "input", VFS_NAME_MAX);
            count++;
        }
        return count;
    }

    if (dev_streq(subpath, "input")) {
        if (count < max) {
            dev_strncpy(names[count], "events", VFS_NAME_MAX);
            count++;
        }
        return count;
    }

    return -1;
}
