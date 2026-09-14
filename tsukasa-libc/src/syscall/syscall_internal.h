/*
 * Project Tsukasa — Internal System Call Helper Declarations
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

#ifndef _TSUKASA_SYSCALL_INTERNAL_H
#define _TSUKASA_SYSCALL_INTERNAL_H

#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>

int64_t __syscall_check(int64_t ret);

#endif /* _TSUKASA_SYSCALL_INTERNAL_H */
