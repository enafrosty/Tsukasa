/*
 * Project Tsukasa — Standard POSIX System Types
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

#ifndef _TSUKASA_SYS_TYPES_H
#define _TSUKASA_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t   pid_t;
typedef int32_t   uid_t;
typedef int32_t   gid_t;
typedef uint32_t  mode_t;
typedef int64_t   off_t;
typedef int64_t   ssize_t;
typedef int64_t   time_t;
typedef int64_t   suseconds_t;
typedef uint32_t  useconds_t;
typedef uint64_t  dev_t;
typedef uint64_t  ino_t;
typedef uint32_t  nlink_t;

#endif /* _TSUKASA_SYS_TYPES_H */
