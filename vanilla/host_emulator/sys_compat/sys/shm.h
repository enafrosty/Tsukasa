/*
 * Project Tsukasa — Host Emulator Shared Memory Definitions
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

#ifndef _HOST_SYS_SHM_H
#define _HOST_SYS_SHM_H

#include <stddef.h>

int   shm_create(size_t size);
void *shm_attach(int shm_id);
int   shm_detach(const void *addr);
int   shm_destroy(int shm_id);

#endif /* _HOST_SYS_SHM_H */
