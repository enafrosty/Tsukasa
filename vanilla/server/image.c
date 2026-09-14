/*
 * Project Tsukasa — Multi-Format Image Loading Pipeline Implementation
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

#include "image.h"
#include "blitter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_STATIC
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#define STBI_ASSERT(x)            ((void)0)

#include "../include/stb_image.h"

#pragma clang diagnostic pop

int image_load_memory(vanilla_image_t *img, const void *data, size_t len)
{
    if (!img || !data || len == 0)
        return -1;

    memset(img, 0, sizeof(*img));

    int w = 0, h = 0, ch = 0;
    stbi_uc *raw = stbi_load_from_memory((const stbi_uc *)data, (int)len, &w, &h, &ch, 4);
    if (!raw || w <= 0 || h <= 0)
        return -1;

    size_t pixel_count = (size_t)w * (size_t)h;
    img->pixels = (uint32_t *)malloc(pixel_count * sizeof(uint32_t));
    if (!img->pixels) {
        stbi_image_free(raw);
        return -1;
    }

    /* Convert RGBA to native Tsukasa ARGB32 (0xAARRGGBB) */
    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t r = raw[i * 4 + 0];
        uint32_t g = raw[i * 4 + 1];
        uint32_t b = raw[i * 4 + 2];
        uint32_t a = raw[i * 4 + 3];
        img->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    stbi_image_free(raw);
    img->width = w;
    img->height = h;
    img->channels = 4;
    return 0;
}

int image_load_file(vanilla_image_t *img, const char *path)
{
    if (!img || !path)
        return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long sz = ftell(fp);
    if (sz <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    void *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    if (read_bytes != (size_t)sz) {
        free(buf);
        return -1;
    }

    int rc = image_load_memory(img, buf, (size_t)sz);
    free(buf);
    return rc;
}

void image_destroy(vanilla_image_t *img)
{
    if (!img)
        return;

    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
    img->width = 0;
    img->height = 0;
    img->channels = 0;
}

void image_draw(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                const vanilla_image_t *img, int32_t x, int32_t y)
{
    if (!dst || !img || !img->pixels || img->width <= 0 || img->height <= 0)
        return;

    vanilla_rect_t img_rect = { x, y, img->width, img->height };
    vanilla_rect_t vis = img_rect;
    if (clip) {
        if (!vanilla_rect_intersect(&img_rect, clip, &vis))
            return;
    }

    int32_t src_x = vis.x - x;
    int32_t src_y = vis.y - y;

    blt_blend_subrect(dst, pitch_px, vis.x, vis.y,
                      img->pixels, (uint32_t)img->width,
                      src_x, src_y, vis.w, vis.h, 255);
}

void image_draw_scaled(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                       const vanilla_image_t *img, const vanilla_rect_t *dst_rect)
{
    if (!dst || !img || !img->pixels || !dst_rect || dst_rect->w <= 0 || dst_rect->h <= 0)
        return;

    vanilla_rect_t vis = *dst_rect;
    if (clip) {
        if (!vanilla_rect_intersect(dst_rect, clip, &vis))
            return;
    }

    int32_t dw = dst_rect->w;
    int32_t dh = dst_rect->h;
    int32_t sw = img->width;
    int32_t sh = img->height;

    for (int32_t y = vis.y; y < vis.y + vis.h; y++) {
        int32_t sy = (int32_t)((int64_t)(y - dst_rect->y) * sh / dh);
        if (sy < 0)
            sy = 0;
        if (sy >= sh)
            sy = sh - 1;

        const uint32_t *s_row = img->pixels + sy * sw;
        uint32_t *d_row = dst + y * pitch_px;

        for (int32_t x = vis.x; x < vis.x + vis.w; x++) {
            int32_t sx = (int32_t)((int64_t)(x - dst_rect->x) * sw / dw);
            if (sx < 0)
                sx = 0;
            if (sx >= sw)
                sx = sw - 1;

            uint32_t sp = s_row[sx];
            uint32_t sa = (sp >> 24) & 0xFF;
            if (sa == 0)
                continue;

            if (sa == 255) {
                d_row[x] = sp;
            } else {
                uint32_t inv = 255 - sa;
                uint32_t dp = d_row[x];
                uint32_t r = (((sp >> 16) & 0xFF) * sa + ((dp >> 16) & 0xFF) * inv + 127) / 255;
                uint32_t g = (((sp >> 8) & 0xFF) * sa + ((dp >> 8) & 0xFF) * inv + 127) / 255;
                uint32_t b = ((sp & 0xFF) * sa + (dp & 0xFF) * inv + 127) / 255;
                uint32_t a = sa + (((dp >> 24) & 0xFF) * inv + 127) / 255;
                if (a > 255)
                    a = 255;
                d_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}
