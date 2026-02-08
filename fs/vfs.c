/*
 * vfs.c - Minimal VFS implementation.
 */

#include "vfs.h"
#include "initrd.h"
#include <stddef.h>
#include <stdint.h>

static struct vfs_file fd_table[VFS_MAX_FD];
static int fd_used[VFS_MAX_FD];

void vfs_init(const void *mb_info)
{
    for (int i = 0; i < VFS_MAX_FD; i++)
        fd_used[i] = 0;
    initrd_init_from_multiboot(mb_info);
}

int vfs_open(const char *path)
{
    const void *data;
    size_t size;
    if (initrd_lookup(path, &data, &size) != 0)
        return -1;
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (!fd_used[i]) {
            fd_table[i].data = data;
            fd_table[i].size = size;
            fd_table[i].pos = 0;
            fd_used[i] = 1;
            return i;
        }
    }
    return -1;
}

size_t vfs_read(int fd, void *buf, size_t count)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_used[fd] || !buf)
        return 0;
    struct vfs_file *f = &fd_table[fd];
    if (f->pos >= f->size)
        return 0;
    if (count > f->size - f->pos)
        count = f->size - f->pos;
    const uint8_t *src = (const uint8_t *)f->data + f->pos;
    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++)
        dst[i] = src[i];
    f->pos += count;
    return count;
}

size_t vfs_seek(int fd, size_t offset, int whence)
{
    if (fd < 0 || fd >= VFS_MAX_FD || !fd_used[fd])
        return (size_t)-1;
    struct vfs_file *f = &fd_table[fd];
    switch (whence) {
    case VFS_SEEK_SET:
        f->pos = offset;
        break;
    case VFS_SEEK_CUR:
        f->pos += offset;
        break;
    case VFS_SEEK_END:
        f->pos = f->size + offset;
        break;
    default:
        return (size_t)-1;
    }
    if (f->pos > f->size)
        f->pos = f->size;
    return f->pos;
}

void vfs_close(int fd)
{
    if (fd >= 0 && fd < VFS_MAX_FD)
        fd_used[fd] = 0;
}
