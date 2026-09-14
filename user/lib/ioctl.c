/*
 * Project Tsukasa — include "../include/sys/ioctl.h"
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

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "user/include/sys/ioctl.h"
#include "user/lib/syscall.h"

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    uintptr_t arg;
    va_start(ap, request);
    arg = va_arg(ap, uintptr_t);
    va_end(ap);
    return fs_ioctl(fd, request, (void *)arg);
}
