/*
 * vfs.c - Virtual File System core with per-process FD tables.
 */

#include "vfs.h"

#include "bootfs.h"
#include "fat12.h"
#include "fat32.h"
#include "initrd.h"
#include "memfs.h"
#include "procfs.h"
#include "sysfs.h"

#include "../drv/ata.h"
#include "../drv/fb.h"
#include "../include/boot_info.h"
#include "../include/kprintf.h"
#include "../include/multiboot.h"
#include "../mm/heap.h"
#include "../proc/process.h"

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS        13
#define VFS_MAX_OPEN_GLOBAL   128
#define VFS_MAX_PIPES         16
#define VFS_PIPE_CAPACITY     4096

typedef enum vfs_backend {
    VFS_BACKEND_NONE = 0,
    VFS_BACKEND_INITRD,
    VFS_BACKEND_FAT12,
    VFS_BACKEND_MEMFS,
    VFS_BACKEND_FAT32,
    VFS_BACKEND_PROCFS,
    VFS_BACKEND_SYSFS,
    VFS_BACKEND_BOOTFS,
    VFS_BACKEND_DEVFS,
    VFS_BACKEND_PIPE
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
} vfs_mount_t;

typedef struct vfs_pipe {
    int used;
    uint8_t data[VFS_PIPE_CAPACITY];
    size_t read_pos;
    size_t write_pos;
    size_t size;
    int readers;
    int writers;
} vfs_pipe_t;

typedef struct vfs_file {
    int used;
    int refcount;
    int flags;
    int mode;
    int dirty;
    vfs_backend_t backend;
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
            vfs_device_kind_t kind;
            int index;
        } device;
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
static int g_kd_mode = VFS_KD_TEXT;

/* --------------------------------------------------------------------- */
/* String/path helpers                                                   */
/* --------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------- */
/* Mount table                                                           */
/* --------------------------------------------------------------------- */

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

static int mount_register(const char *path, vfs_backend_t backend, int read_only, const char *name)
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
            return 0;
        }
    }
    return -1;
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

/* --------------------------------------------------------------------- */
/* Open-file / pipe internals                                            */
/* --------------------------------------------------------------------- */

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
            return p;
        }
    }
    return NULL;
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
        if (p->readers == 0 && p->writers == 0)
            p->used = 0;
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

/* --------------------------------------------------------------------- */
/* devfs                                                                 */
/* --------------------------------------------------------------------- */

static const char *g_dev_names[] = {
    "fb0",
    "tty",
    "tty0",
    "tty1",
    "tty2",
    "tty3",
    "tty4",
    "tty5",
    "tty6",
    "tty7",
    "tty8",
    "tty9",
    "keyboard",
    "mouse"
};

static size_t fb_byte_size(void)
{
    if (!fb_info.addr || fb_info.pitch == 0 || fb_info.height == 0)
        return 0;
    return (size_t)fb_info.pitch * (size_t)fb_info.height;
}

static vfs_device_kind_t dev_lookup(const char *subpath, int *index_out)
{
    const char *name = subpath;
    if (index_out)
        *index_out = 0;
    if (!name)
        return VFS_DEV_NONE;
    while (*name == '/')
        name++;
    if (kstreq(name, "fb0")) {
        if (index_out)
            *index_out = 0;
        return VFS_DEV_FB0;
    }
    if (kstreq(name, "tty")) {
        if (index_out)
            *index_out = -1;
        return VFS_DEV_TTY;
    }
    if (name[0] == 't' && name[1] == 't' && name[2] == 'y' &&
        name[3] >= '0' && name[3] <= '9' && name[4] == '\0') {
        if (index_out)
            *index_out = name[3] - '0';
        return VFS_DEV_TTY;
    }
    if (kstreq(name, "keyboard"))
        return VFS_DEV_KEYBOARD;
    if (kstreq(name, "mouse"))
        return VFS_DEV_MOUSE;
    return VFS_DEV_NONE;
}

static int dev_stat(const char *subpath, vfs_stat_t *out)
{
    vfs_device_kind_t kind;
    if (!subpath || !out)
        return -1;
    if (kstreq(subpath, "/")) {
        out->size = 0;
        out->blocks = 0;
        out->type = VFS_TYPE_DIR;
        out->mode = VFS_MODE_READ;
        return 0;
    }
    kind = dev_lookup(subpath, NULL);
    if (kind == VFS_DEV_NONE)
        return -1;
    out->size = (kind == VFS_DEV_FB0) ? fb_byte_size() : 0;
    out->blocks = (out->size + 511) / 512;
    out->type = VFS_TYPE_CHAR;
    out->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    return 0;
}

static int dev_list(const char *subpath, char names[][VFS_NAME_MAX], int max)
{
    int count = 0;
    if (!subpath || !names || max <= 0 || !kstreq(subpath, "/"))
        return -1;
    for (size_t i = 0; i < sizeof(g_dev_names) / sizeof(g_dev_names[0]) && count < max; i++) {
        kstrncpy(names[count], g_dev_names[i], VFS_NAME_MAX);
        count++;
    }
    return count;
}

static int dev_fill_fb_var(vfs_fb_var_screeninfo_t *out)
{
    if (!out || !fb_info.addr)
        return -1;
    out->xres = fb_info.width;
    out->yres = fb_info.height;
    out->xres_virtual = fb_info.width;
    out->yres_virtual = fb_info.height;
    out->bits_per_pixel = fb_info.bpp;
    return 0;
}

static int dev_fill_fb_fix(vfs_fb_fix_screeninfo_t *out)
{
    const char id[] = "tsukasa-fb";
    if (!out || !fb_info.addr)
        return -1;
    for (size_t i = 0; i < sizeof(out->id); i++)
        out->id[i] = '\0';
    for (size_t i = 0; i < sizeof(id) && i < sizeof(out->id) - 1; i++)
        out->id[i] = id[i];
    out->smem_start = (uintptr_t)fb_info.addr;
    out->smem_len = (uint32_t)fb_byte_size();
    out->type = VFS_FB_TYPE_PACKED_PIXELS;
    out->visual = VFS_FB_VISUAL_TRUECOLOR;
    out->line_length = fb_info.pitch;
    return 0;
}

/* --------------------------------------------------------------------- */
/* Backend stat/list/read helpers                                        */
/* --------------------------------------------------------------------- */

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
        return dev_stat(subpath, out);
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

/* --------------------------------------------------------------------- */
/* Public init                                                           */
/* --------------------------------------------------------------------- */

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
    bootfs_init(boot_info);

    g_fat12_ok = 0;
    if (tsukasa_boot_info_is_valid(boot_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)boot_info;
        if (bi->module_count > 0 && bi->modules) {
            void *disk = (void *)(uintptr_t)bi->modules[0].address;
            size_t sz = (size_t)bi->modules[0].size;
            if (fat12_init(disk, sz) == 0)
                g_fat12_ok = 1;
        }
    } else if (mb && (mb->flags & 8) && mb->mods_count > 0) {
        const struct multiboot_mod_list *mod =
            (const struct multiboot_mod_list *)(uintptr_t)mb->mods_addr;
        void *disk = (void *)(uintptr_t)mod[0].mod_start;
        size_t sz = mod[0].mod_end - mod[0].mod_start;
        if (fat12_init(disk, sz) == 0)
            g_fat12_ok = 1;
    }
    kprintf("[vfs] FAT12 ramdisk: %s\n", g_fat12_ok ? "ok" : "not found");

    g_fat32_ok = 0;
    if (ata_init()) {
        if (fat32_init() == 0)
            g_fat32_ok = 1;
        kprintf("[vfs] FAT32 /disk: %s\n", g_fat32_ok ? "ok" : "not found");
    } else {
        kprintf("[ata] no drive detected\n");
    }

    initrd_init_from_multiboot(boot_info);
    g_initrd_ok = (initrd_lookup("/", &initrd_data, &initrd_size) == 0) ? 1 : 0;

    /* Root no longer depends on FAT12. Prefer bootfs, then initrd, then memfs. */
    if (bootfs_module_count() > 0) {
        mount_register("/", VFS_BACKEND_BOOTFS, 1, "bootfs");
    } else if (g_initrd_ok) {
        mount_register("/", VFS_BACKEND_INITRD, 1, "initrd");
    } else {
        mount_register("/", VFS_BACKEND_MEMFS, 0, "memfs");
    }

    /* FAT12 is now optional compatibility mount. */
    if (g_fat12_ok)
        mount_register("/fat12", VFS_BACKEND_FAT12, 0, "fat12");
    mount_register("/tmp", VFS_BACKEND_MEMFS, 0, "memfs");
    if (g_fat32_ok)
        mount_register("/disk", VFS_BACKEND_FAT32, 0, "fat32");
    mount_register("/proc", VFS_BACKEND_PROCFS, 1, "procfs");
    mount_register("/sys", VFS_BACKEND_SYSFS, 1, "sysfs");
    mount_register("/dev", VFS_BACKEND_DEVFS, 0, "devfs");
    if (bootfs_module_count() > 0)
        mount_register("/boot", VFS_BACKEND_BOOTFS, 1, "bootfs");
}

/* --------------------------------------------------------------------- */
/* Open/create/close                                                     */
/* --------------------------------------------------------------------- */

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

static int open_regular_buffered(vfs_file_t *f, vfs_backend_t backend, const char *subpath, int flags)
{
    size_t file_size = 0;
    uint8_t *buf = NULL;
    int read_bytes = 0;

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
        if (file_size > 0) {
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
    } else {
        return -1;
    }

    f->backend = backend;
    f->u.regular.buf = buf;
    f->u.regular.size = file_size;
    f->u.regular.capacity = file_size;
    f->u.regular.owns_buf = 1;
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
        if (open_regular_buffered(f, m->backend, sub, flags) != 0) {
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
        int dev_index = 0;
        vfs_device_kind_t kind = dev_lookup(sub, &dev_index);
        if (kind == VFS_DEV_NONE) {
            file_release(f);
            return -1;
        }
        f->backend = VFS_BACKEND_DEVFS;
        f->mode = flags_to_mode(flags);
        f->u.device.kind = kind;
        f->u.device.index = dev_index;
        f->pos = 0;
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

void vfs_close(int fd)
{
    process_t *proc = vfs_current_process();
    void **tbl = fd_table_for_process(proc);
    fd_close_on_table(tbl, fd);
}

/* --------------------------------------------------------------------- */
/* Read/write/seek                                                       */
/* --------------------------------------------------------------------- */

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
        size_t got = 0;
        uint8_t *dst = (uint8_t *)buf;
        if (!p || !f->u.pipe.can_read)
            return 0;
        while (got < count && p->size > 0) {
            dst[got++] = p->data[p->read_pos];
            p->read_pos = (p->read_pos + 1) % VFS_PIPE_CAPACITY;
            p->size--;
        }
        return got;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        size_t size = fb_byte_size();
        uint8_t *src = (uint8_t *)fb_info.addr;
        if (f->u.device.kind != VFS_DEV_FB0 || !src || f->pos >= size)
            return 0;
        if (count > size - f->pos)
            count = size - f->pos;
        for (size_t i = 0; i < count; i++)
            ((uint8_t *)buf)[i] = src[f->pos + i];
        f->pos += count;
        return count;
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
        const uint8_t *src = (const uint8_t *)buf;
        size_t wr = 0;
        if (!p || !f->u.pipe.can_write || p->readers == 0)
            return 0;
        while (wr < count && p->size < VFS_PIPE_CAPACITY) {
            p->data[p->write_pos] = src[wr++];
            p->write_pos = (p->write_pos + 1) % VFS_PIPE_CAPACITY;
            p->size++;
        }
        return wr;
    }

    if (f->backend == VFS_BACKEND_DEVFS) {
        size_t size = fb_byte_size();
        uint8_t *dst = (uint8_t *)fb_info.addr;
        if (f->u.device.kind != VFS_DEV_FB0 || !dst || f->pos >= size)
            return 0;
        if (count > size - f->pos)
            count = size - f->pos;
        for (size_t i = 0; i < count; i++)
            dst[f->pos + i] = ((const uint8_t *)buf)[i];
        f->pos += count;
        return count;
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
    if (f->backend == VFS_BACKEND_PIPE)
        return (size_t)-1;

    if (f->backend == VFS_BACKEND_MEMFS)
        size = memfs_size(f->u.memfs.inode);
    else if (f->backend == VFS_BACKEND_DEVFS)
        size = (f->u.device.kind == VFS_DEV_FB0) ? fb_byte_size() : 0;
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
    if (f->backend == VFS_BACKEND_DEVFS)
        return (f->u.device.kind == VFS_DEV_FB0) ? fb_byte_size() : 0;
    return f->u.regular.size;
}

/* --------------------------------------------------------------------- */
/* dup/dup2/pipe/fcntl                                                   */
/* --------------------------------------------------------------------- */

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
    tbl[fd_r] = (void *)1; /* reserve */
    fd_w = process_fd_alloc(proc);
    if (fd_w < 0) {
        tbl[fd_r] = NULL;
        return -1;
    }
    tbl[fd_w] = (void *)1; /* reserve */

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

    if (request == VFS_KDSETMODE) {
        uintptr_t raw = (uintptr_t)arg;
        int mode = (raw <= 0xFFFFu) ? (int)raw : *(int *)arg;
        if (mode != VFS_KD_TEXT && mode != VFS_KD_GRAPHICS)
            return -1;
        g_kd_mode = mode;
        return 0;
    }

    if (!f || f->backend != VFS_BACKEND_DEVFS || f->u.device.kind != VFS_DEV_FB0)
        return -1;

    switch (request) {
    case VFS_FBIOGET_VSCREENINFO:
        return dev_fill_fb_var((vfs_fb_var_screeninfo_t *)arg);
    case VFS_FBIOGET_FSCREENINFO:
        return dev_fill_fb_fix((vfs_fb_fix_screeninfo_t *)arg);
    default:
        return -1;
    }
}

void *vfs_mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset)
{
    process_t *proc = vfs_current_process();
    vfs_file_t *f = fd_lookup(proc, fd);
    size_t size;
    (void)addr;

    if (!f || length == 0)
        return (void *)-1;
    if (!(flags & VFS_MAP_SHARED) || !(prot & (VFS_PROT_READ | VFS_PROT_WRITE)))
        return (void *)-1;
    if (f->backend != VFS_BACKEND_DEVFS || f->u.device.kind != VFS_DEV_FB0)
        return (void *)-1;

    size = fb_byte_size();
    if (!fb_info.addr || offset >= size || length > size - offset)
        return (void *)-1;
    return (void *)((uintptr_t)fb_info.addr + offset);
}

int vfs_munmap(void *addr, size_t length)
{
    uintptr_t p = (uintptr_t)addr;
    uintptr_t fb = (uintptr_t)fb_info.addr;
    size_t size = fb_byte_size();
    if (!addr || length == 0 || !fb_info.addr)
        return -1;
    if (p < fb || p >= fb + size)
        return -1;
    if (length > (fb + size) - p)
        return -1;
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

    if (f->backend == VFS_BACKEND_DEVFS) {
        if (f->u.device.kind == VFS_DEV_FB0 && fb_info.addr) {
            if (f->mode & VFS_MODE_READ)
                mask |= VFS_POLLIN;
            if (f->mode & VFS_MODE_WRITE)
                mask |= VFS_POLLOUT;
        } else if (f->mode & VFS_MODE_WRITE) {
            mask |= VFS_POLLOUT;
        }
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

/* --------------------------------------------------------------------- */
/* stat/fstat/cwd/list                                                   */
/* --------------------------------------------------------------------- */

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
        count = dev_list(sub, names, max);
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

/* --------------------------------------------------------------------- */
/* Process teardown hook                                                 */
/* --------------------------------------------------------------------- */

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
