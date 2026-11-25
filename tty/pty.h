/*
 * Project Tsukasa — Pseudo-terminal (PTY) subsystem (/dev/ptmx and /dev/pts/N)
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

#ifndef TSUKASA_PTY_H
#define TSUKASA_PTY_H

#include "../fs/devfs.h"

void pty_init(void);

const devfs_ops_t *pty_get_ptmx_ops(void);
const devfs_ops_t *pty_get_slave_ops(void);

void *pty_lookup_by_id(int id);
int pty_slave_open_existing(void *priv, int flags);

#endif /* TSUKASA_PTY_H */
