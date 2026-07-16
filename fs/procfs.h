/*
 * procfs.h - Read-only process pseudo filesystem.
 */

#ifndef PROCFS_H
#define PROCFS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

void procfs_init(void);

int procfs_stat(const char *path, vfs_stat_t *out);
int procfs_list(const char *path, char names[][VFS_NAME_MAX], int max);
int procfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out);

#endif /* PROCFS_H */
