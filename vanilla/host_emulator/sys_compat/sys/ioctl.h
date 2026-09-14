/*
 * Project Tsukasa — Host Emulator IOCTL Definitions
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

#ifndef _HOST_SYS_IOCTL_H
#define _HOST_SYS_IOCTL_H

#include <stdint.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4601

#define KDSETMODE   0x4B3A
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t bits_per_pixel;
};

struct fb_fix_screeninfo {
    uint32_t line_length;
    uint32_t smem_len;
};

int ioctl(int fd, unsigned long request, ...);

#endif /* _HOST_SYS_IOCTL_H */
