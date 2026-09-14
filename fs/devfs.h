/*
 * Project Tsukasa — Device filesystem (DevFS)
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

#ifndef DEVFS_H
#define DEVFS_H

#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

typedef struct devfs_ops {
    int    (*open)(void *priv, int flags);
    void   (*close)(void *priv);
    size_t (*read)(void *priv, size_t pos, void *buf, size_t count, int flags);
    size_t (*write)(void *priv, size_t pos, const void *buf, size_t count);
    int    (*ioctl)(void *priv, unsigned long request, void *arg);
    void  *(*mmap)(void *priv, void *addr, size_t length, int prot, int flags, size_t offset);
    int    (*munmap)(void *priv, void *addr, size_t length);
    int    (*poll)(void *priv, int events);
    size_t (*size)(void *priv);
} devfs_ops_t;

void devfs_init(void);
int devfs_register_device(const char *name, uint32_t type, const devfs_ops_t *ops, void *priv);

int devfs_stat(const char *path, vfs_stat_t *out);
int devfs_list(const char *path, char names[][VFS_NAME_MAX], int max);
int devfs_lookup(const char *subpath, const devfs_ops_t **ops_out, void **priv_out, uint32_t *type_out);

#endif /* DEVFS_H */
