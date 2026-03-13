/*
 * memfs.h  -  In-memory writable filesystem (temporary storage).
 *
 * Mounted at /tmp/.  Files are backed by kmalloc'd buffers.
 * Files persist while the kernel is running; lost on reboot.
 */

#ifndef MEMFS_H
#define MEMFS_H

#include <stdint.h>
#include <stddef.h>

#define MEMFS_MAX_FILES    16
#define MEMFS_MAX_NAME     64
#define MEMFS_MAX_FD        8   /* concurrent open file descriptors */
#define MEMFS_INIT_CAP   4096   /* initial buffer size per file      */

typedef struct {
    char     name[MEMFS_MAX_NAME];
    uint8_t *data;
    size_t   size;
    size_t   capacity;
    int      used;
} memfs_inode_t;

/** Initialize memfs (called once at startup). */
void memfs_init(void);

/** Create a new file; returns inode index or -1 on failure. */
int memfs_create(const char *name);

/** Open an existing file; returns inode index or -1. */
int memfs_open(const char *name);

/** Read data from an inode at given position. */
size_t memfs_read(int inode, size_t pos, void *buf, size_t count);

/** Write data into an inode at given position (grows as needed). */
size_t memfs_write(int inode, size_t pos, const void *buf, size_t count);

/** Get size of an inode's data. */
size_t memfs_size(int inode);

/** List all valid file names.
 *  @param names  Caller-supplied 2D array (max x MEMFS_MAX_NAME).
 *  @return       Number of names written. */
int memfs_list(char names[][MEMFS_MAX_NAME], int max);

#endif /* MEMFS_H */
