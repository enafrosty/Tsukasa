/*
 * Project Tsukasa — AF_UNIX socket ABI definitions (guide 20)
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

#ifndef TSUKASA_SOCKET_DEFS_H
#define TSUKASA_SOCKET_DEFS_H

#include <stdint.h>

#define TSK_AF_UNIX      1
#define TSK_SOCK_STREAM  1

#define TSK_UNIX_PATH_MAX 108   /* buffer size incl. NUL; 107 usable bytes */

struct tsk_sockaddr_un {
    uint16_t sun_family;
    char     sun_path[TSK_UNIX_PATH_MAX];
};

#endif /* TSUKASA_SOCKET_DEFS_H */
