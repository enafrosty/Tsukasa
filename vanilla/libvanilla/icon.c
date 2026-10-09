/*
 * Project Tsukasa — libvanilla Icon Loading and Rendering Implementation
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

#include "icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
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

#if defined(STBI_NO_THREAD_LOCALS)
void stbi_set_unpremultiply_on_load_thread(int flag) { (void)flag; }
void stbi_convert_iphone_png_to_rgb_thread(int flag) { (void)flag; }
void stbi_set_flip_vertically_on_load_thread(int flag) { (void)flag; }
#endif

typedef struct {
    const char *name;
    const char *short_name;
} app_alias_t;

static const app_alias_t g_app_aliases[] = {
    { "terminal",       "TERM" },
    { "file manager",   "FILEMGR" },
    { "filemgr",        "FILEMGR" },
    { "notepad",        "NOTEPAD" },
    { "calculator",     "CALC" },
    { "calc",           "CALC" },
    { "task manager",   "TASKMGR" },
    { "taskmgr",        "TASKMGR" },
    { "system fetch",   "SYSFETCH" },
    { "sysfetch",       "SYSFETCH" },
    { "settings",       "SETTINGS" },
    { "process viewer", "PSVIEW" },
    { "psview",         "PSVIEW" },
    { "ps",             "PSVIEW" },
    { "network info",   "NETINFO" },
    { "netinfo",        "NETINFO" },
    { "net",            "NETINFO" },
};

static const char *g_search_dirs[] = {
    "/icons/",
    "/fat12/ICONS/",
    "icons/",
    "fat12/ICONS/",
    "vanilla/icons/builtin/",
    "icons/builtin/",
    "/assets/icons/",
    "assets/icons/",
    "/assets/",
    "assets/",
    "/",
    "",
};

static inline void blend_pixel(uint32_t *dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)
        return;

    if (sa == 255) {
        *dst = src;
        return;
    }

    uint32_t dp = *dst;
    uint32_t da = (dp >> 24) & 0xFF;
    if (da == 0) {
        *dst = src;
        return;
    }

    uint32_t inv = 255 - sa;

    uint32_t r = (((src >> 16) & 0xFF) * sa + ((dp >> 16) & 0xFF) * inv + 127) / 255;
    uint32_t g = (((src >> 8) & 0xFF) * sa + ((dp >> 8) & 0xFF) * inv + 127) / 255;
    uint32_t b = ((src & 0xFF) * sa + (dp & 0xFF) * inv + 127) / 255;
    uint32_t a = sa + (da * inv + 127) / 255;
    if (a > 255)
        a = 255;

    *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

static void *read_file_bytes(const char *path, size_t *out_sz)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long sz = ftell(fp);
    if (sz <= 0 || sz > 16 * 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    void *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }

    if (out_sz)
        *out_sz = (size_t)sz;

    return buf;
}

static void to_lower_str(const char *src, char *dst, size_t max)
{
    size_t i = 0;
    while (src[i] && i + 1 < max) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z')
            dst[i] = (char)(c + 32);
        else
            dst[i] = c;
        i++;
    }
    dst[i] = '\0';
}

static void to_upper_str(const char *src, char *dst, size_t max)
{
    size_t i = 0;
    while (src[i] && i + 1 < max) {
        char c = src[i];
        if (c >= 'a' && c <= 'z')
            dst[i] = (char)(c - 32);
        else
            dst[i] = c;
        i++;
    }
    dst[i] = '\0';
}

icon_t *icon_load(const char *name)
{
    if (!name || !name[0])
        return NULL;

    /* Extract leaf name if path passed */
    const char *leaf = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    }

    char base[32];
    strncpy(base, leaf, sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';

    /* Strip extension if present */
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';

    char lower[32];
    to_lower_str(base, lower, sizeof(lower));

    char upper[32];
    to_upper_str(base, upper, sizeof(upper));

    const char *short_name = NULL;
    size_t num_aliases = sizeof(g_app_aliases) / sizeof(g_app_aliases[0]);
    for (size_t i = 0; i < num_aliases; i++) {
        if (strcmp(lower, g_app_aliases[i].name) == 0) {
            short_name = g_app_aliases[i].short_name;
            break;
        }
    }

    const char *candidates[4];
    int cand_count = 0;
    if (short_name)
        candidates[cand_count++] = short_name;
    candidates[cand_count++] = upper;
    candidates[cand_count++] = lower;
    candidates[cand_count++] = base;

    size_t dir_count = sizeof(g_search_dirs) / sizeof(g_search_dirs[0]);

    /* Pass 1: Probe .vco / .VCO files */
    for (size_t d = 0; d < dir_count; d++) {
        for (int c = 0; c < cand_count; c++) {
            char path[256];

            /* Lowercase extension */
            snprintf(path, sizeof(path), "%s%s.vco", g_search_dirs[d], candidates[c]);
            size_t sz = 0;
            void *buf = read_file_bytes(path, &sz);

            /* Uppercase extension */
            if (!buf) {
                snprintf(path, sizeof(path), "%s%s.VCO", g_search_dirs[d], candidates[c]);
                buf = read_file_bytes(path, &sz);
            }

            if (buf) {
                if (sz >= sizeof(vico_hdr_t)) {
                    vico_hdr_t *hdr = (vico_hdr_t *)buf;
                    if (hdr->magic == VICO_MAGIC && hdr->version == VICO_VERSION) {
                        icon_t *icon = (icon_t *)malloc(sizeof(icon_t));
                        if (!icon) {
                            free(buf);
                            return NULL;
                        }
                        memset(icon, 0, sizeof(*icon));
                        snprintf(icon->name, sizeof(icon->name), "%s", base);
                        icon->source.is_bmp = 0;
                        icon->source.vico.is_loaded = 1;
                        icon->source.vico.data = (uint8_t *)buf;
                        icon->source.vico.data_len = (uint32_t)sz;
                        return icon;
                    }
                }
                free(buf);
            }
        }
    }

    /* Pass 2: Fallback to .bmp / .BMP files */
    for (size_t d = 0; d < dir_count; d++) {
        for (int c = 0; c < cand_count; c++) {
            char path[256];

            snprintf(path, sizeof(path), "%s%s.bmp", g_search_dirs[d], candidates[c]);
            size_t sz = 0;
            void *buf = read_file_bytes(path, &sz);

            if (!buf) {
                snprintf(path, sizeof(path), "%s%s.BMP", g_search_dirs[d], candidates[c]);
                buf = read_file_bytes(path, &sz);
            }

            if (buf) {
                int w = 0, h = 0, ch = 0;
                stbi_uc *raw = stbi_load_from_memory((const stbi_uc *)buf, (int)sz, &w, &h, &ch, 4);
                free(buf);

                if (raw && w > 0 && h > 0) {
                    size_t pixel_count = (size_t)w * (size_t)h;
                    uint32_t *pixels = (uint32_t *)malloc(pixel_count * sizeof(uint32_t));
                    if (!pixels) {
                        stbi_image_free(raw);
                        return NULL;
                    }

                    for (size_t i = 0; i < pixel_count; i++) {
                        uint32_t r = raw[i * 4 + 0];
                        uint32_t g = raw[i * 4 + 1];
                        uint32_t b = raw[i * 4 + 2];
                        uint32_t a = raw[i * 4 + 3];
                        pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                    stbi_image_free(raw);

                    icon_t *icon = (icon_t *)malloc(sizeof(icon_t));
                    if (!icon) {
                        free(pixels);
                        return NULL;
                    }
                    memset(icon, 0, sizeof(*icon));
                    snprintf(icon->name, sizeof(icon->name), "%s", base);
                    icon->source.is_bmp = 1;
                    icon->source.bmp_w = w;
                    icon->source.bmp_h = h;
                    icon->source.pixels = pixels;
                    return icon;
                }
            }
        }
    }

    return NULL;
}

int icon_render(const icon_t *icon, int target_size,
                uint32_t *out_pixels, int stride_px)
{
    if (!icon || target_size <= 0 || target_size > 512 || !out_pixels || stride_px < target_size)
        return -1;

    if (icon->source.is_bmp) {
        if (!icon->source.pixels || icon->source.bmp_w <= 0 || icon->source.bmp_h <= 0)
            return -1;

        int sw = icon->source.bmp_w;
        int sh = icon->source.bmp_h;

        for (int y = 0; y < target_size; y++) {
            int sy = (y * sh) / target_size;
            if (sy >= sh)
                sy = sh - 1;
            const uint32_t *src_row = icon->source.pixels + sy * sw;
            uint32_t *dst_row = out_pixels + y * stride_px;

            for (int x = 0; x < target_size; x++) {
                int sx = (x * sw) / target_size;
                if (sx >= sw)
                    sx = sw - 1;
                blend_pixel(&dst_row[x], src_row[sx]);
            }
        }
        return 0;
    }

    if (!icon->source.vico.is_loaded || !icon->source.vico.data)
        return -1;

    return vico_rasterize(icon->source.vico.data, icon->source.vico.data_len,
                          target_size, out_pixels, stride_px);
}

int icon_render_to_surface(const icon_t *icon, vanilla_surface_t *surf,
                           int32_t x, int32_t y, int target_size)
{
    if (!icon || !surf || !surf->pixels || target_size <= 0 || target_size > 512)
        return -1;

    if (surf->width == 0 || surf->height == 0 || surf->pitch < surf->width * sizeof(uint32_t))
        return -1;

    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;

    if (x >= surf_w || y >= surf_h || x + target_size <= 0 || y + target_size <= 0)
        return 0;

    uint32_t stack_buf[64 * 64];
    uint32_t *buf = stack_buf;
    if (target_size > 64) {
        buf = (uint32_t *)malloc((size_t)target_size * (size_t)target_size * sizeof(uint32_t));
        if (!buf)
            return -1;
    }

    memset(buf, 0, (size_t)target_size * (size_t)target_size * sizeof(uint32_t));

    int rc = icon_render(icon, target_size, buf, target_size);
    if (rc < 0) {
        if (buf != stack_buf)
            free(buf);
        return rc;
    }

    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);

    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = (x + target_size > surf_w) ? surf_w : (x + target_size);
    int32_t y1 = (y + target_size > surf_h) ? surf_h : (y + target_size);

    for (int32_t cy = y0; cy < y1; cy++) {
        int32_t src_y = cy - y;
        uint32_t *src_row = buf + src_y * target_size;
        uint32_t *dst_row = surf->pixels + (size_t)cy * pitch_px;
        for (int32_t cx = x0; cx < x1; cx++) {
            int32_t src_x = cx - x;
            blend_pixel(&dst_row[cx], src_row[src_x]);
        }
    }

    if (buf != stack_buf)
        free(buf);

    return 0;
}

void icon_free(icon_t *icon)
{
    if (!icon)
        return;

    if (icon->source.is_bmp && icon->source.pixels) {
        free(icon->source.pixels);
        icon->source.pixels = NULL;
    }

    if (!icon->source.is_bmp && icon->source.vico.data) {
        free(icon->source.vico.data);
        icon->source.vico.data = NULL;
    }

    free(icon);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
