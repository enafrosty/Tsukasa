/**
 * @file fb_gfx.h
 * @brief Minimal header-only framebuffer graphics primitives.
 *
 * This header provides a tiny C/C++ API for drawing into a linear
 * framebuffer with an explicit pitch/stride. All row stepping uses
 * the hardware-provided pitch to avoid diagonal tearing when the
 * stride in memory is wider than width * bytes_per_pixel.
 *
 * Supported bits-per-pixel values: 8, 16, 24, 32.
 * Colors are passed as opaque 32-bit packed values; you are
 * responsible for packing them to match your pixel format.
 *
 * Example usage in your kernel:
 *
 *   #include "fb_gfx.h"
 *
 *   extern void* g_framebuffer;
 *   extern unsigned g_width, g_height, g_pitch, g_bpp;
 *
 *   void kmain(void) {
 *       FbGfxContext ctx = fb_gfx_make_context(
 *           g_framebuffer,
 *           g_width,
 *           g_height,
 *           g_pitch,
 *           g_bpp
 *       );
 *
 *       // Clear screen to black.
 *       fb_gfx_clear_screen(&ctx, 0x00000000u);
 *
 *       // Draw a solid red rectangle (format-dependent).
 *       fb_gfx_draw_rect(&ctx, 10, 10, 100, 50, 0x00FF0000u);
 *   }
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Framebuffer graphics context.
 *
 * All dimensions are in pixels, except @ref pitch which is in bytes.
 *
 * - framebuffer: Base address of the linear framebuffer.
 * - width, height: Visible resolution in pixels.
 * - pitch: Stride in bytes between the start of two consecutive scanlines.
 *          This must be taken directly from your video mode information
 *          (do not recompute as width * bytes_per_pixel).
 * - bpp: Bits per pixel (8, 16, 24, or 32).
 */
typedef struct FbGfxContext {
    uint8_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch; /* bytes per scanline */
    uint32_t bpp;   /* bits per pixel */
} FbGfxContext;

/**
 * @brief Construct a framebuffer graphics context.
 *
 * @param fb     Base address of the framebuffer.
 * @param width  Visible width in pixels.
 * @param height Visible height in pixels.
 * @param pitch  Stride in bytes between the start of two consecutive scanlines.
 * @param bpp    Bits per pixel (8, 16, 24, or 32).
 * @return Initialized FbGfxContext value.
 */
static inline FbGfxContext
fb_gfx_make_context(void* fb,
                    uint32_t width,
                    uint32_t height,
                    uint32_t pitch,
                    uint32_t bpp)
{
    FbGfxContext ctx;
    ctx.framebuffer = (uint8_t*)fb;
    ctx.width = width;
    ctx.height = height;
    ctx.pitch = pitch;
    ctx.bpp = bpp;
    return ctx;
}

/**
 * @brief Get bytes per pixel from context.
 *
 * Returns 0 for unsupported @ref bpp values.
 */
static inline uint32_t
fb_gfx_bytes_per_pixel(const FbGfxContext* ctx)
{
    if (!ctx) {
        return 0u;
    }

    switch (ctx->bpp) {
    case 8:  return 1u;
    case 16: return 2u;
    case 24: return 3u;
    case 32: return 4u;
    default: return 0u;
    }
}

/**
 * @brief Compute a pointer to pixel (x, y) or NULL if out-of-bounds.
 *
 * Uses the context's @ref pitch for row stepping to avoid diagonal
 * tearing when the framebuffer stride is not exactly
 * width * bytes_per_pixel.
 */
static inline uint8_t*
fb_gfx_pixel_ptr(const FbGfxContext* ctx, uint32_t x, uint32_t y)
{
    if (!ctx || !ctx->framebuffer) {
        return NULL;
    }

    if (x >= ctx->width || y >= ctx->height) {
        return NULL;
    }

    const uint32_t bpp_bytes = fb_gfx_bytes_per_pixel(ctx);
    if (bpp_bytes == 0u) {
        return NULL;
    }

    uint8_t* row = ctx->framebuffer + (size_t)y * (size_t)ctx->pitch;
    return row + (size_t)x * (size_t)bpp_bytes;
}

/**
 * @brief Write a single pixel at (x, y) with the given color.
 *
 * The @p color value is treated as an opaque packed pixel. For example,
 * in a 32bpp X8R8G8B8 mode on little-endian machines, you can pack it
 * as 0x00RRGGBB. For 24bpp, the least significant three bytes of
 * @p color are written in little-endian order.
 *
 * Out-of-bounds coordinates are ignored.
 */
static inline void
fb_gfx_put_pixel(const FbGfxContext* ctx, uint32_t x, uint32_t y, uint32_t color)
{
    uint8_t* p = fb_gfx_pixel_ptr(ctx, x, y);
    if (!p) {
        return;
    }

    switch (ctx->bpp) {
    case 8: {
        p[0] = (uint8_t)(color & 0xFFu);
        break;
    }
    case 16: {
        /* Write two bytes in little-endian order. */
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        break;
    }
    case 24: {
        /* Write three bytes in little-endian order. */
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
        break;
    }
    case 32: {
        /* Write four bytes in little-endian order. */
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
        p[3] = (uint8_t)((color >> 24) & 0xFFu);
        break;
    }
    default:
        /* Unsupported bpp: nothing written. */
        break;
    }
}

/**
 * @brief Draw a solid filled rectangle.
 *
 * The rectangle is defined by its top-left corner (@p x, @p y) and its
 * width @p w and height @p h (in pixels). Negative coordinates and
 * rectangles that extend off-screen are clipped against the visible
 * framebuffer area.
 *
 * All row stepping uses @ref pitch so that diagonal tearing does not
 * occur when the memory stride differs from width * bytes_per_pixel.
 */
static inline void
fb_gfx_draw_rect(const FbGfxContext* ctx,
                 int32_t x,
                 int32_t y,
                 int32_t w,
                 int32_t h,
                 uint32_t color)
{
    if (!ctx || !ctx->framebuffer) {
        return;
    }

    if (w <= 0 || h <= 0) {
        return;
    }

    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)ctx->width)  x1 = (int32_t)ctx->width;
    if (y1 > (int32_t)ctx->height) y1 = (int32_t)ctx->height;

    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    const uint32_t bpp_bytes = fb_gfx_bytes_per_pixel(ctx);
    if (bpp_bytes == 0u) {
        return;
    }

    for (int32_t yy = y0; yy < y1; ++yy) {
        uint8_t* row_start = fb_gfx_pixel_ptr(ctx, (uint32_t)x0, (uint32_t)yy);
        if (!row_start) {
            break;
        }

        uint8_t* p = row_start;

        switch (ctx->bpp) {
        case 8:
            for (int32_t xx = x0; xx < x1; ++xx) {
                p[0] = (uint8_t)(color & 0xFFu);
                p += 1;
            }
            break;
        case 16:
            for (int32_t xx = x0; xx < x1; ++xx) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p += 2;
            }
            break;
        case 24:
            for (int32_t xx = x0; xx < x1; ++xx) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p[2] = (uint8_t)((color >> 16) & 0xFFu);
                p += 3;
            }
            break;
        case 32:
            for (int32_t xx = x0; xx < x1; ++xx) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p[2] = (uint8_t)((color >> 16) & 0xFFu);
                p[3] = (uint8_t)((color >> 24) & 0xFFu);
                p += 4;
            }
            break;
        default:
            /* Unsupported bpp: nothing drawn on this row. */
            return;
        }
    }
}

/**
 * @brief Clear the entire framebuffer to a solid color.
 *
 * All row stepping uses @ref pitch so that diagonal tearing does not
 * occur when the memory stride differs from width * bytes_per_pixel.
 */
static inline void
fb_gfx_clear_screen(const FbGfxContext* ctx, uint32_t color)
{
    if (!ctx || !ctx->framebuffer) {
        return;
    }

    const uint32_t bpp_bytes = fb_gfx_bytes_per_pixel(ctx);
    if (bpp_bytes == 0u) {
        return;
    }

    for (uint32_t y = 0; y < ctx->height; ++y) {
        uint8_t* row = ctx->framebuffer + (size_t)y * (size_t)ctx->pitch;
        uint8_t* p = row;

        switch (ctx->bpp) {
        case 8:
            for (uint32_t x = 0; x < ctx->width; ++x) {
                p[0] = (uint8_t)(color & 0xFFu);
                p += 1;
            }
            break;
        case 16:
            for (uint32_t x = 0; x < ctx->width; ++x) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p += 2;
            }
            break;
        case 24:
            for (uint32_t x = 0; x < ctx->width; ++x) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p[2] = (uint8_t)((color >> 16) & 0xFFu);
                p += 3;
            }
            break;
        case 32:
            for (uint32_t x = 0; x < ctx->width; ++x) {
                p[0] = (uint8_t)(color & 0xFFu);
                p[1] = (uint8_t)((color >> 8) & 0xFFu);
                p[2] = (uint8_t)((color >> 16) & 0xFFu);
                p[3] = (uint8_t)((color >> 24) & 0xFFu);
                p += 4;
            }
            break;
        default:
            /* Unsupported bpp: nothing cleared. */
            return;
        }
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif

