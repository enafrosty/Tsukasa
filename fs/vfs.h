/*
 * vfs.h  -  Virtual File System interface.
 *           Extended with vfs_write, vfs_create, vfs_list, vfs_size.
 *
 * Mount table:
 *   /tmp/   → memfs   (read-write, volatile)
 *   /       → FAT12   (read-write, persists in RAM image)
 *   /initrd → legacy single-module fallback (read-only)
 */

#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_FD    24
#define VFS_NAME_MAX  64

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/**
 * Initialize the VFS.  Must be called before any other vfs_* function.
 * @param mb_info  Multiboot info (for FAT12 ramdisk module).
 */
void vfs_init(const void *mb_info);

/** Open a file by path.  Returns fd >= 0, or -1 on failure. */
int    vfs_open(const char *path);

/**
 * Create a new writable file (in memfs for /tmp/, FAT12 for /).
 * If the file already exists it is truncated to zero.
 * Returns fd >= 0, or -1.
 */
int    vfs_create(const char *path);

/** Read up to count bytes.  Returns bytes actually read. */
size_t vfs_read(int fd, void *buf, size_t count);

/** Write up to count bytes.  Returns bytes actually written. */
size_t vfs_write(int fd, const void *buf, size_t count);

/** Seek within file.  Returns new position, or (size_t)-1 on error. */
size_t vfs_seek(int fd, size_t offset, int whence);

/** Return current file size. */
size_t vfs_size(int fd);

/** Close file descriptor. */
void   vfs_close(int fd);

/**
 * List directory entries.
 * @param dir     Directory path ("/" or "/tmp/").
 * @param names   Caller-allocated 2-D array (max rows, VFS_NAME_MAX cols).
 * @param max     Maximum entries to return.
 * @return        Number of names written, or -1 on error.
 */
int    vfs_list(const char *dir, char names[][VFS_NAME_MAX], int max);

#endif /* VFS_H */
