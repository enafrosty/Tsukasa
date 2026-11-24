/*
 * Project Tsukasa - Virtual File System core with per-process FD tables
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
#include "ext4fs.h"
#include "fat32.h"
#include "initrd.h"
#include "procfs.h"
#include "sysfs.h"
#include "tar.h"
#include "tar_testdata.h"
#include "tmpfs.h"

#include "../lib/lz4.h"

#include "../drv/ahci.h"
#include "../drv/ata.h"
#include "../drv/blockdev.h"
#include "../drv/diskmgr.h"
#include "../drv/nvme.h"
#include "../drv/virtio_blk.h"
#include "mkfs_fat32.h"
#include "../drv/fb.h"
#include "../drv/input_dev.h"
#include "../include/boot_info.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/multiboot.h"
#include "../include/spinlock.h"
#include "../ipc/unix_socket.h"
#include "../mm/heap.h"
#include "../mm/pagecache.h"
#include "../proc/process.h"
#include "../tty/tty.h"
#include "../sys/wait_queue.h"
#include "../drv/pit.h"
#ifdef __x86_64__
#include "../include/paging.h"
#include "../mm/vm_space.h"      /* map the fb into a ring-3 address space   */
#include "../mm/vmm_x64.h"       /* vmm_query_page - idempotent fb re-mmap    */
#include "../mm/pmm.h"

#define VFS_FB_USER_BASE 0x0000000030000000ULL
#endif

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS         32
#define VFS_PIPE_CAPACITY      4096
#define VFS_SYMLOOP_MAX        16
#define VFS_DCACHE_BUCKETS     512U
#define VFS_DCACHE_MAX_ENTRIES 1024U
#define VFS_FLOCK_BUCKETS      128U


typedef enum vfs_device_kind {
    VFS_DEV_NONE = 0,
    VFS_DEV_FB0,
    VFS_DEV_TTY,
    VFS_DEV_KEYBOARD,
    VFS_DEV_MOUSE
} vfs_device_kind_t;

typedef struct vfs_mount {
    int used;
    uint64_t mount_id;
    int open_count;
    int active_ops;
    char path[VFS_PATH_MAX];
    int path_len;
    vfs_backend_t backend;
    int read_only;
    const char *name;
    void *fs_private;
} vfs_mount_t;

typedef struct vfs_pipe {
    uint8_t data[VFS_PIPE_CAPACITY];
    size_t read_pos;
    size_t write_pos;
    size_t size;
    int readers;
    int writers;
    spinlock_t lock;
    wait_queue_head_t waitq;
} vfs_pipe_t;

typedef struct vfs_file {
    int refcount;
    spinlock_t lock;
    int flags;
    int mode;
    int dirty;
    int is_dir;
    vfs_backend_t backend;
    void *fs_ctx;
    uint64_t mount_id;
    uint64_t inode_dev;
    uint64_t inode_ino;
    size_t pos;
    struct vfs_pc_inode *pc_inode;

    char path[VFS_PATH_MAX];
    char abs_path[VFS_PATH_MAX];

    union {
        struct {
            uint8_t *buf;
            size_t size;
            size_t capacity;
            int owns_buf;
        } regular;
        struct {
            tmpfs_inode_t *inode;
        } tmpfs;
        struct {
            void *handle;
        } ext4;
        struct {
            fat32_volume_t *vol;
            uint32_t hint_clus_idx;
            uint32_t hint_clus;
            uint64_t hint_gen;
        } fat32;
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
            unix_ring_t *rx;
            unix_ring_t *tx;
            unix_listener_t *listener;
            int is_listener;
        } sock;
        struct {
            epoll_item_t *items;
            int count;
        } epoll;
    } u;
} vfs_file_t;

typedef struct vfs_dentry {
    uint64_t id;
    uint64_t parent_id;
    uint64_t mount_id;
    uint64_t ino;
    uint32_t type;
    char name[VFS_NAME_MAX];
    char *target;
    struct vfs_dentry *hash_next;
    struct vfs_dentry *lru_prev;
    struct vfs_dentry *lru_next;
} vfs_dentry_t;

typedef struct vfs_flock_holder {
    vfs_file_t *owner_file;
    int type;
    struct vfs_flock_holder *next;
} vfs_flock_holder_t;

typedef struct vfs_flock_inode {
    uint64_t dev_id;
    uint64_t ino_id;
    vfs_flock_holder_t *holders;
    wait_queue_head_t waitq;
    int waiters;
    struct vfs_flock_inode *next;
} vfs_flock_inode_t;

static vfs_mount_t g_mounts[VFS_MAX_MOUNTS];
static uint64_t g_next_mount_id = 1;
static spinlock_t g_mounts_lock = SPINLOCK_INIT;
static spinlock_t g_vfs_alloc_lock = SPINLOCK_INIT;
static spinlock_t g_fd_lock = SPINLOCK_INIT;
static spinlock_t g_dcache_lock = SPINLOCK_INIT;
static spinlock_t g_pc_inode_lock = SPINLOCK_INIT;

#define VFS_PC_OBJ_ID_BASE   0x5646530000000000ULL
#define VFS_PC_INODE_BUCKETS 128U

typedef struct vfs_pc_inode {
    uint64_t obj_id;
    uint64_t mount_id;
    uint64_t key_ino;
    vfs_backend_t backend;
    void *fs_ctx;
    tmpfs_inode_t *tmpfs_ino;
    char subpath[VFS_PATH_MAX];
    const uint8_t *ro_buf;
    size_t ro_size;
    struct vfs_pc_inode *next;
} vfs_pc_inode_t;

static vfs_pc_inode_t *g_pc_inodes[VFS_PC_INODE_BUCKETS];
static uint64_t g_next_pc_obj_seq = 1ULL;
static spinlock_t g_flock_lock = SPINLOCK_INIT;

static vfs_dentry_t *g_dcache_buckets[VFS_DCACHE_BUCKETS];
static vfs_dentry_t *g_dcache_lru_head;
static vfs_dentry_t *g_dcache_lru_tail;
static size_t g_dcache_count;
static uint64_t g_dcache_next_id = 2;

static vfs_flock_inode_t *g_flock_buckets[VFS_FLOCK_BUCKETS];

static inline unsigned long vfs_irq_save_disable(void)
{
    unsigned long flags = 0;
#ifdef __x86_64__
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
#endif
    return flags;
}

static inline void vfs_irq_restore(unsigned long flags)
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

static void *g_kernel_open_files[PROCESS_MAX_OPEN_FILES];
static char g_kernel_cwd[VFS_PATH_MAX] = "/";
static uint32_t g_kernel_umask = 0022U;

static int g_fat32_ok;
static int g_initrd_ok;
static fat32_volume_t *g_fat32_primary_vol;

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

static uint64_t vfs_hash_str(const char *s)
{
    uint64_t h = 14695981039346656037ULL;
    while (s && *s) {
        h ^= (uint8_t)(*s++);
        h *= 1099511628211ULL;
    }
    return h ? h : 1ULL;
}

static uint32_t vfs_current_umask(process_t *proc)
{
    return proc ? (proc->umask & 0777U) : (g_kernel_umask & 0777U);
}

/* Mount table */

static void mount_table_reset(void)
{
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].used = 0;
        g_mounts[i].mount_id = 0;
        g_mounts[i].open_count = 0;
        g_mounts[i].active_ops = 0;
        g_mounts[i].path[0] = '\0';
        g_mounts[i].path_len = 0;
        g_mounts[i].backend = VFS_BACKEND_NONE;
        g_mounts[i].read_only = 1;
        g_mounts[i].name = "none";
        g_mounts[i].fs_private = NULL;
    }
    g_next_mount_id = 1;
    spin_unlock_irqrestore(&g_mounts_lock, flags);
}

static int mount_register_ctx(const char *path, vfs_backend_t backend,
                              int read_only, const char *name, void *fs_private)
{
    char norm[VFS_PATH_MAX];
    unsigned long flags;
    if (normalize_path_for_process(NULL, path, norm, VFS_PATH_MAX) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&g_mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && kstreq(g_mounts[i].path, norm)) {
            spin_unlock_irqrestore(&g_mounts_lock, flags);
            return -EBUSY;
        }
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) {
            g_mounts[i].used = 1;
            g_mounts[i].mount_id = g_next_mount_id++;
            g_mounts[i].open_count = 0;
            g_mounts[i].active_ops = 0;
            kstrncpy(g_mounts[i].path, norm, VFS_PATH_MAX);
            g_mounts[i].path_len = kstrlen(g_mounts[i].path);
            g_mounts[i].backend = backend;
            g_mounts[i].read_only = read_only ? 1 : 0;
            g_mounts[i].name = name ? name : "unknown";
            g_mounts[i].fs_private = fs_private;
            spin_unlock_irqrestore(&g_mounts_lock, flags);
            return 0;
        }
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);
    return -ENOSPC;
}

static int mount_register(const char *path, vfs_backend_t backend, int read_only, const char *name)
{
    return mount_register_ctx(path, backend, read_only, name, NULL);
}

static vfs_mount_t *mount_lookup_ref(const char *path)
{
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    vfs_mount_t *best = NULL;
    int best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t *m = &g_mounts[i];
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
    if (best)
        best->active_ops++;
    spin_unlock_irqrestore(&g_mounts_lock, flags);
    return best;
}

static void mount_unref(vfs_mount_t *m)
{
    if (!m)
        return;
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    if (m->active_ops > 0)
        m->active_ops--;
    spin_unlock_irqrestore(&g_mounts_lock, flags);
}

static const vfs_mount_t *mount_lookup(const char *path)
{
    vfs_mount_t *m = mount_lookup_ref(path);
    if (m)
        mount_unref(m);
    return m;
}

static void mount_note_open_delta(uint64_t mount_id, int delta)
{
    if (mount_id == 0)
        return;
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && g_mounts[i].mount_id == mount_id) {
            g_mounts[i].open_count += delta;
            if (g_mounts[i].open_count < 0)
                g_mounts[i].open_count = 0;
            break;
        }
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);
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
    unsigned long flags;
    if (!out || max <= 0)
        return -1;
    flags = spin_lock_irqsave(&g_mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS && count < max; i++) {
        if (!g_mounts[i].used)
            continue;
        kstrncpy(out[count].path, g_mounts[i].path, VFS_PATH_MAX);
        kstrncpy(out[count].fs_name, g_mounts[i].name, (int)sizeof(out[count].fs_name));
        out[count].read_only = g_mounts[i].read_only;
        count++;
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);
    return count;
}

/* Hierarchical dentry cache: (parent_id, name) -> (id, mount_id, ino, type, target) */

static size_t dcache_hash(uint64_t parent_id, const char *name)
{
    uint64_t h = parent_id * 0x9E3779B97F4A7C15ULL;
    h ^= vfs_hash_str(name);
    h ^= h >> 30;
    return (size_t)(h & (VFS_DCACHE_BUCKETS - 1U));
}

static void dcache_lru_unlink(vfs_dentry_t *d)
{
    if (d->lru_prev)
        d->lru_prev->lru_next = d->lru_next;
    else if (g_dcache_lru_head == d)
        g_dcache_lru_head = d->lru_next;

    if (d->lru_next)
        d->lru_next->lru_prev = d->lru_prev;
    else if (g_dcache_lru_tail == d)
        g_dcache_lru_tail = d->lru_prev;

    d->lru_prev = NULL;
    d->lru_next = NULL;
}

static void dcache_lru_push_head(vfs_dentry_t *d)
{
    d->lru_prev = NULL;
    d->lru_next = g_dcache_lru_head;
    if (g_dcache_lru_head)
        g_dcache_lru_head->lru_prev = d;
    else
        g_dcache_lru_tail = d;
    g_dcache_lru_head = d;
}

static void dcache_hash_unlink(vfs_dentry_t *d)
{
    size_t b = dcache_hash(d->parent_id, d->name);
    vfs_dentry_t *prev = NULL;
    vfs_dentry_t *curr = g_dcache_buckets[b];
    while (curr) {
        if (curr == d) {
            if (prev)
                prev->hash_next = curr->hash_next;
            else
                g_dcache_buckets[b] = curr->hash_next;
            curr->hash_next = NULL;
            return;
        }
        prev = curr;
        curr = curr->hash_next;
    }
}

static void dcache_free_entry_locked(vfs_dentry_t *d)
{
    dcache_hash_unlink(d);
    dcache_lru_unlink(d);
    if (g_dcache_count > 0)
        g_dcache_count--;
    if (d->target)
        kfree(d->target);
    kfree(d);
}

static void dcache_init(void)
{
    unsigned long flags = spin_lock_irqsave(&g_dcache_lock);
    for (size_t i = 0; i < VFS_DCACHE_BUCKETS; i++) {
        vfs_dentry_t *curr = g_dcache_buckets[i];
        while (curr) {
            vfs_dentry_t *next = curr->hash_next;
            if (curr->target)
                kfree(curr->target);
            kfree(curr);
            curr = next;
        }
        g_dcache_buckets[i] = NULL;
    }
    g_dcache_lru_head = NULL;
    g_dcache_lru_tail = NULL;
    g_dcache_count = 0;
    g_dcache_next_id = 2;
    spin_unlock_irqrestore(&g_dcache_lock, flags);
}

static vfs_dentry_t *dcache_find_locked(uint64_t parent_id, const char *name)
{
    size_t b = dcache_hash(parent_id, name);
    vfs_dentry_t *curr = g_dcache_buckets[b];
    while (curr) {
        if (curr->parent_id == parent_id && kstreq(curr->name, name))
            return curr;
        curr = curr->hash_next;
    }
    return NULL;
}

static int dcache_lookup(uint64_t parent_id, const char *name, uint64_t mount_id,
                         uint64_t *id_out, uint64_t *ino_out, uint32_t *type_out,
                         char *target_out, size_t target_cap)
{
    unsigned long flags = spin_lock_irqsave(&g_dcache_lock);
    vfs_dentry_t *d = dcache_find_locked(parent_id, name);
    if (!d || d->mount_id != mount_id) {
        spin_unlock_irqrestore(&g_dcache_lock, flags);
        return -1;
    }
    dcache_lru_unlink(d);
    dcache_lru_push_head(d);
    if (id_out)
        *id_out = d->id;
    if (ino_out)
        *ino_out = d->ino;
    if (type_out)
        *type_out = d->type;
    if (target_out && target_cap > 0) {
        if (d->target)
            kstrncpy(target_out, d->target, (int)target_cap);
        else
            target_out[0] = '\0';
    }
    spin_unlock_irqrestore(&g_dcache_lock, flags);
    return 0;
}

static uint64_t dcache_insert(uint64_t parent_id, const char *name, uint64_t mount_id,
                              uint64_t ino, uint32_t type, const char *target)
{
    if (!name || !name[0] || kstrlen(name) >= VFS_NAME_MAX)
        return 0;

    char *target_copy = NULL;
    if (type == VFS_TYPE_SYMLINK && target) {
        int tlen = kstrlen(target);
        target_copy = (char *)kmalloc((size_t)tlen + 1U);
        if (!target_copy)
            return 0;
        kstrncpy(target_copy, target, tlen + 1);
    }

    vfs_dentry_t *nd = (vfs_dentry_t *)kmalloc(sizeof(vfs_dentry_t));
    if (!nd) {
        if (target_copy)
            kfree(target_copy);
        return 0;
    }

    unsigned long flags = spin_lock_irqsave(&g_dcache_lock);
    vfs_dentry_t *existing = dcache_find_locked(parent_id, name);
    if (existing) {
        existing->mount_id = mount_id;
        existing->ino = ino;
        existing->type = type;
        if (existing->target)
            kfree(existing->target);
        existing->target = target_copy;
        dcache_lru_unlink(existing);
        dcache_lru_push_head(existing);
        uint64_t eid = existing->id;
        spin_unlock_irqrestore(&g_dcache_lock, flags);
        kfree(nd);
        return eid;
    }

    while (g_dcache_count >= VFS_DCACHE_MAX_ENTRIES && g_dcache_lru_tail)
        dcache_free_entry_locked(g_dcache_lru_tail);

    nd->id = g_dcache_next_id++;
    if (nd->id <= 1ULL)
        nd->id = g_dcache_next_id++;
    nd->parent_id = parent_id;
    nd->mount_id = mount_id;
    nd->ino = ino;
    nd->type = type;
    kstrncpy(nd->name, name, VFS_NAME_MAX);
    nd->target = target_copy;
    size_t b = dcache_hash(parent_id, nd->name);
    nd->hash_next = g_dcache_buckets[b];
    g_dcache_buckets[b] = nd;
    dcache_lru_push_head(nd);
    g_dcache_count++;
    uint64_t new_id = nd->id;
    spin_unlock_irqrestore(&g_dcache_lock, flags);
    return new_id;
}

static void dcache_invalidate_children_locked(uint64_t parent_id)
{
    uint64_t queue[64];
    int qhead = 0;
    int qtail = 0;
    queue[qtail++] = parent_id;

    while (qhead < qtail) {
        uint64_t pid = queue[qhead++];
        for (size_t b = 0; b < VFS_DCACHE_BUCKETS; b++) {
            vfs_dentry_t *curr = g_dcache_buckets[b];
            while (curr) {
                vfs_dentry_t *next = curr->hash_next;
                if (curr->parent_id == pid) {
                    if (curr->type == VFS_TYPE_DIR && qtail < 64)
                        queue[qtail++] = curr->id;
                    dcache_free_entry_locked(curr);
                    /* Restart bucket scan after modifying bucket list. */
                    curr = g_dcache_buckets[b];
                    continue;
                }
                curr = next;
            }
        }
    }
}

static void dcache_invalidate_path(const char *abs_path)
{
    if (!abs_path || abs_path[0] != '/')
        return;

    unsigned long flags = spin_lock_irqsave(&g_dcache_lock);
    if (abs_path[1] == '\0') {
        dcache_invalidate_children_locked(1ULL);
        spin_unlock_irqrestore(&g_dcache_lock, flags);
        return;
    }

    uint64_t cur_id = 1ULL;
    const char *p = abs_path + 1;
    while (*p) {
        char comp[VFS_NAME_MAX];
        int ci = 0;
        while (*p && *p != '/') {
            if (ci < VFS_NAME_MAX - 1)
                comp[ci++] = *p;
            p++;
        }
        comp[ci] = '\0';
        while (*p == '/')
            p++;

        vfs_dentry_t *d = dcache_find_locked(cur_id, comp);
        if (!d)
            break;

        if (*p == '\0') {
            uint64_t target_id = d->id;
            uint32_t target_type = d->type;
            dcache_free_entry_locked(d);
            if (target_type == VFS_TYPE_DIR)
                dcache_invalidate_children_locked(target_id);
            break;
        }
        cur_id = d->id;
    }
    spin_unlock_irqrestore(&g_dcache_lock, flags);
}

static void dcache_invalidate_mount(uint64_t mount_id)
{
    if (mount_id == 0)
        return;
    unsigned long flags = spin_lock_irqsave(&g_dcache_lock);
    for (size_t b = 0; b < VFS_DCACHE_BUCKETS; b++) {
        vfs_dentry_t *curr = g_dcache_buckets[b];
        while (curr) {
            if (curr->mount_id == mount_id) {
                dcache_free_entry_locked(curr);
                curr = g_dcache_buckets[b];
                continue;
            }
            curr = curr->hash_next;
        }
    }
    spin_unlock_irqrestore(&g_dcache_lock, flags);
}

/* Per-inode flock table */

static size_t flock_hash(uint64_t dev_id, uint64_t ino_id)
{
    uint64_t h = (dev_id * 0x9E3779B97F4A7C15ULL) ^ (ino_id * 0xBF58476D1CE4E5B9ULL);
    h ^= h >> 30;
    return (size_t)(h & (VFS_FLOCK_BUCKETS - 1U));
}

static void flock_init(void)
{
    unsigned long flags = spin_lock_irqsave(&g_flock_lock);
    for (size_t i = 0; i < VFS_FLOCK_BUCKETS; i++)
        g_flock_buckets[i] = NULL;
    spin_unlock_irqrestore(&g_flock_lock, flags);
}

static vfs_flock_inode_t *flock_find_inode_locked(uint64_t dev_id, uint64_t ino_id,
                                                  vfs_flock_inode_t **prev_out)
{
    size_t b = flock_hash(dev_id, ino_id);
    vfs_flock_inode_t *prev = NULL;
    vfs_flock_inode_t *curr = g_flock_buckets[b];
    while (curr) {
        if (curr->dev_id == dev_id && curr->ino_id == ino_id) {
            if (prev_out)
                *prev_out = prev;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    if (prev_out)
        *prev_out = NULL;
    return NULL;
}

static void flock_maybe_free_inode_locked(vfs_flock_inode_t *fi)
{
    if (!fi || fi->holders != NULL || fi->waiters > 0)
        return;
    size_t b = flock_hash(fi->dev_id, fi->ino_id);
    vfs_flock_inode_t *prev = NULL;
    vfs_flock_inode_t *curr = g_flock_buckets[b];
    while (curr) {
        if (curr == fi) {
            if (prev)
                prev->next = curr->next;
            else
                g_flock_buckets[b] = curr->next;
            kfree(fi);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

static void vfs_flock_release_file(vfs_file_t *f)
{
    if (!f || (f->inode_dev == 0 && f->inode_ino == 0))
        return;

    unsigned long flags = spin_lock_irqsave(&g_flock_lock);
    vfs_flock_inode_t *fi = flock_find_inode_locked(f->inode_dev, f->inode_ino, NULL);
    if (!fi) {
        spin_unlock_irqrestore(&g_flock_lock, flags);
        return;
    }

    vfs_flock_holder_t *prev = NULL;
    vfs_flock_holder_t *curr = fi->holders;
    int removed = 0;
    while (curr) {
        if (curr->owner_file == f) {
            if (prev)
                prev->next = curr->next;
            else
                fi->holders = curr->next;
            kfree(curr);
            removed = 1;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    if (removed)
        wait_queue_wake_all(&fi->waitq);
    flock_maybe_free_inode_locked(fi);
    spin_unlock_irqrestore(&g_flock_lock, flags);
}

/* Open-file / pipe internals */

static void file_pool_reset(void)
{
    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++)
        g_kernel_open_files[i] = NULL;
    kstrncpy(g_kernel_cwd, "/", VFS_PATH_MAX);
    g_kernel_umask = 0022U;
    spin_unlock_irqrestore(&g_fd_lock, flags);
}

static vfs_file_t *file_alloc(void)
{
    vfs_file_t *f = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (!f)
        return NULL;
    for (size_t j = 0; j < sizeof(*f); j++)
        ((uint8_t *)f)[j] = 0;
    f->refcount = 1;
    f->lock = SPINLOCK_INIT;
    return f;
}

static void file_ref(vfs_file_t *f)
{
    if (!f)
        return;
    unsigned long flags = spin_lock_irqsave(&g_vfs_alloc_lock);
    f->refcount++;
    spin_unlock_irqrestore(&g_vfs_alloc_lock, flags);
}

static vfs_pipe_t *pipe_alloc(void)
{
    vfs_pipe_t *p = (vfs_pipe_t *)kmalloc(sizeof(vfs_pipe_t));
    if (!p)
        return NULL;
    p->read_pos = 0;
    p->write_pos = 0;
    p->size = 0;
    p->readers = 0;
    p->writers = 0;
    p->lock = SPINLOCK_INIT;
    wait_queue_init(&p->waitq);
    return p;
}

static void pipe_free_slot(vfs_pipe_t *p)
{
    if (p)
        kfree(p);
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

static int ensure_regular_capacity(vfs_file_t *f, size_t need)
{
    size_t new_cap;
    uint8_t *new_buf;
    if (!f || f->backend == VFS_BACKEND_TMPFS || f->backend == VFS_BACKEND_PIPE ||
        f->backend == VFS_BACKEND_EXT4 || f->backend == VFS_BACKEND_FAT32 ||
        f->backend == VFS_BACKEND_FAT32VOL)
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
    if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        if (f->u.fat32.vol)
            (void)fat32_vol_sync(f->u.fat32.vol);
    }
    f->dirty = 0;
    return 0;
}

static void file_release(vfs_file_t *f)
{
    unsigned long aflags;
    if (!f)
        return;

    aflags = spin_lock_irqsave(&g_vfs_alloc_lock);
    if (f->refcount > 1) {
        f->refcount--;
        spin_unlock_irqrestore(&g_vfs_alloc_lock, aflags);
        return;
    }
    f->refcount = 0;
    spin_unlock_irqrestore(&g_vfs_alloc_lock, aflags);

    vfs_flock_release_file(f);
    int was_dirty = f->dirty;
    (void)flush_regular_file(f);

    if (f->backend == VFS_BACKEND_PIPE && f->u.pipe.pipe) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        unsigned long pflags = spin_lock_irqsave(&p->lock);
        if (f->u.pipe.can_read && p->readers > 0)
            p->readers--;
        if (f->u.pipe.can_write && p->writers > 0)
            p->writers--;
        int empty_claims = (p->readers == 0 && p->writers == 0);
        spin_unlock_irqrestore(&p->lock, pflags);
        wait_queue_wake_all(&p->waitq);
        if (empty_claims)
            pipe_free_slot(p);
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener) {
            unix_pending_conn_t pc;
            while (f->u.sock.listener &&
                   unix_dequeue_pending(f->u.sock.listener, &pc) == 0) {
                unix_ring_drop_claim(pc.pipe1, 1, 0);
                unix_ring_drop_claim(pc.pipe2, 0, 1);
            }
            if (f->path[0])
                unix_unregister_listener(f->path);
        } else {
            unix_ring_drop_claim(f->u.sock.rx, 1, 0);
            unix_ring_drop_claim(f->u.sock.tx, 0, 1);
        }
    }

    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode) {
        tmpfs_inode_unref(f->u.tmpfs.inode);
        f->u.tmpfs.inode = NULL;
    }

    if (f->backend == VFS_BACKEND_EXT4) {
        ext4fs_close((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, was_dirty);
        f->u.ext4.handle = NULL;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->close)
            f->u.device.ops->close(f->u.device.priv);
    }

    if (f->backend != VFS_BACKEND_FAT32 && f->backend != VFS_BACKEND_FAT32VOL &&
        f->backend != VFS_BACKEND_EXT4 && f->backend != VFS_BACKEND_TMPFS &&
        f->backend != VFS_BACKEND_PIPE && f->backend != VFS_BACKEND_UNIXSOCK &&
        f->backend != VFS_BACKEND_DEVFS && f->backend != VFS_BACKEND_EPOLL) {
        if (f->u.regular.owns_buf && f->u.regular.buf)
            kfree(f->u.regular.buf);
    }

    if (f->backend == VFS_BACKEND_EPOLL) {
        epoll_item_t *curr = f->u.epoll.items;
        while (curr) {
            epoll_item_t *next = curr->next;
            kfree(curr);
            curr = next;
        }
        f->u.epoll.items = NULL;
        f->u.epoll.count = 0;
    }

    if (f->mount_id != 0)
        mount_note_open_delta(f->mount_id, -1);

    kfree(f);
}

vfs_file_t *vfs_file_ref(vfs_file_t *f)
{
    if (!f)
        return NULL;
    file_ref(f);
    return f;
}

void vfs_file_unref(vfs_file_t *f)
{
    file_release(f);
}

int vfs_file_is_mmap_regular(const vfs_file_t *f)
{
    if (!f || f->is_dir)
        return 0;
    switch (f->backend) {
    case VFS_BACKEND_EXT4:
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL:
    case VFS_BACKEND_TMPFS:
    case VFS_BACKEND_INITRD:
    case VFS_BACKEND_TAR:
    case VFS_BACKEND_BOOTFS:
        return 1;
    default:
        return 0;
    }
}

int vfs_file_is_writable(const vfs_file_t *f)
{
    if (!f || f->is_dir || !(f->mode & VFS_MODE_WRITE))
        return 0;
    switch (f->backend) {
    case VFS_BACKEND_EXT4:
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL:
    case VFS_BACKEND_TMPFS:
        return 1;
    default:
        return 0;
    }
}

uint64_t vfs_file_size_bytes(vfs_file_t *f)
{
    if (!f || f->is_dir)
        return 0;
    switch (f->backend) {
    case VFS_BACKEND_TMPFS:
        return f->u.tmpfs.inode ? (uint64_t)tmpfs_inode_size(f->u.tmpfs.inode) : 0ULL;
    case VFS_BACKEND_EXT4:
        return (uint64_t)ext4fs_file_size((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, f->path);
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL: {
        fat32_dirent_t de;
        if (f->u.fat32.vol && fat32_vol_stat(f->u.fat32.vol, f->path, &de) == 0)
            return (uint64_t)de.size;
        return 0ULL;
    }
    case VFS_BACKEND_INITRD:
    case VFS_BACKEND_TAR:
    case VFS_BACKEND_BOOTFS:
        return (uint64_t)f->u.regular.size;
    default:
        return 0ULL;
    }
}

static uint64_t vfs_pc_key_ino(const vfs_file_t *f)
{
    if (!f)
        return 0;
    if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL)
        return vfs_hash_str(f->path);
    return f->inode_ino ? f->inode_ino : vfs_hash_str(f->path);
}

static size_t vfs_pc_bucket(uint64_t mount_id, uint64_t key_ino)
{
    uint64_t h = mount_id * 0x9E3779B97F4A7C15ULL ^ key_ino * 0xBF58476D1CE4E5B9ULL;
    return (size_t)((h ^ (h >> 32)) & (VFS_PC_INODE_BUCKETS - 1U));
}

static int vfs_pc_readpage(void *ctx, uint64_t obj_id, uint64_t page_index, void *buf)
{
    vfs_pc_inode_t *pc = (vfs_pc_inode_t *)ctx;
    (void)obj_id;
    if (!pc || !buf)
        return -EINVAL;

    k_memset(buf, 0, PAGECACHE_PAGE_SIZE);
    uint64_t pos = page_index * (uint64_t)PAGECACHE_PAGE_SIZE;

    switch (pc->backend) {
    case VFS_BACKEND_TMPFS:
        if (!pc->tmpfs_ino)
            return -EIO;
        (void)tmpfs_read(pc->tmpfs_ino, (size_t)pos, buf, PAGECACHE_PAGE_SIZE);
        return 0;
    case VFS_BACKEND_EXT4: {
        long rc = ext4fs_read_subpath((ext4fs_t *)pc->fs_ctx, pc->subpath,
                                      (size_t)pos, buf, PAGECACHE_PAGE_SIZE);
        return (rc < 0) ? (int)rc : 0;
    }
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL: {
        size_t got = 0;
        int rc = fat32_vol_read_at((fat32_volume_t *)pc->fs_ctx, pc->subpath,
                                   (size_t)pos, buf, PAGECACHE_PAGE_SIZE,
                                   &got, NULL, NULL, NULL);
        return (rc < 0) ? rc : 0;
    }
    case VFS_BACKEND_INITRD:
    case VFS_BACKEND_TAR:
    case VFS_BACKEND_BOOTFS:
        if (pc->ro_buf && pos < (uint64_t)pc->ro_size) {
            size_t avail = pc->ro_size - (size_t)pos;
            if (avail > PAGECACHE_PAGE_SIZE)
                avail = PAGECACHE_PAGE_SIZE;
            k_memcpy(buf, pc->ro_buf + pos, avail);
        }
        return 0;
    default:
        return -EINVAL;
    }
}

static int vfs_pc_writepage(void *ctx, uint64_t obj_id, uint64_t page_index, const void *buf)
{
    vfs_pc_inode_t *pc = (vfs_pc_inode_t *)ctx;
    (void)obj_id;
    if (!pc || !buf)
        return -EINVAL;

    uint64_t pos = page_index * (uint64_t)PAGECACHE_PAGE_SIZE;

    switch (pc->backend) {
    case VFS_BACKEND_TMPFS: {
        if (!pc->tmpfs_ino)
            return -EIO;
        size_t sz = tmpfs_inode_size(pc->tmpfs_ino);
        if (pos >= (uint64_t)sz)
            return 0;
        size_t to_write = sz - (size_t)pos;
        if (to_write > PAGECACHE_PAGE_SIZE)
            to_write = PAGECACHE_PAGE_SIZE;
        size_t wr = tmpfs_write(pc->tmpfs_ino, (size_t)pos, buf, to_write);
        return (wr == to_write) ? 0 : -EIO;
    }
    case VFS_BACKEND_EXT4: {
        long rc = ext4fs_write_subpath((ext4fs_t *)pc->fs_ctx, pc->subpath,
                                       (size_t)pos, buf, PAGECACHE_PAGE_SIZE);
        return (rc < 0) ? (int)rc : 0;
    }
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL: {
        fat32_volume_t *vol = (fat32_volume_t *)pc->fs_ctx;
        fat32_dirent_t de;
        int src = fat32_vol_stat(vol, pc->subpath, &de);
        if (src != 0)
            return src;
        if (pos >= (uint64_t)de.size)
            return 0;
        size_t to_write = (size_t)de.size - (size_t)pos;
        if (to_write > PAGECACHE_PAGE_SIZE)
            to_write = PAGECACHE_PAGE_SIZE;
        size_t wr = 0;
        int rc = fat32_vol_write_at(vol, pc->subpath, (size_t)pos,
                                    buf, to_write, &wr, NULL, NULL, NULL);
        return (rc < 0) ? rc : 0;
    }
    default:
        return -EROFS;
    }
}

static const pagecache_ops_t g_vfs_file_pc_ops = {
    .readpage = vfs_pc_readpage,
    .writepage = vfs_pc_writepage,
    .flags = PAGECACHE_OPS_FS_LOCK,
};

static vfs_pc_inode_t *vfs_pc_find_locked(uint64_t mount_id, uint64_t key_ino)
{
    size_t b = vfs_pc_bucket(mount_id, key_ino);
    vfs_pc_inode_t *curr = g_pc_inodes[b];
    while (curr) {
        if (curr->mount_id == mount_id && curr->key_ino == key_ino)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

static vfs_pc_inode_t *vfs_pc_find_for_file(vfs_file_t *f)
{
    if (!f || !vfs_file_is_mmap_regular(f))
        return NULL;
    if (f->pc_inode)
        return f->pc_inode;
    uint64_t key_ino = vfs_pc_key_ino(f);
    unsigned long flags = spin_lock_irqsave(&g_pc_inode_lock);
    vfs_pc_inode_t *pc = vfs_pc_find_locked(f->mount_id, key_ino);
    spin_unlock_irqrestore(&g_pc_inode_lock, flags);
    if (pc)
        f->pc_inode = pc;
    return pc;
}

static vfs_pc_inode_t *vfs_pc_get_or_create(vfs_file_t *f)
{
    if (!f || !vfs_file_is_mmap_regular(f))
        return NULL;
    if (f->pc_inode)
        return f->pc_inode;

    uint64_t key_ino = vfs_pc_key_ino(f);
    unsigned long flags = spin_lock_irqsave(&g_pc_inode_lock);
    vfs_pc_inode_t *pc = vfs_pc_find_locked(f->mount_id, key_ino);
    spin_unlock_irqrestore(&g_pc_inode_lock, flags);
    if (pc) {
        f->pc_inode = pc;
        return pc;
    }

    vfs_pc_inode_t *npc = (vfs_pc_inode_t *)kmalloc(sizeof(vfs_pc_inode_t));
    if (!npc)
        return NULL;
    k_memset(npc, 0, sizeof(*npc));
    npc->mount_id = f->mount_id;
    npc->key_ino = key_ino;
    npc->backend = f->backend;
    npc->fs_ctx = f->fs_ctx;
    kstrncpy(npc->subpath, f->path, VFS_PATH_MAX);
    if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL)
        npc->fs_ctx = f->u.fat32.vol;
    if (f->backend == VFS_BACKEND_INITRD || f->backend == VFS_BACKEND_TAR ||
        f->backend == VFS_BACKEND_BOOTFS) {
        npc->ro_buf = f->u.regular.buf;
        npc->ro_size = f->u.regular.size;
    }

    flags = spin_lock_irqsave(&g_pc_inode_lock);
    vfs_pc_inode_t *existing = vfs_pc_find_locked(f->mount_id, key_ino);
    if (existing) {
        spin_unlock_irqrestore(&g_pc_inode_lock, flags);
        kfree(npc);
        f->pc_inode = existing;
        return existing;
    }
    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode) {
        tmpfs_inode_ref(f->u.tmpfs.inode);
        npc->tmpfs_ino = f->u.tmpfs.inode;
    }
    uint64_t seq = __atomic_fetch_add(&g_next_pc_obj_seq, 1ULL, __ATOMIC_RELAXED);
    npc->obj_id = VFS_PC_OBJ_ID_BASE | (seq & 0x0000FFFFFFFFFFFFULL);
    size_t b = vfs_pc_bucket(f->mount_id, key_ino);
    npc->next = g_pc_inodes[b];
    g_pc_inodes[b] = npc;
    spin_unlock_irqrestore(&g_pc_inode_lock, flags);

    f->pc_inode = npc;
    return npc;
}

static void vfs_pc_sync_all(void)
{
    for (size_t b = 0; b < VFS_PC_INODE_BUCKETS; b++) {
        uint64_t oids[16];
        size_t count = 0;
        unsigned long flags = spin_lock_irqsave(&g_pc_inode_lock);
        vfs_pc_inode_t *curr = g_pc_inodes[b];
        while (curr && count < 16) {
            oids[count++] = curr->obj_id;
            curr = curr->next;
        }
        spin_unlock_irqrestore(&g_pc_inode_lock, flags);
        for (size_t i = 0; i < count; i++)
            (void)pagecache_sync_obj(oids[i]);
    }
}

static void vfs_pc_unmount(uint64_t mount_id)
{
    for (size_t b = 0; b < VFS_PC_INODE_BUCKETS; b++) {
        for (;;) {
            vfs_pc_inode_t *victim = NULL;
            unsigned long flags = spin_lock_irqsave(&g_pc_inode_lock);
            vfs_pc_inode_t *prev = NULL;
            vfs_pc_inode_t *curr = g_pc_inodes[b];
            while (curr) {
                if (curr->mount_id == mount_id) {
                    if (prev)
                        prev->next = curr->next;
                    else
                        g_pc_inodes[b] = curr->next;
                    victim = curr;
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
            spin_unlock_irqrestore(&g_pc_inode_lock, flags);
            if (!victim)
                break;
            (void)pagecache_sync_obj(victim->obj_id);
            pagecache_invalidate_obj(victim->obj_id);
            if (victim->tmpfs_ino)
                tmpfs_inode_unref(victim->tmpfs_ino);
            kfree(victim);
        }
    }
}

static void vfs_pc_invalidate_key(uint64_t mount_id, uint64_t key_ino)
{
    vfs_pc_inode_t *victim = NULL;
    size_t b = vfs_pc_bucket(mount_id, key_ino);
    unsigned long flags = spin_lock_irqsave(&g_pc_inode_lock);
    vfs_pc_inode_t *prev = NULL;
    vfs_pc_inode_t *curr = g_pc_inodes[b];
    while (curr) {
        if (curr->mount_id == mount_id && curr->key_ino == key_ino) {
            if (prev)
                prev->next = curr->next;
            else
                g_pc_inodes[b] = curr->next;
            victim = curr;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    spin_unlock_irqrestore(&g_pc_inode_lock, flags);
    if (victim) {
        pagecache_invalidate_obj(victim->obj_id);
        if (victim->tmpfs_ino)
            tmpfs_inode_unref(victim->tmpfs_ino);
        kfree(victim);
    }
}

static void vfs_pc_update_write(vfs_file_t *f, size_t pos, const void *buf, size_t len)
{
    vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
    if (!pc || !buf || len == 0)
        return;

    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (done < len) {
        uint64_t cur = (uint64_t)pos + (uint64_t)done;
        uint64_t pidx = cur >> 12;
        size_t off = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off;
        if (chunk > len - done)
            chunk = len - done;

        pagecache_page_t *pg = pagecache_lookup_page(pc->obj_id, pidx);
        if (pg) {
            k_memcpy((uint8_t *)pg->data + off, src + done, chunk);
            pagecache_put_page(pg);
        }
        done += chunk;
    }
}

uint64_t vfs_file_pc_obj_id(vfs_file_t *f)
{
    vfs_pc_inode_t *pc = vfs_pc_get_or_create(f);
    return pc ? pc->obj_id : 0ULL;
}

struct pagecache_page *vfs_file_get_page(vfs_file_t *f, uint64_t page_index)
{
    vfs_pc_inode_t *pc = vfs_pc_get_or_create(f);
    if (!pc)
        return NULL;
    return pagecache_get_page(pc->obj_id, page_index, &g_vfs_file_pc_ops, pc);
}

int vfs_file_sync_range(vfs_file_t *f, uint64_t start_page, uint64_t end_page, int do_fs_sync)
{
    if (!f)
        return -EINVAL;
    vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
    int rc = 0;
    if (pc) {
        rc = pagecache_sync_obj_range(pc->obj_id, start_page, end_page);
    }
    if (do_fs_sync) {
        if (f->backend == VFS_BACKEND_EXT4 && f->fs_ctx) {
            int frc = ext4fs_sync((ext4fs_t *)f->fs_ctx);
            if (rc == 0 && frc != 0)
                rc = frc;
        } else if ((f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) &&
                   f->u.fat32.vol) {
            int frc = fat32_vol_sync(f->u.fat32.vol);
            if (rc == 0 && frc != 0)
                rc = frc;
        }
    }
    return rc;
}

static vfs_file_t *fd_lookup_ref(process_t *proc, int fd)
{
    void **tbl = fd_table_for_process(proc);
    vfs_file_t *f = NULL;
    if (!fd_valid(fd))
        return NULL;
    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    f = (vfs_file_t *)tbl[fd];
    if (f == (vfs_file_t *)(uintptr_t)1)
        f = NULL;
    if (f)
        file_ref(f);
    spin_unlock_irqrestore(&g_fd_lock, flags);
    return f;
}

static void fd_close_on_table(void **tbl, int fd)
{
    vfs_file_t *f;
    if (!tbl || !fd_valid(fd))
        return;

    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    f = (vfs_file_t *)tbl[fd];
    if (!f || f == (vfs_file_t *)(uintptr_t)1) {
        tbl[fd] = NULL;
        spin_unlock_irqrestore(&g_fd_lock, flags);
        return;
    }

    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        vfs_file_t *ef = (vfs_file_t *)tbl[i];
        if (ef && ef != (vfs_file_t *)(uintptr_t)1 && ef->backend == VFS_BACKEND_EPOLL) {
            unsigned long eflags = spin_lock_irqsave(&ef->lock);
            epoll_item_t *prev = NULL;
            epoll_item_t *curr = ef->u.epoll.items;
            while (curr) {
                if (curr->fd == fd) {
                    if (prev)
                        prev->next = curr->next;
                    else
                        ef->u.epoll.items = curr->next;
                    ef->u.epoll.count--;
                    kfree(curr);
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
            spin_unlock_irqrestore(&ef->lock, eflags);
        }
    }

    tbl[fd] = NULL;
    spin_unlock_irqrestore(&g_fd_lock, flags);
    file_release(f);
}

/* DevFS operations are handled via fs/devfs.c */

static void vfs_stat_finalize(vfs_stat_t *st, uint64_t dev_id, uint64_t ino_id)
{
    if (!st)
        return;
    if (st->st_dev == 0)
        st->st_dev = dev_id;
    if (st->st_ino == 0)
        st->st_ino = ino_id;
    if (st->st_size == 0 && st->size != 0)
        st->st_size = st->size;
    if (st->st_blocks == 0 && st->blocks != 0)
        st->st_blocks = st->blocks;
    if (st->st_blksize == 0)
        st->st_blksize = 4096U;
    if (st->st_nlink == 0)
        st->st_nlink = (st->type == VFS_TYPE_DIR) ? 2U : 1U;
    if (st->st_mode == 0) {
        uint32_t ifmt = VFS_S_IFREG;
        uint32_t perm = 0;
        if (st->type == VFS_TYPE_DIR)
            ifmt = VFS_S_IFDIR;
        else if (st->type == VFS_TYPE_CHAR)
            ifmt = VFS_S_IFCHR;
        else if (st->type == VFS_TYPE_BLOCK)
            ifmt = VFS_S_IFBLK;
        else if (st->type == VFS_TYPE_PIPE)
            ifmt = VFS_S_IFIFO;
        else if (st->type == VFS_TYPE_SOCKET)
            ifmt = VFS_S_IFSOCK;
        else if (st->type == VFS_TYPE_SYMLINK)
            ifmt = VFS_S_IFLNK;

        if (st->mode & VFS_MODE_READ)
            perm |= (st->type == VFS_TYPE_DIR) ? 0555U : 0444U;
        if (st->mode & VFS_MODE_WRITE)
            perm |= 0222U;
        st->st_mode = ifmt | perm;
    }
}

static int backend_stat_mode(const vfs_mount_t *m, const char *subpath, int follow_last, vfs_stat_t *out)
{
    if (!m || !subpath || !out)
        return -EINVAL;
    for (size_t i = 0; i < sizeof(*out); i++)
        ((uint8_t *)out)[i] = 0;

    out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);

    if (kstreq(subpath, "/")) {
        if (m->backend == VFS_BACKEND_TMPFS && m->fs_private) {
            if (tmpfs_stat((tmpfs_fs_t *)m->fs_private, "/", follow_last, out) != 0)
                return -ENOENT;
            out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);
            vfs_stat_finalize(out, m->mount_id, 1ULL);
        } else if (m->backend == VFS_BACKEND_EXT4 && m->fs_private) {
            if (ext4fs_stat((ext4fs_t *)m->fs_private, "/", follow_last, out) != 0)
                return -ENOENT;
            out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);
            vfs_stat_finalize(out, m->mount_id, out->st_ino ? out->st_ino : 2ULL);
        } else {
            out->type = VFS_TYPE_DIR;
            vfs_stat_finalize(out, m->mount_id, 1ULL);
        }
        return 0;
    }

    switch (m->backend) {
    case VFS_BACKEND_EXT4: {
        int rc = ext4fs_stat((ext4fs_t *)m->fs_private, subpath, follow_last, out);
        if (rc != 0)
            return rc;
        out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);
        vfs_stat_finalize(out, m->mount_id, out->st_ino ? out->st_ino : vfs_hash_str(subpath));
        return 0;
    }
    case VFS_BACKEND_TMPFS: {
        int rc = tmpfs_stat((tmpfs_fs_t *)m->fs_private, subpath, follow_last, out);
        if (rc != 0)
            return rc;
        out->mode = VFS_MODE_READ | (m->read_only ? 0 : VFS_MODE_WRITE);
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    }
    case VFS_BACKEND_FAT32: {
        fat32_dirent_t de;
        if (!g_fat32_ok)
            return -ENOENT;
        if (fat32_stat(subpath, &de) != 0)
            return -ENOENT;
        out->type = de.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = de.size;
        out->blocks = (de.size + 511) / 512;
        vfs_stat_finalize(out, m->mount_id, de.first_cluster ? (uint64_t)de.first_cluster : vfs_hash_str(subpath));
        return 0;
    }
    case VFS_BACKEND_FAT32VOL: {
        fat32_dirent_t de;
        if (fat32_vol_stat((fat32_volume_t *)m->fs_private, subpath, &de) != 0)
            return -ENOENT;
        out->type = de.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = de.size;
        out->blocks = (de.size + 511) / 512;
        vfs_stat_finalize(out, m->mount_id, de.first_cluster ? (uint64_t)de.first_cluster : vfs_hash_str(subpath));
        return 0;
    }
    case VFS_BACKEND_INITRD: {
        const void *data = NULL;
        size_t sz = 0;
        if (initrd_lookup(subpath, &data, &sz) != 0)
            return -ENOENT;
        out->type = VFS_TYPE_FILE;
        out->size = sz;
        out->blocks = (sz + 511) / 512;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    }
    case VFS_BACKEND_PROCFS:
        if (procfs_stat(subpath, out) != 0)
            return -ENOENT;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    case VFS_BACKEND_SYSFS:
        if (sysfs_stat(subpath, out) != 0)
            return -ENOENT;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    case VFS_BACKEND_BOOTFS:
        if (bootfs_stat(subpath, out) != 0)
            return -ENOENT;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    case VFS_BACKEND_DEVFS:
        if (devfs_stat(subpath, out) != 0)
            return -ENOENT;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    case VFS_BACKEND_TAR:
        if (tar_stat((tar_fs_t *)m->fs_private, subpath, out) != 0)
            return -ENOENT;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(subpath));
        return 0;
    default:
        return -ENOENT;
    }
}

static int backend_stat(const vfs_mount_t *m, const char *subpath, vfs_stat_t *out)
{
    return backend_stat_mode(m, subpath, 1, out);
}

/* Component-by-component path resolution backed by hierarchical dcache and cross-mount symlinks. */

static int vfs_resolve_path_ex(process_t *proc, const char *path, int follow_last,
                               char *abs_out, vfs_mount_t **mount_out,
                               char *sub_out, int *exists_out, uint32_t *type_out)
{
    char cur_raw[VFS_PATH_MAX];
    char cur_abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];

    if (!path || !path[0] || !abs_out || !mount_out || !sub_out)
        return -EINVAL;

    if (path_is_abs(path)) {
        if (kstrncpy(cur_raw, path, VFS_PATH_MAX) != 0)
            return -ENAMETOOLONG;
    } else {
        char *cwd = cwd_for_process(proc);
        if (path_copy_join(cur_raw, VFS_PATH_MAX, cwd && cwd[0] ? cwd : "/", path) != 0)
            return -ENAMETOOLONG;
    }

    for (int hop = 0; hop <= VFS_SYMLOOP_MAX; hop++) {
        uint64_t stack_ids[32];
        int stack_len[32];
        int depth = 0;

        cur_abs[0] = '/';
        cur_abs[1] = '\0';
        stack_ids[0] = 1ULL;
        stack_len[0] = 1;

        size_t i = 0;
        while (cur_raw[i] == '/')
            i++;

        int restarted = 0;
        int leaf_exists = 1;
        uint32_t leaf_type = VFS_TYPE_DIR;

        while (cur_raw[i]) {
            char comp[VFS_NAME_MAX];
            int ci = 0;
            while (cur_raw[i] && cur_raw[i] != '/') {
                if (ci >= VFS_NAME_MAX - 1)
                    return -ENAMETOOLONG;
                comp[ci++] = cur_raw[i++];
            }
            comp[ci] = '\0';
            while (cur_raw[i] == '/')
                i++;
            int has_more = (cur_raw[i] != '\0');

            if (kstreq(comp, "."))
                continue;
            if (kstreq(comp, "..")) {
                if (depth > 0) {
                    depth--;
                    cur_abs[stack_len[depth]] = '\0';
                }
                continue;
            }

            char next_abs[VFS_PATH_MAX];
            int cur_len = stack_len[depth];
            if (cur_len > 1) {
                if (cur_len + 1 + ci >= VFS_PATH_MAX)
                    return -ENAMETOOLONG;
                kstrncpy(next_abs, cur_abs, VFS_PATH_MAX);
                next_abs[cur_len] = '/';
                kstrncpy(next_abs + cur_len + 1, comp, VFS_PATH_MAX - cur_len - 1);
            } else {
                if (1 + ci >= VFS_PATH_MAX)
                    return -ENAMETOOLONG;
                next_abs[0] = '/';
                kstrncpy(next_abs + 1, comp, VFS_PATH_MAX - 1);
            }

            vfs_mount_t *m = mount_lookup_ref(next_abs);
            if (!m)
                return -ENOENT;
            if (mount_subpath(m, next_abs, sub, VFS_PATH_MAX) != 0) {
                mount_unref(m);
                return -EINVAL;
            }

            uint64_t child_id = 0;
            uint64_t child_ino = 0;
            uint32_t child_type = VFS_TYPE_UNKNOWN;
            char sym_target[VFS_PATH_MAX];
            sym_target[0] = '\0';

            int cache_hit = 0;
            if (m->backend != VFS_BACKEND_PROCFS && m->backend != VFS_BACKEND_SYSFS) {
                if (dcache_lookup(stack_ids[depth], comp, m->mount_id,
                                  &child_id, &child_ino, &child_type,
                                  sym_target, sizeof(sym_target)) == 0) {
                    cache_hit = 1;
                }
            }

            if (!cache_hit) {
                vfs_stat_t st;
                int brc = backend_stat_mode(m, sub, 0, &st);
                if (brc != 0) {
                    /* Allow unix socket path lookup if it's the final component. */
                    if (!has_more && unix_find_listener(next_abs) != NULL) {
                        child_type = VFS_TYPE_SOCKET;
                        child_ino = vfs_hash_str(next_abs);
                    } else if (!has_more) {
                        mount_unref(m);
                        leaf_exists = 0;
                        leaf_type = VFS_TYPE_UNKNOWN;
                        kstrncpy(cur_abs, next_abs, VFS_PATH_MAX);
                        break;
                    } else {
                        mount_unref(m);
                        return -ENOENT;
                    }
                } else {
                    child_type = st.type;
                    child_ino = st.st_ino;
                    if (child_type == VFS_TYPE_SYMLINK) {
                        int rl = -1;
                        if (m->backend == VFS_BACKEND_TMPFS) {
                            rl = tmpfs_readlink((tmpfs_fs_t *)m->fs_private, sub,
                                                sym_target, sizeof(sym_target));
                        } else if (m->backend == VFS_BACKEND_EXT4) {
                            rl = ext4fs_readlink((ext4fs_t *)m->fs_private, sub,
                                                 sym_target, sizeof(sym_target));
                        }
                        if (rl >= 0 && (size_t)rl < sizeof(sym_target))
                            sym_target[rl] = '\0';
                        else
                            sym_target[0] = '\0';
                    }
                    if (m->backend != VFS_BACKEND_PROCFS && m->backend != VFS_BACKEND_SYSFS &&
                        child_type != VFS_TYPE_SOCKET) {
                        child_id = dcache_insert(stack_ids[depth], comp, m->mount_id,
                                                 child_ino, child_type,
                                                 (child_type == VFS_TYPE_SYMLINK) ? sym_target : NULL);
                    }
                }
            }

            if (child_type == VFS_TYPE_SYMLINK && (has_more || follow_last)) {
                mount_unref(m);
                if (hop >= VFS_SYMLOOP_MAX)
                    return -ELOOP;
                if (!sym_target[0])
                    return -ENOENT;

                char rebuilt[VFS_PATH_MAX];
                if (sym_target[0] == '/') {
                    kstrncpy(rebuilt, sym_target, VFS_PATH_MAX);
                } else {
                    if (path_copy_join(rebuilt, VFS_PATH_MAX, cur_abs, sym_target) != 0)
                        return -ENAMETOOLONG;
                }
                if (has_more) {
                    int rlen = kstrlen(rebuilt);
                    if (rlen + 1 >= VFS_PATH_MAX)
                        return -ENAMETOOLONG;
                    if (rlen == 0 || rebuilt[rlen - 1] != '/') {
                        rebuilt[rlen++] = '/';
                        rebuilt[rlen] = '\0';
                    }
                    if (kstrncpy(rebuilt + rlen, cur_raw + i, VFS_PATH_MAX - rlen) != 0)
                        return -ENAMETOOLONG;
                }
                kstrncpy(cur_raw, rebuilt, VFS_PATH_MAX);
                restarted = 1;
                break;
            }

            if (has_more && child_type != VFS_TYPE_DIR) {
                mount_unref(m);
                return -ENOTDIR;
            }

            mount_unref(m);

            if (depth + 1 >= 32)
                return -ENAMETOOLONG;
            depth++;
            kstrncpy(cur_abs, next_abs, VFS_PATH_MAX);
            stack_len[depth] = kstrlen(cur_abs);
            stack_ids[depth] = child_id ? child_id : vfs_hash_str(cur_abs);
            leaf_exists = 1;
            leaf_type = child_type;
        }

        if (restarted)
            continue;

        vfs_mount_t *m = mount_lookup_ref(cur_abs);
        if (!m)
            return -ENOENT;
        if (mount_subpath(m, cur_abs, sub, VFS_PATH_MAX) != 0) {
            mount_unref(m);
            return -EINVAL;
        }
        kstrncpy(abs_out, cur_abs, VFS_PATH_MAX);
        *mount_out = m;
        kstrncpy(sub_out, sub, VFS_PATH_MAX);
        if (exists_out)
            *exists_out = leaf_exists;
        if (type_out)
            *type_out = leaf_type;
        return 0;
    }

    return -ELOOP;
}

static int append_root_mount_names(char names[][VFS_NAME_MAX], int count, int max)
{
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
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
    spin_unlock_irqrestore(&g_mounts_lock, flags);
    return count;
}

/* Public init */

static int ends_with(const char *str, const char *suffix)
{
    int slen = kstrlen(str);
    int flen = kstrlen(suffix);
    if (slen < flen)
        return 0;
    return kstreq(str + slen - flen, suffix);
}

static int find_lz4_initrd(const void *boot_info, const void **data_out,
                           size_t *size_out)
{
    int owns = 0;
    if (bootfs_read_file("/initrd.tar.lz4", data_out, size_out, &owns) == 0 &&
        *data_out && *size_out > 0) {
        return 0;
    }

    if (tsukasa_boot_info_is_valid(boot_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)boot_info;
        for (uint32_t i = 0; i < bi->module_count && bi->modules; i++) {
            const uint8_t *ptr = (const uint8_t *)(uintptr_t)bi->modules[i].address;
            size_t sz = (size_t)bi->modules[i].size;
            if (!ptr || sz < 4)
                continue;
            if (bi->modules[i].path && ends_with(bi->modules[i].path, "initrd.tar.lz4")) {
                *data_out = ptr;
                *size_out = sz;
                return 0;
            }
            if (ptr[0] == 0x04U && ptr[1] == 0x22U &&
                ptr[2] == 0x4DU && ptr[3] == 0x18U) {
                *data_out = ptr;
                *size_out = sz;
                return 0;
            }
        }
    } else {
        const struct multiboot_info *mb = (const struct multiboot_info *)boot_info;
        if (mb && (mb->flags & 8) && mb->mods_count > 0) {
            const struct multiboot_mod_list *mod =
                (const struct multiboot_mod_list *)(uintptr_t)mb->mods_addr;
            for (uint32_t i = 0; i < mb->mods_count; i++) {
                const uint8_t *ptr = (const uint8_t *)(uintptr_t)mod[i].mod_start;
                size_t sz = mod[i].mod_end - mod[i].mod_start;
                if (!ptr || sz < 4)
                    continue;
                if (ptr[0] == 0x04U && ptr[1] == 0x22U &&
                    ptr[2] == 0x4DU && ptr[3] == 0x18U) {
                    *data_out = ptr;
                    *size_out = sz;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static void unpack_lz4_initrd(const void *boot_info, tmpfs_fs_t *root_fs)
{
    const void *lz4_data = NULL;
    size_t lz4_size = 0;
    uint64_t content_size = 0;
    int has_size = 0;
    size_t max_blk = 0;
    int rc;

    if (!root_fs) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 no root tmpfs instance\n");
        return;
    }
    if (find_lz4_initrd(boot_info, &lz4_data, &lz4_size) != 0) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 module not found\n");
        return;
    }

    rc = lz4_frame_info(lz4_data, lz4_size, &content_size, &has_size, &max_blk);
    if (rc != LZ4_OK) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 invalid frame: %s\n",
                lz4_strerror(rc));
        return;
    }

    size_t tar_cap = 0;
    if (has_size && content_size > 0) {
        tar_cap = (size_t)content_size;
    } else if (!has_size) {
        tar_cap = lz4_size * 4U;
        if (tar_cap < 16U * 1024U * 1024U)
            tar_cap = 16U * 1024U * 1024U;
    } else {
        kprintf("[vfs] ERROR: initrd.tar.lz4 empty content size\n");
        return;
    }

    size_t pages = (tar_cap + PAGE_SIZE - 1U) / PAGE_SIZE;
    uintptr_t phys = pmm_alloc_pages(pages);
    if (!phys) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 out of memory (%u KiB)\n",
                (uint32_t)(tar_cap / 1024U));
        return;
    }

    void *tar_buf = (void *)vmm_phys_to_virt((uint64_t)phys);
    size_t tar_size = 0;
    rc = lz4_decompress(lz4_data, lz4_size, tar_buf, tar_cap, &tar_size);
    if (rc != LZ4_OK) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 decompression failed: %s\n",
                lz4_strerror(rc));
        pmm_free_pages(phys, pages);
        return;
    }

    int entries = tar_unpack_to_tmpfs(tar_buf, (uint64_t)tar_size, root_fs);
    pmm_free_pages(phys, pages);

    if (entries < 0) {
        kprintf("[vfs] ERROR: initrd.tar.lz4 tar unpack failed (rc=%d)\n",
                entries);
        return;
    }

    kprintf("[vfs] initrd.tar.lz4: ok (%d entries unpacked)\n", entries);
}

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int starts_with_ci(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;
    for (int i = 0; prefix[i]; i++) {
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z')
            b = (char)(b + 32);
        if (a != b)
            return 0;
    }
    return 1;
}

static int extract_cmdline_root(const char *cmdline, char *out, size_t cap)
{
    int found = 0;
    if (!out || cap == 0)
        return 0;
    out[0] = '\0';
    if (!cmdline)
        return 0;

    const char *p = cmdline;
    while (*p) {
        while (*p && is_ws(*p))
            p++;
        if (!*p)
            break;
        const char *tok = p;
        while (*p && !is_ws(*p))
            p++;
        size_t tlen = (size_t)(p - tok);
        if (tlen >= 5 && tok[0] == 'r' && tok[1] == 'o' &&
            tok[2] == 'o' && tok[3] == 't' && tok[4] == '=') {
            size_t vlen = tlen - 5;
            if (vlen >= cap)
                vlen = cap - 1;
            for (size_t i = 0; i < vlen; i++)
                out[i] = tok[5 + i];
            out[vlen] = '\0';
            found = 1;
        }
    }
    return found;
}

static block_dev_t *resolve_root_blockdev(const char *spec)
{
    if (!spec || !spec[0])
        return NULL;
    if (starts_with_ci(spec, "partuuid="))
        return blockdev_find_by_partuuid(spec + 9);
    if (spec[0] == '/' && spec[1] == 'd' && spec[2] == 'e' &&
        spec[3] == 'v' && spec[4] == '/')
        return blockdev_find(spec + 5);
    return blockdev_find(spec);
}

static block_dev_t *find_esp_on_disk(block_dev_t *root_bd)
{
    if (!root_bd)
        return NULL;
    block_dev_t *parent = root_bd->is_partition ? root_bd->parent : root_bd;
    if (!parent)
        return NULL;

    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *bd = blockdev_at(i);
        if (bd && bd != root_bd && bd->is_partition &&
            bd->parent == parent && bd->is_fat32 && bd->is_esp)
            return bd;
    }
    for (int i = 0; i < blockdev_count(); i++) {
        block_dev_t *bd = blockdev_at(i);
        if (bd && bd != root_bd && bd->is_partition &&
            bd->parent == parent && bd->is_fat32)
            return bd;
    }
    return NULL;
}

void vfs_init(const void *boot_info)
{
    const struct tsukasa_boot_info *bi = (const struct tsukasa_boot_info *)boot_info;
    const char *cmdline = (bi && bi->magic == TSUKASA_BOOT_INFO_MAGIC) ? bi->cmdline : NULL;
    const void *initrd_data = NULL;
    size_t initrd_size = 0;
    tmpfs_fs_t *root_fs;
    tmpfs_fs_t *tmp_fs;
    tmpfs_fs_t *run_fs;
    char root_spec[128];
    int has_root_arg;
    block_dev_t *esp_bd = NULL;

    mount_table_reset();
    file_pool_reset();
    dcache_init();
    flock_init();
    pagecache_init();
    procfs_init();
    sysfs_init();
    devfs_init();
    bootfs_init(boot_info);
    g_fat32_primary_vol = NULL;

    g_fat32_ok = 0;
    /* Storage bring-up: probe PCI DMA drivers first, keep legacy ATA PIO as fallback. */
    ahci_init();
    virtio_blk_init();
    nvme_init();
    if (blockdev_count() == 0) {
        if (!ata_init())
            kprintf("[ata] no drive detected\n");
    }
    diskmgr_scan_all();

    has_root_arg = extract_cmdline_root(cmdline, root_spec, sizeof(root_spec));
    if (has_root_arg) {
        block_dev_t *root_bd = resolve_root_blockdev(root_spec);
        if (!root_bd) {
            kprintf("[vfs] ERROR: failed to mount root=%s (device not found)\n",
                    root_spec);
            return;
        }
        int mrc = -EINVAL;
        ext4fs_t *efs = ext4fs_mount(root_bd, 0, &mrc);
        if (!efs) {
            kprintf("[vfs] ERROR: failed to mount root=%s on %s as ext4 (err=%d)\n",
                    root_spec, root_bd->name, mrc);
            return;
        }
        if (mount_register_ctx("/", VFS_BACKEND_EXT4, 0, "ext4", efs) != 0) {
            (void)ext4fs_umount(efs);
            kprintf("[vfs] ERROR: failed to mount root=%s at / (register failed)\n",
                    root_spec);
            return;
        }
        kprintf("[vfs] root=%s mounted ext4 rw (%s)\n", root_spec, root_bd->name);

        (void)ext4fs_mkdir(efs, "/tmp", 01777U);
        (void)ext4fs_mkdir(efs, "/var", 0755U);
        (void)ext4fs_mkdir(efs, "/var/run", 0755U);
        (void)ext4fs_mkdir(efs, "/dev", 0755U);
        (void)ext4fs_mkdir(efs, "/proc", 0555U);
        (void)ext4fs_mkdir(efs, "/sys", 0555U);
        (void)ext4fs_mkdir(efs, "/boot", 0755U);
        (void)ext4fs_mkdir(efs, "/mnt", 0755U);

        esp_bd = find_esp_on_disk(root_bd);
        if (esp_bd) {
            fat32_volume_t *esp_vol = fat32_vol_mount(esp_bd);
            if (esp_vol && mount_register_ctx("/boot", VFS_BACKEND_FAT32VOL, 0, "fat32", esp_vol) == 0) {
                kprintf("[vfs] ESP /boot mounted rw (%s)\n", esp_bd->name);
            } else {
                if (esp_vol)
                    (void)fat32_vol_umount(esp_vol);
                kprintf("[vfs] WARN: failed to mount ESP %s at /boot\n", esp_bd->name);
                esp_bd = NULL;
            }
        }
    }

    for (int di = 0; di < blockdev_count(); di++) {
        block_dev_t *bd = blockdev_at(di);
        fat32_volume_t *vol;
        char mnt[VFS_PATH_MAX];

        if (!bd || !bd->is_partition || !bd->is_fat32 || bd == esp_bd)
            continue;
        vol = fat32_vol_mount(bd);
        if (!vol) {
            kprintf("[vfs] %s: FAT32 probe ok but volume mount failed\n",
                    bd->name);
            continue;
        }
        mnt[0] = '/';
        mnt[1] = 'm';
        mnt[2] = 'n';
        mnt[3] = 't';
        mnt[4] = '/';
        kstrncpy(mnt + 5, bd->name, VFS_PATH_MAX - 5);
        if (mount_register_ctx(mnt, VFS_BACKEND_FAT32VOL, 0, "fat32", vol) == 0)
            kprintf("[vfs] FAT32 %s: mounted rw (%u MiB)\n",
                    mnt, (uint32_t)(bd->sector_count / 2048u));
    }
    if (blockdev_first_raw_fat32()) {
        if (fat32_init() == 0) {
            g_fat32_ok = 1;
            g_fat32_primary_vol = fat32_primary_vol();
        }
        kprintf("[vfs] FAT32 /disk: %s\n", g_fat32_ok ? "ok" : "not found");
    }

    if (!has_root_arg) {
        initrd_init_from_multiboot(boot_info);
        g_initrd_ok = (initrd_lookup("/", &initrd_data, &initrd_size) == 0) ? 1 : 0;

        root_fs = tmpfs_create();
        if (root_fs) {
            mount_register_ctx("/", VFS_BACKEND_TMPFS, 0, "tmpfs", root_fs);
            unpack_lz4_initrd(boot_info, root_fs);
            tmpfs_mkdir_p(root_fs, "/tmp", 01777U);
            tmpfs_mkdir_p(root_fs, "/var/run", 0755U);
            tmpfs_mkdir_p(root_fs, "/dev", 0755U);
            tmpfs_mkdir_p(root_fs, "/proc", 0555U);
            tmpfs_mkdir_p(root_fs, "/sys", 0555U);
            tmpfs_mkdir_p(root_fs, "/boot", 0755U);
            tmpfs_mkdir_p(root_fs, "/mnt", 0755U);
        }
    }

    tmp_fs = tmpfs_create();
    if (tmp_fs)
        mount_register_ctx("/tmp", VFS_BACKEND_TMPFS, 0, "tmpfs", tmp_fs);

    run_fs = tmpfs_create();
    if (run_fs)
        mount_register_ctx("/var/run", VFS_BACKEND_TMPFS, 0, "tmpfs", run_fs);

    if (g_fat32_ok)
        mount_register("/disk", VFS_BACKEND_FAT32, 0, "fat32");
    mount_register("/proc", VFS_BACKEND_PROCFS, 1, "procfs");
    mount_register("/sys", VFS_BACKEND_SYSFS, 1, "sysfs");
    mount_register("/dev", VFS_BACKEND_DEVFS, 0, "devfs");
    if (!esp_bd && bootfs_module_count() > 0)
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

int vfs_is_root_mounted(void)
{
    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    int mounted = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && kstreq(g_mounts[i].path, "/")) {
            mounted = 1;
            break;
        }
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);
    return mounted;
}

void vfs_sync_all(void)
{
    vfs_pc_sync_all();

    vfs_backend_t backends[VFS_MAX_MOUNTS];
    void *privs[VFS_MAX_MOUNTS];
    int count = 0;

    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used || g_mounts[i].read_only)
            continue;
        backends[count] = g_mounts[i].backend;
        privs[count] = g_mounts[i].fs_private;
        count++;
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);

    for (int i = 0; i < count; i++) {
        if (backends[i] == VFS_BACKEND_EXT4 && privs[i]) {
            (void)ext4fs_sync((ext4fs_t *)privs[i]);
        } else if (backends[i] == VFS_BACKEND_FAT32VOL && privs[i]) {
            (void)fat32_vol_sync((fat32_volume_t *)privs[i]);
        } else if (backends[i] == VFS_BACKEND_FAT32) {
            fat32_volume_t *pv = privs[i] ? (fat32_volume_t *)privs[i] : fat32_primary_vol();
            if (pv)
                (void)fat32_vol_sync(pv);
        }
    }
    (void)pagecache_sync_all();
}

void vfs_shutdown(void)
{
    vfs_sync_all();

    for (;;) {
        char path[VFS_PATH_MAX];
        int found = 0;
        unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
        for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
            if (g_mounts[i].used && !kstreq(g_mounts[i].path, "/")) {
                kstrncpy(path, g_mounts[i].path, VFS_PATH_MAX);
                found = 1;
                break;
            }
        }
        spin_unlock_irqrestore(&g_mounts_lock, flags);
        if (!found)
            break;
        if (vfs_umount2(path, MNT_DETACH) != 0)
            break;
    }

    unsigned long flags = spin_lock_irqsave(&g_mounts_lock);
    vfs_backend_t root_backend = VFS_BACKEND_NONE;
    void *root_priv = NULL;
    uint64_t root_mid = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && kstreq(g_mounts[i].path, "/")) {
            root_mid = g_mounts[i].mount_id;
            root_backend = g_mounts[i].backend;
            root_priv = g_mounts[i].fs_private;
            g_mounts[i].used = 0;
            g_mounts[i].mount_id = 0;
            g_mounts[i].open_count = 0;
            g_mounts[i].active_ops = 0;
            g_mounts[i].path[0] = '\0';
            g_mounts[i].path_len = 0;
            g_mounts[i].backend = VFS_BACKEND_NONE;
            g_mounts[i].fs_private = NULL;
            break;
        }
    }
    spin_unlock_irqrestore(&g_mounts_lock, flags);

    if (root_mid) {
        vfs_pc_unmount(root_mid);
        dcache_invalidate_mount(root_mid);
    }
    if (root_backend == VFS_BACKEND_EXT4 && root_priv)
        (void)ext4fs_umount((ext4fs_t *)root_priv);
    else if (root_backend == VFS_BACKEND_FAT32VOL && root_priv)
        (void)fat32_vol_umount((fat32_volume_t *)root_priv);
    else if (root_backend == VFS_BACKEND_TMPFS && root_priv)
        tmpfs_destroy((tmpfs_fs_t *)root_priv);
    (void)pagecache_sync_all();
}

int vfs_sync_fd(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;

    int rc = 0;
    vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
    if (pc)
        rc = pagecache_sync_obj(pc->obj_id);
    if (f->backend == VFS_BACKEND_EXT4 && f->fs_ctx) {
        int frc = ext4fs_sync((ext4fs_t *)f->fs_ctx);
        if (rc == 0)
            rc = frc;
    } else if ((f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) &&
               f->u.fat32.vol) {
        int frc = fat32_vol_sync(f->u.fat32.vol);
        if (rc == 0)
            rc = frc;
    } else if (f->backend == VFS_BACKEND_DEVFS && f->u.device.priv &&
               f->u.device.ops && f->u.device.ops->ioctl) {
        /* Block devices sync on every write; nothing extra needed unless sync exists. */
    }
    file_release(f);
    return rc;
}

/* Open/create/close */

static int open_tmpfs_file(const vfs_mount_t *m, vfs_file_t *f,
                           const char *subpath, int flags, uint32_t create_mode)
{
    tmpfs_fs_t *fs = (tmpfs_fs_t *)m->fs_private;
    tmpfs_inode_t *ino;

    if (!fs || !subpath)
        return -EINVAL;

    ino = tmpfs_lookup(fs, subpath, 1);
    if (ino) {
        if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
            tmpfs_inode_unref(ino);
            return -EEXIST;
        }
        int itype = tmpfs_inode_type(ino);
        if (itype == TMPFS_TYPE_DIR) {
            if ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC)) {
                tmpfs_inode_unref(ino);
                return -EISDIR;
            }
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
        } else if (itype != TMPFS_TYPE_FILE) {
            tmpfs_inode_unref(ino);
            return -EINVAL;
        } else {
            if (flags & VFS_O_DIRECTORY) {
                tmpfs_inode_unref(ino);
                return -ENOTDIR;
            }
            if (flags & VFS_O_TRUNC)
                tmpfs_truncate(ino, 0);
        }
    } else if (flags & VFS_O_CREAT) {
        if (flags & VFS_O_DIRECTORY)
            return -ENOTDIR;
        int rc = tmpfs_create_file(fs, subpath, create_mode, &ino);
        if (rc != 0 || !ino)
            return (rc < 0) ? rc : -ENOENT;
    } else {
        return -ENOENT;
    }

    f->backend = VFS_BACKEND_TMPFS;
    f->fs_ctx = fs;
    f->u.tmpfs.inode = ino;
    f->inode_dev = m->mount_id;
    f->inode_ino = tmpfs_inode_ino(ino);
    f->pos = (!f->is_dir && (flags & VFS_O_APPEND)) ? tmpfs_inode_size(ino) : 0;
    return 0;
}

static int open_fat32_file(const vfs_mount_t *m, vfs_file_t *f, vfs_backend_t backend,
                           void *fs_ctx, const char *subpath, int flags)
{
    fat32_volume_t *vol = (backend == VFS_BACKEND_FAT32)
                              ? fat32_primary_vol()
                              : (fat32_volume_t *)fs_ctx;
    uint64_t ino_id = vfs_hash_str(subpath);
    size_t file_size = 0;

    if (!vol && backend == VFS_BACKEND_FAT32 && blockdev_first_raw_fat32()) {
        vol = fat32_vol_mount(blockdev_first_raw_fat32());
        g_fat32_primary_vol = vol;
    }
    if (!vol)
        return -ENODEV;

    if (kstreq(subpath, "/")) {
        if ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC))
            return -EISDIR;
        f->backend = backend;
        f->fs_ctx = vol;
        f->u.fat32.vol = vol;
        f->is_dir = 1;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = 1ULL;
        return 0;
    }

    fat32_dirent_t de;
    if (fat32_vol_stat(vol, subpath, &de) != 0) {
        if (flags & VFS_O_CREAT) {
            if (flags & VFS_O_DIRECTORY)
                return -ENOTDIR;
            int crc = fat32_vol_create_file(vol, subpath);
            if (crc != 0)
                return crc;
            file_size = 0;
        } else {
            return -ENOENT;
        }
    } else {
        if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL))
            return -EEXIST;
        if (de.is_dir) {
            if ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC))
                return -EISDIR;
            f->backend = backend;
            f->fs_ctx = vol;
            f->u.fat32.vol = vol;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = de.first_cluster ? (uint64_t)de.first_cluster : ino_id;
            return 0;
        }
        if (flags & VFS_O_DIRECTORY)
            return -ENOTDIR;
        if (de.first_cluster)
            ino_id = (uint64_t)de.first_cluster;
        if (flags & VFS_O_TRUNC) {
            int trc = fat32_vol_truncate(vol, subpath, 0);
            if (trc != 0)
                return trc;
            file_size = 0;
            f->dirty = 1;
        } else {
            file_size = de.size;
        }
    }

    f->backend = backend;
    f->fs_ctx = vol;
    f->u.fat32.vol = vol;
    f->u.fat32.hint_clus_idx = 0;
    f->u.fat32.hint_clus = 0;
    f->u.fat32.hint_gen = 0;
    f->inode_dev = m->mount_id;
    f->inode_ino = ino_id;
    f->pos = (flags & VFS_O_APPEND) ? file_size : 0;
    return 0;
}

static int open_ext4_file(const vfs_mount_t *m, vfs_file_t *f,
                          const char *subpath, int flags, uint32_t create_mode)
{
    ext4fs_t *efs = (ext4fs_t *)m->fs_private;
    void *handle = NULL;
    int is_dir = 0;
    uint64_t ino = 0;
    size_t sz = 0;

    if (!efs || !subpath)
        return -EINVAL;

    int rc = ext4fs_open(efs, subpath, flags, create_mode, &handle, &is_dir, &ino, &sz);
    if (rc != 0)
        return rc;

    f->backend = VFS_BACKEND_EXT4;
    f->fs_ctx = efs;
    f->u.ext4.handle = handle;
    f->is_dir = is_dir;
    if (is_dir)
        f->mode = VFS_MODE_READ;
    f->inode_dev = m->mount_id;
    f->inode_ino = ino ? ino : vfs_hash_str(subpath);
    f->pos = (!is_dir && (flags & VFS_O_APPEND)) ? sz : 0;
    if (!is_dir && (flags & (VFS_O_CREAT | VFS_O_TRUNC)))
        f->dirty = 1;
    return 0;
}

static int vfs_install_fd(process_t *proc, vfs_file_t *f)
{
    void **tbl = fd_table_for_process(proc);
    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        if (!tbl[i]) {
            tbl[i] = f;
            spin_unlock_irqrestore(&g_fd_lock, flags);
            return i;
        }
    }
    spin_unlock_irqrestore(&g_fd_lock, flags);
    return -EMFILE;
}

int vfs_open_flags_mode(const char *path, int flags, int mode)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t leaf_type = VFS_TYPE_UNKNOWN;
    int follow_last = (flags & VFS_O_NOFOLLOW) ? 0 : 1;
    int fd;
    vfs_file_t *f;
    int rc;

    if (!path || !path[0])
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, follow_last, abs, &m, sub, &exists, &leaf_type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;

    if ((flags & VFS_O_NOFOLLOW) && exists && leaf_type == VFS_TYPE_SYMLINK) {
        mount_unref(m);
        return -ELOOP;
    }

    if (m->read_only && ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC))) {
        mount_unref(m);
        return -EROFS;
    }

    if (exists && leaf_type == VFS_TYPE_DIR &&
        ((flags & VFS_O_WRONLY) || (flags & VFS_O_CREAT) || (flags & VFS_O_TRUNC))) {
        mount_unref(m);
        return -EISDIR;
    }

    if ((flags & VFS_O_DIRECTORY) && exists && leaf_type != VFS_TYPE_DIR) {
        mount_unref(m);
        return -ENOTDIR;
    }

    f = file_alloc();
    if (!f) {
        mount_unref(m);
        return -ENOMEM;
    }
    f->flags = flags;
    f->mode = flags_to_mode(flags);
    f->mount_id = m->mount_id;
    mount_note_open_delta(m->mount_id, 1);
    kstrncpy(f->path, sub, VFS_PATH_MAX);
    kstrncpy(f->abs_path, abs, VFS_PATH_MAX);

    uint32_t um = vfs_current_umask(proc);
    uint32_t create_mode = ((uint32_t)(mode ? mode : 0666) & ~um) & 07777U;

    switch (m->backend) {
    case VFS_BACKEND_TMPFS:
        rc = open_tmpfs_file(m, f, sub, flags, create_mode);
        if (rc != 0) {
            mount_unref(m);
            file_release(f);
            return rc;
        }
        break;
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL:
        rc = open_fat32_file(m, f, m->backend, m->fs_private, sub, flags);
        if (rc != 0) {
            mount_unref(m);
            file_release(f);
            return rc;
        }
        break;
    case VFS_BACKEND_EXT4:
        rc = open_ext4_file(m, f, sub, flags, create_mode);
        if (rc != 0) {
            mount_unref(m);
            file_release(f);
            return rc;
        }
        break;
    case VFS_BACKEND_INITRD:
    {
        if (kstreq(sub, "/")) {
            f->backend = VFS_BACKEND_INITRD;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = 1ULL;
            break;
        }
        const void *data = NULL;
        size_t sz = 0;
        if (initrd_lookup(sub, &data, &sz) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        f->backend = VFS_BACKEND_INITRD;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.regular.buf = (uint8_t *)(uintptr_t)data;
        f->u.regular.size = sz;
        f->u.regular.capacity = sz;
        f->u.regular.owns_buf = 0;
        break;
    }
    case VFS_BACKEND_PROCFS:
    {
        vfs_stat_t pst;
        if (procfs_stat(sub, &pst) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        if (pst.type == VFS_TYPE_DIR) {
            f->backend = VFS_BACKEND_PROCFS;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = vfs_hash_str(sub);
            break;
        }
        uint8_t *pbuf = NULL;
        size_t psz = 0;
        if (procfs_read_file(sub, &pbuf, &psz) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        f->backend = VFS_BACKEND_PROCFS;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.regular.buf = pbuf;
        f->u.regular.size = psz;
        f->u.regular.capacity = psz;
        f->u.regular.owns_buf = 1;
        break;
    }
    case VFS_BACKEND_SYSFS:
    {
        vfs_stat_t sst;
        if (sysfs_stat(sub, &sst) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        if (sst.type == VFS_TYPE_DIR) {
            f->backend = VFS_BACKEND_SYSFS;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = vfs_hash_str(sub);
            break;
        }
        uint8_t *sbuf = NULL;
        size_t ssz = 0;
        if (sysfs_read_file(sub, &sbuf, &ssz) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        f->backend = VFS_BACKEND_SYSFS;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.regular.buf = sbuf;
        f->u.regular.size = ssz;
        f->u.regular.capacity = ssz;
        f->u.regular.owns_buf = 1;
        break;
    }
    case VFS_BACKEND_BOOTFS:
    {
        vfs_stat_t bst;
        if (bootfs_stat(sub, &bst) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        if (bst.type == VFS_TYPE_DIR) {
            f->backend = VFS_BACKEND_BOOTFS;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = vfs_hash_str(sub);
            break;
        }
        const void *bbuf = NULL;
        size_t bsz = 0;
        int owns = 0;
        if (bootfs_read_file(sub, &bbuf, &bsz, &owns) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        f->backend = VFS_BACKEND_BOOTFS;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.regular.buf = (uint8_t *)(uintptr_t)bbuf;
        f->u.regular.size = bsz;
        f->u.regular.capacity = bsz;
        f->u.regular.owns_buf = owns;
        break;
    }
    case VFS_BACKEND_DEVFS:
    {
        vfs_stat_t dst;
        if (devfs_stat(sub, &dst) == 0 && dst.type == VFS_TYPE_DIR) {
            f->backend = VFS_BACKEND_DEVFS;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = vfs_hash_str(sub);
            break;
        }
        const devfs_ops_t *ops = NULL;
        void *priv = NULL;
        uint32_t type = VFS_TYPE_CHAR;
        if (devfs_lookup(sub, &ops, &priv, &type) != 0) {
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        f->backend = VFS_BACKEND_DEVFS;
        f->mode = flags_to_mode(flags);
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.device.ops = ops;
        f->u.device.priv = priv;
        f->pos = 0;
        if (ops && ops->open) {
            if (ops->open(priv, flags) != 0) {
                mount_unref(m);
                file_release(f);
                return -EIO;
            }
        }
        break;
    }
    case VFS_BACKEND_TAR:
    {
        if (kstreq(sub, "/")) {
            f->backend = VFS_BACKEND_TAR;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = 1ULL;
            break;
        }
        const tar_inode_t *ino = tar_lookup((tar_fs_t *)m->fs_private, sub);
        if (!ino) {
            vfs_stat_t tst;
            if (tar_stat((tar_fs_t *)m->fs_private, sub, &tst) == 0 && tst.type == VFS_TYPE_DIR) {
                f->backend = VFS_BACKEND_TAR;
                f->is_dir = 1;
                f->mode = VFS_MODE_READ;
                f->inode_dev = m->mount_id;
                f->inode_ino = vfs_hash_str(sub);
                break;
            }
            mount_unref(m);
            file_release(f);
            return -ENOENT;
        }
        if (ino->is_dir) {
            f->backend = VFS_BACKEND_TAR;
            f->is_dir = 1;
            f->mode = VFS_MODE_READ;
            f->inode_dev = m->mount_id;
            f->inode_ino = vfs_hash_str(sub);
            break;
        }
        f->backend = VFS_BACKEND_TAR;
        f->mode = VFS_MODE_READ;
        f->inode_dev = m->mount_id;
        f->inode_ino = vfs_hash_str(sub);
        f->u.regular.buf = (uint8_t *)(uintptr_t)ino->data;
        f->u.regular.size = (size_t)ino->size;
        f->u.regular.capacity = (size_t)ino->size;
        f->u.regular.owns_buf = 0;
        break;
    }
    default:
        mount_unref(m);
        file_release(f);
        return -ENOENT;
    }

    mount_unref(m);

    if ((flags & VFS_O_DIRECTORY) && !f->is_dir) {
        file_release(f);
        return -ENOTDIR;
    }

    fd = vfs_install_fd(proc, f);
    if (fd < 0) {
        file_release(f);
        return fd;
    }
    return fd;
}

int vfs_open_flags(const char *path, int flags)
{
    return vfs_open_flags_mode(path, flags, 0666);
}

int vfs_open(const char *path)
{
    return vfs_open_flags_mode(path, VFS_O_RDONLY, 0);
}

int vfs_create(const char *path)
{
    return vfs_open_flags_mode(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0666);
}

int vfs_unlink(const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 0, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        rc = tmpfs_unlink((tmpfs_fs_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_unlink((ext4fs_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_FAT32VOL) {
        rc = fat32_vol_unlink((fat32_volume_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_FAT32) {
        rc = fat32_vol_unlink(fat32_primary_vol(), sub);
    } else {
        mount_unref(m);
        return -EPERM;
    }
    mount_unref(m);
    if (rc == 0)
        dcache_invalidate_path(abs);
    return rc;
}

int vfs_rmdir(const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 0, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        rc = tmpfs_rmdir((tmpfs_fs_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_rmdir((ext4fs_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_FAT32VOL) {
        rc = fat32_vol_rmdir((fat32_volume_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_FAT32) {
        rc = fat32_vol_rmdir(fat32_primary_vol(), sub);
    } else {
        mount_unref(m);
        return -EPERM;
    }
    mount_unref(m);
    if (rc == 0)
        dcache_invalidate_path(abs);
    return rc;
}

int vfs_mkdir(const char *path, int mode)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 0, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (exists) {
        mount_unref(m);
        return -EEXIST;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (unix_find_listener(abs) != NULL) {
        mount_unref(m);
        return -EEXIST;
    }
    uint32_t um = vfs_current_umask(proc);
    uint32_t eff_mode = ((uint32_t)(mode ? mode : 0777) & ~um) & 07777U;
    if (m->backend == VFS_BACKEND_TMPFS) {
        rc = tmpfs_mkdir((tmpfs_fs_t *)m->fs_private, sub, eff_mode);
    } else if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_mkdir((ext4fs_t *)m->fs_private, sub, eff_mode);
    } else if (m->backend == VFS_BACKEND_FAT32VOL) {
        rc = fat32_vol_mkdir((fat32_volume_t *)m->fs_private, sub);
    } else if (m->backend == VFS_BACKEND_FAT32) {
        rc = fat32_vol_mkdir(fat32_primary_vol(), sub);
    } else {
        mount_unref(m);
        return -EPERM;
    }
    mount_unref(m);
    if (rc == 0)
        dcache_invalidate_path(abs);
    return rc;
}

int vfs_rename(const char *old_path, const char *new_path)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m1 = NULL, *m2 = NULL;
    char abs1[VFS_PATH_MAX], abs2[VFS_PATH_MAX];
    char sub1[VFS_PATH_MAX], sub2[VFS_PATH_MAX];
    int ex1 = 0, ex2 = 0;
    uint32_t t1 = 0, t2 = 0;
    int rc;

    if (!old_path || !new_path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, old_path, 0, abs1, &m1, sub1, &ex1, &t1);
    if (rc != 0 || !m1)
        return (rc < 0) ? rc : -ENOENT;
    if (!ex1) {
        mount_unref(m1);
        return -ENOENT;
    }
    rc = vfs_resolve_path_ex(proc, new_path, 0, abs2, &m2, sub2, &ex2, &t2);
    if (rc != 0 || !m2) {
        mount_unref(m1);
        return (rc < 0) ? rc : -ENOENT;
    }
    if (m1 != m2) {
        mount_unref(m1);
        mount_unref(m2);
        return -EXDEV;
    }
    if (m1->read_only) {
        mount_unref(m1);
        mount_unref(m2);
        return -EROFS;
    }

    if (m1->backend == VFS_BACKEND_TMPFS)
        rc = tmpfs_rename((tmpfs_fs_t *)m1->fs_private, sub1, sub2);
    else if (m1->backend == VFS_BACKEND_EXT4)
        rc = ext4fs_rename((ext4fs_t *)m1->fs_private, sub1, sub2);
    else if (m1->backend == VFS_BACKEND_FAT32VOL)
        rc = fat32_vol_rename((fat32_volume_t *)m1->fs_private, sub1, sub2);
    else if (m1->backend == VFS_BACKEND_FAT32)
        rc = fat32_vol_rename(fat32_primary_vol(), sub1, sub2);
    else
        rc = -EPERM;

    mount_unref(m1);
    mount_unref(m2);
    if (rc == 0) {
        dcache_invalidate_path(abs1);
        dcache_invalidate_path(abs2);
    }
    return rc;
}

int vfs_symlink(const char *target, const char *linkpath)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!target || !target[0] || !linkpath || !linkpath[0])
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, linkpath, 0, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (exists) {
        mount_unref(m);
        return -EEXIST;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        rc = tmpfs_symlink((tmpfs_fs_t *)m->fs_private, sub, target, 0777U);
    } else if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_symlink((ext4fs_t *)m->fs_private, target, sub);
    } else {
        mount_unref(m);
        return -EPERM;
    }
    mount_unref(m);
    if (rc == 0)
        dcache_invalidate_path(abs);
    return rc;
}

int vfs_readlink(const char *path, char *buf, size_t cap)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path || !buf || cap == 0)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 0, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (type != VFS_TYPE_SYMLINK) {
        mount_unref(m);
        return -EINVAL;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        rc = tmpfs_readlink((tmpfs_fs_t *)m->fs_private, sub, buf, cap);
    } else if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_readlink((ext4fs_t *)m->fs_private, sub, buf, cap);
    } else {
        mount_unref(m);
        return -EINVAL;
    }
    mount_unref(m);
    return rc;
}

int vfs_truncate(const char *path, size_t length)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 1, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (type == VFS_TYPE_DIR) {
        mount_unref(m);
        return -EISDIR;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        tmpfs_inode_t *ino = tmpfs_lookup((tmpfs_fs_t *)m->fs_private, sub, 1);
        mount_unref(m);
        if (!ino)
            return -ENOENT;
        rc = tmpfs_truncate(ino, length);
        tmpfs_inode_unref(ino);
        return rc;
    }
    if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_truncate((ext4fs_t *)m->fs_private, sub, length);
        mount_unref(m);
        return rc;
    }
    if (m->backend == VFS_BACKEND_FAT32VOL) {
        rc = fat32_vol_truncate((fat32_volume_t *)m->fs_private, sub, length);
        mount_unref(m);
        return rc;
    }
    if (m->backend == VFS_BACKEND_FAT32) {
        rc = fat32_vol_truncate(fat32_primary_vol(), sub, length);
        mount_unref(m);
        return rc;
    }
    mount_unref(m);
    return -EINVAL;
}

int vfs_ftruncate(int fd, size_t length)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;
    if (f->is_dir) {
        file_release(f);
        return -EISDIR;
    }
    if (!(f->mode & VFS_MODE_WRITE)) {
        file_release(f);
        return -EINVAL;
    }
    int rc = -EINVAL;
    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode) {
        rc = tmpfs_truncate(f->u.tmpfs.inode, length);
    } else if (f->backend == VFS_BACKEND_EXT4 && f->u.ext4.handle) {
        rc = ext4fs_ftruncate((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, length);
        if (rc == 0)
            f->dirty = 1;
    } else if ((f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) &&
               f->u.fat32.vol) {
        rc = fat32_vol_truncate(f->u.fat32.vol, f->path, length);
        if (rc == 0)
            f->dirty = 1;
    }
    file_release(f);
    return rc;
}

int vfs_chmod(const char *path, uint32_t mode)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 1, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        tmpfs_inode_t *ino = tmpfs_lookup((tmpfs_fs_t *)m->fs_private, sub, 1);
        mount_unref(m);
        if (!ino)
            return -ENOENT;
        rc = tmpfs_chmod_inode(ino, mode);
        tmpfs_inode_unref(ino);
        return rc;
    }
    if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_chmod((ext4fs_t *)m->fs_private, sub, mode);
        mount_unref(m);
        return rc;
    }
    mount_unref(m);
    return 0;
}

int vfs_fchmod(int fd, uint32_t mode)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;
    int rc = 0;
    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode)
        rc = tmpfs_chmod_inode(f->u.tmpfs.inode, mode);
    else if (f->backend == VFS_BACKEND_EXT4 && f->fs_ctx)
        rc = ext4fs_chmod((ext4fs_t *)f->fs_ctx, f->path, mode);
    file_release(f);
    return rc;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 1, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    if (m->read_only) {
        mount_unref(m);
        return -EROFS;
    }
    if (m->backend == VFS_BACKEND_TMPFS) {
        tmpfs_inode_t *ino = tmpfs_lookup((tmpfs_fs_t *)m->fs_private, sub, 1);
        mount_unref(m);
        if (!ino)
            return -ENOENT;
        rc = tmpfs_chown_inode(ino, uid, gid);
        tmpfs_inode_unref(ino);
        return rc;
    }
    if (m->backend == VFS_BACKEND_EXT4) {
        rc = ext4fs_chown((ext4fs_t *)m->fs_private, sub, uid, gid);
        mount_unref(m);
        return rc;
    }
    mount_unref(m);
    return 0;
}

int vfs_fchown(int fd, uint32_t uid, uint32_t gid)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;
    int rc = 0;
    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode)
        rc = tmpfs_chown_inode(f->u.tmpfs.inode, uid, gid);
    else if (f->backend == VFS_BACKEND_EXT4 && f->fs_ctx)
        rc = ext4fs_chown((ext4fs_t *)f->fs_ctx, f->path, uid, gid);
    file_release(f);
    return rc;
}

int vfs_access(const char *path, int mode)
{
    vfs_stat_t st;
    int rc;
    if (!path)
        return -EINVAL;
    if (mode & ~(R_OK | W_OK | X_OK))
        return -EINVAL;
    rc = vfs_stat(path, &st);
    if (rc != 0)
        return rc;
    if (mode == F_OK)
        return 0;

    process_t *proc = vfs_current_process();
    uint32_t uid = proc ? proc->uid : 0U;
    if ((mode & W_OK) && !(st.mode & VFS_MODE_WRITE))
        return -EROFS;
    if (uid == 0U) {
        if ((mode & X_OK) && st.type != VFS_TYPE_DIR && (st.st_mode & 0111U) == 0U)
            return -EACCES;
        return 0;
    }
    if ((mode & R_OK) && (st.st_mode & 0444U) == 0U)
        return -EACCES;
    if ((mode & W_OK) && (st.st_mode & 0222U) == 0U)
        return -EACCES;
    if ((mode & X_OK) && (st.st_mode & 0111U) == 0U)
        return -EACCES;
    return 0;
}

uint32_t vfs_umask(uint32_t mask)
{
    process_t *proc = vfs_current_process();
    uint32_t prev;
    uint32_t next = mask & 0777U;
    if (proc) {
        prev = proc->umask & 0777U;
        proc->umask = next;
    } else {
        prev = g_kernel_umask & 0777U;
        g_kernel_umask = next;
    }
    return prev;
}

int vfs_flock(int fd, int operation)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;

    int nonblock = (operation & LOCK_NB) != 0;
    int base = operation & ~LOCK_NB;
    if (base != LOCK_SH && base != LOCK_EX && base != LOCK_UN) {
        file_release(f);
        return -EINVAL;
    }

    uint64_t dev_id = f->inode_dev;
    uint64_t ino_id = f->inode_ino;
    if (dev_id == 0 && ino_id == 0) {
        dev_id = (uint64_t)(uintptr_t)f;
        ino_id = 1ULL;
    }

    if (base == LOCK_UN) {
        vfs_flock_release_file(f);
        file_release(f);
        return 0;
    }

    for (;;) {
        unsigned long flags = spin_lock_irqsave(&g_flock_lock);
        vfs_flock_inode_t *fi = flock_find_inode_locked(dev_id, ino_id, NULL);
        if (!fi) {
            fi = (vfs_flock_inode_t *)kmalloc(sizeof(vfs_flock_inode_t));
            if (!fi) {
                spin_unlock_irqrestore(&g_flock_lock, flags);
                file_release(f);
                return -ENOMEM;
            }
            fi->dev_id = dev_id;
            fi->ino_id = ino_id;
            fi->holders = NULL;
            wait_queue_init(&fi->waitq);
            fi->waiters = 0;
            size_t b = flock_hash(dev_id, ino_id);
            fi->next = g_flock_buckets[b];
            g_flock_buckets[b] = fi;
        }

        vfs_flock_holder_t *self_h = NULL;
        int conflict = 0;
        for (vfs_flock_holder_t *h = fi->holders; h; h = h->next) {
            if (h->owner_file == f) {
                self_h = h;
                continue;
            }
            if (base == LOCK_EX || h->type == LOCK_EX)
                conflict = 1;
        }

        if (!conflict) {
            if (self_h) {
                int was_ex = (self_h->type == LOCK_EX);
                self_h->type = base;
                if (was_ex && base == LOCK_SH)
                    wait_queue_wake_all(&fi->waitq);
            } else {
                vfs_flock_holder_t *nh = (vfs_flock_holder_t *)kmalloc(sizeof(vfs_flock_holder_t));
                if (!nh) {
                    flock_maybe_free_inode_locked(fi);
                    spin_unlock_irqrestore(&g_flock_lock, flags);
                    file_release(f);
                    return -ENOMEM;
                }
                nh->owner_file = f;
                nh->type = base;
                nh->next = fi->holders;
                fi->holders = nh;
            }
            spin_unlock_irqrestore(&g_flock_lock, flags);
            file_release(f);
            return 0;
        }

        if (nonblock) {
            flock_maybe_free_inode_locked(fi);
            spin_unlock_irqrestore(&g_flock_lock, flags);
            file_release(f);
            return -EAGAIN;
        }

        if (!proc) {
            flock_maybe_free_inode_locked(fi);
            spin_unlock_irqrestore(&g_flock_lock, flags);
            file_release(f);
            return -EAGAIN;
        }

        wait_queue_entry_t wqe;
        wqe.proc = proc;
        wqe.next = NULL;
        fi->waiters++;
        wait_queue_prepare_to_wait(&fi->waitq, &wqe);
        spin_unlock_irqrestore(&g_flock_lock, flags);

        process_yield();
        wait_queue_finish_wait(&fi->waitq, &wqe);

        flags = spin_lock_irqsave(&g_flock_lock);
        if (fi->waiters > 0)
            fi->waiters--;
        flock_maybe_free_inode_locked(fi);
        spin_unlock_irqrestore(&g_flock_lock, flags);
    }
}

int vfs_mount(const char *source, const char *target, const char *fstype, unsigned long flags)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *parent_m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int ro = (flags & MS_RDONLY) ? 1 : 0;
    int rc;

    (void)source;
    if (!target || !fstype)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, target, 1, abs, &parent_m, sub, &exists, &type);
    if (rc != 0 || !parent_m)
        return (rc < 0) ? rc : -ENOENT;
    mount_unref(parent_m);
    if (!exists)
        return -ENOENT;
    if (type != VFS_TYPE_DIR)
        return -ENOTDIR;

    if (kstreq(fstype, "tmpfs")) {
        tmpfs_fs_t *tfs = tmpfs_create();
        if (!tfs)
            return -ENOMEM;
        rc = mount_register_ctx(abs, VFS_BACKEND_TMPFS, ro, "tmpfs", tfs);
        if (rc != 0) {
            tmpfs_destroy(tfs);
            return rc;
        }
        dcache_invalidate_path(abs);
        return 0;
    }

    if (kstreq(fstype, "fat32")) {
        const char *dev_name = source;
        if (!dev_name)
            return -EINVAL;
        if (dev_name[0] == '/' && dev_name[1] == 'd' && dev_name[2] == 'e' &&
            dev_name[3] == 'v' && dev_name[4] == '/') {
            dev_name += 5;
        }
        block_dev_t *bd = blockdev_find(dev_name);
        if (!bd)
            return -ENODEV;
        fat32_volume_t *vol = fat32_vol_mount(bd);
        if (!vol)
            return -EINVAL;
        rc = mount_register_ctx(abs, VFS_BACKEND_FAT32VOL, ro, "fat32", vol);
        if (rc != 0) {
            (void)fat32_vol_umount(vol);
            return rc;
        }
        dcache_invalidate_path(abs);
        return 0;
    }

    if (kstreq(fstype, "ext4") || kstreq(fstype, "ext3") || kstreq(fstype, "ext2")) {
        const char *dev_name = source;
        if (!dev_name)
            return -EINVAL;
        if (dev_name[0] == '/' && dev_name[1] == 'd' && dev_name[2] == 'e' &&
            dev_name[3] == 'v' && dev_name[4] == '/') {
            dev_name += 5;
        }
        block_dev_t *bd = blockdev_find(dev_name);
        if (!bd)
            return -ENODEV;
        int mrc = -EINVAL;
        ext4fs_t *efs = ext4fs_mount(bd, ro, &mrc);
        if (!efs)
            return (mrc < 0) ? mrc : -EINVAL;
        rc = mount_register_ctx(abs, VFS_BACKEND_EXT4, ro, "ext4", efs);
        if (rc != 0) {
            (void)ext4fs_umount(efs);
            return rc;
        }
        dcache_invalidate_path(abs);
        return 0;
    }

    return -ENODEV;
}

int vfs_umount2(const char *target, int flags)
{
    process_t *proc = vfs_current_process();
    char norm[VFS_PATH_MAX];
    vfs_mount_t *target_m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!target)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, target, (flags & UMOUNT_NOFOLLOW) ? 0 : 1,
                             abs, &target_m, sub, &exists, &type);
    if (rc == 0 && target_m) {
        kstrncpy(norm, abs, VFS_PATH_MAX);
        mount_unref(target_m);
    } else if (normalize_path_for_process(proc, target, norm, VFS_PATH_MAX) != 0) {
        return -EINVAL;
    }

    if (kstreq(norm, "/"))
        return -EBUSY;

    unsigned long mflags = spin_lock_irqsave(&g_mounts_lock);
    vfs_mount_t *found = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && kstreq(g_mounts[i].path, norm)) {
            found = &g_mounts[i];
            break;
        }
    }
    if (!found) {
        spin_unlock_irqrestore(&g_mounts_lock, mflags);
        return -EINVAL;
    }
    if ((found->open_count > 0 || found->active_ops > 0) && !(flags & MNT_DETACH)) {
        spin_unlock_irqrestore(&g_mounts_lock, mflags);
        return -EBUSY;
    }

    uint64_t mid = found->mount_id;
    vfs_backend_t backend = found->backend;
    void *priv = found->fs_private;

    found->used = 0;
    found->mount_id = 0;
    found->open_count = 0;
    found->active_ops = 0;
    found->path[0] = '\0';
    found->path_len = 0;
    found->backend = VFS_BACKEND_NONE;
    found->fs_private = NULL;
    spin_unlock_irqrestore(&g_mounts_lock, mflags);

    vfs_pc_unmount(mid);
    dcache_invalidate_mount(mid);
    dcache_invalidate_path(norm);
    if (backend == VFS_BACKEND_TMPFS && priv)
        tmpfs_destroy((tmpfs_fs_t *)priv);
    else if (backend == VFS_BACKEND_EXT4 && priv)
        (void)ext4fs_umount((ext4fs_t *)priv);
    else if (backend == VFS_BACKEND_FAT32VOL && priv)
        (void)fat32_vol_umount((fat32_volume_t *)priv);
    else if (backend == VFS_BACKEND_FAT32) {
        fat32_volume_t *pv = priv ? (fat32_volume_t *)priv : fat32_primary_vol();
        if (pv)
            (void)fat32_vol_umount(pv);
    }
    return 0;
}

void vfs_close(int fd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    fd_close_on_table(tbl, fd);
}

/* Read/write/seek */

int vfs_fd_has_mode(int fd, int mode)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    int ok = (f && (f->mode & mode) == mode) ? 0 : -EBADF;
    if (f)
        file_release(f);
    return ok;
}

size_t vfs_read(int fd, void *buf, size_t count)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f || !buf || !(f->mode & VFS_MODE_READ)) {
        if (f)
            file_release(f);
        return 0;
    }
    if (f->is_dir) {
        file_release(f);
        return (size_t)-EISDIR;
    }
    if (count == 0) {
        file_release(f);
        return 0;
    }

    if (f->backend == VFS_BACKEND_TMPFS) {
        vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
        if (pc)
            (void)pagecache_sync_obj(pc->obj_id);
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        size_t pos = f->pos;
        spin_unlock_irqrestore(&f->lock, fflags);
        size_t got = tmpfs_read(f->u.tmpfs.inode, pos, buf, count);
        fflags = spin_lock_irqsave(&f->lock);
        f->pos = pos + got;
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return got;
    }

    if (f->backend == VFS_BACKEND_EXT4) {
        vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
        if (pc)
            (void)pagecache_sync_obj(pc->obj_id);
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        size_t pos = f->pos;
        spin_unlock_irqrestore(&f->lock, fflags);
        long got = ext4fs_read((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, pos, buf, count);
        if (got > 0) {
            fflags = spin_lock_irqsave(&f->lock);
            f->pos = pos + (size_t)got;
            spin_unlock_irqrestore(&f->lock, fflags);
        }
        file_release(f);
        return (size_t)got;
    }

    if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        vfs_pc_inode_t *pc = vfs_pc_find_for_file(f);
        if (pc)
            (void)pagecache_sync_obj(pc->obj_id);
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        size_t pos = f->pos;
        uint32_t hidx = f->u.fat32.hint_clus_idx;
        uint32_t hclus = f->u.fat32.hint_clus;
        uint64_t hgen = f->u.fat32.hint_gen;
        spin_unlock_irqrestore(&f->lock, fflags);
        size_t got = 0;
        int rc = fat32_vol_read_at(f->u.fat32.vol, f->path, pos, buf, count,
                                   &got, &hidx, &hclus, &hgen);
        fflags = spin_lock_irqsave(&f->lock);
        f->u.fat32.hint_clus_idx = hidx;
        f->u.fat32.hint_clus = hclus;
        f->u.fat32.hint_gen = hgen;
        if (got > 0)
            f->pos = pos + got;
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return (rc < 0 && got == 0) ? (size_t)rc : got;
    }

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (!p || !f->u.pipe.can_read) {
            file_release(f);
            return 0;
        }

        for (;;) {
            unsigned long pflags = spin_lock_irqsave(&p->lock);
            if (p->size > 0) {
                size_t got = pipe_ring_read(p, buf, count);
                spin_unlock_irqrestore(&p->lock, pflags);
                if (got > 0)
                    wait_queue_wake_all(&p->waitq);
                file_release(f);
                return got;
            }
            if (p->writers == 0) {
                spin_unlock_irqrestore(&p->lock, pflags);
                file_release(f);
                return 0;
            }
            if (f->flags & VFS_O_NONBLOCK) {
                spin_unlock_irqrestore(&p->lock, pflags);
                file_release(f);
                return (size_t)-EAGAIN;
            }
#ifdef __x86_64__
            process_t *curp = vfs_current_process();
            if (!curp) {
                spin_unlock_irqrestore(&p->lock, pflags);
                file_release(f);
                return 0;
            }
            wait_queue_entry_t wqe;
            wqe.proc = curp;
            wqe.next = NULL;
            wait_queue_prepare_to_wait(&p->waitq, &wqe);
            spin_unlock_irqrestore(&p->lock, pflags);
            process_yield();
            wait_queue_finish_wait(&p->waitq, &wqe);
#else
            spin_unlock_irqrestore(&p->lock, pflags);
            file_release(f);
            return 0;
#endif
        }
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener || !f->u.sock.rx) {
            file_release(f);
            return 0;
        }
        int err = 0;
        size_t got = unix_ring_read(f->u.sock.rx, buf, count,
                                    (f->flags & VFS_O_NONBLOCK) ? 1 : 0, &err);
        file_release(f);
        return (err < 0) ? (size_t)err : got;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        size_t n = 0;
        if (f->u.device.ops && f->u.device.ops->read) {
            unsigned long fflags = spin_lock_irqsave(&f->lock);
            size_t pos = f->pos;
            spin_unlock_irqrestore(&f->lock, fflags);
            n = f->u.device.ops->read(f->u.device.priv, pos, buf, count, f->flags);
            fflags = spin_lock_irqsave(&f->lock);
            f->pos = pos + n;
            spin_unlock_irqrestore(&f->lock, fflags);
        }
        file_release(f);
        return n;
    }

    unsigned long fflags = spin_lock_irqsave(&f->lock);
    if (!f->u.regular.buf || f->pos >= f->u.regular.size) {
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return 0;
    }
    if (count > f->u.regular.size - f->pos)
        count = f->u.regular.size - f->pos;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = f->u.regular.buf[f->pos + i];
    f->pos += count;
    spin_unlock_irqrestore(&f->lock, fflags);
    file_release(f);
    return count;
}

size_t vfs_write(int fd, const void *buf, size_t count)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f || !buf || !(f->mode & VFS_MODE_WRITE)) {
        if (f)
            file_release(f);
        return 0;
    }
    if (f->is_dir) {
        file_release(f);
        return (size_t)-EISDIR;
    }
    if (count == 0) {
        file_release(f);
        return 0;
    }

    if (f->backend == VFS_BACKEND_TMPFS) {
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        if (f->flags & VFS_O_APPEND)
            f->pos = tmpfs_inode_size(f->u.tmpfs.inode);
        size_t pos = f->pos;
        spin_unlock_irqrestore(&f->lock, fflags);

        size_t wr = tmpfs_write(f->u.tmpfs.inode, pos, buf, count);
        if (wr > 0)
            vfs_pc_update_write(f, pos, buf, wr);

        fflags = spin_lock_irqsave(&f->lock);
        f->pos = pos + wr;
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return wr;
    }

    if (f->backend == VFS_BACKEND_EXT4) {
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        int is_append = (f->flags & VFS_O_APPEND) ? 1 : 0;
        size_t pos = f->pos;
        spin_unlock_irqrestore(&f->lock, fflags);
        if (is_append)
            pos = ext4fs_file_size((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, f->path);

        long wr = ext4fs_write((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, pos, buf, count);
        if (wr > 0) {
            vfs_pc_update_write(f, pos, buf, (size_t)wr);
            fflags = spin_lock_irqsave(&f->lock);
            f->pos = pos + (size_t)wr;
            f->dirty = 1;
            spin_unlock_irqrestore(&f->lock, fflags);
        }
        file_release(f);
        return (size_t)wr;
    }

    if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        int is_append = (f->flags & VFS_O_APPEND) ? 1 : 0;
        size_t pos = f->pos;
        uint32_t hidx = f->u.fat32.hint_clus_idx;
        uint32_t hclus = f->u.fat32.hint_clus;
        uint64_t hgen = f->u.fat32.hint_gen;
        spin_unlock_irqrestore(&f->lock, fflags);

        if (is_append) {
            fat32_dirent_t de;
            if (fat32_vol_stat(f->u.fat32.vol, f->path, &de) == 0)
                pos = de.size;
        }
        size_t wr = 0;
        int rc = fat32_vol_write_at(f->u.fat32.vol, f->path, pos, buf, count,
                                    &wr, &hidx, &hclus, &hgen);
        if (wr > 0)
            vfs_pc_update_write(f, pos, buf, wr);
        fflags = spin_lock_irqsave(&f->lock);
        f->u.fat32.hint_clus_idx = hidx;
        f->u.fat32.hint_clus = hclus;
        f->u.fat32.hint_gen = hgen;
        if (wr > 0) {
            f->pos = pos + wr;
            f->dirty = 1;
        }
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return (rc < 0 && wr == 0) ? (size_t)rc : wr;
    }

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (!p || !f->u.pipe.can_write) {
            file_release(f);
            return (size_t)-EPIPE;
        }
        for (;;) {
            unsigned long pflags = spin_lock_irqsave(&p->lock);
            if (p->readers == 0) {
                spin_unlock_irqrestore(&p->lock, pflags);
                file_release(f);
                return (size_t)-EPIPE;
            }
            size_t wr = pipe_ring_write(p, buf, count);
            spin_unlock_irqrestore(&p->lock, pflags);
            if (wr > 0) {
                wait_queue_wake_all(&p->waitq);
                file_release(f);
                return wr;
            }
            if (f->flags & VFS_O_NONBLOCK) {
                file_release(f);
                return (size_t)-EAGAIN;
            }
#ifdef __x86_64__
            process_t *curp = vfs_current_process();
            if (!curp) {
                file_release(f);
                return 0;
            }
            wait_queue_entry_t wqe;
            wqe.proc = curp;
            wqe.next = NULL;
            wait_queue_prepare_to_wait(&p->waitq, &wqe);
            process_yield();
            wait_queue_finish_wait(&p->waitq, &wqe);
#else
            file_release(f);
            return 0;
#endif
        }
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener || !f->u.sock.tx) {
            file_release(f);
            return 0;
        }
        int err = 0;
        size_t wr = unix_ring_write(f->u.sock.tx, buf, count,
                                    (f->flags & VFS_O_NONBLOCK) ? 1 : 0, &err);
        file_release(f);
        return (err < 0) ? (size_t)err : wr;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        size_t n = 0;
        if (f->u.device.ops && f->u.device.ops->write) {
            unsigned long fflags = spin_lock_irqsave(&f->lock);
            size_t pos = f->pos;
            spin_unlock_irqrestore(&f->lock, fflags);
            n = f->u.device.ops->write(f->u.device.priv, pos, buf, count);
            fflags = spin_lock_irqsave(&f->lock);
            f->pos = pos + n;
            spin_unlock_irqrestore(&f->lock, fflags);
        }
        file_release(f);
        return n;
    }

    if (f->backend == VFS_BACKEND_INITRD ||
        f->backend == VFS_BACKEND_PROCFS ||
        f->backend == VFS_BACKEND_SYSFS ||
        f->backend == VFS_BACKEND_BOOTFS ||
        f->backend == VFS_BACKEND_TAR) {
        file_release(f);
        return 0;
    }

    unsigned long fflags = spin_lock_irqsave(&f->lock);
    if (f->flags & VFS_O_APPEND)
        f->pos = f->u.regular.size;
    if (ensure_regular_capacity(f, f->pos + count) != 0) {
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return 0;
    }
    for (size_t i = 0; i < count; i++)
        f->u.regular.buf[f->pos + i] = ((const uint8_t *)buf)[i];
    f->pos += count;
    if (f->pos > f->u.regular.size)
        f->u.regular.size = f->pos;
    f->dirty = 1;
    spin_unlock_irqrestore(&f->lock, fflags);
    file_release(f);
    return count;
}

size_t vfs_seek(int fd, size_t offset, int whence)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    size_t size = 0;
    size_t new_pos;
    if (!f)
        return (size_t)-1;
    if (f->backend == VFS_BACKEND_PIPE || f->backend == VFS_BACKEND_UNIXSOCK) {
        file_release(f);
        return (size_t)-ESPIPE;
    }

    if (f->is_dir) {
        unsigned long fflags = spin_lock_irqsave(&f->lock);
        if (whence == VFS_SEEK_SET)
            f->pos = offset;
        else if (whence == VFS_SEEK_CUR)
            f->pos += offset;
        new_pos = f->pos;
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return new_pos;
    }

    if (f->backend == VFS_BACKEND_TMPFS)
        size = tmpfs_inode_size(f->u.tmpfs.inode);
    else if (f->backend == VFS_BACKEND_EXT4)
        size = ext4fs_file_size((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, f->path);
    else if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        fat32_dirent_t de;
        size = (f->u.fat32.vol && fat32_vol_stat(f->u.fat32.vol, f->path, &de) == 0)
                   ? de.size
                   : 0;
    } else if (f->backend == VFS_BACKEND_DEVFS)
        size = (f->u.device.ops && f->u.device.ops->size) ? f->u.device.ops->size(f->u.device.priv) : 0;
    else
        size = f->u.regular.size;

    unsigned long fflags = spin_lock_irqsave(&f->lock);
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
        spin_unlock_irqrestore(&f->lock, fflags);
        file_release(f);
        return (size_t)-1;
    }
    if (f->pos > size)
        f->pos = size;
    new_pos = f->pos;
    spin_unlock_irqrestore(&f->lock, fflags);
    file_release(f);
    return new_pos;
}

size_t vfs_tell(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return (size_t)-1;
    unsigned long fflags = spin_lock_irqsave(&f->lock);
    size_t pos = f->pos;
    spin_unlock_irqrestore(&f->lock, fflags);
    file_release(f);
    return pos;
}

size_t vfs_size(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    size_t sz = 0;
    if (!f)
        return 0;
    if (f->backend == VFS_BACKEND_TMPFS) {
        sz = tmpfs_inode_size(f->u.tmpfs.inode);
    } else if (f->backend == VFS_BACKEND_EXT4) {
        sz = ext4fs_file_size((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, f->path);
    } else if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        fat32_dirent_t de;
        sz = (f->u.fat32.vol && fat32_vol_stat(f->u.fat32.vol, f->path, &de) == 0)
                 ? de.size
                 : 0;
    } else if (f->backend == VFS_BACKEND_PIPE) {
        if (f->u.pipe.pipe) {
            unsigned long pflags = spin_lock_irqsave(&f->u.pipe.pipe->lock);
            sz = f->u.pipe.pipe->size;
            spin_unlock_irqrestore(&f->u.pipe.pipe->lock, pflags);
        }
    } else if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (!f->u.sock.is_listener && f->u.sock.rx)
            sz = unix_ring_avail(f->u.sock.rx);
    } else if (f->backend == VFS_BACKEND_DEVFS) {
        sz = (f->u.device.ops && f->u.device.ops->size) ? f->u.device.ops->size(f->u.device.priv) : 0;
    } else {
        sz = f->u.regular.size;
    }
    file_release(f);
    return sz;
}

/* dup/dup2/pipe/fcntl */

int vfs_dup(int oldfd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    vfs_file_t *f = fd_lookup_ref(proc, oldfd);
    if (!f)
        return -EBADF;
    int newfd = vfs_install_fd(proc, f);
    if (newfd < 0) {
        file_release(f);
        return newfd;
    }
    (void)tbl;
    return newfd;
}

int vfs_dup2(int oldfd, int newfd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    if (!fd_valid(newfd))
        return -EBADF;
    vfs_file_t *f = fd_lookup_ref(proc, oldfd);
    if (!f)
        return -EBADF;
    if (oldfd == newfd) {
        file_release(f);
        return newfd;
    }
    fd_close_on_table(tbl, newfd);
    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    vfs_file_t *existing = (vfs_file_t *)tbl[newfd];
    tbl[newfd] = f;
    spin_unlock_irqrestore(&g_fd_lock, flags);
    if (existing && existing != (vfs_file_t *)(uintptr_t)1)
        file_release(existing);
    return newfd;
}

int vfs_pipe(int pipefd[2])
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    int fd_r = -1;
    int fd_w = -1;
    vfs_pipe_t *p;
    vfs_file_t *fr;
    vfs_file_t *fw;

    if (!pipefd)
        return -EINVAL;

    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        if (!tbl[i]) {
            if (fd_r < 0) {
                fd_r = i;
                tbl[i] = (void *)1;
            } else {
                fd_w = i;
                tbl[i] = (void *)1;
                break;
            }
        }
    }
    if (fd_r < 0 || fd_w < 0) {
        if (fd_r >= 0)
            tbl[fd_r] = NULL;
        spin_unlock_irqrestore(&g_fd_lock, flags);
        return -EMFILE;
    }
    spin_unlock_irqrestore(&g_fd_lock, flags);

    p = pipe_alloc();
    fr = file_alloc();
    fw = file_alloc();
    if (!p || !fr || !fw) {
        if (fr)
            file_release(fr);
        if (fw)
            file_release(fw);
        if (p)
            pipe_free_slot(p);
        flags = spin_lock_irqsave(&g_fd_lock);
        tbl[fd_r] = NULL;
        tbl[fd_w] = NULL;
        spin_unlock_irqrestore(&g_fd_lock, flags);
        return -ENOMEM;
    }

    fr->backend = VFS_BACKEND_PIPE;
    fr->mode = VFS_MODE_READ;
    fr->flags = VFS_O_RDONLY;
    fr->inode_dev = 0x50495045ULL;
    fr->inode_ino = (uint64_t)(uintptr_t)p;
    fr->u.pipe.pipe = p;
    fr->u.pipe.can_read = 1;
    fr->u.pipe.can_write = 0;

    fw->backend = VFS_BACKEND_PIPE;
    fw->mode = VFS_MODE_WRITE;
    fw->flags = VFS_O_WRONLY;
    fw->inode_dev = 0x50495045ULL;
    fw->inode_ino = (uint64_t)(uintptr_t)p;
    fw->u.pipe.pipe = p;
    fw->u.pipe.can_read = 0;
    fw->u.pipe.can_write = 1;

    p->readers = 1;
    p->writers = 1;

    flags = spin_lock_irqsave(&g_fd_lock);
    tbl[fd_r] = fr;
    tbl[fd_w] = fw;
    spin_unlock_irqrestore(&g_fd_lock, flags);

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
    vfs_file_t *f = file_alloc();
    if (!f)
        return -ENOMEM;

    f->backend = VFS_BACKEND_UNIXSOCK;
    f->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    f->flags = VFS_O_RDWR;
    f->inode_dev = 0x534F434BULL;
    f->inode_ino = (uint64_t)(uintptr_t)f;
    f->path[0] = '\0';
    f->u.sock.rx = NULL;
    f->u.sock.tx = NULL;
    f->u.sock.listener = NULL;
    f->u.sock.is_listener = 0;

    int fd = vfs_install_fd(proc, f);
    if (fd < 0) {
        file_release(f);
        return -EMFILE;
    }
    return fd;
}

int vfs_socket_bind(int fd, const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    unix_listener_t *l;
    int rc;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK) {
        if (f) file_release(f);
        return -EINVAL;
    }
    if (f->u.sock.is_listener || f->u.sock.rx || f->u.sock.tx) {
        file_release(f);
        return -EINVAL;
    }
    if (!path || !path[0]) {
        file_release(f);
        return -EINVAL;
    }
    rc = unix_register_listener(path, proc ? (int)proc->pid : 0, fd);
    if (rc != 0) {
        file_release(f);
        return rc;
    }
    l = unix_find_listener(path);
    if (!l) {
        unix_unregister_listener(path);
        file_release(f);
        return -EINVAL;
    }
    kstrncpy(f->path, path, VFS_PATH_MAX);
    f->u.sock.listener = l;
    f->u.sock.is_listener = 1;
    file_release(f);
    return 0;
}

int vfs_socket_listen(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK || !f->u.sock.is_listener) {
        if (f) file_release(f);
        return -EINVAL;
    }
    unix_listener_set_listening(f->u.sock.listener, 1);
    file_release(f);
    return 0;
}

int vfs_socket_connect(int fd, const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    unix_listener_t *l;
    unix_ring_t *c2s;
    unix_ring_t *s2c;
    unix_pending_conn_t pc;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK) {
        if (f) file_release(f);
        return -EINVAL;
    }
    if (f->u.sock.is_listener || f->u.sock.rx || f->u.sock.tx) {
        file_release(f);
        return -EINVAL;
    }
    l = unix_find_listener(path);
    if (!l || !unix_listener_is_listening(l)) {
        file_release(f);
        return -ECONNREFUSED;
    }

    c2s = unix_ring_create();
    s2c = unix_ring_create();
    if (!c2s || !s2c) {
        if (c2s)
            unix_ring_drop_claim(c2s, 1, 1);
        if (s2c)
            unix_ring_drop_claim(s2c, 1, 1);
        file_release(f);
        return -ENOMEM;
    }

    pc.pipe1 = c2s;
    pc.pipe2 = s2c;
    pc.client_pid = proc ? (int)proc->pid : 0;
    pc.client_fd = fd;
    if (unix_enqueue_pending(l, &pc) != 0) {
        unix_ring_drop_claim(c2s, 1, 1);
        unix_ring_drop_claim(s2c, 1, 1);
        file_release(f);
        return -ECONNREFUSED;
    }

    f->u.sock.rx = s2c;
    f->u.sock.tx = c2s;
    file_release(f);
    return 0;
}

int vfs_socket_accept(int fd)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    unix_pending_conn_t pc;
    vfs_file_t *nf;
    int nfd;

    if (!f || f->backend != VFS_BACKEND_UNIXSOCK || !f->u.sock.is_listener) {
        if (f) file_release(f);
        return -EINVAL;
    }
    if (!unix_listener_is_listening(f->u.sock.listener)) {
        file_release(f);
        return -EINVAL;
    }

    for (;;) {
        if (unix_dequeue_pending(f->u.sock.listener, &pc) == 0)
            break;

        if (f->flags & VFS_O_NONBLOCK) {
            file_release(f);
            return -EAGAIN;
        }

#ifdef __x86_64__
        wait_queue_head_t *wq = unix_listener_get_accept_waitq(f->u.sock.listener);
        if (!wq || !proc) {
            file_release(f);
            return -EAGAIN;
        }

        wait_queue_entry_t wqe;
        wqe.proc = proc;
        wqe.next = NULL;
        wait_queue_add(wq, &wqe);
        unsigned long irqf = vfs_irq_save_disable();
        if (unix_dequeue_pending(f->u.sock.listener, &pc) == 0) {
            vfs_irq_restore(irqf);
            wait_queue_remove(wq, &wqe);
            break;
        }
        process_block_current();
        process_yield();
        vfs_irq_restore(irqf);
        wait_queue_remove(wq, &wqe);
#else
        file_release(f);
        return -EAGAIN;
#endif
    }

    nf = file_alloc();
    if (!nf) {
        unix_ring_drop_claim(pc.pipe1, 1, 0);
        unix_ring_drop_claim(pc.pipe2, 0, 1);
        file_release(f);
        return -ENOMEM;
    }
    nf->backend = VFS_BACKEND_UNIXSOCK;
    nf->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    nf->flags = VFS_O_RDWR;
    nf->inode_dev = 0x534F434BULL;
    nf->inode_ino = (uint64_t)(uintptr_t)nf;
    nf->path[0] = '\0';
    nf->u.sock.rx = pc.pipe1;
    nf->u.sock.tx = pc.pipe2;
    nf->u.sock.listener = NULL;
    nf->u.sock.is_listener = 0;

    nfd = vfs_install_fd(proc, nf);
    if (nfd < 0) {
        file_release(nf);
        file_release(f);
        return -EMFILE;
    }
    file_release(f);
    return nfd;
}

int vfs_fcntl(int fd, int cmd, int arg)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    int rc = -EINVAL;
    if (!f)
        return -EBADF;
    unsigned long flags = spin_lock_irqsave(&f->lock);
    switch (cmd) {
    case VFS_F_GETFL:
        rc = f->flags;
        break;
    case VFS_F_SETFL:
        f->flags = (f->flags & ~(VFS_O_APPEND | VFS_O_NONBLOCK)) |
                   (arg & (VFS_O_APPEND | VFS_O_NONBLOCK));
        rc = 0;
        break;
    default:
        rc = -EINVAL;
        break;
    }
    spin_unlock_irqrestore(&f->lock, flags);
    file_release(f);
    return rc;
}

int vfs_ioctl(int fd, unsigned long request, void *arg)
{
    process_t *proc = vfs_current_process();
    if (request == VFS_KDSETMODE)
        return fb_ioctl(request, arg);

    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;
    int rc = -ENOTTY;
    if (f->backend == VFS_BACKEND_DEVFS && f->u.device.ops && f->u.device.ops->ioctl)
        rc = f->u.device.ops->ioctl(f->u.device.priv, request, arg);
    file_release(f);
    return rc;
}

int vfs_kd_mode(void)
{
    return fb_kd_mode();
}

void *vfs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    process_t *proc = vfs_current_process();
    if (!proc || !proc->vm_space.owns_pml4 || length == 0)
        return (void *)-1;

    if ((prot & ~(VFS_PROT_READ | VFS_PROT_WRITE | VFS_PROT_EXEC)) != 0)
        return (void *)(intptr_t)-EINVAL;

    /* Handle anonymous mmap (backing for user heap and anonymous regions). */
    if ((flags & VFS_MAP_ANONYMOUS) || fd == -1) {
        uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
        if (length > SIZE_MAX - page_mask)
            return (void *)(intptr_t)-ENOMEM;
        size_t span = (length + page_mask) & ~page_mask;
        size_t page_count = span / PAGE_SIZE;
        uintptr_t target_va = 0;

        vm_space_mmap_write_lock(&proc->vm_space);

        if (flags & VFS_MAP_FIXED) {
            uintptr_t req_va = (uintptr_t)addr;
            if (!req_va || !paging_is_page_aligned_uintptr(req_va)) {
                vm_space_mmap_write_unlock(&proc->vm_space);
                return (void *)(intptr_t)-EINVAL;
            }
            if (!paging_range_is_user(req_va, span)) {
                vm_space_mmap_write_unlock(&proc->vm_space);
                return (void *)(intptr_t)-ENOMEM;
            }
            (void)vma_unmap_range(&proc->vm_space, req_va, span);
            target_va = req_va;
        } else {
            if (addr != NULL && paging_is_page_aligned_uintptr((uintptr_t)addr) &&
                paging_range_is_user((uintptr_t)addr, span)) {
                uintptr_t hint = (uintptr_t)addr;
                target_va = vma_find_unmapped_area(&proc->vm_space, hint, hint + span, span);
            }
            if (!target_va)
                target_va = vm_space_reserve_anon_range(&proc->vm_space, page_count);
            if (!target_va) {
                vm_space_mmap_write_unlock(&proc->vm_space);
                return (void *)(intptr_t)-ENOMEM;
            }
        }

        uint32_t vma_prot = 0;
        if (prot & VFS_PROT_READ)  vma_prot |= VMA_PROT_READ;
        if (prot & VFS_PROT_WRITE) vma_prot |= VMA_PROT_WRITE;
        if (prot & VFS_PROT_EXEC)  vma_prot |= VMA_PROT_EXEC;

        uint32_t vma_flags = VMA_FLAG_ANON |
                             ((flags & VFS_MAP_SHARED) ? VMA_FLAG_SHARED : VMA_FLAG_PRIVATE);

        vm_area_t *vma = vma_insert(&proc->vm_space, target_va, target_va + span,
                                    vma_prot, vma_flags);
        vm_space_mmap_write_unlock(&proc->vm_space);
        if (!vma)
            return (void *)(intptr_t)-ENOMEM;

        return (void *)target_va;
    }

    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return (void *)(intptr_t)-EBADF;

    void *res = (void *)-1;
    if (f->backend == VFS_BACKEND_DEVFS && f->u.device.ops && f->u.device.ops->mmap)
        res = f->u.device.ops->mmap(f->u.device.priv, addr, length, prot, flags, offset);
    file_release(f);
    return res;
}

int vfs_munmap(void *addr, size_t length)
{
    if (!addr || length == 0)
        return -EINVAL;

    process_t *proc = vfs_current_process();
    if (!proc || !proc->vm_space.owns_pml4)
        return -EINVAL;

    uintptr_t p = (uintptr_t)addr;
    uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
    if (!paging_is_page_aligned_uintptr(p))
        return -EINVAL;
    if (p + length < p || (p + length) > UINTPTR_MAX - page_mask)
        return -EINVAL;

    uintptr_t base = p;
    uintptr_t end = (p + length + page_mask) & ~page_mask;
    if (end <= base)
        return -EINVAL;

    size_t span = (size_t)(end - base);
    if (!paging_range_is_user(base, span))
        return -EINVAL;

    size_t fb_sz = fb_byte_size();
    if (fb_sz > 0) {
        uintptr_t fb_end = (uintptr_t)VFS_FB_USER_BASE +
                           ((fb_sz + page_mask) & ~page_mask);
        if (base >= (uintptr_t)VFS_FB_USER_BASE && end <= fb_end)
            return fb_munmap((void *)base, span);
    }

    vm_space_mmap_write_lock(&proc->vm_space);
    int rc = vma_unmap_range(&proc->vm_space, base, span);
    vm_space_mmap_write_unlock(&proc->vm_space);
    return rc;
}

int vfs_msync(void *addr, size_t length, int flags)
{
    const int allowed = VFS_MS_ASYNC | VFS_MS_INVALIDATE | VFS_MS_SYNC;
    if ((flags & ~allowed) != 0)
        return -EINVAL;
    if ((flags & VFS_MS_ASYNC) != 0 && (flags & VFS_MS_SYNC) != 0)
        return -EINVAL;
    if (!addr || !paging_is_page_aligned_uintptr((uintptr_t)addr))
        return -EINVAL;

    process_t *proc = vfs_current_process();
    if (!proc || !proc->vm_space.owns_pml4)
        return -ENOMEM;

    if (length == 0)
        return paging_range_is_user((uintptr_t)addr, PAGE_SIZE) ? 0 : -ENOMEM;

    uintptr_t p = (uintptr_t)addr;
    uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
    if (p + length < p || (p + length) > UINTPTR_MAX - page_mask)
        return -ENOMEM;

    uintptr_t end = (p + length + page_mask) & ~page_mask;
    if (end <= p || !paging_range_is_user(p, (size_t)(end - p)))
        return -ENOMEM;

    for (uintptr_t va = p; va < end; va += PAGE_SIZE) {
        uint64_t q_phys = 0, q_flags = 0;
        if (vmm_query_page(proc->vm_space.pml4_phys, va, &q_phys, &q_flags) != 0 ||
            !(q_flags & VMM_X64_PTE_PRESENT)) {
            return -ENOMEM;
        }
    }

    if (flags & VFS_MS_SYNC)
        vfs_sync_all();
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
        unsigned long pflags = spin_lock_irqsave(&p->lock);
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
        spin_unlock_irqrestore(&p->lock, pflags);
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
        return unix_ring_poll_mask(f->u.sock.rx, f->u.sock.tx);
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.ops && f->u.device.ops->poll)
            return f->u.device.ops->poll(f->u.device.priv, VFS_POLLIN | VFS_POLLOUT);
        if (f->mode & VFS_MODE_WRITE)
            mask |= VFS_POLLOUT;
        return mask;
    }

    if (f->backend == VFS_BACKEND_EPOLL) {
        process_t *proc = vfs_current_process();
        unsigned long eflags = spin_lock_irqsave(&f->lock);
        int fds[64];
        uint32_t evs[64];
        int cnt = 0;
        epoll_item_t *item = f->u.epoll.items;
        while (item && cnt < 64) {
            fds[cnt] = item->fd;
            evs[cnt] = item->events;
            cnt++;
            item = item->next;
        }
        spin_unlock_irqrestore(&f->lock, eflags);
        for (int i = 0; i < cnt; i++) {
            vfs_file_t *tf = fd_lookup_ref(proc, fds[i]);
            if (tf) {
                int tmask = vfs_file_poll_mask(tf);
                file_release(tf);
                if (tmask & (int)(evs[i] | VFS_POLLERR | VFS_POLLHUP))
                    return VFS_POLLIN;
            }
        }
        return 0;
    }

    if (f->mode & VFS_MODE_READ)
        mask |= VFS_POLLIN;
    if (f->mode & VFS_MODE_WRITE)
        mask |= VFS_POLLOUT;
    return mask;
}

static int vfs_poll_scan(vfs_pollfd_t *fds, size_t nfds)
{
    process_t *proc = vfs_current_process();
    int ready = 0;
    if (!fds)
        return 0;

    for (size_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0)
            continue;
        vfs_file_t *f = fd_lookup_ref(proc, fds[i].fd);
        if (!f) {
            fds[i].revents = VFS_POLLNVAL;
            ready++;
            continue;
        }
        int mask = vfs_file_poll_mask(f);
        file_release(f);
        int rev = mask & (fds[i].events | VFS_POLLERR | VFS_POLLHUP);
        fds[i].revents = (int16_t)rev;
        if (rev)
            ready++;
    }
    return ready;
}

static void vfs_poll_register_file(vfs_file_t *f, poll_table_t *pt, int16_t events)
{
    if (!f || !pt)
        return;

    if (f->backend == VFS_BACKEND_PIPE) {
        vfs_pipe_t *p = f->u.pipe.pipe;
        if (p)
            poll_wtable_queue(&p->waitq, pt);
        return;
    }

    if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (f->u.sock.is_listener) {
            wait_queue_head_t *wq = unix_listener_get_accept_waitq(f->u.sock.listener);
            if (wq)
                poll_wtable_queue(wq, pt);
            return;
        }
        if (f->u.sock.rx) {
            wait_queue_head_t *wq = unix_ring_get_waitq(f->u.sock.rx);
            if (wq)
                poll_wtable_queue(wq, pt);
        }
        if (f->u.sock.tx && f->u.sock.tx != f->u.sock.rx) {
            wait_queue_head_t *wq = unix_ring_get_waitq(f->u.sock.tx);
            if (wq)
                poll_wtable_queue(wq, pt);
        }
        return;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        const char *sub = f->path;
        while (*sub == '/')
            sub++;
        if ((events & VFS_POLLIN) &&
            (kstreq(sub, "input/events") || kstreq(sub, "events") ||
             kstreq(sub, "keyboard") || kstreq(sub, "mouse") ||
             kstreq(sub, "tty") ||
             (sub[0] == 't' && sub[1] == 't' && sub[2] == 'y' &&
              sub[3] >= '0' && sub[3] <= '9' && sub[4] == '\0'))) {
            wait_queue_head_t *wq = input_dev_get_events_waitq();
            if (wq)
                poll_wtable_queue(wq, pt);
        }
        return;
    }

    if (f->backend == VFS_BACKEND_EPOLL) {
        process_t *proc = vfs_current_process();
        unsigned long eflags = spin_lock_irqsave(&f->lock);
        int fds[64];
        uint32_t evs[64];
        int cnt = 0;
        epoll_item_t *item = f->u.epoll.items;
        while (item && cnt < 64) {
            fds[cnt] = item->fd;
            evs[cnt] = item->events;
            cnt++;
            item = item->next;
        }
        spin_unlock_irqrestore(&f->lock, eflags);
        for (int i = 0; i < cnt; i++) {
            vfs_file_t *tf = fd_lookup_ref(proc, fds[i]);
            if (tf) {
                vfs_poll_register_file(tf, pt, (int16_t)evs[i]);
                file_release(tf);
            }
        }
        return;
    }
}

static void vfs_poll_register(vfs_pollfd_t *fds, size_t nfds, poll_table_t *pt)
{
    process_t *proc = vfs_current_process();
    if (!fds || !pt)
        return;

    for (size_t i = 0; i < nfds; i++) {
        if (fds[i].fd < 0)
            continue;
        vfs_file_t *f = fd_lookup_ref(proc, fds[i].fd);
        if (f) {
            vfs_poll_register_file(f, pt, fds[i].events);
            file_release(f);
        }
    }
}

int vfs_poll(vfs_pollfd_t *fds, size_t nfds, int timeout_ms)
{
    process_t *proc = vfs_current_process();

    if (nfds > 256)
        return -EINVAL;

    if (nfds == 0) {
        if (timeout_ms == 0)
            return 0;
        if (timeout_ms > 0)
            return process_sleep_ms((uint64_t)timeout_ms);
        for (;;) {
            unsigned long irqf = vfs_irq_save_disable();
            if (proc && (proc->signal_pending & ~proc->signal_mask)) {
                vfs_irq_restore(irqf);
                return -EINTR;
            }
            process_set_poll_blocked(0);
            process_yield();
            vfs_irq_restore(irqf);
            process_clear_poll_deadline();
        }
    }

    if (!fds)
        return -EFAULT;

    /* Phase 1: non-blocking scan */
    int ready = vfs_poll_scan(fds, nfds);
    if (ready > 0 || timeout_ms == 0)
        return ready;

    /* Phase 2: register on wait queues */
    poll_wtable_t wt;
    int cap = (int)nfds * 2;
    if (cap < MAX_POLL_ENTRIES)
        cap = MAX_POLL_ENTRIES;
    poll_wtable_init_with_capacity(&wt, proc, cap);
    vfs_poll_register(fds, nfds, &wt.pt);

    /* Phase 3: sleep without lost-wakeup window across scan -> block -> yield */
    uint64_t deadline = 0;
    if (timeout_ms > 0) {
        uint64_t now_ms = pit_ticks_ms();
        uint64_t tms = (uint64_t)timeout_ms;
        deadline = (tms > UINT64_MAX - now_ms) ? UINT64_MAX : (now_ms + tms);
    }
    for (;;) {
        unsigned long irqf = vfs_irq_save_disable();
        ready = vfs_poll_scan(fds, nfds);
        if (ready > 0) {
            vfs_irq_restore(irqf);
            break;
        }
        if (proc && (proc->signal_pending & ~proc->signal_mask)) {
            vfs_irq_restore(irqf);
            poll_wtable_unregister_all(&wt);
            return -EINTR;
        }
        if (timeout_ms > 0 && pit_ticks_ms() >= deadline) {
            vfs_irq_restore(irqf);
            break;
        }
        process_set_poll_blocked(deadline);
        process_yield();
        vfs_irq_restore(irqf);
        process_clear_poll_deadline();
    }

    /* Phase 4: deregister and return final scan */
    poll_wtable_unregister_all(&wt);
    return vfs_poll_scan(fds, nfds);
}

int vfs_epoll_create1(int flags)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = file_alloc();
    if (!f)
        return -ENFILE;
    f->backend = VFS_BACKEND_EPOLL;
    f->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    f->flags = flags;
    f->u.epoll.items = NULL;
    f->u.epoll.count = 0;
    int fd = vfs_install_fd(proc, f);
    if (fd < 0) {
        file_release(f);
        return -EMFILE;
    }
    return fd;
}

int vfs_epoll_ctl(int epfd, int op, int fd, uint32_t events, uint64_t data)
{
    process_t *proc = vfs_current_process();
    if (!fd_valid(epfd) || !fd_valid(fd))
        return -EBADF;
    if (epfd == fd)
        return -EINVAL;
    vfs_file_t *epf = fd_lookup_ref(proc, epfd);
    if (!epf)
        return -EBADF;
    if (epf->backend != VFS_BACKEND_EPOLL) {
        file_release(epf);
        return -EINVAL;
    }
    vfs_file_t *target = fd_lookup_ref(proc, fd);
    if (!target) {
        file_release(epf);
        return -EBADF;
    }
    if (target->backend == VFS_BACKEND_EPOLL) {
        file_release(target);
        file_release(epf);
        return -EINVAL;
    }
    file_release(target);

    unsigned long eflags = spin_lock_irqsave(&epf->lock);
    epoll_item_t *prev = NULL;
    epoll_item_t *curr = epf->u.epoll.items;
    while (curr) {
        if (curr->fd == fd)
            break;
        prev = curr;
        curr = curr->next;
    }

    int rc = 0;
    switch (op) {
    case EPOLL_CTL_ADD: {
        if (curr) {
            rc = -EEXIST;
            break;
        }
        epoll_item_t *item = (epoll_item_t *)kmalloc(sizeof(epoll_item_t));
        if (!item) {
            rc = -ENOMEM;
            break;
        }
        item->fd = fd;
        item->events = events;
        item->data = data;
        item->next = epf->u.epoll.items;
        epf->u.epoll.items = item;
        epf->u.epoll.count++;
        break;
    }
    case EPOLL_CTL_MOD:
        if (!curr) {
            rc = -ENOENT;
            break;
        }
        curr->events = events;
        curr->data = data;
        break;
    case EPOLL_CTL_DEL:
        if (!curr) {
            rc = -ENOENT;
            break;
        }
        if (prev)
            prev->next = curr->next;
        else
            epf->u.epoll.items = curr->next;
        epf->u.epoll.count--;
        kfree(curr);
        break;
    default:
        rc = -EINVAL;
        break;
    }
    spin_unlock_irqrestore(&epf->lock, eflags);
    file_release(epf);
    return rc;
}

int vfs_epoll_wait(int epfd, epoll_event_t *events, int maxevents, int timeout_ms)
{
    process_t *proc = vfs_current_process();
    if (!fd_valid(epfd))
        return -EBADF;
    if (!events || maxevents <= 0 || maxevents > 256)
        return -EINVAL;
    vfs_file_t *epf = fd_lookup_ref(proc, epfd);
    if (!epf)
        return -EBADF;
    if (epf->backend != VFS_BACKEND_EPOLL) {
        file_release(epf);
        return -EINVAL;
    }

    unsigned long eflags = spin_lock_irqsave(&epf->lock);
    int count = epf->u.epoll.count;
    if (count == 0) {
        spin_unlock_irqrestore(&epf->lock, eflags);
        file_release(epf);
        if (timeout_ms == 0)
            return 0;
        return vfs_poll(NULL, 0, timeout_ms);
    }

    vfs_pollfd_t pfds_stack[64];
    vfs_pollfd_t *pfds = pfds_stack;
    int is_heap = 0;
    if (count > 64) {
        pfds = (vfs_pollfd_t *)kmalloc((size_t)count * sizeof(vfs_pollfd_t));
        if (!pfds) {
            spin_unlock_irqrestore(&epf->lock, eflags);
            file_release(epf);
            return -ENOMEM;
        }
        is_heap = 1;
    }

    epoll_item_t *item = epf->u.epoll.items;
    int idx = 0;
    while (item && idx < count) {
        pfds[idx].fd = item->fd;
        pfds[idx].events = (int16_t)(item->events & (VFS_POLLIN | VFS_POLLOUT | VFS_POLLERR | VFS_POLLHUP));
        pfds[idx].revents = 0;
        item = item->next;
        idx++;
    }
    spin_unlock_irqrestore(&epf->lock, eflags);

    int ret = vfs_poll(pfds, (size_t)idx, timeout_ms);
    if (ret <= 0) {
        if (is_heap)
            kfree(pfds);
        file_release(epf);
        return ret;
    }

    eflags = spin_lock_irqsave(&epf->lock);
    int num_ready = 0;
    for (int i = 0; i < idx && num_ready < maxevents; i++) {
        if (pfds[i].revents) {
            epoll_item_t *it = epf->u.epoll.items;
            while (it) {
                if (it->fd == pfds[i].fd) {
                    events[num_ready].events = (uint32_t)pfds[i].revents;
                    events[num_ready].data.u64 = it->data;
                    num_ready++;
                    break;
                }
                it = it->next;
            }
        }
    }
    spin_unlock_irqrestore(&epf->lock, eflags);

    if (is_heap)
        kfree(pfds);
    file_release(epf);
    return num_ready;
}

/* stat/lstat/fstat/cwd/list/getdents64 */

static int vfs_stat_common(const char *path, int follow_last, vfs_stat_t *out)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!path || !out)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, follow_last, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }

    unix_listener_t *lst = unix_find_listener(abs);
    if (lst) {
        for (size_t i = 0; i < sizeof(*out); i++)
            ((uint8_t *)out)[i] = 0;
        out->type = VFS_TYPE_SOCKET;
        out->mode = VFS_MODE_READ | VFS_MODE_WRITE;
        vfs_stat_finalize(out, m->mount_id, vfs_hash_str(abs));
        mount_unref(m);
        return 0;
    }

    rc = backend_stat_mode(m, sub, follow_last, out);
    mount_unref(m);
    return rc;
}

int vfs_stat(const char *path, vfs_stat_t *out)
{
    return vfs_stat_common(path, 1, out);
}

int vfs_lstat(const char *path, vfs_stat_t *out)
{
    return vfs_stat_common(path, 0, out);
}

int vfs_fstat(int fd, vfs_stat_t *out)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f || !out) {
        if (f) file_release(f);
        return -EBADF;
    }
    for (size_t i = 0; i < sizeof(*out); i++)
        ((uint8_t *)out)[i] = 0;

    if (f->backend == VFS_BACKEND_TMPFS && f->u.tmpfs.inode) {
        tmpfs_stat_inode(f->u.tmpfs.inode, out);
        out->mode = f->mode;
        vfs_stat_finalize(out, f->inode_dev, f->inode_ino);
        file_release(f);
        return 0;
    }
    if (f->backend == VFS_BACKEND_EXT4 && f->fs_ctx) {
        if (ext4fs_fstat((ext4fs_t *)f->fs_ctx, f->u.ext4.handle, f->path, out) == 0) {
            out->mode = f->mode;
            vfs_stat_finalize(out, f->inode_dev, f->inode_ino);
            file_release(f);
            return 0;
        }
    }
    if (f->backend == VFS_BACKEND_DEVFS && f->abs_path[0]) {
        int rc = vfs_stat_common(f->abs_path, 1, out);
        if (rc == 0) {
            out->mode = f->mode;
            file_release(f);
            return 0;
        }
    }

    size_t sz = 0;
    if (f->backend == VFS_BACKEND_PIPE) {
        if (f->u.pipe.pipe) {
            unsigned long pflags = spin_lock_irqsave(&f->u.pipe.pipe->lock);
            sz = f->u.pipe.pipe->size;
            spin_unlock_irqrestore(&f->u.pipe.pipe->lock, pflags);
        }
        out->type = VFS_TYPE_PIPE;
    } else if (f->backend == VFS_BACKEND_UNIXSOCK) {
        if (!f->u.sock.is_listener && f->u.sock.rx)
            sz = unix_ring_avail(f->u.sock.rx);
        out->type = VFS_TYPE_SOCKET;
    } else if (f->backend == VFS_BACKEND_DEVFS) {
        sz = (f->u.device.ops && f->u.device.ops->size) ? f->u.device.ops->size(f->u.device.priv) : 0;
        out->type = VFS_TYPE_CHAR;
    } else if (f->is_dir) {
        out->type = VFS_TYPE_DIR;
    } else if (f->backend == VFS_BACKEND_FAT32 || f->backend == VFS_BACKEND_FAT32VOL) {
        fat32_dirent_t de;
        sz = (f->u.fat32.vol && fat32_vol_stat(f->u.fat32.vol, f->path, &de) == 0)
                 ? de.size
                 : 0;
        out->type = VFS_TYPE_FILE;
    } else {
        sz = f->u.regular.size;
        out->type = VFS_TYPE_FILE;
    }

    out->size = sz;
    out->blocks = (sz + 511) / 512;
    out->mode = f->mode;
    vfs_stat_finalize(out, f->inode_dev, f->inode_ino ? f->inode_ino : (uint64_t)(uintptr_t)f);
    file_release(f);
    return 0;
}

int vfs_getcwd(char *buf, size_t size)
{
    process_t *proc = vfs_current_process();
    char *cwd = cwd_for_process(proc);
    if (!buf || size == 0 || !cwd)
        return -EINVAL;
    return kstrncpy(buf, cwd, (int)size);
}

int vfs_chdir(const char *path)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    vfs_stat_t st;
    char *cwd;
    int rc;

    if (!path)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, path, 1, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    rc = backend_stat(m, sub, &st);
    mount_unref(m);
    if (rc != 0)
        return rc;
    if (st.type != VFS_TYPE_DIR)
        return -ENOTDIR;

    cwd = cwd_for_process(proc);
    if (!cwd)
        return -EINVAL;
    return kstrncpy(cwd, abs, VFS_PATH_MAX);
}

static int backend_list_dir(const char *abs, const vfs_mount_t *m, const char *sub,
                            char names[][VFS_NAME_MAX], int max)
{
    int count = 0;
    switch (m->backend) {
    case VFS_BACKEND_TMPFS:
        count = tmpfs_readdir((tmpfs_fs_t *)m->fs_private, sub, names, max);
        if (count < 0)
            return count;
        break;
    case VFS_BACKEND_EXT4:
        count = ext4fs_readdir((ext4fs_t *)m->fs_private, sub, names, max);
        if (count < 0)
            return count;
        break;
    case VFS_BACKEND_FAT32:
    case VFS_BACKEND_FAT32VOL:
    {
        int cap = (max < FAT32_MAX_DIRENT) ? max : FAT32_MAX_DIRENT;
        fat32_dirent_t *entries = (fat32_dirent_t *)kmalloc((size_t)cap * sizeof(fat32_dirent_t));
        if (!entries)
            return -ENOMEM;
        fat32_volume_t *vol = (m->backend == VFS_BACKEND_FAT32)
                                  ? fat32_primary_vol()
                                  : (fat32_volume_t *)m->fs_private;
        int n = fat32_vol_list_dir(vol, sub, entries, cap);
        if (n < 0) {
            kfree(entries);
            return -ENOENT;
        }
        for (int i = 0; i < n && count < max; i++) {
            kstrncpy(names[count], entries[i].name, VFS_NAME_MAX);
            count++;
        }
        kfree(entries);
        break;
    }
    case VFS_BACKEND_PROCFS:
        count = procfs_list(sub, names, max);
        if (count < 0)
            return -ENOENT;
        break;
    case VFS_BACKEND_SYSFS:
        count = sysfs_list(sub, names, max);
        if (count < 0)
            return -ENOENT;
        break;
    case VFS_BACKEND_BOOTFS:
        count = bootfs_list(sub, names, max);
        if (count < 0)
            return -ENOENT;
        break;
    case VFS_BACKEND_DEVFS:
        count = devfs_list(sub, names, max);
        if (count < 0)
            return -ENOENT;
        break;
    case VFS_BACKEND_INITRD:
        if (!kstreq(sub, "/"))
            return -ENOTDIR;
        if (g_initrd_ok) {
            kstrncpy(names[0], "initrd", VFS_NAME_MAX);
            count = 1;
        } else {
            count = 0;
        }
        break;
    case VFS_BACKEND_TAR:
        count = tar_list_dir((tar_fs_t *)m->fs_private, sub, names, max);
        if (count < 0)
            return -ENOENT;
        break;
    default:
        return -ENOENT;
    }

    if (kstreq(abs, "/"))
        count = append_root_mount_names(names, count, max);
    return count;
}

int vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max)
{
    process_t *proc = vfs_current_process();
    vfs_mount_t *m = NULL;
    char abs[VFS_PATH_MAX];
    char sub[VFS_PATH_MAX];
    int exists = 0;
    uint32_t type = VFS_TYPE_UNKNOWN;
    int rc;

    if (!dir || !names || max <= 0)
        return -EINVAL;
    rc = vfs_resolve_path_ex(proc, dir, 1, abs, &m, sub, &exists, &type);
    if (rc != 0 || !m)
        return (rc < 0) ? rc : -ENOENT;
    if (!exists) {
        mount_unref(m);
        return -ENOENT;
    }
    int count = backend_list_dir(abs, m, sub, names, max);
    mount_unref(m);
    return (count < 0) ? -1 : count;
}

static uint8_t vfs_type_to_dt(uint32_t vtype)
{
    switch (vtype) {
    case VFS_TYPE_DIR:     return DT_DIR;
    case VFS_TYPE_FILE:    return DT_REG;
    case VFS_TYPE_SYMLINK: return DT_LNK;
    case VFS_TYPE_CHAR:    return DT_CHR;
    case VFS_TYPE_BLOCK:   return DT_BLK;
    case VFS_TYPE_PIPE:    return DT_FIFO;
    case VFS_TYPE_SOCKET:  return DT_SOCK;
    default:               return DT_UNKNOWN;
    }
}

long vfs_getdents64(int fd, void *dirp, size_t count)
{
    process_t *proc = vfs_current_process();
    if (!dirp)
        return -EFAULT;
    if (count == 0)
        return -EINVAL;

    vfs_file_t *f = fd_lookup_ref(proc, fd);
    if (!f)
        return -EBADF;
    if (!f->is_dir) {
        file_release(f);
        return -ENOTDIR;
    }

    const int max_entries = 128;
    char (*names)[VFS_NAME_MAX] = (char (*)[VFS_NAME_MAX])kmalloc((size_t)max_entries * VFS_NAME_MAX);
    if (!names) {
        file_release(f);
        return -ENOMEM;
    }

    kstrncpy(names[0], ".", VFS_NAME_MAX);
    kstrncpy(names[1], "..", VFS_NAME_MAX);

    vfs_mount_t *m = mount_lookup_ref(f->abs_path);
    if (!m) {
        kfree(names);
        file_release(f);
        return -ENOENT;
    }
    char sub[VFS_PATH_MAX];
    if (mount_subpath(m, f->abs_path, sub, VFS_PATH_MAX) != 0) {
        mount_unref(m);
        kfree(names);
        file_release(f);
        return -EINVAL;
    }
    int bcnt = backend_list_dir(f->abs_path, m, sub, names + 2, max_entries - 2);
    mount_unref(m);
    if (bcnt < 0)
        bcnt = 0;
    int total = 2 + bcnt;

    unsigned long fflags = spin_lock_irqsave(&f->lock);
    size_t start_idx = f->pos;
    spin_unlock_irqrestore(&f->lock, fflags);

    size_t written = 0;
    size_t cur_idx = start_idx;
    uint8_t *out_bytes = (uint8_t *)dirp;

    for (size_t idx = start_idx; idx < (size_t)total; idx++) {
        int nlen = kstrlen(names[idx]);
        size_t hdr = offsetof(struct linux_dirent64, d_name);
        size_t reclen = (hdr + (size_t)nlen + 1U + 7U) & ~(size_t)7U;
        if (written + reclen > count) {
            if (written == 0) {
                kfree(names);
                file_release(f);
                return -EINVAL;
            }
            break;
        }

        uint64_t dino = f->inode_ino ? f->inode_ino : 1ULL;
        uint8_t dtype = DT_DIR;
        if (idx >= 2U) {
            char child_abs[VFS_PATH_MAX];
            vfs_stat_t cst;
            if (path_copy_join(child_abs, VFS_PATH_MAX, f->abs_path, names[idx]) == 0 &&
                vfs_lstat(child_abs, &cst) == 0) {
                dtype = vfs_type_to_dt(cst.type);
                dino = cst.st_ino ? cst.st_ino : vfs_hash_str(child_abs);
            } else {
                dtype = DT_UNKNOWN;
                dino = (uint64_t)(idx + 1U);
            }
        }

        for (size_t z = 0; z < reclen; z++)
            out_bytes[written + z] = 0;

        struct linux_dirent64 *de = (struct linux_dirent64 *)(out_bytes + written);
        de->d_ino = dino;
        de->d_off = (int64_t)(idx + 1U);
        de->d_reclen = (uint16_t)reclen;
        de->d_type = dtype;
        for (int c = 0; c <= nlen; c++)
            de->d_name[c] = names[idx][c];

        written += reclen;
        cur_idx = idx + 1U;
    }

    fflags = spin_lock_irqsave(&f->lock);
    f->pos = cur_idx;
    spin_unlock_irqrestore(&f->lock, fflags);

    kfree(names);
    file_release(f);
    return (long)written;
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

    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    for (int i = 0; i < PROCESS_MAX_OPEN_FILES; i++) {
        vfs_file_t *f = (vfs_file_t *)src_tbl[i];
        if (!f || f == (vfs_file_t *)(uintptr_t)1)
            continue;
        file_ref(f);
        dst_tbl[i] = f;
    }
    spin_unlock_irqrestore(&g_fd_lock, flags);
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

    unsigned long flags = spin_lock_irqsave(&g_fd_lock);
    f = (vfs_file_t *)src_tbl[src_fd];
    if (!f || f == (vfs_file_t *)(uintptr_t)1) {
        spin_unlock_irqrestore(&g_fd_lock, flags);
        return -1;
    }
    file_ref(f);
    spin_unlock_irqrestore(&g_fd_lock, flags);

    fd_close_on_table(dst_tbl, dst_fd);

    flags = spin_lock_irqsave(&g_fd_lock);
    vfs_file_t *old = (vfs_file_t *)dst_tbl[dst_fd];
    dst_tbl[dst_fd] = f;
    spin_unlock_irqrestore(&g_fd_lock, flags);
    if (old && old != (vfs_file_t *)(uintptr_t)1)
        file_release(old);
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
            if (bd->parent->type == BLOCKDEV_TYPE_NVME) {
                if (bd->name[pl] != 'p' || bd->name[pl + 1] < '1' ||
                    bd->name[pl + 1] > '9')
                    ok = 0;
            } else {
                if (bd->name[pl] < '1' || bd->name[pl] > '9')
                    ok = 0;
            }
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
                mnt[0] = '/'; mnt[1] = 'm'; mnt[2] = 'n'; mnt[3] = 't';
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
                kprintf("[guide12] planted-file r/w via /mnt/%s PASS\n",
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

void vfs_run_guide13_selftests(void)
{
    int pass = 0, fail = 0;

    kprintf("[guide13][test] start\n");

    {
        static const char *const mods[] = {
            "/bootblob.bin", "/tsukasa_x64.elf", "/initrd.tar.lz4",
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
