/*
 * sysfs.h - Read-only system pseudo filesystem.
 */

#ifndef SYSFS_H
#define SYSFS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

void sysfs_init(void);

int sysfs_stat(const char *path, vfs_stat_t *out);
int sysfs_list(const char *path, char names[][VFS_NAME_MAX], int max);
int sysfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out);

#endif /* SYSFS_H */
