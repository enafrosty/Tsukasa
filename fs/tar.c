/*
 * Project Tsukasa — read-only USTAR (tar) filesystem backend
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

#include "tar.h"
#include "../mm/heap.h"   /* kmalloc / kfree */

struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

struct tar_fs {
    const uint8_t *base;
    uint64_t       size;
    tar_inode_t   *inodes;
    int            count;
};

/* Self-contained string helpers (no libc, no vfs.c statics) */

static int t_strlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int t_streq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int t_strneq(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
        if (a[i] == '\0')
            return 1;
    }
    return 1;
}

static void t_strncpy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint64_t tar_parse_octal(const char *str, int size)
{
    uint64_t result = 0;
    while (size-- > 0) {
        if (*str >= '0' && *str <= '7')
            result = (result << 3) + (uint64_t)(*str - '0');
        str++;
    }
    return result;
}

/* Build a normalized absolute path from a header, honoring the USTAR prefix extension (prefix + '/' + name)... */
static void tar_build_path(const struct tar_header *h, char *out, int cap,
                           int *dir_out)
{
    int o = 0;

    if (cap < 2) {
        if (cap > 0)
            out[0] = '\0';
        return;
    }
    out[o++] = '/';
    if (h->prefix[0]) {
        for (int i = 0; i < 155 && h->prefix[i] && o < cap - 1; i++)
            out[o++] = h->prefix[i];
        if (o < cap - 1)
            out[o++] = '/';
    }
    for (int i = 0; i < 100 && h->filename[i] && o < cap - 1; i++)
        out[o++] = h->filename[i];
    out[o] = '\0';

    while (o > 1 && out[o - 1] == '/') {
        out[--o] = '\0';
        if (dir_out)
            *dir_out = 1;
    }
}

/* When `out` is NULL, only counts indexable entries; otherwise fills up to `max` inodes. */
static int tar_walk(const uint8_t *base, uint64_t size, tar_inode_t *out,
                    int max)
{
    uint64_t off = 0;
    int n = 0;

    while (off + 512 <= size) {
        const struct tar_header *h = (const struct tar_header *)(base + off);
        uint64_t fsz;
        uint64_t data_blocks;
        uint64_t next;
        char typ;
        int is_dir = 0;

        if (h->filename[0] == '\0')
            break;

        fsz = tar_parse_octal(h->size, 11);
        data_blocks = (fsz + 511) / 512;
        next = off + 512 + data_blocks * 512;
        if (next < off || next > size)
            break;

        typ = h->typeflag;
        if (typ == '5')
            is_dir = 1;

        /* Index regular files ('0'/'\0') and directories ('5'); skip other typeflags (links, devices, fifos) for... */
        if (typ == '0' || typ == '\0' || typ == '5') {
            if (out) {
                if (n < max) {
                    tar_inode_t *e = &out[n];
                    tar_build_path(h, e->path, VFS_PATH_MAX, &is_dir);
                    e->is_dir = is_dir;
                    e->size = is_dir ? 0 : fsz;
                    e->data = is_dir ? (const uint8_t *)0 : (base + off + 512);
                    n++;
                }
            } else {
                n++;
            }
        }
        off = next;
    }
    return n;
}

tar_fs_t *tar_mount(const void *archive, uint64_t archive_size)
{
    const uint8_t *base = (const uint8_t *)archive;
    tar_fs_t *fs;
    int count;

    if (!base || archive_size < 512)
        return (tar_fs_t *)0;

    count = tar_walk(base, archive_size, (tar_inode_t *)0, 0);

    fs = (tar_fs_t *)kmalloc(sizeof(*fs));
    if (!fs)
        return (tar_fs_t *)0;
    fs->base = base;
    fs->size = archive_size;
    fs->inodes = (tar_inode_t *)0;
    fs->count = 0;

    if (count > 0) {
        fs->inodes = (tar_inode_t *)kmalloc((size_t)count * sizeof(tar_inode_t));
        if (!fs->inodes) {
            kfree(fs);
            return (tar_fs_t *)0;
        }
        fs->count = tar_walk(base, archive_size, fs->inodes, count);
    }
    return fs;
}

int tar_file_count(const tar_fs_t *fs)
{
    return fs ? fs->count : 0;
}

/* Normalize an incoming path into `out`: single leading '/', trailing '/' stripped (VFS subpaths already... */
static void tar_norm(const char *path, char *out, int cap)
{
    int o = 0;

    if (cap < 2) {
        if (cap > 0)
            out[0] = '\0';
        return;
    }
    if (!path || path[0] != '/')
        out[o++] = '/';
    for (int i = 0; path && path[i] && o < cap - 1; i++)
        out[o++] = path[i];
    out[o] = '\0';
    while (o > 1 && out[o - 1] == '/')
        out[--o] = '\0';
}

const tar_inode_t *tar_lookup(const tar_fs_t *fs, const char *path)
{
    char p[VFS_PATH_MAX];

    if (!fs || !path)
        return (const tar_inode_t *)0;
    tar_norm(path, p, VFS_PATH_MAX);
    for (int i = 0; i < fs->count; i++)
        if (t_streq(fs->inodes[i].path, p))
            return &fs->inodes[i];
    return (const tar_inode_t *)0;
}

int tar_stat(const tar_fs_t *fs, const char *path, vfs_stat_t *out)
{
    char p[VFS_PATH_MAX];
    const tar_inode_t *ino;
    int dlen;

    if (!fs || !out)
        return -1;
    out->size = 0;
    out->blocks = 0;
    out->mode = VFS_MODE_READ;
    out->type = VFS_TYPE_UNKNOWN;

    tar_norm(path, p, VFS_PATH_MAX);
    if (p[0] == '/' && p[1] == '\0') {
        out->type = VFS_TYPE_DIR;
        return 0;
    }

    ino = tar_lookup(fs, p);
    if (ino) {
        out->type = ino->is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
        out->size = ino->size;
        out->blocks = (ino->size + 511) / 512;
        return 0;
    }

    dlen = t_strlen(p);
    for (int i = 0; i < fs->count; i++) {
        const char *f = fs->inodes[i].path;
        if (t_strneq(f, p, dlen) && f[dlen] == '/') {
            out->type = VFS_TYPE_DIR;
            return 0;
        }
    }
    return -1;
}

int tar_list_dir(const tar_fs_t *fs, const char *path,
                 char names[][VFS_NAME_MAX], int max)
{
    char dir[VFS_PATH_MAX];
    int is_root;
    int dlen;
    int count = 0;

    if (!fs || !names || max <= 0)
        return -1;
    tar_norm(path, dir, VFS_PATH_MAX);
    is_root = (dir[0] == '/' && dir[1] == '\0');
    dlen = t_strlen(dir);

    for (int i = 0; i < fs->count; i++) {
        const char *full = fs->inodes[i].path;
        const char *rel;
        char child[VFS_NAME_MAX];
        int c = 0;
        int dup = 0;

        if (is_root) {
            if (full[0] != '/')
                continue;
            rel = full + 1;
        } else {
            if (!t_strneq(full, dir, dlen) || full[dlen] != '/')
                continue;
            rel = full + dlen + 1;
        }
        if (*rel == '\0')
            continue;

        while (rel[c] && rel[c] != '/' && c < VFS_NAME_MAX - 1) {
            child[c] = rel[c];
            c++;
        }
        child[c] = '\0';
        if (c == 0)
            continue;

        for (int j = 0; j < count; j++) {
            if (t_streq(names[j], child)) {
                dup = 1;
                break;
            }
        }
        if (dup)
            continue;
        if (count < max) {
            t_strncpy(names[count], child, VFS_NAME_MAX);
            count++;
        }
    }
    return count;
}

uint64_t tar_read(const tar_fs_t *fs, const tar_inode_t *ino,
                  uint64_t pos, void *buf, uint64_t count)
{
    const uint8_t *src;
    uint8_t *dst;

    (void)fs;
    if (!ino || ino->is_dir || !ino->data || !buf)
        return 0;
    if (pos >= ino->size)
        return 0;
    if (count > ino->size - pos)
        count = ino->size - pos;
    src = ino->data + pos;
    dst = (uint8_t *)buf;
    for (uint64_t i = 0; i < count; i++)
        dst[i] = src[i];
    return count;
}
