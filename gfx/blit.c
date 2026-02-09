/*
 * blit.c - Bit-blit engine implementation.
 */

#include "blit.h"
#include "../drv/fb.h"
#include <stddef.h>

static inline uint32_t *pixel_ptr(int x, int y)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || fb->bpp != 32)
        return NULL;
    if (x < 0 || x >= (int)fb->width || y < 0 || y >= (int)fb->height)
        return NULL;
    return (uint32_t *)((char *)fb->addr + y * fb->pitch + x * 4);
}

void fb_putpixel(int x, int y, color_t color)
{
    uint32_t *p = pixel_ptr(x, y);
    if (p)
        *p = color;
}

void fb_fill_rect(int x, int y, int w, int h, color_t color)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || fb->bpp != 32)
        return;
    if (w <= 0 || h <= 0)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x + w > (int)fb->width)
        w = fb->width - x;
    if (y + h > (int)fb->height)
        h = fb->height - y;
    if (w <= 0 || h <= 0)
        return;

    for (int row = 0; row < h; row++) {
        uint32_t *p = pixel_ptr(x, y + row);
        for (int col = 0; col < w; col++)
            p[col] = color;
    }
}

void fb_blit(const void *src, void *dst, int w, int h, int pitch)
{
    if (!src || !dst || w <= 0 || h <= 0 || pitch <= 0)
        return;

    const char *s = (const char *)src;
    char *d = (char *)dst;
    int row_bytes = w * 4;
    if (row_bytes > pitch)
        row_bytes = pitch;

    for (int i = 0; i < h; i++) {
        for (int j = 0; j < row_bytes / 4; j++)
            ((uint32_t *)d)[j] = ((const uint32_t *)s)[j];
        s += pitch;
        d += pitch;
    }
}
