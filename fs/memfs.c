/*
 * Project Tsukasa — In-memory writable filesystem implementation
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

#include "memfs.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

static memfs_inode_t g_inodes[MEMFS_MAX_FILES];

static int mstrlen(const char *s)
{
    int n = 0; while (s && s[n]) n++; return n;
}

static int mstreq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return (a[i] == '\0' && b[i] == '\0');
}

static void mstrcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void memfs_init(void)
{
    for (int i = 0; i < MEMFS_MAX_FILES; i++) {
        g_inodes[i].used     = 0;
        g_inodes[i].data     = NULL;
        g_inodes[i].size     = 0;
        g_inodes[i].capacity = 0;
        g_inodes[i].name[0]  = '\0';
    }
}

int memfs_create(const char *name)
{
    if (!name || mstrlen(name) == 0) return -1;

    for (int i = 0; i < MEMFS_MAX_FILES; i++) {
        if (g_inodes[i].used && mstreq(g_inodes[i].name, name))
            return i;
    }

    for (int i = 0; i < MEMFS_MAX_FILES; i++) {
        if (!g_inodes[i].used) {
            mstrcpy(g_inodes[i].name, name, MEMFS_MAX_NAME);
            g_inodes[i].data     = (uint8_t *)kmalloc(MEMFS_INIT_CAP);
            g_inodes[i].size     = 0;
            g_inodes[i].capacity = g_inodes[i].data ? MEMFS_INIT_CAP : 0;
            g_inodes[i].used     = 1;
            return i;
        }
    }
    return -1;
}

int memfs_open(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < MEMFS_MAX_FILES; i++)
        if (g_inodes[i].used && mstreq(g_inodes[i].name, name))
            return i;
    return -1;
}

size_t memfs_read(int inode, size_t pos, void *buf, size_t count)
{
    if (inode < 0 || inode >= MEMFS_MAX_FILES || !g_inodes[inode].used)
        return 0;
    memfs_inode_t *n = &g_inodes[inode];
    if (pos >= n->size) return 0;
    if (count > n->size - pos) count = n->size - pos;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) dst[i] = n->data[pos + i];
    return count;
}

size_t memfs_write(int inode, size_t pos, const void *buf, size_t count)
{
    if (inode < 0 || inode >= MEMFS_MAX_FILES || !g_inodes[inode].used)
        return 0;
    memfs_inode_t *n = &g_inodes[inode];

    size_t needed = pos + count;
    if (needed > n->capacity) {
        size_t new_cap = n->capacity ? n->capacity * 2 : MEMFS_INIT_CAP;
        while (new_cap < needed) new_cap *= 2;
        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return 0;
        for (size_t i = 0; i < n->size; i++) new_data[i] = n->data[i];
        kfree(n->data);
        n->data     = new_data;
        n->capacity = new_cap;
    }

    const uint8_t *src = (const uint8_t *)buf;
    if (count > 0 && !src)
        return 0;
    for (size_t i = 0; i < count; i++) n->data[pos + i] = src[i];
    if (pos + count > n->size) n->size = pos + count;
    return count;
}

size_t memfs_size(int inode)
{
    if (inode < 0 || inode >= MEMFS_MAX_FILES || !g_inodes[inode].used)
        return 0;
    return g_inodes[inode].size;
}

int memfs_list(char names[][MEMFS_MAX_NAME], int max)
{
    int cnt = 0;
    for (int i = 0; i < MEMFS_MAX_FILES && cnt < max; i++) {
        if (g_inodes[i].used) {
            mstrcpy(names[cnt], g_inodes[i].name, MEMFS_MAX_NAME);
            cnt++;
        }
    }
    return cnt;
}

int memfs_truncate(int inode)
{
    if (inode < 0 || inode >= MEMFS_MAX_FILES || !g_inodes[inode].used)
        return -1;
    g_inodes[inode].size = 0;
    return 0;
}

int memfs_stat(const char *name, size_t *size_out)
{
    int inode = memfs_open(name);
    if (inode < 0)
        return -1;
    if (size_out)
        *size_out = g_inodes[inode].size;
    return 0;
}

int memfs_unlink(const char *name)
{
    if (!name)
        return -1;
    while (*name == '/')
        name++;
    int in = memfs_open(name);
    if (in < 0)
        return -1;
    if (g_inodes[in].data) {
        kfree(g_inodes[in].data);
        g_inodes[in].data = NULL;
    }
    g_inodes[in].used = 0;
    g_inodes[in].size = 0;
    g_inodes[in].capacity = 0;
    g_inodes[in].name[0] = '\0';
    return 0;
}

int memfs_rename(const char *old_name, const char *new_name)
{
    if (!old_name || !new_name)
        return -1;
    while (*old_name == '/')
        old_name++;
    while (*new_name == '/')
        new_name++;
    int in = memfs_open(old_name);
    if (in < 0)
        return -1;
    mstrcpy(g_inodes[in].name, new_name, MEMFS_MAX_NAME);
    return 0;
}

