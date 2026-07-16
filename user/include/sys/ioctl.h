#ifndef TSUKASA_SYS_IOCTL_H
#define TSUKASA_SYS_IOCTL_H

#include <stdint.h>
#include "../syscall_nums.h"

#define FBIOGET_VSCREENINFO TSUKASA_FBIOGET_VSCREENINFO
#define FBIOGET_FSCREENINFO TSUKASA_FBIOGET_FSCREENINFO

#define FB_TYPE_PACKED_PIXELS 0
#define FB_VISUAL_TRUECOLOR   2

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t bits_per_pixel;
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

#endif /* TSUKASA_SYS_IOCTL_H */
