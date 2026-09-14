/*
 * Project Tsukasa — Virtual File System core with per-process FD tables
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

#include "vfs.h"

#include "bootfs.h"
#include "devfs.h"
#include "fat12.h"
#include "fat32.h"
#include "initrd.h"
#include "memfs.h"
#include "procfs.h"
#include "sysfs.h"
#include "tar.h"
#include "tar_testdata.h"

#include "../drv/ata.h"
#include "../drv/blockdev.h"
#include "../drv/diskmgr.h"
#include "mkfs_fat32.h"
#include "../drv/fb.h"
#include "../drv/input_dev.h"
#include "../include/boot_info.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/multiboot.h"
#include "../ipc/unix_socket.h"
#include "../mm/heap.h"
#include "../proc/process.h"
#include "../tty/tty.h"
#include "../sys/wait_queue.h"
#ifdef __x86_64__
#include "../include/paging.h"
#include "../mm/vm_space.h"      /* map the fb into a ring-3 address space   */
#include "../mm/vmm_x64.h"       /* vmm_query_page - idempotent fb re-mmap    */
#include "../mm/pmm.h"

#define VFS_FB_USER_BASE 0x0000000030000000ULL
#endif

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS        16
#define VFS_MAX_OPEN_GLOBAL   128
#define VFS_MAX_PIPES         32
#define VFS_PIPE_CAPACITY     4096

typedef enum vfs_backend {
    VFS_BACKEND_NONE = 0,
    VFS_BACKEND_INITRD,
    VFS_BACKEND_FAT12,
    VFS_BACKEND_MEMFS,
    VFS_BACKEND_FAT32,
    VFS_BACKEND_FAT32VOL,
    VFS_BACKEND_PROCFS,
    VFS_BACKEND_SYSFS,
    VFS_BACKEND_BOOTFS,
    VFS_BACKEND_TAR,
    VFS_BACKEND_DEVFS,
    VFS_BACKEND_PIPE,
    VFS_BACKEND_UNIXSOCK
} vfs_backend_t;

typedef enum vfs_device_kind {
    VFS_DEV_NONE = 0,
    VFS_DEV_FB0,
    VFS_DEV_TTY,
    VFS_DEV_KEYBOARD,
    VFS_DEV_MOUSE
} vfs_device_kind_t;

typedef struct vfs_mount {
    int used;
    char path[VFS_PATH_MAX];
    int path_len;
    vfs_backend_t backend;
    int read_only;
    const char *name;
    void *ctx;
} vfs_mount_t;

typedef struct vfs_pipe {
    int used;
    uint8_t data[VFS_PIPE_CAPACITY];
    size_t read_pos;
    size_t write_pos;
    size_t size;
    int readers;
    int writers;
    wait_queue_head_t waitq;
} vfs_pipe_t;

typedef struct vfs_file {
    int used;
    int refcount;
    int flags;
    int mode;
    int dirty;
    vfs_backend_t backend;
    void *fs_ctx;
    size_t pos;

    char path[VFS_PATH_MAX];

    union {
        struct {
            uint8_t *buf;
            size_t size;
            size_t capacity;
            int owns_buf;
        } regular;
        struct {
            int inode;
        } memfs;
        struct {
            vfs_pipe_t *pipe;
            int can_read;
            int can_write;
        } pipe;
        struct {
            const devfs_ops_t *ops;
            void *priv;
            vfs_device_kind_t kind;
            int index;
        } device;
        struct {
            /* The bound path lives in f->path (reused for unregister-on-close). */
            vfs_pipe_t *rx;
            vfs_pipe_t *tx;
            unix_listener_t *listener;
            int is_listener;
        } sock;
    } u;
} vfs_file_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static vfs_file_t g_open_files[VFS_MAX_OPEN_GLOBAL];
static vfs_pipe_t g_pipes[VFS_MAX_PIPES];

static void *g_kernel_open_files[PROCESS_MAX_OPEN_FILES];
static char g_kernel_cwd[VFS_PATH_MAX] = "/";

static int g_fat12_ok;
static int g_fat32_ok;
static int g_initrd_ok;

/* String/path helpers */

static int kstrlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int kstrcmp(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int kstreq(const char *a, const char *b)
{
    return kstrcmp(a, b) == 0;
}

static int kstrncpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return -1;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] ? -1 : 0;
}

static int path_is_abs(const char *path)
{
    return path && path[0] == '/';
}

static int path_copy_join(char *out, int cap, const char *base, const char *extra)
{
    int oi = 0;
    int i = 0;
    if (!out || cap <= 1)
        return -1;
    out[0] = '\0';

    if (base && base[0]) {
        while (base[i] && oi < cap - 1)
            out[oi++] = base[i++];
    }

    if (oi == 0 || out[oi - 1] != '/') {
        if (oi >= cap - 1)
            return -1;
        out[oi++] = '/';
    }

    i = 0;
    while (extra && extra[i] && oi < cap - 1)
        out[oi++] = extra[i++];
    out[oi] = '\0';
    return (extra && extra[i]) ? -1 : 0;
}

static void **fd_table_for_process(process_t *proc)
{
    if (proc)
        return proc->open_files;
    return g_kernel_open_files;
}

static char *cwd_for_process(process_t *proc)
{
    if (proc)
        return proc->cwd;
    return g_kernel_cwd;
}

static int normalize_path_for_process(process_t *proc, const char *path, char *out, int cap)
{
    char raw[VFS_PATH_MAX];
    char segs[32][VFS_NAME_MAX];
    int seg_count = 0;
    int i = 0;
    int oi = 0;
    const char *scan = NULL;

    if (!path || !out || cap <= 1)
        return -1;

    if (path_is_abs(path)) {
        if (kstrncpy(raw, path, VFS_PATH_MAX) != 0)
            return -1;
    } else {
        char *cwd = cwd_for_process(proc);
        if (path_copy_join(raw, VFS_PATH_MAX, cwd && cwd[0] ? cwd : "/", path) != 0)
            return -1;
    }

    scan = raw;
    while (*scan == '/')
        scan++;
    while (*scan) {
        char seg[VFS_NAME_MAX];
        int si = 0;
        while (*scan && *scan != '/') {
            if (si < VFS_NAME_MAX - 1)
                seg[si++] = *scan;
            scan++;
        }
        seg[si] = '\0';
        while (*scan == '/')
            scan++;

        if (seg[0] == '\0' || (seg[0] == '.' && seg[1] == '\0'))
            continue;
        if (seg[0] == '.' && seg[1] == '.' && seg[2] == '\0') {
            if (seg_count > 0)
                seg_count--;
            continue;
        }
        if (seg_count >= 32)
            return -1;
        kstrncpy(segs[seg_count], seg, VFS_NAME_MAX);
        seg_count++;
    }

    out[oi++] = '/';
    if (seg_count == 0) {
        out[oi] = '\0';
        return 0;
    }
    for (i = 0; i < seg_count; i++) {
        int j = 0;
        if (i > 0) {
            if (oi >= cap - 1)
                return -1;
            out[oi++] = '/';
        }
        while (segs[i][j]) {
            if (oi >= cap - 1)
                return -1;
            out[oi++] = segs[i][j++];
        }
    }
    out[oi] = '\0';
    return 0;
}

static int fd_valid(int fd)
{
    return fd >= 0 && fd < PROCESS_MAX_OPEN_FILES;
}

static int flags_to_mode(int flags)
{
    int has_rd = (flags & VFS_O_RDONLY) != 0;
    int has_wr = (flags & VFS_O_WRONLY) != 0;
    int mode = 0;
    if (!has_rd && !has_wr)
        has_rd = 1;
    if (has_rd)
        mode |= VFS_MODE_READ;
    if (has_wr)
        mode |= VFS_MODE_WRITE;
    return mode;
}

static process_t *vfs_current_process(void)
{
#ifdef __x86_64__
    return process_current();
#else
    return NULL;
#endif
}

/* Mount table */

static void mount_table_reset(void)
{
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].used = 0;
        g_mounts[i].path[0] = '\0';
        g_mounts[i].path_len = 0;
        g_mounts[i].backend = VFS_BACKEND_NONE;
        g_mounts[i].read_only = 1;
        g_mounts[i].name = "none";
    }
}

static int mount_register_ctx(const char *path, vfs_backend_t backend,
                              int read_only, const char *name, void *ctx)
{
    char norm[VFS_PATH_MAX];
    if (normalize_path_for_process(NULL, path, norm, VFS_PATH_MAX) != 0)
        return -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) {
            g_mounts[i].used = 1;
            kstrncpy(g_mounts[i].path, norm, VFS_PATH_MAX);
            g_mounts[i].path_len = kstrlen(g_mounts[i].path);
            g_mounts[i].backend = backend;
            g_mounts[i].read_only = read_only ? 1 : 0;
            g_mounts[i].name = name ? name : "unknown";
            g_mounts[i].ctx = ctx;
            return 0;
        }
    }
    return -1;
}

static int mount_register(const char *path, vfs_backend_t backend, int read_only, const char *name)
{
    return mount_register_ctx(path, backend, read_only, name, NULL);
}

static const vfs_mount_t *mount_lookup(const char *path)
{
    const vfs_mount_t *best = NULL;
    int best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        const vfs_mount_t *m = &g_mounts[i];
        int j = 0;
        if (!m->used)
            continue;
        if (m->path_len == 1 && m->path[0] == '/') {
            if (best_len < 1) {
                best = m;
                best_len = 1;
            }
            continue;
        }
        while (j < m->path_len && path[j] && path[j] == m->path[j])
            j++;
        if (j != m->path_len)
            continue;
        if (path[j] != '\0' && path[j] != '/')
            continue;
        if (m->path_len > best_len) {
            best = m;
            best_len = m->path_len;
        }
    }
    return best;
}

static int mount_subpath(const vfs_mount_t *m, const char *abs_path, char *out, int cap)
{
    const char *p;
    if (!m || !abs_path || !out || cap <= 1)
        return -1;

    if (m->path_len == 1 && m->path[0] == '/') {
        if (kstrncpy(out, abs_path, cap) != 0)
            return -1;
        return 0;
    }

    p = abs_path + m->path_len;
    if (*p == '\0') {
        return kstrncpy(out, "/", cap);
    }
    return kstrncpy(out, p, cap);
}

int vfs_get_mounts(vfs_mount_info_t *out, int max)
{
    int count = 0;
    if (!out || max <= 0)
        return -1;
    for (int i = 0; i < VFS_MAX_MOUNTS && count < max; i++) {
        if (!g_mounts[i].used)
            continue;
        kstrncpy(out[count].path, g_mounts[i].path, VFS_PATH_MAX);
        kstrncpy(out[count].fs_name, g_mounts[i].name, (int)sizeof(out[count].fs_name));
        out[count].read_only = g_mounts[i].read_only;
        count++;
    }
    return count;
}

/* Open-file / pipe internals */

static void file_pool_reset(void)
{
    for (int i = 0; i < VFS_MAX_OPEN_GLOBAL; i++) {
        g_open_files[i].used = 0;
        g_open_files[i].refcount = 0;
    }
    for (int i = 0; i < VFS_MAX_PIPES; i++) {
        g_pipes[i].used = 0;
        g_pipes[i].read_pos = 0;
        g_pipes[i].write_pos = 0;
        g_pipes[i].size = 0;
        g_pipes[i].readers = 0;
        g_pipes[i].writers = 0;
    }
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++)
        g_kernel_open_files[i] = NULL;
    kstrncpy(g_kernel_cwd, "/", VFS_PATH_MAX);
}

static vfs_file_t *file_alloc(void)
{
    for (int i = 0; i < VFS_MAX_OPEN_GLOBAL; i++) {
        vfs_file_t *f = &g_open_files[i];
        if (!f->used) {
            for (size_t j = 0; j < sizeof(*f); j++)
                ((uint8_t *)f)[j] = 0;
            f->used = 1;
            f->refcount = 1;
            return f;
        }
    }
    return NULL;
}

static vfs_pipe_t *pipe_alloc(void)
{
    for (int i = 0; i < VFS_MAX_PIPES; i++) {
        vfs_pipe_t *p = &g_pipes[i];
        if (!p->used) {
            p->used = 1;
            p->read_pos = 0;
            p->write_pos = 0;
            p->size = 0;
            p->readers = 0;
            p->writers = 0;
            wait_queue_init(&p->waitq);
            return p;
        }
    }
    return NULL;
}

static size_t pipe_ring_read(vfs_pipe_t *p, void *buf, size_t count)
{
    uint8_t *dst = (uint8_t *)buf;
    size_t got = 0;
    if (!p || !buf)
        return 0;
    while (got < count && p->size > 0) {
        dst[got++] = p->data[p->read_pos];
        p->read_pos = (p->read_pos + 1) % VFS_PIPE_CAPACITY;
        p->size--;
    }
    return got;
}

static size_t pipe_ring_write(vfs_pipe_t *p, const void *buf, size_t count)
{
    const uint8_t *src = (const uint8_t *)buf;
    size_t wr = 0;
    if (!p || !buf)
        return 0;
    while (wr < count && p->size < VFS_PIPE_CAPACITY) {
        p->data[p->write_pos] = src[wr++];
        p->write_pos = (p->write_pos + 1) % VFS_PIPE_CAPACITY;
        p->size++;
    }
    return wr;
}

/* Drop one end's claim on a socket pipe; free the slot when both sides are gone (the exact rule file_release uses). */
static void sock_drop_pipe_claim(vfs_pipe_t *p, int drop_reader, int drop_writer)
{
    if (!p)
        return;
    if (drop_reader && p->readers > 0)
        p->readers--;
    if (drop_writer && p->writers > 0)
        p->writers--;
    wait_queue_wake_all(&p->waitq);
    if (p->readers == 0 && p->writers == 0)
        p->used = 0;
}

static int ensure_regular_capacity(vfs_file_t *f, size_t need)
{
    size_t new_cap;
    uint8_t *new_buf;
    if (!f || f->backend == VFS_BACKEND_MEMFS || f->backend == VFS_BACKEND_PIPE)
        return -1;
    if (need <= f->u.regular.capacity)
        return 0;
    new_cap = f->u.regular.capacity ? f->u.regular.capacity : 64;
    while (new_cap < need)
        new_cap *= 2;
    new_buf = (uint8_t *)kmalloc(new_cap);
    if (!new_buf)
        return -1;
    for (size_t i = 0; i < f->u.regular.size; i++)
        new_buf[i] = f->u.regular.buf ? f->u.regular.buf[i] : 0;
    if (f->u.regular.owns_buf && f->u.regular.buf)
        kfree(f->u.regular.buf);
    f->u.regular.buf = new_buf;
    f->u.regular.capacity = new_cap;
    f->u.regular.owns_buf = 1;
    return 0;
}

static int flush_regular_file(vfs_file_t *f)
{
    if (!f || !f->dirty)
        return 0;
    if (!(f->mode & VFS_MODE_WRITE))
        return 0;
    switch (f->backend) {
    case VFS_BACKEND_FAT12:
        if (fat12_write_file(f->path, f->u.regular.buf, f->u.regular.size) != 0)
            return -1;
        break;
    case VFS_BACKEND_FAT32:
        if (fat32_write_file(f->path, f->u.regular.buf, f->u.regular.size) < 0)
            return -1;
        break;
    case VFS_BACKEND_FAT32VOL:
        if (fat32_vol_write_file((fat32_volume_t *)f->fs_ctx, f->path,
                                 f->u.regular.buf, f->u.regular.size) < 0)
            return -1;
        break;
    default:
        break;
    }
    f->dirty = 0;
    return 0;
}

static void file_release(vfs_file_t *f)
{
    if (!f || !f->used)
        return;
    if (f->refcount > 1) {
        f->refcount--;
        return;
    }

    (void)flush_regular_file(f);

    if (f->backend == VFS_BACKEND_PIPE && f->u.pipe.pipe) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (f->u.pipe.can_read && p->readers > 0)
            p->readers--;
        if (f->u.pipe.can_write && p->writers > 0)
            p->writers--;
        wait_queue_wake_all(&p->waitq);
        if (p->readers == 0 && p->writers == 0)
            p->used = 0;
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener) {
            /* Drain not-yet-accepted connections: each pending record carries the SERVER-side claims (reader on c2s,... */
            unix_pending_conn_t pc;
            while (f->u.sock.listener &&
                   unix_dequeue_pending(f->u.sock.listener, &pc) == 0) {
                sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe1, 1, 0);
                sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe2, 0, 1);
            }
            if (f->path[0])
                unix_unregister_listener(f->path);
        } else {
            sock_drop_pipe_claim(f->u.sock.rx, 1, 0);
            sock_drop_pipe_claim(f->u.sock.tx, 0, 1);
        }
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->close)
            f->u.device.ops->close(f->u.device.priv);
    }

    if (f->u.regular.owns_buf && f->u.regular.buf)
        kfree(f->u.regular.buf);
    f->used = 0;
    f->refcount = 0;
}

static int process_fd_alloc(process_t *proc)
{
    void **tbl = fd_table_for_process(proc);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        if (!tbl[i])
            return i;
    }
    return -1;
}

static vfs_file_t *fd_lookup(process_t *proc, int fd)
{
    void **tbl = fd_table_for_process(proc);
    if (!fd_valid(fd))
        return NULL;
    return (vfs_file_t *)tbl[fd];
}

static void fd_close_on_table(void **tbl, int fd)
{
    vfs_file_t *f;
    if (!tbl || !fd_valid(fd))
        return;
    f = (vfs_file_t *)tbl[fd];
    if (!f)
        return;
    tbl[fd] = NULL;
    file_release(f);
}

static vfs_file_t *fd_lookup_on_table(void **tbl, int fd)
{
    if (!tbl || !fd_valid(fd))
        return NULL;
    return (vfs_file_t *)tbl[fd];
}

/* DevFS operations are handled via fs/devfs.c */

/* Backend stat/list/read helpers */

static int backend_stat(const vfs_mount_t *m, const char *subpath, vfs_stat_t *out)
{
    if (!m || !subpath || !out)
        return -1;
    out->size = 0;
    out->blocks = 0;
    out->type = VFS_TYPE_UNKNOWN;
    out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);

    if (kstreq(subpath, "/")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }

    switch (m->backend) {
    case VFS_BACKEND_MEMFS:
    {
        size_t sz = 0;
        const char *name = subpath;
        while (*name == '/')
            name++;
        if (memfs_stat(name, &sz) != 0)
            return -1;
        out->type = VFS_TYPE_FILE;
        out->size = sz;
        out->blocks = (sz + 511) / 512;
        return 0;
    }
    case VFS_BACKEND_FAT12:
    {
        fat12_dirent_t de;
        if (!g_fat12_ok)
            return -1;
        if (fat12_stat(subpath, &de) != 0)
            return -1;
        out->type = de.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = de.size;
        out->blocks = (de.size + 511) / 512;
        return 0;
    }
    case VFS_BACKEND_FAT32:
    {
        fat32_dirent_t de;
        if (!g_fat32_ok)
            return -1;
        if (fat32_stat(subpath, &de) != 0)
            return -1;
        out->type = de.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = de.size;
        out->blocks = (de.size + 511) / 512;
        return 0;
    }
    case VFS_BACKEND_FAT32VOL:
    {
        fat32_dirent_t de;
        if (fat32_vol_stat((fat32_volume_t *)m->ctx, subpath, &de) != 0)
            return -1;
        out->type = de.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = de.size;
        out->blocks = (de.size + 511) / 512;
        return 0;
    }
    case VFS_BACKEND_INITRD:
    {
        const void *data = NULL;
        size_t sz = 0;
        if (initrd_lookup(subpath, &data, &sz) != 0)
            return -1;
        out->type = VFS_TYPE_FILE;
        out->size = sz;
        out->blocks = (sz + 511) / 512;
        return 0;
    }
    case VFS_BACKEND_PROCFS:
        return procfs_stat(subpath, out);
    case VFS_BACKEND_SYSFS:
        return sysfs_stat(subpath, out);
    case VFS_BACKEND_BOOTFS:
        return bootfs_stat(subpath, out);
    case VFS_BACKEND_DEVFS:
        return devfs_stat(subpath, out);
    case VFS_BACKEND_TAR:
        return tar_stat((tar_fs_t *)m->ctx, subpath, out);
    default:
        return -1;
    }
}

static int append_root_mount_names(char names[][VFS_NAME_MAX], int count, int max)
{
    for (int i = 0; i < VFS_MAX_MOUNTS && count < max; i++) {
        const vfs_mount_t *m = &g_mounts[i];
        const char *base;
        int j = 0;
        int exists = 0;
        if (!m->used)
            continue;
        if (m->path_len <= 1)
            continue;
        base = m->path + 1;
        while (base[j] && base[j] != '/')
            j++;
        if (j <= 0)
            continue;
        for (int n = 0; n < count; n++) {
            int k = 0;
            while (k < j && names[n][k] && names[n][k] == base[k])
                k++;
            if (k == j && names[n][k] == '\0') {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;
        for (int k = 0; k < j && k < VFS_NAME_MAX - 1; k++)
            names[count][k] = base[k];
        names[count][(j < VFS_NAME_MAX - 1) ? j : (VFS_NAME_MAX - 1)] = '\0';
        count++;
    }
    return count;
}

/* Public init */

void vfs_init(const void *boot_info)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)boot_info;
    const void *initrd_data = NULL;
    size_t initrd_size = 0;

    mount_table_reset();
    file_pool_reset();
    memfs_init();
    procfs_init();
    sysfs_init();
    devfs_init();
    bootfs_init(boot_info);

    g_fat12_ok = 0;
    if (tsukasa_boot_info_is_valid(boot_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)boot_info;
        if (bi->module_count > 0 && bi->modules) {
            for (uint32_t i = 0; i < bi->module_count; i++) {
                void *disk = (void *)(uintptr_t)bi->modules[i].address;
                size_t sz = (size_t)bi->modules[i].size;
                if (fat12_init(disk, sz) == 0) {
                    g_fat12_ok = 1;
                    break;
                }
            }
        }
    } else if (mb && (mb->flags & 8) && mb->mods_count > 0) {
        const struct multiboot_mod_list *mod =
            (const struct multiboot_mod_list *)(uintptr_t)mb->mods_addr;
        for (uint32_t i = 0; i < mb->mods_count; i++) {
            void *disk = (void *)(uintptr_t)mod[i].mod_start;
            size_t sz = mod[i].mod_end - mod[i].mod_start;
            if (fat12_init(disk, sz) == 0) {
                g_fat12_ok = 1;
                break;
            }
        }
    }
    kprintf("[vfs] FAT12 ramdisk: %s\n", g_fat12_ok ? "ok" : "not found");

    g_fat32_ok = 0;
    /* Storage bring-up (guides 11+12): the drivers register whole disks (ATA PIO first, then AHCI ports), the... */
    if (!ata_init())
        kprintf("[ata] no drive detected\n");
    diskmgr_scan_all();
    for (int di = 0; di < blockdev_count(); di++) {
        block_dev_t *bd = blockdev_at(di);
        fat32_volume_t *vol;
        char mnt[VFS_PATH_MAX];

        if (!bd || !bd->is_partition || !bd->is_fat32)
            continue;
        vol = fat32_vol_mount(bd);
        if (!vol) {
            kprintf("[vfs] %s: FAT32 probe ok but volume mount failed\n",
                    bd->name);
            continue;
        }
        mnt[0] = '/';
        mnt[1] = 'd';
        mnt[2] = 'e';
        mnt[3] = 'v';
        mnt[4] = '/';
        kstrncpy(mnt + 5, bd->name, VFS_PATH_MAX - 5);
        if (mount_register_ctx(mnt, VFS_BACKEND_FAT32VOL, 0, "fat32", vol) == 0)
            kprintf("[vfs] FAT32 %s: mounted rw (%u MiB)\n",
                    mnt, (uint32_t)(bd->sector_count / 2048u));
    }
    if (blockdev_first_raw_fat32()) {
        if (fat32_init() == 0)
            g_fat32_ok = 1;
        kprintf("[vfs] FAT32 /disk: %s\n", g_fat32_ok ? "ok" : "not found");
    }

    initrd_init_from_multiboot(boot_info);
    g_initrd_ok = (initrd_lookup("/", &initrd_data, &initrd_size) == 0) ? 1 : 0;

    if (bootfs_module_count() > 0) {
        mount_register("/", VFS_BACKEND_BOOTFS, 1, "bootfs");
    } else if (g_initrd_ok) {
        mount_register("/", VFS_BACKEND_INITRD, 1, "initrd");
    } else {
        mount_register("/", VFS_BACKEND_MEMFS, 0, "memfs");
    }

    if (g_fat12_ok) {
        mount_register("/fat12", VFS_BACKEND_FAT12, 0, "fat12");
        mount_register("/bin", VFS_BACKEND_FAT12, 0, "fat12");
    }
    mount_register("/tmp", VFS_BACKEND_MEMFS, 0, "memfs");
    if (g_fat32_ok)
        mount_register("/disk", VFS_BACKEND_FAT32, 0, "fat32");
    mount_register("/proc", VFS_BACKEND_PROCFS, 1, "procfs");
    mount_register("/sys", VFS_BACKEND_SYSFS, 1, "sysfs");
    mount_register("/dev", VFS_BACKEND_DEVFS, 0, "devfs");
    if (bootfs_module_count() > 0)
        mount_register("/boot", VFS_BACKEND_BOOTFS, 1, "bootfs");

    if (tar_test_archive_len > 0) {
        tar_fs_t *tpkg = tar_mount(tar_test_archive,
                                   (uint64_t)tar_test_archive_len);
        if (tpkg && mount_register_ctx("/pkg", VFS_BACKEND_TAR, 1, "tar",
                                       tpkg) == 0)
            kprintf("[vfs] TAR /pkg: mounted ro (%d entries)\n",
                    tar_file_count(tpkg));
    }
}

/* Open/create/close */

static int open_memfs_file(vfs_file_t *f, const char *subpath, int flags)
{
    const char *name = subpath;
    int inode;
    while (*name == '/')
        name++;
    if (!name[0])
        return -1;

    inode = memfs_open(name);
    if (inode < 0 && (flags & VFS_O_CREAT))
        inode = memfs_create(name);
    if (inode < 0)
        return -1;
    if (flags & VFS_O_TRUNC)
        memfs_truncate(inode);

    f->backend = VFS_BACKEND_MEMFS;
    f->u.memfs.inode = inode;
    f->pos = (flags & VFS_O_APPEND) ? memfs_size(inode) : 0;
    return 0;
}

static int open_regular_buffered(vfs_file_t *f, vfs_backend_t backend,
                                 void *fs_ctx, const char *subpath, int flags)
{
    size_t file_size = 0;
    uint8_t *buf = NULL;
    int read_bytes = 0;
    int is_readonly = ((flags & (VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC | VFS_O_APPEND)) == 0);

    if (backend == VFS_BACKEND_FAT12) {
        fat12_dirent_t de;
        if (fat12_stat(subpath, &de) != 0) {
            if (flags & VFS_O_CREAT) {
                if (fat12_write_file(subpath, "", 0) != 0)
                    return -1;
                de.size = 0;
            } else {
                return -1;
            }
        } else if (de.is_dir) {
            return -1;
        } else {
            file_size = de.size;
        }
        if (flags & VFS_O_TRUNC)
            file_size = 0;

        if (is_readonly) {
            /* Zero-allocation streaming mode for read-only FAT12 files */
            buf = NULL;
        } else if (file_size > 0) {
            buf = (uint8_t *)kmalloc(file_size);
            if (!buf)
                return -1;
            read_bytes = fat12_read_file(subpath, buf, file_size);
            if (read_bytes < 0)
                read_bytes = 0;
            file_size = (size_t)read_bytes;
        }
    } else if (backend == VFS_BACKEND_FAT32) {
        fat32_dirent_t de;
        if (fat32_stat(subpath, &de) != 0) {
            if (flags & VFS_O_CREAT)
                return -1;
            return -1;
        }
        if (de.is_dir)
            return -1;
        file_size = (flags & VFS_O_TRUNC) ? 0 : de.size;
        if (file_size > 0) {
            buf = (uint8_t *)kmalloc(file_size);
            if (!buf)
                return -1;
            read_bytes = fat32_read_file(subpath, buf, file_size);
            if (read_bytes < 0)
                read_bytes = 0;
            file_size = (size_t)read_bytes;
        }
    } else if (backend == VFS_BACKEND_FAT32VOL) {
        fat32_volume_t *vol = (fat32_volume_t *)fs_ctx;
        fat32_dirent_t de;
        if (fat32_vol_stat(vol, subpath, &de) != 0)
            return -1;
        if (de.is_dir)
            return -1;
        file_size = (flags & VFS_O_TRUNC) ? 0 : de.size;
        if (file_size > 0) {
            buf = (uint8_t *)kmalloc(file_size);
            if (!buf)
                return -1;
            read_bytes = fat32_vol_read_file(vol, subpath, buf, file_size);
            if (read_bytes < 0)
                read_bytes = 0;
            file_size = (size_t)read_bytes;
        }
    } else {
        return -1;
    }

    f->backend = backend;
    f->fs_ctx = fs_ctx;
    f->u.regular.buf = buf;
    f->u.regular.size = file_size;
    f->u.regular.capacity = (buf != NULL) ? file_size : 0;
    f->u.regular.owns_buf = (buf != NULL) ? 1 : 0;
    f->pos = (flags & VFS_O_APPEND) ? file_size : 0;
    if (flags & VFS_O_TRUNC)
        f->dirty = 1;
    return 0;
}

int vfs_open_flags(const char *path, int flags)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int fd;
    void **tbl;
    vfs_file_t *f;

    if (!path)
        return -1;
    if (normalize_path_for_process(proc, path, abs, VFS_PATH_MAX) != 0)
        return -1;
    m = mount_lookup(abs);
    if (!m)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;

    if (m->read_only && ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC)))
        return -1;

    f = file_alloc();
    if (!f)
        return -1;
    f->flags = flags;
    f->mode = flags_to_mode(flags);
    kstrncpy(f->path, sub, VFS_PATH_MAX);

    switch (m->backend) {
    case VFS_BACKEND_MEMFS:
        if (open_memfs_file(f, sub, flags) != 0) {
            file_release(f);
            return -1;
        }
        break;
    case VFS_BACKEND_FAT12:
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL:
        if (open_regular_buffered(f, m->backend, m->ctx, sub, flags) != 0) {
            file_release(f);
            return -1;
        }
        break;
    case VFS_BACKEND_INITRD:
    {
        const void *data = NULL;
        size_t sz = 0;
        if (initrd_lookup(sub, &data, &sz) != 0) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_INITRD;
        f->mode = VFS_MODE_READ;
        f->u.regular.buf = (uint8_t *)(uintptr_t)data;
        f->u.regular.size = sz;
        f->u.regular.capacity = sz;
        f->u.regular.owns_buf = 0;
        break;
    }
    case VFS_BACKEND_PROCFS:
    {
        uint8_t *pbuf = NULL;
        size_t psz = 0;
        if (procfs_read_file(sub, &pbuf, &psz) != 0) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_PROCFS;
        f->mode = VFS_MODE_READ;
        f->u.regular.buf = pbuf;
        f->u.regular.size = psz;
        f->u.regular.capacity = psz;
        f->u.regular.owns_buf = 1;
        break;
    }
    case VFS_BACKEND_SYSFS:
    {
        uint8_t *sbuf = NULL;
        size_t ssz = 0;
        if (sysfs_read_file(sub, &sbuf, &ssz) != 0) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_SYSFS;
        f->mode = VFS_MODE_READ;
        f->u.regular.buf = sbuf;
        f->u.regular.size = ssz;
        f->u.regular.capacity = ssz;
        f->u.regular.owns_buf = 1;
        break;
    }
    case VFS_BACKEND_BOOTFS:
    {
        const void *bbuf = NULL;
        size_t bsz = 0;
        int owns = 0;
        if (bootfs_read_file(sub, &bbuf, &bsz, &owns) != 0) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_BOOTFS;
        f->mode = VFS_MODE_READ;
        f->u.regular.buf = (uint8_t *)(uintptr_t)bbuf;
        f->u.regular.size = bsz;
        f->u.regular.capacity = bsz;
        f->u.regular.owns_buf = owns;
        break;
    }
    case VFS_BACKEND_DEVFS:
    {
        const devfs_ops_t *ops = NULL;
        void *priv = NULL;
        uint32_t type = VFS_TYPE_CHAR;
        if (devfs_lookup(sub, &ops, &priv, &type) != 0) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_DEVFS;
        f->mode = flags_to_mode(flags);
        f->u.device.ops = ops;
        f->u.device.priv = priv;
        f->pos = 0;
        if (ops && ops->open) {
            if (ops->open(priv, flags) != 0) {
                file_release(f);
                return -1;
            }
        }
        break;
    }
    case VFS_BACKEND_TAR:
    {
        /* Read-only: point the regular-file read path straight into the archive buffer (owns_buf=0, never freed on... */
        const tar_inode_t *ino = tar_lookup((tar_fs_t *)m->ctx, sub);
        if (!ino || ino->is_dir) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_TAR;
        f->mode = VFS_MODE_READ;
        f->u.regular.buf = (uint8_t *)(uintptr_t)ino->data;
        f->u.regular.size = (size_t)ino->size;
        f->u.regular.capacity = (size_t)ino->size;
        f->u.regular.owns_buf = 0;
        break;
    }
    default:
        file_release(f);
        return -1;
    }

    fd = process_fd_alloc(proc);
    if (fd < 0) {
        file_release(f);
        return -1;
    }
    tbl = fd_table_for_process(proc);
    tbl[fd] = f;
    return fd;
}

int vfs_open(const char *path)
{
    return vfs_open_flags(path, VFS_O_RDONLY);
}

int vfs_create(const char *path)
{
    return vfs_open_flags(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
}

int vfs_unlink(const char *path)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];

    if (!path)
        return -1;
    if (normalize_path_for_process(proc, path, abs, VFS_PATH_MAX) != 0)
        return -1;
    m = mount_lookup(abs);
    if (!m || m->read_only)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;

    if (m->backend == VFS_BACKEND_MEMFS) {
        const char *name = sub;
        while (*name == '/')
            name++;
        return memfs_unlink(name);
    }
    return 0;
}

int vfs_rmdir(const char *path)
{
    return vfs_unlink(path);
}

int vfs_mkdir(const char *path, int mode)
{
    (void)mode;
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];

    if (!path)
        return -1;
    if (normalize_path_for_process(proc, path, abs, VFS_PATH_MAX) != 0)
        return -1;
    m = mount_lookup(abs);
    if (!m || m->read_only)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;

    return 0;
}

int vfs_rename(const char *old_path, const char *new_path)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m1, *m2;
    char abs1[VFS_PATH_MAX], abs2[VFS_PATH_MAX];
    char sub1[VFS_PATH_MAX], sub2[VFS_PATH_MAX];

    if (!old_path || !new_path)
        return -1;
    if (normalize_path_for_process(proc, old_path, abs1, VFS_PATH_MAX) != 0)
        return -1;
    if (normalize_path_for_process(proc, new_path, abs2, VFS_PATH_MAX) != 0)
        return -1;

    m1 = mount_lookup(abs1);
    m2 = mount_lookup(abs2);
    if (!m1 || !m2 || m1 != m2 || m1->read_only)
        return -1;

    if (mount_subpath(m1, abs1, sub1, VFS_PATH_MAX) != 0)
        return -1;
    if (mount_subpath(m2, abs2, sub2, VFS_PATH_MAX) != 0)
        return -1;

    if (m1->backend == VFS_BACKEND_MEMFS) {
        const char *n1 = sub1;
        const char *n2 = sub2;
        while (*n1 == '/')
            n1++;
        while (*n2 == '/')
            n2++;
        return memfs_rename(n1, n2);
    }
    if (m1->backend == VFS_BACKEND_FAT32VOL) {
        return fat32_vol_rename((fat32_volume_t *)m1->ctx, sub1, sub2);
    }
    if (m1->backend == VFS_BACKEND_FAT32) {
        return fat32_rename(sub1, sub2);
    }
    return -1;
}


void vfs_close(int fd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    fd_close_on_table(tbl, fd);
}

/* Read/write/seek */

size_t vfs_read(int fd, void *buf, size_t count)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f || !buf || !(f->mode & VFS_MODE_READ))
        return 0;

    if (f->backend == VFS_BACKEND_MEMFS) {
        size_t got = memfs_read(f->u.memfs.inode, f->pos, buf, count);
        f->pos += got;
        return got;
    }

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (!p || !f->u.pipe.can_read)
            return 0;

        for (;;) {
            if (p->size > 0)
                return pipe_ring_read(p, buf, count);
            if (p->writers == 0)
                return 0; /* All writers closed and buffer drained: true EOF */
            if (f->flags & VFS_O_NONBLOCK)
                return (size_t)-EAGAIN;
#ifdef __x86_64__
            process_t *proc = vfs_current_process();
            if (!proc)
                return 0;
            wait_queue_entry_t wqe;
            wqe.proc = proc;
            wqe.next = NULL;
            wait_queue_add(&p->waitq, &wqe);
            process_block_current();
            process_yield();
            wait_queue_remove(&p->waitq, &wqe);
#else
            return 0;
#endif
        }
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener || !f->u.sock.rx)
            return 0;
        vfs_pipe_t *rx = f->u.sock.rx;
        for (;;) {
            if (rx->size > 0)
                return pipe_ring_read(rx, buf, count);
            if (rx->writers == 0)
                return 0;
            if (f->flags & VFS_O_NONBLOCK)
                return 0;
#ifdef __x86_64__
            process_t *p = vfs_current_process();
            if (!p)
                return 0;
            wait_queue_entry_t wqe;
            wqe.proc = p;
            wqe.next = NULL;
            wait_queue_add(&rx->waitq, &wqe);
            process_block_current();
            process_yield();
            wait_queue_remove(&rx->waitq, &wqe);
#else
            return 0;
#endif
        }
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->read) {
            size_t n = f->u.device.ops->read(f->u.device.priv, f->pos, buf, count, f->flags);
            f->pos += n;
            return n;
        }
        return 0;
    }

    if (f->backend == VFS_BACKEND_FAT12 && !f->u.regular.buf) {
        if (f->pos >= f->u.regular.size)
            return 0;
        if (count > f->u.regular.size - f->pos)
            count = f->u.regular.size - f->pos;
        int n = fat12_read_file_offset(f->path, buf, f->pos, count);
        if (n <= 0)
            return 0;
        f->pos += (size_t)n;
        return (size_t)n;
    }

    if (!f->u.regular.buf || f->pos >= f->u.regular.size)
        return 0;
    if (count > f->u.regular.size - f->pos)
        count = f->u.regular.size - f->pos;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = f->u.regular.buf[f->pos + i];
    f->pos += count;
    return count;
}

size_t vfs_write(int fd, const void *buf, size_t count)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f || !buf || !(f->mode & VFS_MODE_WRITE))
        return 0;

    if (f->flags & VFS_O_APPEND) {
        if (f->backend == VFS_BACKEND_MEMFS)
            f->pos = memfs_size(f->u.memfs.inode);
        else if (f->backend != VFS_BACKEND_PIPE)
            f->pos = f->u.regular.size;
    }

    if (f->backend == VFS_BACKEND_MEMFS) {
        size_t wr = memfs_write(f->u.memfs.inode, f->pos, buf, count);
        f->pos += wr;
        return wr;
    }

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (!p || !f->u.pipe.can_write || p->readers == 0)
            return (size_t)-EPIPE;
        size_t wr = pipe_ring_write(p, buf, count);
        if (wr > 0)
            wait_queue_wake_all(&p->waitq);
        return wr;
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        /* Connected socket: write this end's tx pipe. */
        if (f->u.sock.is_listener || !f->u.sock.tx)
            return 0;
        if (f->u.sock.tx->readers == 0)
            return (size_t)-EPIPE;
        size_t wr = pipe_ring_write(f->u.sock.tx, buf, count);
        if (wr > 0)
            wait_queue_wake_all(&f->u.sock.tx->waitq);
        return wr;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->write) {
            size_t n = f->u.device.ops->write(f->u.device.priv, f->pos, buf, count);
            f->pos += n;
            return n;
        }
        return 0;
    }

    if (f->backend == VFS_BACKEND_INITRD ||
        f->backend == VFS_BACKEND_PROCFS ||
        f->backend == VFS_BACKEND_SYSFS ||
        f->backend == VFS_BACKEND_BOOTFS)
        return 0;

    if (ensure_regular_capacity(f, f->pos + count) != 0)
        return 0;
    for (size_t i = 0; i < count; i++)
        f->u.regular.buf[f->pos + i] = ((const uint8_t *)buf)[i];
    f->pos += count;
    if (f->pos > f->u.regular.size)
        f->u.regular.size = f->pos;
    f->dirty = 1;
    return count;
}

size_t vfs_seek(int fd, size_t offset, int whence)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    size_t size = 0;
    if (!f)
        return (size_t)-1;
    if (f->backend == VFS_BACKEND_PIPE || f->backend == VFS_BACKEND_UNIXSOCK)
        return (size_t)-1;

    if (f->backend == VFS_BACKEND_MEMFS)
        size = memfs_size(f->u.memfs.inode);
    else if (f->backend == VFS_BACKEND_DEVFS)
        size = (f->u.device.ops && f->u.device.ops->size) ? f->u.device.ops->size(f->u.device.priv) : 0;
    else
        size = f->u.regular.size;

    switch (whence) {
    case VFS_SEEK_SET:
        f->pos = offset;
        break;
    case VFS_SEEK_CUR:
        f->pos += offset;
        break;
    case VFS_SEEK_END:
        f->pos = size + offset;
        break;
    default:
        return (size_t)-1;
    }
    if (f->pos > size)
        f->pos = size;
    return f->pos;
}

size_t vfs_tell(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f)
        return (size_t)-1;
    return f->pos;
}

size_t vfs_size(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f)
        return 0;
    if (f->backend == VFS_BACKEND_MEMFS)
        return memfs_size(f->u.memfs.inode);
    if (f->backend == VFS_BACKEND_PIPE) {
        if (!f->u.pipe.pipe)
            return 0;
        return f->u.pipe.pipe->size;
    }
    if (f->backend == VFS_BACKEND_UNIXSOCK)
        return (!f->u.sock.is_listener && f->u.sock.rx)
                   ? f->u.sock.rx->size : 0;
    if (f->backend == VFS_BACKEND_DEVFS)
        return (f->u.device.ops && f->u.device.ops->size) ? f->u.device.ops->size(f->u.device.priv) : 0;
    return f->u.regular.size;
}

/* dup/dup2/pipe/fcntl */

int vfs_dup(int oldfd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    vfs_file_t *f = fd_lookup(proc, oldfd);
    int newfd = process_fd_alloc(proc);
    if (!f || newfd < 0)
        return -1;
    f->refcount++;
    tbl[newfd] = f;
    return newfd;
}

int vfs_dup2(int oldfd, int newfd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    vfs_file_t *f = fd_lookup(proc, oldfd);
    if (!f || !fd_valid(newfd))
        return -1;
    if (oldfd == newfd)
        return newfd;
    fd_close_on_table(tbl, newfd);
    f->refcount++;
    tbl[newfd] = f;
    return newfd;
}

int vfs_pipe(int pipefd[2])
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    int fd_r;
    int fd_w;
    vfs_pipe_t *p;
    vfs_file_t *fr;
    vfs_file_t *fw;

    if (!pipefd)
        return -1;

    fd_r = process_fd_alloc(proc);
    if (fd_r < 0)
        return -1;
    tbl[fd_r] = (void *)1;
    fd_w = process_fd_alloc(proc);
    if (fd_w < 0) {
        tbl[fd_r] = NULL;
        return -1;
    }
    tbl[fd_w] = (void *)1;

    p = pipe_alloc();
    fr = file_alloc();
    fw = file_alloc();
    if (!p || !fr || !fw) {
        if (fr)
            file_release(fr);
        if (fw)
            file_release(fw);
        if (p)
            p->used = 0;
        tbl[fd_r] = NULL;
        tbl[fd_w] = NULL;
        return -1;
    }

    fr->backend = VFS_BACKEND_PIPE;
    fr->mode = VFS_MODE_READ;
    fr->flags = VFS_O_RDONLY;
    fr->u.pipe.pipe = p;
    fr->u.pipe.can_read = 1;
    fr->u.pipe.can_write = 0;

    fw->backend = VFS_BACKEND_PIPE;
    fw->mode = VFS_MODE_WRITE;
    fw->flags = VFS_O_WRONLY;
    fw->u.pipe.pipe = p;
    fw->u.pipe.can_read = 0;
    fw->u.pipe.can_write = 1;

    p->readers = 1;
    p->writers = 1;

    tbl[fd_r] = fr;
    tbl[fd_w] = fw;
    pipefd[0] = fd_r;
    pipefd[1] = fd_w;
    return 0;
}

/* A connected socket is TWO pipes (c2s + s2c) with one fd per end; the */
/* rendezvous is ipc/unix_socket.c's pathname listener registry. Model: */

/* connection, non-blocking connect, EAGAIN-style accept). All claims */
/* (readers/writers on both pipes) are counted at connect() time, so */
/* there are no half-alive pipe states: the pending record carries the */
/* server side until accept() transfers it or the listener drains it. */
/* Verbs return negative errnos. */

int vfs_socket_create(void)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    int fd = process_fd_alloc(proc);
    vfs_file_t *f;

    if (fd < 0)
        return -ENOMEM;
    f = file_alloc();
    if (!f)
        return -ENOMEM;
    f->backend = VFS_BACKEND_UNIXSOCK;
    f->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    f->flags = VFS_O_RDWR;
    f->path[0] = '\0';
    f->u.sock.rx = NULL;
    f->u.sock.tx = NULL;
    f->u.sock.listener = NULL;
    f->u.sock.is_listener = 0;
    tbl[fd] = f;
    return fd;
}

int vfs_socket_bind(int fd, const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    unix_listener_t *l;
    int rc;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK)
        return -EINVAL;
    if (f->u.sock.is_listener || f->u.sock.rx || f->u.sock.tx)
        return -EINVAL;
    if (!path || !path[0])
        return -EINVAL;
    rc = unix_register_listener(path, proc ? (int)proc->pid : 0, fd);
    if (rc != 0)
        return rc;
    l = unix_find_listener(path);
    if (!l) {
        unix_unregister_listener(path);
        return -EINVAL;
    }
    kstrncpy(f->path, path, VFS_PATH_MAX);
    f->u.sock.listener = l;
    f->u.sock.is_listener = 1;
    return 0;
}

int vfs_socket_listen(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK || !f->u.sock.is_listener)
        return -EINVAL;
    unix_listener_set_listening(f->u.sock.listener, 1);
    return 0;
}

int vfs_socket_connect(int fd, const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    unix_listener_t *l;
    vfs_pipe_t *c2s;
    vfs_pipe_t *s2c;
    unix_pending_conn_t pc;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK)
        return -EINVAL;
    if (f->u.sock.is_listener || f->u.sock.rx || f->u.sock.tx)
        return -EINVAL;
    l = unix_find_listener(path);
    if (!l || !unix_listener_is_listening(l))
        return -ECONNREFUSED;

    c2s = pipe_alloc();
    s2c = pipe_alloc();
    if (!c2s || !s2c) {
        if (c2s)
            c2s->used = 0;
        if (s2c)
            s2c->used = 0;
        return -ENOMEM;
    }
    /* Count BOTH sides' claims now: client = reader(s2c) + writer(c2s); the pending record carries the server... */
    c2s->readers = 1;
    c2s->writers = 1;
    s2c->readers = 1;
    s2c->writers = 1;

    pc.pipe1 = c2s;
    pc.pipe2 = s2c;
    pc.client_pid = proc ? (int)proc->pid : 0;
    pc.client_fd = fd;
    if (unix_enqueue_pending(l, &pc) != 0) {
        c2s->used = 0;
        s2c->used = 0;
        return -ECONNREFUSED;
    }
    
    f->u.sock.rx = s2c;
    f->u.sock.tx = c2s;
    return 0;
}

int vfs_socket_accept(int fd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    vfs_file_t *f = fd_lookup(proc, fd);
    unix_pending_conn_t pc;
    vfs_file_t *nf;
    int nfd;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK || !f->u.sock.is_listener)
        return -EINVAL;
    if (!unix_listener_is_listening(f->u.sock.listener))
        return -EINVAL;

    for (;;) {
        if (unix_dequeue_pending(f->u.sock.listener, &pc) == 0)
            break;

        if (f->flags & VFS_O_NONBLOCK)
            return -EAGAIN;

#ifdef __x86_64__
        wait_queue_head_t *wq = unix_listener_get_accept_waitq(f->u.sock.listener);
        if (!wq || !proc)
            return -EAGAIN;

        wait_queue_entry_t wqe;
        wqe.proc = proc;
        wqe.next = NULL;
        wait_queue_add(wq, &wqe);
        process_block_current();
        process_yield();
        wait_queue_remove(wq, &wqe);
#else
        return -EAGAIN;
#endif
    }

    nfd = process_fd_alloc(proc);
    if (nfd < 0) {
        sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe1, 1, 0);
        sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe2, 0, 1);
        return -ENOMEM;
    }
    nf = file_alloc();
    if (!nf) {
        sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe1, 1, 0);
        sock_drop_pipe_claim((vfs_pipe_t *)pc.pipe2, 0, 1);
        return -ENOMEM;
    }
    nf->backend = VFS_BACKEND_UNIXSOCK;
    nf->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    nf->flags = VFS_O_RDWR;
    nf->path[0] = '\0';
    /* Pipe roles swap across the boundary: the server reads what the client wrote (c2s) and writes what the... */
    nf->u.sock.rx = (vfs_pipe_t *)pc.pipe1;
    nf->u.sock.tx = (vfs_pipe_t *)pc.pipe2;
    nf->u.sock.listener = NULL;
    nf->u.sock.is_listener = 0;
    tbl[nfd] = nf;
    return nfd;
}

int vfs_fcntl(int fd, int cmd, int arg)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f)
        return -1;
    switch (cmd) {
    case VFS_F_GETFL:
        return f->flags;
    case VFS_F_SETFL:
        f->flags = (f->flags & ~(VFS_O_APPEND | VFS_O_NONBLOCK)) |
                   (arg & (VFS_O_APPEND | VFS_O_NONBLOCK));
        return 0;
    default:
        return -1;
    }
}

int vfs_ioctl(int fd, unsigned long request, void *arg)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);

    if (request == VFS_KDSETMODE)
        return fb_ioctl(request, arg);

    if (!f || f->backend != VFS_BACKEND_DEVFS)
        return -1;

    if (f->u.device.ops && f->u.device.ops->ioctl)
        return f->u.device.ops->ioctl(f->u.device.priv, request, arg);

    return -1;
}

int vfs_kd_mode(void)
{
    return fb_kd_mode();
}

void *vfs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    process_t *proc = vfs_current_process();
    if (!proc || length == 0)
        return (void *)-1;

    /* Handle anonymous mmap (backing for user heap malloc) */
    if ((flags & VFS_MAP_ANONYMOUS) || fd == -1) {
        uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
        size_t page_count = (length + page_mask) / PAGE_SIZE;
        uintptr_t target_va;

        if (addr != NULL && paging_is_page_aligned_uintptr((uintptr_t)addr) &&
            paging_range_is_user((uintptr_t)addr, page_count * PAGE_SIZE)) {
            target_va = (uintptr_t)addr;
        } else {
            target_va = vm_space_reserve_anon_range(&proc->vm_space, page_count);
            if (!target_va)
                return (void *)-1;
        }

        uint64_t map_flags = PAGING_MAP_USER;
        if (prot & VFS_PROT_READ)
            map_flags |= PAGING_MAP_READ;
        if (prot & VFS_PROT_WRITE)
            map_flags |= PAGING_MAP_WRITE;

        for (size_t i = 0; i < page_count; i++) {
            uintptr_t phys = pmm_alloc_pages(1);
            if (!phys) {
                for (size_t j = 0; j < i; j++) {
                    uint64_t q_phys = 0, q_flags = 0;
                    if (vmm_query_page(proc->vm_space.pml4_phys, target_va + j * PAGE_SIZE, &q_phys, &q_flags) == 0 &&
                        (q_flags & VMM_X64_PTE_PRESENT)) {
                        pmm_free_pages(q_phys, 1);
                    }
                    vm_space_unmap_user_pages(&proc->vm_space, target_va + j * PAGE_SIZE, 1);
                }
                return (void *)-1;
            }
            uint8_t *kva = (uint8_t *)vmm_phys_to_virt(phys);
            k_memset(kva, 0, PAGE_SIZE);
            if (vm_space_map_user_pages(&proc->vm_space, target_va + i * PAGE_SIZE, phys, 1, map_flags) != 0) {
                pmm_free_pages(phys, 1);
                for (size_t j = 0; j < i; j++) {
                    uint64_t q_phys = 0, q_flags = 0;
                    if (vmm_query_page(proc->vm_space.pml4_phys, target_va + j * PAGE_SIZE, &q_phys, &q_flags) == 0 &&
                        (q_flags & VMM_X64_PTE_PRESENT)) {
                        pmm_free_pages(q_phys, 1);
                    }
                    vm_space_unmap_user_pages(&proc->vm_space, target_va + j * PAGE_SIZE, 1);
                }
                return (void *)-1;
            }
        }
        return (void *)target_va;
    }

    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f)
        return (void *)-1;
    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->mmap)
            return f->u.device.ops->mmap(f->u.device.priv, addr, length, prot, flags, offset);
    }
    return (void *)-1;
}

int vfs_munmap(void *addr, size_t length)
{
    if (!addr || length == 0)
        return -1;

    process_t *proc = vfs_current_process();
    if (!proc || !proc->vm_space.owns_pml4)
        return -1;

    uintptr_t p = (uintptr_t)addr;
    if (p == (uintptr_t)VFS_FB_USER_BASE && length <= fb_byte_size())
        return fb_munmap(addr, length);

    if (paging_range_is_user(p, length)) {
        uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
        size_t page_count = (length + page_mask) / PAGE_SIZE;
        uintptr_t base = p & ~page_mask;

        for (size_t i = 0; i < page_count; i++) {
            uint64_t q_phys = 0, q_flags = 0;
            if (vmm_query_page(proc->vm_space.pml4_phys, base + i * PAGE_SIZE, &q_phys, &q_flags) == 0 &&
                (q_flags & VMM_X64_PTE_PRESENT)) {
                pmm_free_pages(q_phys, 1);
            }
        }
        return vm_space_unmap_user_pages(&proc->vm_space, base, page_count);
    }

    return 0;
}

static int vfs_file_poll_mask(vfs_file_t *f)
{
    int mask = 0;
    if (!f)
        return VFS_POLLERR;

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (!p)
            return VFS_POLLERR;
        if (f->u.pipe.can_read) {
            if (p->size > 0)
                mask |= VFS_POLLIN;
            if (p->writers == 0)
                mask |= VFS_POLLHUP;
        }
        if (f->u.pipe.can_write) {
            if (p->readers == 0)
                mask |= VFS_POLLHUP;
            else if (p->size < VFS_PIPE_CAPACITY)
                mask |= VFS_POLLOUT;
        }
        return mask;
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener) {
            if (unix_listener_has_pending(f->u.sock.listener))
                mask |= VFS_POLLIN;
            return mask;
        }
        if (!f->u.sock.rx || !f->u.sock.tx)
            return VFS_POLLERR;
        if (f->u.sock.rx->size > 0)
            mask |= VFS_POLLIN;
        if (f->u.sock.rx->writers == 0)
            mask |= VFS_POLLHUP;
        if (f->u.sock.tx->readers == 0)
            mask |= VFS_POLLHUP;
        else if (f->u.sock.tx->size < VFS_PIPE_CAPACITY)
            mask |= VFS_POLLOUT;
        return mask;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->poll)
            return f->u.device.ops->poll(f->u.device.priv, VFS_POLLIN | VFS_POLLOUT);
        if (f->mode & VFS_MODE_WRITE)
            mask |= VFS_POLLOUT;
        return mask;
    }

    if (f->mode & VFS_MODE_READ)
        mask |= VFS_POLLIN;
    if (f->mode & VFS_MODE_WRITE)
        mask |= VFS_POLLOUT;
    return mask;
}

int vfs_poll(vfs_pollfd_t *fds, size_t nfds, int timeout_ms)
{
    process_t *proc = vfs_current_process();
    int ready = 0;
    (void)timeout_ms;

    if (!fds)
        return -1;
    for (size_t i = 0; i < nfds; i++) {
        vfs_file_t *f = fd_lookup(proc, fds[i].fd);
        int mask = vfs_file_poll_mask(f);
        fds[i].revents = (int16_t)(mask & fds[i].events);
        if ((mask & (VFS_POLLERR | VFS_POLLHUP)) != 0)
            fds[i].revents |= (int16_t)(mask & (VFS_POLLERR | VFS_POLLHUP));
        if (fds[i].revents)
            ready++;
    }
    return ready;
}

/* stat/fstat/cwd/list */

int vfs_stat(const char *path, vfs_stat_t *out)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];

    if (!path || !out)
        return -1;
    if (normalize_path_for_process(proc, path, abs, VFS_PATH_MAX) != 0)
        return -1;

    /* Check if an AF_UNIX listener is bound to this path */
    unix_listener_t *lst = unix_find_listener(abs);
    if (lst) {
        out->size = 0;
        out->blocks = 0;
        out->type = VFS_TYPE_SOCKET;
        out->mode = VFS_MODE_READ | VFS_MODE_WRITE;
        return 0;
    }

    m = mount_lookup(abs);
    if (!m)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;
    return backend_stat(m, sub, out);
}

int vfs_fstat(int fd, vfs_stat_t *out)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    if (!f || !out)
        return -1;
    out->size = vfs_size(fd);
    out->blocks = (out->size + 511) / 512;
    out->mode = f->mode;
    if (f->backend == VFS_BACKEND_PIPE)
        out->type = VFS_TYPE_PIPE;
    else if (f->backend == VFS_BACKEND_UNIXSOCK)
        out->type = VFS_TYPE_SOCKET;
    else if (f->backend == VFS_BACKEND_DEVFS)
        out->type = VFS_TYPE_CHAR;
    else
        out->type = VFS_TYPE_FILE;
    return 0;
}

int vfs_getcwd(char *buf, size_t size)
{
    process_t *proc = vfs_current_process();
    char *cwd = cwd_for_process(proc);
    if (!buf || size == 0 || !cwd)
        return -1;
    return kstrncpy(buf, cwd, (int)size);
}

int vfs_chdir(const char *path)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    vfs_stat_t st;
    char *cwd;

    if (!path)
        return -1;
    if (normalize_path_for_process(proc, path, abs, VFS_PATH_MAX) != 0)
        return -1;

    m = mount_lookup(abs);
    if (!m)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;
    if (backend_stat(m, sub, &st) != 0 || st.type != VFS_TYPE_DIR)
        return -1;

    cwd = cwd_for_process(proc);
    if (!cwd)
        return -1;
    return kstrncpy(cwd, abs, VFS_PATH_MAX);
}

int vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max)
{
    process_t *proc = vfs_current_process();
    const vfs_mount_t *m;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int count = 0;

    if (!dir || !names || max <= 0)
        return -1;
    if (normalize_path_for_process(proc, dir, abs, VFS_PATH_MAX) != 0)
        return -1;
    m = mount_lookup(abs);
    if (!m)
        return -1;
    if (mount_subpath(m, abs, sub, VFS_PATH_MAX) != 0)
        return -1;

    switch (m->backend) {
    case VFS_BACKEND_MEMFS:
        if (!kstreq(sub, "/"))
            return -1;
        count = memfs_list(names, max);
        break;
    case VFS_BACKEND_FAT12:
    {
        fat12_dirent_t entries[FAT12_MAX_DIRENT];
        int n = fat12_list_dir(sub, entries, FAT12_MAX_DIRENT);
        if (n < 0)
            return -1;
        for (int i = 0; i < n && count < max; i++) {
            kstrncpy(names[count], entries[i].name, VFS_NAME_MAX);
            count++;
        }
        break;
    }
    case VFS_BACKEND_FAT32:
    {
        fat32_dirent_t entries[FAT32_MAX_DIRENT];
        int n = fat32_list_dir(sub, entries, FAT32_MAX_DIRENT);
        if (n < 0)
            return -1;
        for (int i = 0; i < n && count < max; i++) {
            kstrncpy(names[count], entries[i].name, VFS_NAME_MAX);
            count++;
        }
        break;
    }
    case VFS_BACKEND_FAT32VOL:
    {
        fat32_dirent_t entries[FAT32_MAX_DIRENT];
        int n = fat32_vol_list_dir((fat32_volume_t *)m->ctx, sub,
                                   entries, FAT32_MAX_DIRENT);
        if (n < 0)
            return -1;
        for (int i = 0; i < n && count < max; i++) {
            kstrncpy(names[count], entries[i].name, VFS_NAME_MAX);
            count++;
        }
        break;
    }
    case VFS_BACKEND_PROCFS:
        count = procfs_list(sub, names, max);
        if (count < 0)
            return -1;
        break;
    case VFS_BACKEND_SYSFS:
        count = sysfs_list(sub, names, max);
        if (count < 0)
            return -1;
        break;
    case VFS_BACKEND_BOOTFS:
        count = bootfs_list(sub, names, max);
        if (count < 0)
            return -1;
        break;
    case VFS_BACKEND_DEVFS:
        count = devfs_list(sub, names, max);
        if (count < 0)
            return -1;
        break;
    case VFS_BACKEND_INITRD:
        if (!kstreq(sub, "/"))
            return -1;
        if (g_initrd_ok) {
            kstrncpy(names[0], "initrd", VFS_NAME_MAX);
            count = 1;
        } else {
            count = 0;
        }
        break;
    case VFS_BACKEND_TAR:
        count = tar_list_dir((tar_fs_t *)m->ctx, sub, names, max);
        if (count < 0)
            return -1;
        break;
    default:
        return -1;
    }

    if (kstreq(abs, "/"))
        count = append_root_mount_names(names, count, max);
    return count;
}

int vfs_process_inherit(struct process *dst, const struct process *src)
{
    void **dst_tbl;
    void **src_tbl;
    if (!dst)
        return -1;

    dst_tbl = fd_table_for_process((process_t *)dst);
    src_tbl = fd_table_for_process((process_t *)src);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++)
        fd_close_on_table(dst_tbl, i);

    if (!src)
        return 0;

    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        vfs_file_t *f = fd_lookup_on_table(src_tbl, i);
        if (!f)
            continue;
        f->refcount++;
        dst_tbl[i] = f;
    }
    return 0;
}

int vfs_process_dup2(struct process *dst,
                     int dst_fd,
                     const struct process *src,
                     int src_fd)
{
    void **dst_tbl;
    void **src_tbl;
    vfs_file_t *f;
    if (!dst || !src || !fd_valid(dst_fd) || !fd_valid(src_fd))
        return -1;

    dst_tbl = fd_table_for_process((process_t *)dst);
    src_tbl = fd_table_for_process((process_t *)src);
    f = fd_lookup_on_table(src_tbl, src_fd);
    if (!f)
        return -1;

    fd_close_on_table(dst_tbl, dst_fd);
    f->refcount++;
    dst_tbl[dst_fd] = f;
    return 0;
}

/* Process teardown hook */

void vfs_process_cleanup(struct process *proc)
{
    void **tbl;
    process_t *p = (process_t *)proc;
    if (!p)
        return;
    tbl = fd_table_for_process(p);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++)
        fd_close_on_table(tbl, i);
    if (!p->cwd[0])
        kstrncpy(p->cwd, "/", PROCESS_CWD_MAX);
}

/* Boot-time, inline, pre-sti (same placement rationale as [guide11]). */
/* Lives in vfs.c because the suite spans discovery (drv/diskmgr), */
/* formatting (fs/mkfs_fat32), volumes (fs/fat32) and mounting (here). */
/* Every hardware-dependent test SKIPS loudly when its disk is absent. */
/* Writes touch only the disposable gate images (BOOT_QEMU.md). */

static int g12_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

static int g12_plant_file(block_dev_t *bd, const void *content, uint32_t len)
{
    uint8_t *sec = (uint8_t *)kmalloc(512);
    uint32_t reserved, spc, fat_size, fat_count, data_lba, c3_lba;
    int ok = 0;

    if (!sec)
        return -1;

    if (bd->read(bd, 0, 1, sec) != 0)
        goto out;
    reserved  = (uint32_t)sec[14] | ((uint32_t)sec[15] << 8);
    spc       = sec[13];
    fat_count = sec[16];
    fat_size  = (uint32_t)sec[36] | ((uint32_t)sec[37] << 8) |
                ((uint32_t)sec[38] << 16) | ((uint32_t)sec[39] << 24);
    if (!reserved || !spc || !fat_count || !fat_size || len > 512)
        goto out;
    data_lba = reserved + fat_count * fat_size;
    c3_lba   = data_lba + spc;

    if (bd->read(bd, data_lba, 1, sec) != 0)
        goto out;
    k_memcpy(sec, "HELLO   TXT", 11);
    sec[11] = 0x20;
    for (int i = 12; i < 20; i++) sec[i] = 0;
    sec[20] = 0; sec[21] = 0;
    for (int i = 22; i < 26; i++) sec[i] = 0;
    sec[26] = 3; sec[27] = 0;
    sec[28] = (uint8_t)len; sec[29] = (uint8_t)(len >> 8);
    sec[30] = 0; sec[31] = 0;
    if (bd->write(bd, data_lba, 1, sec) != 0)
        goto out;

    for (uint32_t f = 0; f < fat_count; f++) {
        if (bd->read(bd, reserved + f * fat_size, 1, sec) != 0)
            goto out;
        sec[12] = 0xFF; sec[13] = 0xFF; sec[14] = 0xFF; sec[15] = 0x0F;
        if (bd->write(bd, reserved + f * fat_size, 1, sec) != 0)
            goto out;
    }

    k_memset(sec, 0, 512);
    k_memcpy(sec, content, len);
    if (bd->write(bd, c3_lba, 1, sec) != 0)
        goto out;
    ok = 1;
out:
    kfree(sec);
    return ok ? 0 : -1;
}

void vfs_run_guide12_selftests(void)
{
    int pass = 0, fail = 0;
    block_dev_t *gpt_disk = NULL, *esp_part = NULL, *data_part = NULL;
    block_dev_t *mbr_part = NULL;
    block_dev_t *any_part = NULL;
    int part_count = 0;

    kprintf("[guide12][test] start\n");

    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *bd = blockdev_at(i);
        if (!bd || !bd->is_partition)
            continue;
        part_count++;
        any_part = bd;
        if (bd->is_esp) {
            esp_part = bd;
            gpt_disk = bd->parent;
        }
    }
    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *bd = blockdev_at(i);
        if (!bd || !bd->is_partition)
            continue;
        if (gpt_disk && bd->parent == gpt_disk && !bd->is_esp)
            data_part = bd;
        if (bd->parent && bd->parent != gpt_disk && !mbr_part)
            mbr_part = bd;
    }

    if (part_count == 0) {
        kprintf("[guide12] no partitions - discovery tests skipped "
                "(expected without a partitioned gate disk)\n");
    } else {
        int ok = 1;
        for (int i = 0; i < blockdev_count(); i++) {
            block_dev_t *bd = blockdev_at(i);
            int pl;
            if (!bd || !bd->is_partition)
                continue;
            if (!bd->parent || bd->parent->is_partition) ok = 0;
            if (bd->sector_count == 0) ok = 0;
            if (bd->lba_offset == 0) ok = 0;
            if (bd->lba_offset + bd->sector_count > bd->parent->sector_count)
                ok = 0;
            pl = kstrlen(bd->parent->name);
            for (int c = 0; c < pl; c++)
                if (bd->name[c] != bd->parent->name[c]) ok = 0;
            if (bd->name[pl] < '1' || bd->name[pl] > '9' || bd->name[pl + 1])
                ok = 0;
        }
        if (ok) {
            kprintf("[guide12] registry/naming PASS (%d partition(s))\n",
                    part_count);
            pass++;
        } else {
            kprintf("[guide12] registry/naming FAIL\n");
            fail++;
        }
    }

    if (!esp_part) {
        kprintf("[guide12] no GPT/ESP disk - GPT tests skipped\n");
    } else {
        int ok = 1;
        if (esp_part->lba_offset != 2048u) ok = 0;
        if (esp_part->sector_count != 32768u) ok = 0;
        if (!data_part) ok = 0;
        if (data_part && data_part->lba_offset != 34816u) ok = 0;
        for (int i = 0; i < blockdev_count(); i++) {
            block_dev_t *bd = blockdev_at(i);
            if (bd && bd->is_partition && bd->lba_offset == 1u) ok = 0;
        }
        if (ok) {
            kprintf("[guide12] GPT parse PASS (%s esp@2048 x32768, %s data@34816)\n",
                    esp_part->name, data_part->name);
            pass++;
        } else {
            kprintf("[guide12] GPT parse FAIL\n");
            fail++;
        }
    }

    if (!blockdev_first_raw_fat32()) {
        kprintf("[guide12] no raw FAT32 disk - /disk fallback test skipped\n");
    } else {
        const vfs_mount_t *m = mount_lookup("/disk");
        if (g_fat32_ok && m && m->backend == VFS_BACKEND_FAT32) {
            kprintf("[guide12] raw fallback PASS (/disk on %s)\n",
                    blockdev_first_raw_fat32()->name);
            pass++;
        } else {
            kprintf("[guide12] raw fallback FAIL\n");
            fail++;
        }
    }

    if (!mbr_part) {
        kprintf("[guide12] no MBR disk - MBR test skipped\n");
    } else {
        if (mbr_part->lba_offset == 2048u && mbr_part->sector_count > 0) {
            kprintf("[guide12] MBR parse PASS (%s @2048 x%u)\n",
                    mbr_part->name, (uint32_t)mbr_part->sector_count);
            pass++;
        } else {
            kprintf("[guide12] MBR parse FAIL (lba hi=0x%x lo=0x%x)\n",
                    (uint32_t)(mbr_part->lba_offset >> 32),
                    (uint32_t)(mbr_part->lba_offset & 0xFFFFFFFFu));
            fail++;
        }
    }

    /* T5 + T6 - mkfs.fat32 on the blank GPT data partition, remount, empty root, then the planted-file r/w round... */
    if (!data_part || !data_part->write) {
        kprintf("[guide12] no GPT data partition - mkfs/mount tests skipped\n");
    } else {
        fat32_volume_t *vol = NULL;
        fat32_dirent_t entries[4];
        int n = -1;

        if (mkfs_fat32_format(data_part, "TSUKASA") == 0)
            vol = fat32_vol_mount(data_part);
        if (vol)
            n = fat32_vol_list_dir(vol, "/", entries, 4);
        if (vol && n == 0) {
            kprintf("[guide12] mkfs+remount PASS (%s: empty root)\n",
                    data_part->name);
            pass++;
        } else {
            kprintf("[guide12] mkfs+remount FAIL (vol=%s n=%d)\n",
                    vol ? "ok" : "null", n);
            fail++;
        }

        if (vol) {
            static const char msg1[] = "Tsukasa on /dev, by frosty";
            static const char msg2[] = "rewritten through the mount";
            char rb[64];
            char mnt[VFS_PATH_MAX];
            int ok = 1;
            fat32_dirent_t de;

            if (g12_plant_file(data_part, msg1, (uint32_t)sizeof(msg1) - 1) != 0)
                ok = 0;
            if (ok && fat32_vol_stat(vol, "/hello.txt", &de) != 0) ok = 0;
            if (ok && de.size != sizeof(msg1) - 1) ok = 0;
            k_memset(rb, 0, sizeof(rb));
            if (ok && fat32_vol_read_file(vol, "/hello.txt", rb,
                                          sizeof(rb)) != (int)(sizeof(msg1) - 1))
                ok = 0;
            if (ok && g12_memcmp(rb, msg1, (uint32_t)sizeof(msg1) - 1) != 0)
                ok = 0;
            if (ok && fat32_vol_write_file(vol, "/hello.txt", msg2,
                                           sizeof(msg2) - 1) != 0)
                ok = 0;
            k_memset(rb, 0, sizeof(rb));
            if (ok && fat32_vol_read_file(vol, "/hello.txt", rb,
                                          sizeof(rb)) != (int)(sizeof(msg2) - 1))
                ok = 0;
            if (ok && g12_memcmp(rb, msg2, (uint32_t)sizeof(msg2) - 1) != 0)
                ok = 0;

            if (ok) {
                const vfs_mount_t *m;
                mnt[0] = '/'; mnt[1] = 'd'; mnt[2] = 'e'; mnt[3] = 'v';
                mnt[4] = '/';
                kstrncpy(mnt + 5, data_part->name, VFS_PATH_MAX - 5);
                m = mount_lookup(mnt);
                if (!m || m->backend != VFS_BACKEND_FAT32VOL) {
                    if (mount_register_ctx(mnt, VFS_BACKEND_FAT32VOL, 0,
                                           "fat32", vol) == 0)
                        m = mount_lookup(mnt);
                }
                if (!m || m->backend != VFS_BACKEND_FAT32VOL) {
                    ok = 0;
                } else {
                    vfs_stat_t st;
                    if (backend_stat(m, "/hello.txt", &st) != 0 ||
                        st.size != sizeof(msg2) - 1)
                        ok = 0;
                }
            }

            if (ok) {
                kprintf("[guide12] planted-file r/w via /dev/%s PASS\n",
                        data_part->name);
                pass++;
            } else {
                kprintf("[guide12] planted-file r/w FAIL\n");
                fail++;
            }

            if (!g_fat32_ok) {
                kprintf("[guide12] /disk absent - dual-volume test skipped\n");
            } else {
                fat32_dirent_t rootents[4];
                int okd = 1;
                char rb2[64];
                k_memset(rb2, 0, sizeof(rb2));
                if (fat32_vol_read_file(vol, "/hello.txt", rb2,
                                        sizeof(rb2)) != (int)(sizeof(msg2) - 1))
                    okd = 0;
                if (fat32_list_dir("/", rootents, 4) < 0)
                    okd = 0;
                k_memset(rb2, 0, sizeof(rb2));
                if (fat32_vol_read_file(vol, "/hello.txt", rb2,
                                        sizeof(rb2)) != (int)(sizeof(msg2) - 1))
                    okd = 0;
                if (okd && g12_memcmp(rb2, msg2, (uint32_t)sizeof(msg2) - 1) != 0)
                    okd = 0;
                if (okd) {
                    kprintf("[guide12] dual-volume isolation PASS\n");
                    pass++;
                } else {
                    kprintf("[guide12] dual-volume isolation FAIL\n");
                    fail++;
                }
            }
        }
    }

    if (!any_part) {
        kprintf("[guide12] no partition - offset test skipped\n");
    } else {
        uint8_t *a = (uint8_t *)kmalloc(512);
        uint8_t *b = (uint8_t *)kmalloc(512);
        int ok = (a && b) ? 1 : 0;
        if (ok && any_part->read(any_part, 0, 1, a) != 0) ok = 0;
        if (ok && any_part->parent->read(any_part->parent,
                                         any_part->lba_offset, 1, b) != 0)
            ok = 0;
        if (ok && g12_memcmp(a, b, 512) != 0) ok = 0;
        if (ok && any_part->read(any_part, any_part->sector_count, 1, a) != -1)
            ok = 0;
        if (ok) {
            kprintf("[guide12] offset identity + bounds PASS (%s)\n",
                    any_part->name);
            pass++;
        } else {
            kprintf("[guide12] offset identity + bounds FAIL\n");
            fail++;
        }
        kfree(a);
        kfree(b);
    }

    kprintf("[guide12][test] done pass=%d fail=%d\n", pass, fail);
}

/* NON-DESTRUCTIVE by design: the real install erases a disk and is */
/* therefore driven interactively by the owner (gate g6: /bin/install */
/* in the terminal; gate g7 boots the result with no CD). These tests */
/* cover the pieces that can be proven safely at boot: install sources */
/* on the live medium, the Limine boot blob's shape, the new FAT32 */
/* create/LFN/grow write path (exercised on the disposable /disk */
/* scratch volume), and the preflight validator. */

#ifdef __x86_64__
#include "../sys/installer.h"
#endif

void vfs_run_guide13_selftests(void)
{
    int pass = 0, fail = 0;

    kprintf("[guide13][test] start\n");

    {
        static const char *const mods[] = {
            "/bootblob.bin", "/tsukasa_x64.elf", "/initrd.img",
            "/limine-bios.sys",
        };
        vfs_stat_t st;
        int have = 1;
        for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
            if (bootfs_stat(mods[i], &st) != 0 || st.size == 0)
                have = 0;
        }
        if (!have) {
            kprintf("[guide13] install source modules absent - "
                    "source tests skipped (old ISO or i386 boot)\n");
        } else {
            const void *blob = NULL;
            size_t bs = 0;
            int owns = 0, ok = 0;
            if (bootfs_read_file("/bootblob.bin", &blob, &bs, &owns) == 0 &&
                blob && bs >= 1024 && bs <= 2048u * 512u) {
                const uint8_t *b = (const uint8_t *)blob;
                int stage2_nonzero = 0;
                for (int i = 512; i < 1024; i++) {
                    if (b[i] != 0) {
                        stage2_nonzero = 1;
                        break;
                    }
                }
                ok = (b[510] == 0x55u && b[511] == 0xAAu && stage2_nonzero);
            }
            if (owns && blob)
                kfree((void *)blob);
            if (ok) {
                kprintf("[guide13] boot blob sanity PASS "
                        "(sig + stage2 present, %u bytes)\n", (uint32_t)bs);
                pass++;
            } else {
                kprintf("[guide13] boot blob sanity FAIL\n");
                fail++;
            }
        }
    }

    if (!g_fat32_ok || !blockdev_first_raw_fat32()) {
        kprintf("[guide13] /disk absent - fat32 write-path tests skipped\n");
    } else {
        fat32_volume_t *vol = fat32_vol_mount(blockdev_first_raw_fat32());
        int ok = (vol != NULL);
        static uint8_t pat[8000];
        static uint8_t rb[8000];
        fat32_dirent_t de;

        for (int i = 0; i < 8000; i++)
            pat[i] = (uint8_t)(0x5Au ^ i ^ (i >> 8));

        /* IDEMPOTENT: /disk (scratch.img) persists across gate runs, so the files may already exist from a prior boot. */

        fat32_vol_create_file(vol, "/g13-longname-chk.txt");
        if (ok && fat32_vol_stat(vol, "/g13-longname-chk.txt", &de) != 0)
            ok = 0;
        if (ok && fat32_vol_write_file(vol, "/g13-longname-chk.txt",
                                       pat, 3000) != 0)
            ok = 0;
        if (ok && fat32_vol_read_file(vol, "/g13-longname-chk.txt",
                                      rb, sizeof(rb)) != 3000)
            ok = 0;
        if (ok) {
            for (int i = 0; i < 3000; i++)
                if (rb[i] != pat[i]) { ok = 0; break; }
        }
        if (ok && fat32_vol_write_file(vol, "/g13-longname-chk.txt",
                                       pat, 8000) != 0)
            ok = 0;
        if (ok && fat32_vol_read_file(vol, "/g13-longname-chk.txt",
                                      rb, sizeof(rb)) != 8000)
            ok = 0;
        if (ok) {
            for (int i = 0; i < 8000; i++)
                if (rb[i] != pat[i]) { ok = 0; break; }
        }
        fat32_vol_create_file(vol, "/G13OK.TXT");
        if (ok && fat32_vol_stat(vol, "/G13OK.TXT", &de) != 0)
            ok = 0;
        if (ok && fat32_vol_write_file(vol, "/G13OK.TXT", pat, 32) != 0)
            ok = 0;
        if (ok && fat32_vol_create_file(vol, "/g13-longname-chk.txt") == 0)
            ok = 0;

        if (ok) {
            kprintf("[guide13] fat32 create/LFN/grow PASS "
                    "(3000->8000 bytes + 8.3 + dup-reject)\n");
            pass++;
        } else {
            kprintf("[guide13] fat32 create/LFN/grow FAIL\n");
            fail++;
        }
    }

#ifdef __x86_64__
    {
        char err[96];
        int ok = 1;
        block_dev_t *raw = blockdev_first_raw_fat32();
        block_dev_t *some_part = NULL;
        vfs_stat_t st;
        int sources = (bootfs_stat("/bootblob.bin", &st) == 0 && st.size > 0);

        if (installer_preflight(NULL, err, sizeof(err)) != -1)
            ok = 0;
        for (int i = 0; i < blockdev_count() && !some_part; i++) {
            block_dev_t *bd = blockdev_at(i);
            if (bd && bd->is_partition)
                some_part = bd;
        }
        if (some_part &&
            installer_preflight(some_part, err, sizeof(err)) != -1)
            ok = 0;
        if (sources && raw &&
            raw->sector_count >= INSTALLER_MIN_SECTORS) {
            if (installer_preflight(raw, err, sizeof(err)) != 0)
                ok = 0;
        }
        if (ok) {
            kprintf("[guide13] installer preflight PASS\n");
            pass++;
        } else {
            kprintf("[guide13] installer preflight FAIL (%s)\n", err);
            fail++;
        }
    }
#endif

    kprintf("[guide13][test] done pass=%d fail=%d\n", pass, fail);
}

/* Exercises the read-only USTAR backend against the embedded fixture */
/* (fs/tar_testdata.c) via the tar_* API directly - boot-time, pre-sti, */
/* no process/fd context (same rationale as guide11/12). Read-only and */
/* idempotent: re-mounting the const fixture has no side effects. */

void vfs_run_guide14_selftests(void)
{
    int pass = 0, fail = 0;
    tar_fs_t *fs;

    kprintf("[guide14][test] start\n");

    if (tar_test_archive_len == 0) {
        kprintf("[guide14] embedded tar fixture absent - tests skipped\n");
        kprintf("[guide14][test] done pass=%d fail=%d\n", pass, fail);
        return;
    }
    fs = tar_mount(tar_test_archive, (uint64_t)tar_test_archive_len);
    if (!fs) {
        kprintf("[guide14] tar_mount FAILED\n");
        fail++;
        kprintf("[guide14][test] done pass=%d fail=%d\n", pass, fail);
        return;
    }

    {
        int n = tar_file_count(fs);
        if (n == 8) {
            kprintf("[guide14] index build PASS (%d entries)\n", n);
            pass++;
        } else {
            kprintf("[guide14] index build FAIL (n=%d, expected 8)\n", n);
            fail++;
        }
    }

    {
        char names[16][VFS_NAME_MAX];
        int n = tar_list_dir(fs, "/", names, 16);
        int m = 0, e = 0, d = 0;
        for (int i = 0; i < n; i++) {
            if (kstreq(names[i], "motd")) m = 1;
            else if (kstreq(names[i], "etc")) e = 1;
            else if (kstreq(names[i], "docs")) d = 1;
        }
        if (n == 3 && m && e && d) {
            kprintf("[guide14] list / PASS (motd, etc, docs)\n");
            pass++;
        } else {
            kprintf("[guide14] list / FAIL (n=%d m=%d e=%d d=%d)\n", n, m, e, d);
            fail++;
        }
    }

    {
        static const char expect[] = "Tsukasa TAR fs OK\n";
        const tar_inode_t *ino = tar_lookup(fs, "/motd");
        char buf[64];
        uint64_t got = 0;
        int ok = 0;
        if (ino && !ino->is_dir) {
            got = tar_read(fs, ino, 0, buf, sizeof(buf));
            if (got == sizeof(expect) - 1 &&
                g12_memcmp(buf, expect, (uint32_t)got) == 0)
                ok = 1;
        }
        if (ok) {
            kprintf("[guide14] read /motd PASS (%u bytes)\n", (uint32_t)got);
            pass++;
        } else {
            kprintf("[guide14] read /motd FAIL (got=%u)\n", (uint32_t)got);
            fail++;
        }
    }

    {
        char names[16][VFS_NAME_MAX];
        int n = tar_list_dir(fs, "/etc", names, 16);
        if (n == 1 && kstreq(names[0], "hostname")) {
            kprintf("[guide14] list /etc PASS (hostname)\n");
            pass++;
        } else {
            kprintf("[guide14] list /etc FAIL (n=%d)\n", n);
            fail++;
        }
    }

    {
        static const char lp[] =
            "/docs/long_dir_component_number_one_aaaaaaaaaaaaaaaa/"
            "long_dir_component_number_two_bbbbbbbbbbbbbbbb/leaf.txt";
        static const char expect[] = "deep\n";
        const tar_inode_t *ino = tar_lookup(fs, lp);
        char buf[32];
        uint64_t got = 0;
        int ok = 0;
        if (ino && !ino->is_dir) {
            got = tar_read(fs, ino, 0, buf, sizeof(buf));
            if (got == sizeof(expect) - 1 &&
                g12_memcmp(buf, expect, (uint32_t)got) == 0)
                ok = 1;
        }
        if (ok) {
            kprintf("[guide14] long-path (ustar prefix) PASS\n");
            pass++;
        } else {
            kprintf("[guide14] long-path FAIL (found=%d got=%u)\n",
                    ino ? 1 : 0, (uint32_t)got);
            fail++;
        }
    }

    {
        vfs_mount_info_t mi[24];
        int n = vfs_get_mounts(mi, 24);
        int found = 0, ro = 0;
        for (int i = 0; i < n; i++) {
            if (kstreq(mi[i].path, "/pkg")) {
                found = 1;
                ro = mi[i].read_only;
            }
        }
        if (found && ro) {
            kprintf("[guide14] /pkg mount present + read-only PASS\n");
            pass++;
        } else {
            kprintf("[guide14] /pkg mount FAIL (found=%d ro=%d)\n", found, ro);
            fail++;
        }
    }

    kprintf("[guide14][test] done pass=%d fail=%d\n", pass, fail);
}
