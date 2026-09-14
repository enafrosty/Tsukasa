/*
 * Project Tsukasa — Read-only process pseudo filesystem
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
