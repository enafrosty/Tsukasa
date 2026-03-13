/*
 * memfs.c  -  In-memory writable filesystem implementation.
 */

#include "memfs.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

static memfs_inode_t g_inodes[MEMFS_MAX_FILES];

/* ---- String helpers --------------------------------------------------- */

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

/* ---- API -------------------------------------------------------------- */

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

    /* Check if it already exists — update in place. */
    for (int i = 0; i < MEMFS_MAX_FILES; i++) {
        if (g_inodes[i].used && mstreq(g_inodes[i].name, name))
            return i;  /* re-use existing inode */
    }

    /* Find free slot. */
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
    return -1;  /* full */
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
        /* Grow: double until large enough. */
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
