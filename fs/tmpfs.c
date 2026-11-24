/*
 * Project Tsukasa - Scalable multi-instance in-memory filesystem (tmpfs)
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

#include "tmpfs.h"

#include "../drv/rtc.h"
#include "../include/errno.h"
#include "../include/spinlock.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../mm/vmm_x64.h"

typedef struct tmpfs_dirent {
    char                 name[VFS_NAME_MAX];
    tmpfs_inode_t       *inode;
    struct tmpfs_dirent *next;
} tmpfs_dirent_t;

struct tmpfs_inode {
    uint64_t         ino;
    int              type;
    uint32_t         mode;
    uint32_t         uid;
    uint32_t         gid;
    uint64_t         atime;
    uint64_t         mtime;
    uint64_t         ctime;
    uint32_t         nlink;
    uint32_t         refcount;
    spinlock_t       io_lock;
    size_t           size;
    uint8_t        **pages;
    size_t           page_cap;
    size_t           page_count;
    tmpfs_dirent_t  *children;
    char            *symlink_target;
};

struct tmpfs_fs {
    tmpfs_inode_t *root;
    uint64_t       next_ino;
    spinlock_t     lock;
};

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
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == '\0' && b[i] == '\0';
}

static void t_strcpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void t_zero(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        b[i] = 0;
}

static void t_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static int is_leap_year(int year)
{
    if ((year % 4) != 0) return 0;
    if ((year % 100) != 0) return 1;
    return (year % 400) == 0;
}

static int month_days(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)
        return is_leap_year(year) ? 29 : 28;
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1];
}

static uint64_t tmpfs_now_sec(void)
{
    rtc_time_t rt;
    rtc_read(&rt);
    if (rt.year < 1970)
        return 0;

    uint64_t days = 0;
    for (int y = 1970; y < (int)rt.year; y++)
        days += is_leap_year(y) ? 366ULL : 365ULL;
    for (int m = 1; m < (int)rt.month; m++)
        days += (uint64_t)month_days((int)rt.year, m);
    if (rt.day > 0)
        days += (uint64_t)(rt.day - 1);

    return days * 86400ULL +
           (uint64_t)rt.hour * 3600ULL +
           (uint64_t)rt.min * 60ULL +
           (uint64_t)rt.sec;
}

static uint8_t *tmpfs_alloc_page(void)
{
    uintptr_t phys = pmm_alloc();
    if (!phys)
        return NULL;
    uint8_t *virt = (uint8_t *)vmm_phys_to_virt((uint64_t)phys);
    t_zero(virt, TMPFS_PAGE_SIZE);
    return virt;
}

static void tmpfs_free_page(uint8_t *virt)
{
    if (!virt)
        return;
    uint64_t phys = vmm_virt_to_phys((uintptr_t)virt);
    if (phys)
        pmm_free((uintptr_t)phys);
}

static tmpfs_inode_t *inode_alloc(tmpfs_fs_t *fs, int type, uint32_t mode)
{
    tmpfs_inode_t *ino = (tmpfs_inode_t *)kmalloc(sizeof(tmpfs_inode_t));
    if (!ino)
        return NULL;
    t_zero(ino, sizeof(*ino));
    ino->ino = fs->next_ino++;
    ino->type = type;
    ino->mode = mode & TMPFS_MODE_MASK;
    ino->nlink = (type == TMPFS_TYPE_DIR) ? 2U : 1U;
    ino->refcount = 1U;
    ino->io_lock = SPINLOCK_INIT;
    uint64_t now = tmpfs_now_sec();
    ino->atime = now;
    ino->mtime = now;
    ino->ctime = now;
    return ino;
}

static void inode_free_storage(tmpfs_inode_t *ino)
{
    if (!ino)
        return;
    if (ino->type == TMPFS_TYPE_FILE && ino->pages) {
        for (size_t i = 0; i < ino->page_count; i++)
            tmpfs_free_page(ino->pages[i]);
        kfree(ino->pages);
        ino->pages = NULL;
        ino->page_cap = 0;
        ino->page_count = 0;
    } else if (ino->type == TMPFS_TYPE_SYMLINK && ino->symlink_target) {
        kfree(ino->symlink_target);
        ino->symlink_target = NULL;
    } else if (ino->type == TMPFS_TYPE_DIR) {
        tmpfs_dirent_t *d = ino->children;
        while (d) {
            tmpfs_dirent_t *next = d->next;
            if (d->inode)
                tmpfs_inode_unref(d->inode);
            kfree(d);
            d = next;
        }
        ino->children = NULL;
    }
    kfree(ino);
}

void tmpfs_inode_ref(tmpfs_inode_t *ino)
{
    if (!ino)
        return;
    __atomic_add_fetch(&ino->refcount, 1U, __ATOMIC_RELAXED);
}

void tmpfs_inode_unref(tmpfs_inode_t *ino)
{
    if (!ino)
        return;
    if (__atomic_sub_fetch(&ino->refcount, 1U, __ATOMIC_ACQ_REL) == 0U)
        inode_free_storage(ino);
}

tmpfs_fs_t *tmpfs_create(void)
{
    tmpfs_fs_t *fs = (tmpfs_fs_t *)kmalloc(sizeof(tmpfs_fs_t));
    if (!fs)
        return NULL;
    fs->next_ino = 1;
    fs->lock = SPINLOCK_INIT;
    fs->root = inode_alloc(fs, TMPFS_TYPE_DIR, 0755);
    if (!fs->root) {
        kfree(fs);
        return NULL;
    }
    return fs;
}

void tmpfs_destroy(tmpfs_fs_t *fs)
{
    if (!fs)
        return;
    if (fs->root)
        tmpfs_inode_unref(fs->root);
    kfree(fs);
}

static tmpfs_dirent_t *dir_find_child(const tmpfs_inode_t *dir, const char *name,
                                      tmpfs_dirent_t **prev_out)
{
    tmpfs_dirent_t *prev = NULL;
    tmpfs_dirent_t *curr = dir ? dir->children : NULL;
    while (curr) {
        if (t_streq(curr->name, name)) {
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

static int dir_add_child(tmpfs_inode_t *dir, const char *name, tmpfs_inode_t *child)
{
    tmpfs_dirent_t *de = (tmpfs_dirent_t *)kmalloc(sizeof(tmpfs_dirent_t));
    if (!de)
        return -ENOMEM;
    t_strcpy(de->name, name, VFS_NAME_MAX);
    de->inode = child;
    de->next = dir->children;
    dir->children = de;
    if (child->type == TMPFS_TYPE_DIR)
        dir->nlink++;
    uint64_t now = tmpfs_now_sec();
    dir->mtime = now;
    dir->ctime = now;
    return 0;
}

/* Walk subpath component by component.
 * Returns:
 *   0 on resolution to *ino_out
 *   1 if a symlink that needs following is hit (fills sym_target_out & consumed_out)
 *  <0 on error (-ENOENT, -ENOTDIR, -EINVAL)
 */
static int walk_step_locked(tmpfs_fs_t *fs, const char *subpath, int follow_last,
                            tmpfs_inode_t **ino_out, char *sym_target_out,
                            size_t sym_cap, size_t *consumed_out)
{
    tmpfs_inode_t *stack[32];
    int depth = 0;
    size_t i = 0;

    if (!fs || !fs->root || !subpath)
        return -EINVAL;

    stack[0] = fs->root;

    while (subpath[i] == '/')
        i++;

    while (subpath[i]) {
        char comp[VFS_NAME_MAX];
        int ci = 0;
        size_t comp_end;
        int has_more = 0;

        if (stack[depth]->type != TMPFS_TYPE_DIR)
            return -ENOTDIR;

        while (subpath[i] && subpath[i] != '/') {
            if (ci >= VFS_NAME_MAX - 1)
                return -EINVAL;
            comp[ci++] = subpath[i++];
        }
        comp[ci] = '\0';
        comp_end = i;

        while (subpath[i] == '/')
            i++;
        has_more = (subpath[i] != '\0');

        if (t_streq(comp, "."))
            continue;
        if (t_streq(comp, "..")) {
            if (depth > 0)
                depth--;
            continue;
        }

        tmpfs_dirent_t *de = dir_find_child(stack[depth], comp, NULL);
        if (!de || !de->inode)
            return -ENOENT;

        tmpfs_inode_t *child = de->inode;
        if (child->type == TMPFS_TYPE_SYMLINK && (has_more || follow_last)) {
            if (sym_target_out && sym_cap > 0)
                t_strcpy(sym_target_out, child->symlink_target, (int)sym_cap);
            if (consumed_out)
                *consumed_out = comp_end;
            return 1;
        }

        if (depth + 1 >= 32)
            return -EINVAL;
        stack[++depth] = child;
    }

    if (ino_out)
        *ino_out = stack[depth];
    return 0;
}

int tmpfs_resolve_step(tmpfs_fs_t *fs, const char *subpath, int follow_last,
                       tmpfs_inode_t **ino_out, char *sym_target_out,
                       size_t sym_cap, size_t *consumed_out)
{
    unsigned long flags;
    int rc;
    if (!fs)
        return -EINVAL;
    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_step_locked(fs, subpath, follow_last, ino_out,
                          sym_target_out, sym_cap, consumed_out);
    if (rc == 0 && ino_out && *ino_out)
        tmpfs_inode_ref(*ino_out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return rc;
}

static int join_symlink_subpath(const char *orig, size_t consumed_end,
                                const char *target, char *out, size_t cap)
{
    size_t oi = 0;
    const char *rest = orig + consumed_end;

    if (!target || !target[0])
        return -EINVAL;

    if (target[0] == '/') {
        for (size_t i = 0; target[i]; i++) {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = target[i];
        }
    } else {
        /* Keep directory prefix before the symlink component */
        size_t dir_end = consumed_end;
        while (dir_end > 0 && orig[dir_end - 1] != '/')
            dir_end--;
        for (size_t i = 0; i < dir_end; i++) {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = orig[i];
        }
        if (oi == 0 || out[oi - 1] != '/') {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = '/';
        }
        for (size_t i = 0; target[i]; i++) {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = target[i];
        }
    }

    if (rest[0]) {
        if (oi == 0 || out[oi - 1] != '/') {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = '/';
        }
        while (*rest == '/')
            rest++;
        for (size_t i = 0; rest[i]; i++) {
            if (oi + 1 >= cap)
                return -EINVAL;
            out[oi++] = rest[i];
        }
    }
    out[oi] = '\0';
    return 0;
}

static int walk_resolve_locked(tmpfs_fs_t *fs, const char *subpath,
                               int follow_last, tmpfs_inode_t **ino_out)
{
    char cur[VFS_PATH_MAX];
    char next[VFS_PATH_MAX];
    char target[VFS_PATH_MAX];

    if (!subpath)
        return -EINVAL;
    if (t_strlen(subpath) >= VFS_PATH_MAX)
        return -EINVAL;
    t_strcpy(cur, subpath, VFS_PATH_MAX);

    for (int hop = 0; hop <= TMPFS_MAX_SYMLINKS; hop++) {
        size_t consumed = 0;
        int rc = walk_step_locked(fs, cur, follow_last, ino_out,
                                  target, sizeof(target), &consumed);
        if (rc <= 0)
            return rc;
        if (hop == TMPFS_MAX_SYMLINKS)
            return -ELOOP;
        if (join_symlink_subpath(cur, consumed, target, next, sizeof(next)) != 0)
            return -EINVAL;
        t_strcpy(cur, next, VFS_PATH_MAX);
    }
    return -ELOOP;
}

tmpfs_inode_t *tmpfs_lookup(tmpfs_fs_t *fs, const char *subpath, int follow_symlinks)
{
    tmpfs_inode_t *ino = NULL;
    unsigned long flags;
    if (!fs || !subpath)
        return NULL;
    flags = spin_lock_irqsave(&fs->lock);
    if (walk_resolve_locked(fs, subpath, follow_symlinks, &ino) == 0 && ino)
        tmpfs_inode_ref(ino);
    else
        ino = NULL;
    spin_unlock_irqrestore(&fs->lock, flags);
    return ino;
}

/* Split `subpath` into normalized parent subpath and leaf component. */
static int split_parent(const char *subpath, char *parent_out, size_t cap,
                        char *leaf_out, size_t leaf_cap)
{
    int len;
    int end;
    int last_slash = -1;

    if (!subpath || !parent_out || !leaf_out || cap < 2 || leaf_cap < 2)
        return -EINVAL;

    len = t_strlen(subpath);
    end = len;
    while (end > 0 && subpath[end - 1] == '/')
        end--;
    if (end == 0)
        return -EINVAL; /* Cannot split root "/" */

    for (int i = 0; i < end; i++) {
        if (subpath[i] == '/')
            last_slash = i;
    }

    if (last_slash < 0) {
        t_strcpy(parent_out, "/", (int)cap);
        if (end >= (int)leaf_cap)
            return -EINVAL;
        for (int i = 0; i < end; i++)
            leaf_out[i] = subpath[i];
        leaf_out[end] = '\0';
    } else {
        int plen = (last_slash == 0) ? 1 : last_slash;
        if (plen >= (int)cap)
            return -EINVAL;
        for (int i = 0; i < plen; i++)
            parent_out[i] = subpath[i];
        parent_out[plen] = '\0';

        int llen = end - last_slash - 1;
        if (llen <= 0 || llen >= (int)leaf_cap)
            return -EINVAL;
        for (int i = 0; i < llen; i++)
            leaf_out[i] = subpath[last_slash + 1 + i];
        leaf_out[llen] = '\0';
    }

    if (t_streq(leaf_out, ".") || t_streq(leaf_out, ".."))
        return -EINVAL;
    return 0;
}

void tmpfs_stat_inode(const tmpfs_inode_t *ino, vfs_stat_t *out)
{
    uint32_t ifmt;
    if (!ino || !out)
        return;
    t_zero(out, sizeof(*out));
    out->size = (uint64_t)ino->size;
    out->blocks = ((uint64_t)ino->size + 511ULL) / 512ULL;
    out->mode = VFS_MODE_READ | VFS_MODE_WRITE;
    if (ino->type == TMPFS_TYPE_DIR) {
        out->type = VFS_TYPE_DIR;
        ifmt = 0040000U;
    } else if (ino->type == TMPFS_TYPE_SYMLINK) {
        out->type = VFS_TYPE_SYMLINK;
        ifmt = 0120000U;
    } else {
        out->type = VFS_TYPE_FILE;
        ifmt = 0100000U;
    }
    out->st_ino = ino->ino;
    out->st_mode = ifmt | (ino->mode & TMPFS_MODE_MASK);
    out->st_nlink = ino->nlink ? ino->nlink : 1U;
    out->st_uid = ino->uid;
    out->st_gid = ino->gid;
    out->st_rdev = 0;
    out->st_size = (uint64_t)ino->size;
    out->st_blksize = TMPFS_PAGE_SIZE;
    out->st_blocks = out->blocks;
    out->st_atime = (int64_t)ino->atime;
    out->st_mtime = (int64_t)ino->mtime;
    out->st_ctime = (int64_t)ino->ctime;
}

int tmpfs_stat(tmpfs_fs_t *fs, const char *subpath, int follow_symlinks, vfs_stat_t *out)
{
    tmpfs_inode_t *ino = NULL;
    unsigned long flags;
    int rc;

    if (!fs || !subpath || !out)
        return -EINVAL;
    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, subpath, follow_symlinks, &ino);
    if (rc == 0 && ino)
        tmpfs_stat_inode(ino, out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return rc;
}

int tmpfs_mkdir(tmpfs_fs_t *fs, const char *subpath, uint32_t mode)
{
    char parent_path[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    tmpfs_inode_t *parent = NULL;
    tmpfs_inode_t *dir;
    unsigned long flags;
    int rc;

    if (!fs || !subpath)
        return -EINVAL;
    if (split_parent(subpath, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, parent_path, 1, &parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }
    if (dir_find_child(parent, leaf, NULL)) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -EEXIST;
    }

    dir = inode_alloc(fs, TMPFS_TYPE_DIR, mode & TMPFS_MODE_MASK);
    if (!dir) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOMEM;
    }
    rc = dir_add_child(parent, leaf, dir);
    if (rc != 0)
        tmpfs_inode_unref(dir);
    spin_unlock_irqrestore(&fs->lock, flags);
    return rc;
}

int tmpfs_mkdir_p(tmpfs_fs_t *fs, const char *subpath, uint32_t mode)
{
    char cur[VFS_PATH_MAX];
    size_t i = 0;

    if (!fs || !subpath)
        return -EINVAL;

    while (subpath[i] == '/')
        i++;
    if (!subpath[i])
        return 0;

    cur[0] = '/';
    cur[1] = '\0';

    while (subpath[i]) {
        char comp[VFS_NAME_MAX];
        int ci = 0;
        while (subpath[i] && subpath[i] != '/') {
            if (ci >= VFS_NAME_MAX - 1)
                return -EINVAL;
            comp[ci++] = subpath[i++];
        }
        comp[ci] = '\0';
        while (subpath[i] == '/')
            i++;

        if (t_streq(comp, ".") || comp[0] == '\0')
            continue;

        int cur_len = t_strlen(cur);
        if (cur_len > 1) {
            if (cur_len + 1 >= VFS_PATH_MAX)
                return -EINVAL;
            cur[cur_len++] = '/';
            cur[cur_len] = '\0';
        }
        if (cur_len + ci >= VFS_PATH_MAX)
            return -EINVAL;
        t_strcpy(cur + cur_len, comp, VFS_PATH_MAX - cur_len);

        int rc = tmpfs_mkdir(fs, cur, mode);
        if (rc != 0 && rc != -EEXIST)
            return rc;
    }
    return 0;
}

int tmpfs_rmdir(tmpfs_fs_t *fs, const char *subpath)
{
    char parent_path[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    tmpfs_inode_t *parent = NULL;
    tmpfs_dirent_t *prev = NULL;
    tmpfs_dirent_t *de;
    unsigned long flags;
    int rc;

    if (!fs || !subpath)
        return -EINVAL;
    if (split_parent(subpath, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, parent_path, 1, &parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }
    de = dir_find_child(parent, leaf, &prev);
    if (!de || !de->inode) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOENT;
    }
    if (de->inode->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }
    if (de->inode->children != NULL) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTEMPTY;
    }

    if (prev)
        prev->next = de->next;
    else
        parent->children = de->next;

    if (parent->nlink > 0)
        parent->nlink--;
    uint64_t now = tmpfs_now_sec();
    parent->mtime = now;
    parent->ctime = now;
    if (de->inode->nlink > 0)
        de->inode->nlink--;
    tmpfs_inode_unref(de->inode);
    kfree(de);
    spin_unlock_irqrestore(&fs->lock, flags);
    return 0;
}

int tmpfs_readdir(tmpfs_fs_t *fs, const char *subpath,
                  char names[][VFS_NAME_MAX], int max)
{
    tmpfs_inode_t *dir = NULL;
    unsigned long flags;
    int rc;
    int count = 0;

    if (!fs || !subpath || !names || max <= 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, subpath, 1, &dir);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (dir->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }

    tmpfs_dirent_t *de = dir->children;
    while (de && count < max) {
        t_strcpy(names[count], de->name, VFS_NAME_MAX);
        count++;
        de = de->next;
    }
    spin_unlock_irqrestore(&fs->lock, flags);
    return count;
}

int tmpfs_create_file(tmpfs_fs_t *fs, const char *subpath, uint32_t mode,
                      tmpfs_inode_t **out_ino)
{
    char parent_path[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    tmpfs_inode_t *parent = NULL;
    tmpfs_dirent_t *existing;
    tmpfs_inode_t *ino;
    unsigned long flags;
    int rc;

    if (!fs || !subpath)
        return -EINVAL;
    if (split_parent(subpath, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, parent_path, 1, &parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }

    existing = dir_find_child(parent, leaf, NULL);
    if (existing) {
        if (existing->inode->type != TMPFS_TYPE_FILE) {
            spin_unlock_irqrestore(&fs->lock, flags);
            return -EEXIST;
        }
        if (out_ino) {
            tmpfs_inode_ref(existing->inode);
            *out_ino = existing->inode;
        }
        spin_unlock_irqrestore(&fs->lock, flags);
        return 0;
    }

    ino = inode_alloc(fs, TMPFS_TYPE_FILE, mode & TMPFS_MODE_MASK);
    if (!ino) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOMEM;
    }
    rc = dir_add_child(parent, leaf, ino);
    if (rc != 0) {
        tmpfs_inode_unref(ino);
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (out_ino) {
        tmpfs_inode_ref(ino);
        *out_ino = ino;
    }
    spin_unlock_irqrestore(&fs->lock, flags);
    return 0;
}

int tmpfs_symlink(tmpfs_fs_t *fs, const char *subpath, const char *target,
                  uint32_t mode)
{
    char parent_path[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    tmpfs_inode_t *parent = NULL;
    tmpfs_inode_t *ino;
    int tlen;
    unsigned long flags;
    int rc;

    if (!fs || !subpath || !target || !target[0])
        return -EINVAL;
    if (split_parent(subpath, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0)
        return -EINVAL;

    tlen = t_strlen(target);
    if (tlen >= VFS_PATH_MAX)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, parent_path, 1, &parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }
    if (dir_find_child(parent, leaf, NULL)) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -EEXIST;
    }

    ino = inode_alloc(fs, TMPFS_TYPE_SYMLINK, mode ? mode : 0777U);
    if (!ino) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOMEM;
    }
    ino->symlink_target = (char *)kmalloc((size_t)tlen + 1U);
    if (!ino->symlink_target) {
        tmpfs_inode_unref(ino);
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOMEM;
    }
    t_strcpy(ino->symlink_target, target, tlen + 1);
    ino->size = (size_t)tlen;

    rc = dir_add_child(parent, leaf, ino);
    if (rc != 0)
        tmpfs_inode_unref(ino);
    spin_unlock_irqrestore(&fs->lock, flags);
    return rc;
}

int tmpfs_readlink(tmpfs_fs_t *fs, const char *subpath, char *buf, size_t cap)
{
    tmpfs_inode_t *ino = NULL;
    unsigned long flags;
    int rc;

    if (!fs || !subpath || !buf || cap == 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, subpath, 0, &ino);
    if (rc == 0 && ino) {
        if (ino->type != TMPFS_TYPE_SYMLINK || !ino->symlink_target) {
            rc = -EINVAL;
        } else {
            int tlen = t_strlen(ino->symlink_target);
            size_t copy_len = ((size_t)tlen < cap) ? (size_t)tlen : cap;
            t_memcpy(buf, ino->symlink_target, copy_len);
            if (copy_len < cap)
                buf[copy_len] = '\0';
            rc = (int)copy_len;
        }
    }
    spin_unlock_irqrestore(&fs->lock, flags);
    return rc;
}

int tmpfs_unlink(tmpfs_fs_t *fs, const char *subpath)
{
    char parent_path[VFS_PATH_MAX];
    char leaf[VFS_NAME_MAX];
    tmpfs_inode_t *parent = NULL;
    tmpfs_dirent_t *prev = NULL;
    tmpfs_dirent_t *de;
    unsigned long flags;
    int rc;

    if (!fs || !subpath)
        return -EINVAL;
    if (split_parent(subpath, parent_path, sizeof(parent_path), leaf, sizeof(leaf)) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, parent_path, 1, &parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }
    de = dir_find_child(parent, leaf, &prev);
    if (!de || !de->inode) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOENT;
    }
    if (de->inode->type == TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -EISDIR;
    }

    if (prev)
        prev->next = de->next;
    else
        parent->children = de->next;

    uint64_t now = tmpfs_now_sec();
    parent->mtime = now;
    parent->ctime = now;
    if (de->inode->nlink > 0)
        de->inode->nlink--;
    de->inode->ctime = now;
    tmpfs_inode_unref(de->inode);
    kfree(de);
    spin_unlock_irqrestore(&fs->lock, flags);
    return 0;
}

static int dir_is_descendant(const tmpfs_inode_t *ancestor, const tmpfs_inode_t *target)
{
    if (!ancestor || ancestor->type != TMPFS_TYPE_DIR || !target)
        return 0;
    if (ancestor == target)
        return 1;
    for (const tmpfs_dirent_t *de = ancestor->children; de; de = de->next) {
        if (de->inode == target)
            return 1;
        if (de->inode && de->inode->type == TMPFS_TYPE_DIR) {
            if (dir_is_descendant(de->inode, target))
                return 1;
        }
    }
    return 0;
}

int tmpfs_rename(tmpfs_fs_t *fs, const char *old_subpath, const char *new_subpath)
{
    char old_parent_path[VFS_PATH_MAX], new_parent_path[VFS_PATH_MAX];
    char old_leaf[VFS_NAME_MAX], new_leaf[VFS_NAME_MAX];
    tmpfs_inode_t *old_parent = NULL, *new_parent = NULL;
    tmpfs_dirent_t *old_prev = NULL, *old_de;
    tmpfs_dirent_t *dst_prev = NULL, *dst_de;
    unsigned long flags;
    int rc;

    if (!fs || !old_subpath || !new_subpath)
        return -EINVAL;
    if (split_parent(old_subpath, old_parent_path, sizeof(old_parent_path),
                     old_leaf, sizeof(old_leaf)) != 0)
        return -EINVAL;
    if (split_parent(new_subpath, new_parent_path, sizeof(new_parent_path),
                     new_leaf, sizeof(new_leaf)) != 0)
        return -EINVAL;

    flags = spin_lock_irqsave(&fs->lock);
    rc = walk_resolve_locked(fs, old_parent_path, 1, &old_parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    rc = walk_resolve_locked(fs, new_parent_path, 1, &new_parent);
    if (rc != 0) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return rc;
    }
    if (old_parent->type != TMPFS_TYPE_DIR || new_parent->type != TMPFS_TYPE_DIR) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOTDIR;
    }

    old_de = dir_find_child(old_parent, old_leaf, &old_prev);
    if (!old_de || !old_de->inode) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -ENOENT;
    }

    dst_de = dir_find_child(new_parent, new_leaf, &dst_prev);
    if (dst_de == old_de) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return 0;
    }

    if (old_de->inode->type == TMPFS_TYPE_DIR &&
        dir_is_descendant(old_de->inode, new_parent)) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return -EINVAL;
    }

    if (dst_de) {
        if (dst_de->inode->type == TMPFS_TYPE_DIR) {
            if (old_de->inode->type != TMPFS_TYPE_DIR || dst_de->inode->children != NULL) {
                spin_unlock_irqrestore(&fs->lock, flags);
                return -EINVAL;
            }
        } else if (old_de->inode->type == TMPFS_TYPE_DIR) {
            spin_unlock_irqrestore(&fs->lock, flags);
            return -ENOTDIR;
        }
    }

    if (dst_de) {
        if (dst_prev)
            dst_prev->next = dst_de->next;
        else
            new_parent->children = dst_de->next;
        if (dst_de->inode->type == TMPFS_TYPE_DIR && new_parent->nlink > 0)
            new_parent->nlink--;
        if (dst_de->inode->nlink > 0)
            dst_de->inode->nlink--;
        tmpfs_inode_unref(dst_de->inode);
        kfree(dst_de);
        /* Refresh old_prev in case dst_de was immediately before old_de */
        old_de = dir_find_child(old_parent, old_leaf, &old_prev);
    }

    t_strcpy(old_de->name, new_leaf, VFS_NAME_MAX);
    if (old_parent != new_parent) {
        if (old_prev)
            old_prev->next = old_de->next;
        else
            old_parent->children = old_de->next;
        old_de->next = new_parent->children;
        new_parent->children = old_de;
        if (old_de->inode->type == TMPFS_TYPE_DIR) {
            if (old_parent->nlink > 0)
                old_parent->nlink--;
            new_parent->nlink++;
        }
    }

    uint64_t now = tmpfs_now_sec();
    old_parent->mtime = now;
    old_parent->ctime = now;
    new_parent->mtime = now;
    new_parent->ctime = now;
    old_de->inode->ctime = now;
    spin_unlock_irqrestore(&fs->lock, flags);
    return 0;
}

static int ensure_pages(tmpfs_inode_t *ino, size_t need_pages)
{
    if (need_pages <= ino->page_count)
        return 0;

    if (need_pages > ino->page_cap) {
        size_t new_cap = ino->page_cap ? ino->page_cap * 2U : 4U;
        while (new_cap < need_pages)
            new_cap *= 2U;
        uint8_t **new_arr = (uint8_t **)kmalloc(new_cap * sizeof(uint8_t *));
        if (!new_arr)
            return -ENOMEM;
        for (size_t i = 0; i < ino->page_count; i++)
            new_arr[i] = ino->pages[i];
        for (size_t i = ino->page_count; i < new_cap; i++)
            new_arr[i] = NULL;
        if (ino->pages)
            kfree(ino->pages);
        ino->pages = new_arr;
        ino->page_cap = new_cap;
    }

    while (ino->page_count < need_pages) {
        uint8_t *pg = tmpfs_alloc_page();
        if (!pg)
            return -ENOMEM;
        ino->pages[ino->page_count++] = pg;
    }
    return 0;
}

size_t tmpfs_read(tmpfs_inode_t *ino, size_t pos, void *buf, size_t count)
{
    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;
    unsigned long flags;

    if (!ino || ino->type != TMPFS_TYPE_FILE || !dst || count == 0)
        return 0;

    flags = spin_lock_irqsave(&ino->io_lock);
    if (pos >= ino->size) {
        spin_unlock_irqrestore(&ino->io_lock, flags);
        return 0;
    }
    if (count > ino->size - pos)
        count = ino->size - pos;

    while (done < count) {
        size_t cur = pos + done;
        size_t p_idx = cur / TMPFS_PAGE_SIZE;
        size_t p_off = cur % TMPFS_PAGE_SIZE;
        size_t chunk = TMPFS_PAGE_SIZE - p_off;
        if (chunk > count - done)
            chunk = count - done;
        if (p_idx < ino->page_count && ino->pages[p_idx])
            t_memcpy(dst + done, ino->pages[p_idx] + p_off, chunk);
        else
            t_zero(dst + done, chunk);
        done += chunk;
    }
    ino->atime = tmpfs_now_sec();
    spin_unlock_irqrestore(&ino->io_lock, flags);
    return done;
}

size_t tmpfs_write(tmpfs_inode_t *ino, size_t pos, const void *buf, size_t count)
{
    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;
    size_t end_pos;
    size_t need_pages;
    unsigned long flags;

    if (!ino || ino->type != TMPFS_TYPE_FILE || (!src && count > 0))
        return 0;
    if (count == 0)
        return 0;
    if (pos > SIZE_MAX - count)
        return 0;

    flags = spin_lock_irqsave(&ino->io_lock);
    end_pos = pos + count;
    need_pages = (end_pos + TMPFS_PAGE_SIZE - 1U) / TMPFS_PAGE_SIZE;
    if (ensure_pages(ino, need_pages) != 0) {
        spin_unlock_irqrestore(&ino->io_lock, flags);
        return 0;
    }

    while (done < count) {
        size_t cur = pos + done;
        size_t p_idx = cur / TMPFS_PAGE_SIZE;
        size_t p_off = cur % TMPFS_PAGE_SIZE;
        size_t chunk = TMPFS_PAGE_SIZE - p_off;
        if (chunk > count - done)
            chunk = count - done;
        t_memcpy(ino->pages[p_idx] + p_off, src + done, chunk);
        done += chunk;
    }

    if (end_pos > ino->size)
        ino->size = end_pos;
    uint64_t now = tmpfs_now_sec();
    ino->mtime = now;
    ino->ctime = now;
    spin_unlock_irqrestore(&ino->io_lock, flags);
    return done;
}

int tmpfs_truncate(tmpfs_inode_t *ino, size_t new_size)
{
    unsigned long flags;
    if (!ino || ino->type != TMPFS_TYPE_FILE)
        return -EINVAL;

    flags = spin_lock_irqsave(&ino->io_lock);
    if (new_size < ino->size) {
        size_t keep_pages = (new_size + TMPFS_PAGE_SIZE - 1U) / TMPFS_PAGE_SIZE;
        for (size_t i = keep_pages; i < ino->page_count; i++) {
            tmpfs_free_page(ino->pages[i]);
            ino->pages[i] = NULL;
        }
        ino->page_count = keep_pages;
        if (keep_pages > 0 && (new_size % TMPFS_PAGE_SIZE) != 0U) {
            size_t tail_off = new_size % TMPFS_PAGE_SIZE;
            t_zero(ino->pages[keep_pages - 1U] + tail_off,
                   TMPFS_PAGE_SIZE - tail_off);
        }
    } else if (new_size > ino->size) {
        size_t need_pages = (new_size + TMPFS_PAGE_SIZE - 1U) / TMPFS_PAGE_SIZE;
        if (ensure_pages(ino, need_pages) != 0) {
            spin_unlock_irqrestore(&ino->io_lock, flags);
            return -ENOMEM;
        }
    }

    ino->size = new_size;
    uint64_t now = tmpfs_now_sec();
    ino->mtime = now;
    ino->ctime = now;
    spin_unlock_irqrestore(&ino->io_lock, flags);
    return 0;
}

size_t tmpfs_inode_size(const tmpfs_inode_t *ino)
{
    return ino ? ino->size : 0;
}

int tmpfs_inode_type(const tmpfs_inode_t *ino)
{
    return ino ? ino->type : 0;
}

uint32_t tmpfs_inode_mode(const tmpfs_inode_t *ino)
{
    return ino ? ino->mode : 0;
}

uint64_t tmpfs_inode_ino(const tmpfs_inode_t *ino)
{
    return ino ? ino->ino : 0;
}

uint32_t tmpfs_inode_uid(const tmpfs_inode_t *ino)
{
    return ino ? ino->uid : 0;
}

uint32_t tmpfs_inode_gid(const tmpfs_inode_t *ino)
{
    return ino ? ino->gid : 0;
}

void tmpfs_set_attrs(tmpfs_inode_t *ino, uint32_t mode, uint32_t uid,
                     uint32_t gid, uint64_t mtime)
{
    if (!ino)
        return;
    ino->mode = mode & TMPFS_MODE_MASK;
    ino->uid = uid;
    ino->gid = gid;
    if (mtime != 0)
        ino->mtime = mtime;
}

int tmpfs_chmod_inode(tmpfs_inode_t *ino, uint32_t mode)
{
    unsigned long flags;
    if (!ino)
        return -EINVAL;
    flags = spin_lock_irqsave(&ino->io_lock);
    ino->mode = mode & TMPFS_MODE_MASK;
    ino->ctime = tmpfs_now_sec();
    spin_unlock_irqrestore(&ino->io_lock, flags);
    return 0;
}

int tmpfs_chown_inode(tmpfs_inode_t *ino, uint32_t uid, uint32_t gid)
{
    unsigned long flags;
    if (!ino)
        return -EINVAL;
    flags = spin_lock_irqsave(&ino->io_lock);
    if (uid != (uint32_t)-1)
        ino->uid = uid;
    if (gid != (uint32_t)-1)
        ino->gid = gid;
    ino->ctime = tmpfs_now_sec();
    spin_unlock_irqrestore(&ino->io_lock, flags);
    return 0;
}
