/*
 * bootfs.h - Read-only boot module filesystem.
 */

#ifndef BOOTFS_H
#define BOOTFS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

void bootfs_init(const void *boot_info);
int bootfs_module_count(void);

int bootfs_stat(const char *path, vfs_stat_t *out);
int bootfs_list(const char *path, char names[][VFS_NAME_MAX], int max);
int bootfs_read_file(const char *path,
                     const void **data_out,
                     size_t *size_out,
                     int *owns_buffer_out);

#endif /* BOOTFS_H */
