/*
 * fb.c - Linear framebuffer driver.
 * Uses Multiboot framebuffer info when available.
 */

#include "fb.h"
#include "../include/multiboot.h"
#include <stddef.h>

struct fb_info fb_info = { NULL, 0, 0, 0, 0 };

int fb_init(const void *mb_info)
{
    const struct multiboot_info *mb = (const struct multiboot_info *)mb_info;

    if (!mb || !(mb->flags & MULTIBOOT_INFO_FRAMEBUFFER))
        return -1;

    fb_info.addr = (void *)(uintptr_t)(mb->framebuffer_addr & 0xFFFFFFFFu);
    fb_info.pitch = mb->framebuffer_pitch;
    fb_info.width = mb->framebuffer_width;
    fb_info.height = mb->framebuffer_height;
    fb_info.bpp = mb->framebuffer_bpp;

    if (!fb_info.addr || fb_info.width == 0 || fb_info.height == 0)
        return -1;

    return 0;
}

void *fb_addr(void)
{
    return fb_info.addr;
}

uint8_t fb_bpp(void)
{
    return fb_info.bpp;
}
