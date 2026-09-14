/*
 * Project Tsukasa — Linear framebuffer driver
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

#ifndef FB_H
#define FB_H

#include <stdint.h>
#include <stddef.h>

/* Framebuffer info. */
struct fb_info {
    void *addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uintptr_t phys;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

/* Global framebuffer state. */
extern struct fb_info fb_info;

/* Initialize framebuffer from boot info. */
int fb_init(const void *mb_info);

/* Framebuffer byte size. */
size_t fb_byte_size(void);

/* Get framebuffer base address. */
void *fb_addr(void);

/* Get bytes per pixel. */
uint8_t fb_bpp(void);

/* DevFS device operations for /dev/fb0. */
size_t fb_read(size_t pos, void *buf, size_t count);
size_t fb_write(size_t pos, const void *buf, size_t count);
int fb_ioctl(unsigned long request, void *arg);
void *fb_mmap(void *addr, size_t length, int prot, int flags, size_t offset);
int fb_munmap(void *addr, size_t length);
int fb_poll(int events);

/* KD console mode state. */
int fb_kd_mode(void);
void fb_set_kd_mode(int mode);

#endif /* FB_H */
