/*
 * vfs.c  -  Virtual File System implementation.
 *
 * Mount routing:
 *   /tmp/<name>    → memfs   (writable, volatile)
 *   /<name>        → FAT12   (r/w, in-RAM image)
 *   /initrd        → legacy  (read-only fallback if FAT12 not present)
 */

#include "vfs.h"
#include "initrd.h"
#include "fat12.h"
#include "fat32.h"
#include "memfs.h"
#include "../include/multiboot.h"
#include "../include/boot_info.h"
#include "../drv/ata.h"
#include "../include/kprintf.h"
#include <stddef.h>
#include <stdint.h>

extern void *kmalloc(size_t);
extern void  kfree(void *);

/* ---- File descriptor table ------------------------------------------- */

/* Backend tag for each open fd. */
typedef enum {
    FD_NONE        = 0,
    FD_INITRD      = 1,   /* legacy read-only fallback */
    FD_FAT12       = 2,   /* FAT12 ramdisk             */
    FD_MEMFS       = 3,   /* in-memory writable fs     */
    FD_FAT32_DISK  = 4,   /* FAT32 ATA hard disk       */
} fd_backend_t;

typedef struct {
    fd_backend_t backend;
    int          used;

    /* For FD_INITRD: simple data-pointer + size. */
    const void  *data;
    size_t       size;
    size_t       pos;

    /* For FD_FAT12: path copy (for write). */
    char         fat12_path[VFS_NAME_MAX];
    /* Buffer holding the entire file content (for read/seek). */
    uint8_t     *fat12_buf;
    size_t       fat12_size;

    /* For FD_FAT32_DISK: same layout as FAT12 (buffered read). */
    char         fat32_path[VFS_NAME_MAX];
    uint8_t     *fat32_buf;
    size_t       fat32_size;

    /* For FD_MEMFS: inode index. */
    int          memfs_inode;
    size_t       memfs_pos;
} vfs_fd_t;

static vfs_fd_t fd_table[VFS_MAX_FD];

/* Whether FAT12 / FAT32 were successfully initialised. */
static int g_fat12_ok = 0;
static int g_fat32_ok = 0;

/* ---- Path helpers ----------------------------------------------------- */

/* Returns 1 if path starts with /tmp/ (or equals /tmp). */
static int is_tmp(const char *path)
{
    return (path[0]=='/' && path[1]=='t' && path[2]=='m' && path[3]=='p' &&
            (path[4]=='/' || path[4]=='\0'));
}

/* Strip /tmp/ prefix. Returns pointer to bare name. */
static const char *tmp_name(const char *path)
{
    /* skip /tmp */
    path += 4;
    while (*path == '/') path++;
    return path;
}

/* Returns 1 if path starts with /disk/ (FAT32 ATA volume). */
static int is_disk(const char *path)
{
    return (path[0]=='/' && path[1]=='d' && path[2]=='i' &&
            path[3]=='s' && path[4]=='k' &&
            (path[5]=='/' || path[5]=='\0'));
}

/* Strip /disk/ prefix for FAT32 sub-path. */
static const char *disk_subpath(const char *path)
{
    path += 5;  /* skip /disk */
    if (*path == '/') path++;
    return path;
}

/* Find a free fd slot. */
static int alloc_fd(void)
{
    for (int i = 0; i < VFS_MAX_FD; i++)
        if (!fd_table[i].used) return i;
    return -1;
}

/* ---- Initialisation --------------------------------------------------- */

void vfs_init(const void *mb_info)
{
    for (int i = 0; i < VFS_MAX_FD; i++) {
        fd_table[i].used      = 0;
        fd_table[i].fat12_buf = NULL;
        fd_table[i].fat32_buf = NULL;
    }

    memfs_init();

    /* Attempt FAT12 init from the first boot module. */
    const struct multiboot_info *mb = (const struct multiboot_info *)mb_info;
    g_fat12_ok = 0;
    if (tsukasa_boot_info_is_valid(mb_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)mb_info;
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

    /* Probe ATA primary master for a FAT32 volume at /disk/. */
    g_fat32_ok = 0;
    if (ata_init()) {
        kprintf("[ata] drive detected, %u sectors\n", ata_sector_count());
        if (fat32_init() == 0) {
            g_fat32_ok = 1;
            kprintf("[vfs] /disk/ mounted (FAT32)\n");
        } else {
            kprintf("[vfs] /disk/ not FAT32, skipping\n");
        }
    } else {
        kprintf("[ata] no drive detected\n");
    }

    /* Legacy initrd fallback. */
    initrd_init_from_multiboot(mb_info);
}

/* ---- vfs_open --------------------------------------------------------- */

int vfs_open(const char *path)
{
    if (!path) return -1;

    int fd = alloc_fd();
    if (fd < 0) return -1;
    vfs_fd_t *f = &fd_table[fd];

    if (is_tmp(path)) {
        /* ---- memfs ---- */
        const char *name = tmp_name(path);
        int inode = memfs_open(name);
        if (inode < 0) return -1;
        f->backend     = FD_MEMFS;
        f->used        = 1;
        f->memfs_inode = inode;
        f->memfs_pos   = 0;
        return fd;
    }

    if (g_fat32_ok && is_disk(path)) {
        /* ---- FAT32 ATA disk ---- */
        const char *sub = disk_subpath(path);
        /* Treat an empty sub-path as root of disk. */
        const char *fat32_path = (sub[0] == '\0') ? "/" : sub;
        fat32_dirent_t de;
        if (fat32_stat(fat32_path, &de) == 0 && !de.is_dir) {
            uint8_t *buf = NULL;
            size_t   fsz = de.size;
            if (fsz > 0) {
                buf = (uint8_t *)kmalloc(fsz);
                if (!buf) return -1;
                int got = fat32_read_file(fat32_path, buf, fsz);
                if (got < 0) got = 0;
                fsz = (size_t)got;
            }
            int pi = 0;
            while (fat32_path[pi] && pi < VFS_NAME_MAX - 1) {
                f->fat32_path[pi] = fat32_path[pi];
                pi++;
            }
            f->fat32_path[pi] = '\0';
            f->backend    = FD_FAT32_DISK;
            f->used       = 1;
            f->fat32_buf  = buf;
            f->fat32_size = fsz;
            f->pos        = 0;
            return fd;
        }
        return -1;  /* file not found on disk */
    }

    if (g_fat12_ok) {
        /* ---- FAT12 ---- */
        /* Read the whole file into a buffer so we can seek freely. */
        fat12_dirent_t de;
        if (fat12_stat(path, &de) == 0 && !de.is_dir) {
            uint8_t *buf = NULL;
            size_t   fsz = de.size;
            if (fsz > 0) {
                buf = (uint8_t *)kmalloc(fsz);
                if (!buf) return -1;
                int got = fat12_read_file(path, buf, fsz);
                if (got < 0) got = 0;
                fsz = (size_t)got;
            }
            /* Store path for write-back. */
            int pi = 0;
            while (path[pi] && pi < VFS_NAME_MAX - 1) {
                f->fat12_path[pi] = path[pi]; pi++;
            }
            f->fat12_path[pi] = '\0';

            f->backend    = FD_FAT12;
            f->used       = 1;
            f->fat12_buf  = buf;
            f->fat12_size = fsz;
            f->pos        = 0;
            return fd;
        }
    }

    /* ---- Legacy initrd fallback ---- */
    {
        const void *data; size_t size;
        if (initrd_lookup(path, &data, &size) == 0) {
            f->backend = FD_INITRD;
            f->used    = 1;
            f->data    = data;
            f->size    = size;
            f->pos     = 0;
            return fd;
        }
    }

    return -1;
}

/* ---- vfs_create ------------------------------------------------------- */

int vfs_create(const char *path)
{
    if (!path) return -1;
    int fd = alloc_fd();
    if (fd < 0) return -1;
    vfs_fd_t *f = &fd_table[fd];

    if (is_tmp(path)) {
        const char *name = tmp_name(path);
        int inode = memfs_create(name);
        if (inode < 0) return -1;
        /* Truncate. */
        memfs_write(inode, 0, NULL, 0);   /* size stays 0 */
        f->backend     = FD_MEMFS;
        f->used        = 1;
        f->memfs_inode = inode;
        f->memfs_pos   = 0;
        return fd;
    }

    /* FAT12: create empty file then open it. */
    if (g_fat12_ok) {
        fat12_write_file(path, "", 0);
        return vfs_create(path);   /* should now hit vfs_open via fat12_stat */
    }

    return -1;
}

/* ---- vfs_read --------------------------------------------------------- */

size_t vfs_read(int fd, void *buf, size_t count)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].used || !buf)
        return 0;
    vfs_fd_t *f = &fd_table[fd];

    if (f->backend == FD_MEMFS)
        return memfs_read(f->memfs_inode, f->memfs_pos, buf, count);

    /* FD_INITRD, FD_FAT12, FD_FAT32_DISK — all use a buffered data/size/pos. */
    const uint8_t *src;
    size_t         sz;
    if (f->backend == FD_INITRD) {
        src = (const uint8_t *)f->data;
        sz  = f->size;
    } else if (f->backend == FD_FAT32_DISK) {
        src = f->fat32_buf;
        sz  = f->fat32_size;
    } else {
        src = f->fat12_buf;
        sz  = f->fat12_size;
    }
    if (f->pos >= sz) return 0;
    if (count > sz - f->pos) count = sz - f->pos;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) dst[i] = src[f->pos + i];
    f->pos += count;
    return count;
}

/* ---- vfs_write -------------------------------------------------------- */

size_t vfs_write(int fd, const void *buf, size_t count)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].used)
        return 0;
    vfs_fd_t *f = &fd_table[fd];

    if (f->backend == FD_MEMFS) {
        size_t written = memfs_write(f->memfs_inode, f->memfs_pos, buf, count);
        f->memfs_pos += written;
        return written;
    }

    if (f->backend == FD_FAT12 && g_fat12_ok && buf && count > 0) {
        /* Append to in-memory buffer then write-back to FAT12. */
        /* Simple: re-write the whole file from current accumulated buffer.
           For simplicity we write the passed buf directly at pos. */
        fat12_write_file(f->fat12_path, buf, count);
        f->pos += count;
        return count;
    }

    return 0;
}

/* ---- vfs_seek --------------------------------------------------------- */

size_t vfs_seek(int fd, size_t offset, int whence)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].used)
        return (size_t)-1;
    vfs_fd_t *f = &fd_table[fd];

    size_t *pos_ptr = NULL;
    size_t   sz     = 0;

    if (f->backend == FD_MEMFS) {
        pos_ptr = &f->memfs_pos;
        sz      = memfs_size(f->memfs_inode);
    } else {
        pos_ptr = &f->pos;
        if (f->backend == FD_INITRD)       sz = f->size;
        else if (f->backend == FD_FAT32_DISK) sz = f->fat32_size;
        else                               sz = f->fat12_size;
    }

    switch (whence) {
    case VFS_SEEK_SET: *pos_ptr = offset;        break;
    case VFS_SEEK_CUR: *pos_ptr += offset;       break;
    case VFS_SEEK_END: *pos_ptr = sz + offset;   break;
    default: return (size_t)-1;
    }
    if (*pos_ptr > sz) *pos_ptr = sz;
    return *pos_ptr;
}

/* ---- vfs_size --------------------------------------------------------- */

size_t vfs_size(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].used)
        return 0;
    vfs_fd_t *f = &fd_table[fd];
    if (f->backend == FD_MEMFS)       return memfs_size(f->memfs_inode);
    if (f->backend == FD_INITRD)       return f->size;
    if (f->backend == FD_FAT32_DISK)   return f->fat32_size;
    return f->fat12_size;
}

/* ---- vfs_close -------------------------------------------------------- */

void vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FD) return;
    vfs_fd_t *f = &fd_table[fd];
    if (!f->used) return;
    if (f->backend == FD_FAT12 && f->fat12_buf) {
        kfree(f->fat12_buf);
        f->fat12_buf = NULL;
    }
    if (f->backend == FD_FAT32_DISK && f->fat32_buf) {
        kfree(f->fat32_buf);
        f->fat32_buf = NULL;
    }
    f->used = 0;
}

/* ---- vfs_list --------------------------------------------------------- */

int vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max)
{
    if (!dir || !names || max <= 0) return -1;
    int count = 0;

    if (is_tmp(dir) || dir[1]=='t') {
        /* List memfs files. */
        int n = memfs_list(names, max);
        return n;
    }

    /* FAT32 ATA disk: /disk/ or /disk/subdir */
    if (g_fat32_ok && is_disk(dir)) {
        const char *sub = disk_subpath(dir);
        const char *fat32_dir = (sub[0] == '\0') ? "/" : sub;
        fat32_dirent_t entries[FAT32_MAX_DIRENT];
        int n = fat32_list_dir(fat32_dir, entries, FAT32_MAX_DIRENT);
        for (int i = 0; i < n && count < max; i++) {
            const char *src = entries[i].name;
            int j = 0;
            while (src[j] && j < VFS_NAME_MAX - 1) {
                names[count][j] = src[j];
                j++;
            }
            names[count][j] = '\0';
            count++;
        }
        return count;
    }

    /* FAT12 root directory. */
    if (g_fat12_ok) {
        fat12_dirent_t entries[FAT12_MAX_DIRENT];
        int n = fat12_list_dir("/", entries, FAT12_MAX_DIRENT);
        for (int i = 0; i < n && count < max; i++) {
            const char *src = entries[i].name;
            int j = 0;
            while (src[j] && j < VFS_NAME_MAX - 1) {
                names[count][j] = src[j]; j++;
            }
            names[count][j] = '\0';
            count++;
        }
        return count;
    }

    /* Fallback: single initrd entry. */
    if (count < max) {
        const char *n = "initrd";
        int j = 0;
        while (n[j]) { names[count][j] = n[j]; j++; }
        names[count][j] = '\0';
        count++;
    }
    return count;
}
