/*
 * Project Tsukasa — @file fb_gfx.h
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

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* @brief Framebuffer graphics context. */
typedef struct FbGfxContext {
    uint8_t* framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} FbGfxContext;

/* @brief Construct a framebuffer graphics context. */
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

/* @brief Get bytes per pixel from context. Returns 0 for unsupported @ref bpp values. */
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

/* @brief Compute a pointer to pixel (x, y) or NULL if out-of-bounds. */
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

/* @brief Write a single pixel at (x, y) with the given color. */
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
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        break;
    }
    case 24: {
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
        break;
    }
    case 32: {
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
        p[3] = (uint8_t)((color >> 24) & 0xFFu);
        break;
    }
    default:
        break;
    }
}

/* @brief Draw a solid filled rectangle. */
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
            return;
        }
    }
}

/* @brief Clear the entire framebuffer to a solid color. */
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
            return;
        }
    }
}

#ifdef __cplusplus
}
#endif
