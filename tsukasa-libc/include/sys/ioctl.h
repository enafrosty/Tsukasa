/*
 * Project Tsukasa — I/O Control Macros and Data Structures
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

#ifndef _TSUKASA_SYS_IOCTL_H
#define _TSUKASA_SYS_IOCTL_H

#include <stdint.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define KDSETMODE   0x4B3A
#define KDGETMODE   0x4B3B
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01

#define VT_GETSTATE   0x5603
#define VT_ACTIVATE   0x5606
#define VT_WAITACTIVE 0x5607

struct vt_stat {
    uint16_t v_active;
    uint16_t v_signal;
    uint16_t v_state;
};

/* TTY / PTY ioctls (Linux x86_64 compatible) */
#define TCGETS     0x5401UL
#define TCSETS     0x5402UL
#define TCSETSW    0x5403UL
#define TCSETSF    0x5404UL
#define TIOCSCTTY  0x540EUL
#define TIOCGPGRP  0x540FUL
#define TIOCSPGRP  0x5410UL
#define TIOCGWINSZ 0x5413UL
#define TIOCSWINSZ 0x5414UL
#define FIONREAD   0x541BUL
#define TIOCGPTN   0x80045430UL
#define TIOCSPTLCK 0x40045431UL

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* Block device ioctls (Linux-compatible) */
#define BLKRRPART    0x125F
#define BLKSSZGET    0x1268
#define BLKGETSIZE64 0x80081272UL

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct fb_fix_screeninfo {
    char id[16];
    uintptr_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t visual;
    uint32_t line_length;
};

int ioctl(int fd, unsigned long request, ...);

#endif /* _TSUKASA_SYS_IOCTL_H */
