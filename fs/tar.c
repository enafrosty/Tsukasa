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
#include "tmpfs.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
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

static int tar_verify_checksum(const struct tar_header *h)
{
    const uint8_t *raw = (const uint8_t *)h;
    uint64_t expected = tar_parse_octal(h->chksum, 8);
    uint64_t usum = 0;
    int64_t ssum = 0;

    for (int i = 0; i < 512; i++) {
        uint8_t ub = (i >= 148 && i < 156) ? (uint8_t)' ' : raw[i];
        int8_t sb = (int8_t)ub;
        usum += ub;
        ssum += sb;
    }
    return (usum == expected || (int64_t)expected == ssum);
}

static void tar_normalize_rel(const char *raw, char *out, int cap)
{
    int i = 0;
    int o = 0;

    if (!out || cap < 2)
        return;
    if (!raw) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }
    while (raw[i] == '.' && raw[i + 1] == '/')
        i += 2;
    while (raw[i] == '/')
        i++;

    out[o++] = '/';
    while (raw[i] && o < cap - 1) {
        if (raw[i] == '/' && raw[i + 1] == '.' && raw[i + 2] == '/') {
            i += 2;
            continue;
        }
        if (raw[i] == '/' && o > 0 && out[o - 1] == '/') {
            i++;
            continue;
        }
        out[o++] = raw[i++];
    }
    out[o] = '\0';
    while (o > 1 && out[o - 1] == '/')
        out[--o] = '\0';
}

static void tar_build_full_path(const struct tar_header *h,
                                const char *long_name,
                                char *out, int cap)
{
    char combined[512];
    int ci = 0;

    if (long_name && long_name[0]) {
        tar_normalize_rel(long_name, out, cap);
        return;
    }

    if (h->prefix[0]) {
        for (int i = 0; i < 155 && h->prefix[i] && ci < (int)sizeof(combined) - 2; i++)
            combined[ci++] = h->prefix[i];
        if (ci > 0 && combined[ci - 1] != '/')
            combined[ci++] = '/';
    }
    for (int i = 0; i < 100 && h->filename[i] && ci < (int)sizeof(combined) - 1; i++)
        combined[ci++] = h->filename[i];
    combined[ci] = '\0';

    tar_normalize_rel(combined, out, cap);
}

static void tar_parent_dir(const char *abs_path, char *parent_out, int cap)
{
    int len = t_strlen(abs_path);
    int last_slash = -1;

    for (int i = 0; i < len; i++) {
        if (abs_path[i] == '/')
            last_slash = i;
    }
    if (last_slash <= 0) {
        t_strncpy(parent_out, "/", cap);
        return;
    }
    int plen = (last_slash < cap - 1) ? last_slash : (cap - 1);
    for (int i = 0; i < plen; i++)
        parent_out[i] = abs_path[i];
    parent_out[plen] = '\0';
}

int tar_unpack_to_tmpfs(const void *archive, uint64_t archive_size,
                        tmpfs_fs_t *fs)
{
    const uint8_t *base = (const uint8_t *)archive;
    uint64_t off = 0;
    int count = 0;
    char *long_name = NULL;
    char *long_link = NULL;

    if (!base || !fs || archive_size < 512)
        return -EINVAL;

    while (off + 512 <= archive_size) {
        const struct tar_header *h = (const struct tar_header *)(base + off);
        int all_zero = 1;

        for (int i = 0; i < 512; i++) {
            if (((const uint8_t *)h)[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
            break;

        if (!tar_verify_checksum(h)) {
            kprintf("[tar] ERROR: invalid header checksum at offset 0x%llx\n",
                    (unsigned long long)off);
            if (long_name) kfree(long_name);
            if (long_link) kfree(long_link);
            return -EINVAL;
        }

        uint64_t fsz = tar_parse_octal(h->size, 12);
        uint32_t mode = (uint32_t)tar_parse_octal(h->mode, 8);
        uint32_t uid = (uint32_t)tar_parse_octal(h->uid, 8);
        uint32_t gid = (uint32_t)tar_parse_octal(h->gid, 8);
        uint64_t mtime = tar_parse_octal(h->mtime, 12);
        uint64_t data_blocks = (fsz + 511ULL) / 512ULL;
        uint64_t data_off = off + 512ULL;
        uint64_t next = data_off + data_blocks * 512ULL;

        if (next < off || next > archive_size) {
            kprintf("[tar] ERROR: truncated member at offset 0x%llx\n",
                    (unsigned long long)off);
            if (long_name) kfree(long_name);
            if (long_link) kfree(long_link);
            return -EINVAL;
        }

        char typ = h->typeflag;
        if (typ == 'L' || typ == 'K') {
            if (fsz >= 4096ULL) {
                if (long_name) kfree(long_name);
                if (long_link) kfree(long_link);
                return -EINVAL;
            }
            char *buf = (char *)kmalloc((size_t)fsz + 1U);
            if (!buf) {
                if (long_name) kfree(long_name);
                if (long_link) kfree(long_link);
                return -ENOMEM;
            }
            for (uint64_t i = 0; i < fsz; i++)
                buf[i] = (char)base[data_off + i];
            buf[fsz] = '\0';
            if (typ == 'L') {
                if (long_name) kfree(long_name);
                long_name = buf;
            } else {
                if (long_link) kfree(long_link);
                long_link = buf;
            }
            off = next;
            continue;
        }

        char path[VFS_PATH_MAX];
        tar_build_full_path(h, long_name, path, sizeof(path));

        if (long_name) {
            kfree(long_name);
            long_name = NULL;
        }

        if (typ == '5') {
            if (!t_streq(path, "/")) {
                int rc = tmpfs_mkdir_p(fs, path, mode ? mode : 0755U);
                if (rc != 0) {
                    if (long_link) kfree(long_link);
                    return rc;
                }
            }
            tmpfs_inode_t *ino = tmpfs_lookup(fs, path, 0);
            if (ino) {
                tmpfs_set_attrs(ino, mode ? mode : 0755U, uid, gid, mtime);
                tmpfs_inode_unref(ino);
            }
            count++;
        } else if (typ == '0' || typ == '\0') {
            if (!t_streq(path, "/")) {
                char parent[VFS_PATH_MAX];
                tmpfs_inode_t *ino = NULL;
                tar_parent_dir(path, parent, sizeof(parent));
                if (!t_streq(parent, "/")) {
                    int mrc = tmpfs_mkdir_p(fs, parent, 0755U);
                    if (mrc != 0) {
                        if (long_link) kfree(long_link);
                        return mrc;
                    }
                }
                int crc = tmpfs_create_file(fs, path, mode ? mode : 0644U, &ino);
                if (crc != 0 || !ino) {
                    if (long_link) kfree(long_link);
                    return crc < 0 ? crc : -EIO;
                }
                if (fsz > 0) {
                    size_t wr = tmpfs_write(ino, 0, base + data_off, (size_t)fsz);
                    if (wr != (size_t)fsz) {
                        tmpfs_inode_unref(ino);
                        if (long_link) kfree(long_link);
                        return -ENOMEM;
                    }
                }
                tmpfs_set_attrs(ino, mode ? mode : 0644U, uid, gid, mtime);
                tmpfs_inode_unref(ino);
                count++;
            }
        } else if (typ == '2') {
            char target[VFS_PATH_MAX];
            if (long_link && long_link[0]) {
                t_strncpy(target, long_link, sizeof(target));
            } else {
                int li = 0;
                while (li < 100 && h->linkname[li] && li < (int)sizeof(target) - 1) {
                    target[li] = h->linkname[li];
                    li++;
                }
                target[li] = '\0';
            }
            if (!t_streq(path, "/") && target[0]) {
                char parent[VFS_PATH_MAX];
                tar_parent_dir(path, parent, sizeof(parent));
                if (!t_streq(parent, "/")) {
                    int mrc = tmpfs_mkdir_p(fs, parent, 0755U);
                    if (mrc != 0) {
                        if (long_link) kfree(long_link);
                        return mrc;
                    }
                }
                int src = tmpfs_symlink(fs, path, target, mode ? mode : 0777U);
                if (src != 0) {
                    if (long_link) kfree(long_link);
                    return src;
                }
                tmpfs_inode_t *ino = tmpfs_lookup(fs, path, 0);
                if (ino) {
                    tmpfs_set_attrs(ino, mode ? mode : 0777U, uid, gid, mtime);
                    tmpfs_inode_unref(ino);
                }
                count++;
            }
        }

        if (long_link) {
            kfree(long_link);
            long_link = NULL;
        }
        off = next;
    }

    if (long_name) kfree(long_name);
    if (long_link) kfree(long_link);
    return count;
}
