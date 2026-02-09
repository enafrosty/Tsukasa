/*
 * vfs.h - Virtual File System interface.
 */

#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_FD 16
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

struct vfs_file {
    const void *data;
    size_t size;
    size_t pos;
};

/**
 * Initialize VFS.
 *
 * @param mb_info Multiboot info for initrd (may be NULL).
 */
void vfs_init(const void *mb_info);

/**
 * Open a file by path (e.g. "/initrd/hello").
 *
 * @return File descriptor (>= 0) or -1.
 */
int vfs_open(const char *path);

/**
 * Read from file.
 *
 * @return Bytes read.
 */
size_t vfs_read(int fd, void *buf, size_t count);

/**
 * Seek in file.
 *
 * @return New position or (size_t)-1.
 */
size_t vfs_seek(int fd, size_t offset, int whence);

/**
 * Close file.
 */
void vfs_close(int fd);

#endif /* VFS_H */
