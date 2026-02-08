/*
 * fb.h - Linear framebuffer driver.
 */

#ifndef FB_H
#define FB_H

#include <stdint.h>

/** Framebuffer info. */
struct fb_info {
    void *addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
};

/** Global framebuffer state. */
extern struct fb_info fb_info;

/**
 * Initialize framebuffer from Multiboot info.
 *
 * @param mb_info Multiboot info (NULL to use VGA text fallback).
 * @return 0 on success, -1 if no framebuffer (use VGA text).
 */
int fb_init(const void *mb_info);

/**
 * Get framebuffer base address.
 */
void *fb_addr(void);

/**
 * Get bytes per pixel.
 */
uint8_t fb_bpp(void);

#endif /* FB_H */
