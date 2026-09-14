/*
 * Project Tsukasa — Vanilla Display Server Host Compatibility Layer Header
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

#ifndef _VANILLA_HOST_COMPAT_H
#define _VANILLA_HOST_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>

/* In-process shared memory shims for host prototyping */
int   shm_create(size_t size);
void *shm_attach(int shm_id);
int   shm_detach(const void *addr);
int   shm_destroy(int shm_id);

#endif /* _VANILLA_HOST_COMPAT_H */
