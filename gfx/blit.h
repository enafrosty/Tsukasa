/*
 * blit.h  -  Bit-blit engine for pixel drawing and compositing.
 *
 * Color format: 0xAARRGGBB (32-bit).
 * The alpha byte is used by compositing helpers; raw fb writes ignore it.
 */

#ifndef BLIT_H
#define BLIT_H

#include <stdint.h>

/** 32-bit color: 0xAARRGGBB. */
typedef uint32_t color_t;

/* ---- Basic primitives ------------------------------------------------- */

/** Put a pixel at (x, y) – ignores alpha, direct write. */
void fb_putpixel(int x, int y, color_t color);

/** Alpha-blend a pixel at (x, y) over whatever is already there. */
void fb_blend_pixel(int x, int y, color_t color);

/** Fill a rectangle with a solid color (no alpha). */
void fb_fill_rect(int x, int y, int w, int h, color_t color);

/** Fill a rectangle blending with the framebuffer (alpha from color). */
void fb_fill_rect_alpha(int x, int y, int w, int h, color_t color);

/** Draw a horizontal line (1 pixel high). */
void fb_draw_hline(int x, int y, int len, color_t color);

/** Draw a vertical line (1 pixel wide). */
void fb_draw_vline(int x, int y, int len, color_t color);

/* ---- Gradient / shape helpers ---------------------------------------- */

/** Vertical gradient from color @c top to @c bot across @c h rows. */
void fb_fill_gradient_v(int x, int y, int w, int h,
                        color_t top, color_t bot);

/**
 * Draw a drop shadow beneath a rectangle.
 * The shadow is drawn OUTSIDE (below and to the right) of the given rect.
 * @radius: shadow spread in pixels (1-8 recommended).
 */
void fb_draw_shadow_rect(int x, int y, int w, int h, int radius);

/**
 * Fill a rounded rectangle (solid, no alpha).
 * @r: corner radius in pixels.
 */
void fb_fill_rounded_rect(int x, int y, int w, int h, int r, color_t color);

/**
 * Draw the outline of a rounded rectangle (1px border).
 */
void fb_draw_rounded_rect(int x, int y, int w, int h, int r, color_t color);

/**
 * Fill a circle centered at (cx, cy) with the given color.
 */
void fb_fill_circle(int cx, int cy, int radius, color_t color);

/* ---- Blit ------------------------------------------------------------- */

/**
 * Copy a rectangle from src to dst using pitch-aware row stepping.
 * Both src and dst are assumed to have the same pitch as the framebuffer.
 */
void fb_blit(const void *src, void *dst, int w, int h, int pitch);

/**
 * Blit a 32-bit ARGB pixel buffer (w×h pixels) to framebuffer at (x,y),
 * using alpha blending for each pixel.
 */
void fb_blit_alpha(int x, int y, const uint32_t *pixels, int w, int h);

/* ---- Color constructor ------------------------------------------------ */

static inline color_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline color_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  | (uint32_t)b;
}

#endif /* BLIT_H */
