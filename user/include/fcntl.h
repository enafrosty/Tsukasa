/*
 * Project Tsukasa — File control options header
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

#ifndef TSUKASA_FCNTL_H
#define TSUKASA_FCNTL_H

#include "sys/types.h"

#define O_RDONLY   0x0001
#define O_WRONLY   0x0002
#define O_RDWR     (O_RDONLY | O_WRONLY)
#define O_APPEND   0x0004
#define O_CREAT    0x0008
#define O_TRUNC    0x0010
#define O_NONBLOCK 0x0020

#define F_GETFL 1
#define F_SETFL 2

int open(const char *pathname, int flags, ...);
int fcntl(int fd, int cmd, ...);

#endif /* TSUKASA_FCNTL_H */
