/*
 * Project Tsukasa — Initial RAM disk (Multiboot module)
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

#ifndef INITRD_H
#define INITRD_H

#include <stddef.h>

void initrd_init_from_multiboot(const void *mb_info);

/* Look up file in initrd. */
int initrd_lookup(const char *path, const void **data, size_t *size);

#endif /* INITRD_H */
