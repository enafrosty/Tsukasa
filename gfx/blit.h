/*
 * blit.h - Bit-blit engine for pixel drawing and memory copies.
 */

#ifndef BLIT_H
#define BLIT_H

#include <stdint.h>

/** 32-bit color: 0xAARRGGBB. */
typedef uint32_t color_t;

/**
 * Put a pixel at (x, y).
 */
void fb_putpixel(int x, int y, color_t color);

/**
 * Fill a rectangle with a solid color.
 */
void fb_fill_rect(int x, int y, int w, int h, color_t color);

/**
 * Copy a rectangle from src to dst (same pitch).
 */
void fb_blit(const void *src, void *dst, int w, int h, int pitch);

/**
 * Create color from RGB components.
 */
static inline color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

#endif /* BLIT_H */
