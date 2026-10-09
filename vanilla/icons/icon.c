/*
 * Project Tsukasa — Scalable Vector Icon Rasteriser Implementation
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
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define VICO_MAX_POINTS 256

typedef struct {
    float x;
    float y;
} vico_point_t;

typedef struct {
    vico_point_t points[VICO_MAX_POINTS];
    int          count;
    vico_point_t current;
    vico_point_t subpath_start;
} vico_path_context_t;

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
    uint32_t inv = 255 - sa;

    uint32_t r = (((src >> 16) & 0xFF) * sa + ((dp >> 16) & 0xFF) * inv + 127) / 255;
    uint32_t g = (((src >> 8) & 0xFF) * sa + ((dp >> 8) & 0xFF) * inv + 127) / 255;
    uint32_t b = ((src & 0xFF) * sa + (dp & 0xFF) * inv + 127) / 255;
    uint32_t a = sa + (da * inv + 127) / 255;
    if (a > 255)
        a = 255;

    *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

static void rasterize_rect(int target_size, uint32_t *out_pixels, int stride_px,
                           uint8_t x8, uint8_t y8, uint8_t w8, uint8_t h8,
                           uint32_t color)
{
    float fx0 = ((float)x8 / 255.0f) * (float)target_size;
    float fy0 = ((float)y8 / 255.0f) * (float)target_size;
    float fx1 = ((float)(x8 + w8) / 255.0f) * (float)target_size;
    float fy1 = ((float)(y8 + h8) / 255.0f) * (float)target_size;

    int ix0 = (int)(fx0 + 0.5f);
    int iy0 = (int)(fy0 + 0.5f);
    int ix1 = (int)(fx1 + 0.5f);
    int iy1 = (int)(fy1 + 0.5f);

    if (w8 > 0 && ix1 <= ix0)
        ix1 = ix0 + 1;
    if (h8 > 0 && iy1 <= iy0)
        iy1 = iy0 + 1;

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > target_size) ix1 = target_size;
    if (iy1 > target_size) iy1 = target_size;

    for (int y = iy0; y < iy1; y++) {
        uint32_t *row = out_pixels + y * stride_px;
        for (int x = ix0; x < ix1; x++) {
            blend_pixel(&row[x], color);
        }
    }
}

static void rasterize_rrect(int target_size, uint32_t *out_pixels, int stride_px,
                            uint8_t x8, uint8_t y8, uint8_t w8, uint8_t h8,
                            uint8_t corner8, uint32_t color)
{
    float fx0 = ((float)x8 / 255.0f) * (float)target_size;
    float fy0 = ((float)y8 / 255.0f) * (float)target_size;
    float fx1 = ((float)(x8 + w8) / 255.0f) * (float)target_size;
    float fy1 = ((float)(y8 + h8) / 255.0f) * (float)target_size;
    float cr = ((float)corner8 / 255.0f) * (float)target_size;

    float max_r = 0.5f * ((fx1 - fx0) < (fy1 - fy0) ? (fx1 - fx0) : (fy1 - fy0));
    if (cr > max_r)
        cr = max_r;

    int ix0 = (int)(fx0 + 0.5f);
    int iy0 = (int)(fy0 + 0.5f);
    int ix1 = (int)(fx1 + 0.5f);
    int iy1 = (int)(fy1 + 0.5f);

    if (w8 > 0 && ix1 <= ix0)
        ix1 = ix0 + 1;
    if (h8 > 0 && iy1 <= iy0)
        iy1 = iy0 + 1;

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > target_size) ix1 = target_size;
    if (iy1 > target_size) iy1 = target_size;

    float cr2 = cr * cr;

    for (int y = iy0; y < iy1; y++) {
        float py = (float)y + 0.5f;
        uint32_t *row = out_pixels + y * stride_px;
        for (int x = ix0; x < ix1; x++) {
            float px = (float)x + 0.5f;
            int in_corner = 0;
            float dx = 0.0f, dy = 0.0f;

            if (px < fx0 + cr && py < fy0 + cr) {
                dx = px - (fx0 + cr);
                dy = py - (fy0 + cr);
                in_corner = 1;
            } else if (px > fx1 - cr && py < fy0 + cr) {
                dx = px - (fx1 - cr);
                dy = py - (fy0 + cr);
                in_corner = 1;
            } else if (px < fx0 + cr && py > fy1 - cr) {
                dx = px - (fx0 + cr);
                dy = py - (fy1 - cr);
                in_corner = 1;
            } else if (px > fx1 - cr && py > fy1 - cr) {
                dx = px - (fx1 - cr);
                dy = py - (fy1 - cr);
                in_corner = 1;
            }

            if (!in_corner || (dx * dx + dy * dy <= cr2)) {
                blend_pixel(&row[x], color);
            }
        }
    }
}

static void rasterize_circle(int target_size, uint32_t *out_pixels, int stride_px,
                             uint8_t cx8, uint8_t cy8, uint8_t r8, uint32_t color)
{
    float fcx = ((float)cx8 / 255.0f) * (float)target_size;
    float fcy = ((float)cy8 / 255.0f) * (float)target_size;
    float fr = ((float)r8 / 255.0f) * (float)target_size;

    int ix0 = (int)(fcx - fr + 0.5f);
    int iy0 = (int)(fcy - fr + 0.5f);
    int ix1 = (int)(fcx + fr + 1.5f);
    int iy1 = (int)(fcy + fr + 1.5f);

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > target_size) ix1 = target_size;
    if (iy1 > target_size) iy1 = target_size;

    float r2 = fr * fr;

    for (int y = iy0; y < iy1; y++) {
        float py = (float)y + 0.5f;
        uint32_t *row = out_pixels + y * stride_px;
        for (int x = ix0; x < ix1; x++) {
            float px = (float)x + 0.5f;
            float dx = px - fcx;
            float dy = py - fcy;
            if (dx * dx + dy * dy <= r2) {
                blend_pixel(&row[x], color);
            }
        }
    }
}

static void rasterize_polygon_fill(const vico_path_context_t *ctx, int target_size,
                                   uint32_t *out_pixels, int stride_px,
                                   int is_grad, float gx0, float gy0, float gx1, float gy1,
                                   uint32_t c0, uint32_t c1)
{
    if (ctx->count < 3)
        return;

    float grad_dx = gx1 - gx0;
    float grad_dy = gy1 - gy0;
    float grad_len2 = grad_dx * grad_dx + grad_dy * grad_dy;

    uint32_t c0_a = (c0 >> 24) & 0xFF, c0_r = (c0 >> 16) & 0xFF, c0_g = (c0 >> 8) & 0xFF, c0_b = c0 & 0xFF;
    uint32_t c1_a = (c1 >> 24) & 0xFF, c1_r = (c1 >> 16) & 0xFF, c1_g = (c1 >> 8) & 0xFF, c1_b = c1 & 0xFF;

    for (int y = 0; y < target_size; y++) {
        float y_scan = (float)y + 0.5f;
        float x_nodes[64];
        int node_count = 0;

        for (int i = 0; i < ctx->count; i++) {
            int next = (i + 1 == ctx->count) ? 0 : i + 1;
            float py0 = ctx->points[i].y;
            float py1 = ctx->points[next].y;
            float px0 = ctx->points[i].x;
            float px1 = ctx->points[next].x;

            if ((py0 < y_scan && py1 >= y_scan) || (py1 < y_scan && py0 >= y_scan)) {
                if (node_count < 64 && (py1 != py0)) {
                    x_nodes[node_count++] = px0 + (y_scan - py0) / (py1 - py0) * (px1 - px0);
                }
            }
        }

        /* Sort intersection nodes */
        for (int i = 1; i < node_count; i++) {
            float key = x_nodes[i];
            int j = i - 1;
            while (j >= 0 && x_nodes[j] > key) {
                x_nodes[j + 1] = x_nodes[j];
                j--;
            }
            x_nodes[j + 1] = key;
        }

        uint32_t *row = out_pixels + y * stride_px;
        for (int i = 0; i + 1 < node_count; i += 2) {
            int ix0 = (int)(x_nodes[i] + 0.5f);
            int ix1 = (int)(x_nodes[i + 1] + 0.5f);

            if (ix0 < 0) ix0 = 0;
            if (ix1 > target_size) ix1 = target_size;

            for (int x = ix0; x < ix1; x++) {
                uint32_t color = c0;
                if (is_grad) {
                    float px = (float)x + 0.5f;
                    float py = y_scan;
                    float t = 0.0f;
                    if (grad_len2 > 0.0001f) {
                        t = ((px - gx0) * grad_dx + (py - gy0) * grad_dy) / grad_len2;
                    }
                    if (t < 0.0f) t = 0.0f;
                    else if (t > 1.0f) t = 1.0f;

                    uint32_t a = (uint32_t)(c0_a + t * ((float)c1_a - (float)c0_a));
                    uint32_t r = (uint32_t)(c0_r + t * ((float)c1_r - (float)c0_r));
                    uint32_t g = (uint32_t)(c0_g + t * ((float)c1_g - (float)c0_g));
                    uint32_t b = (uint32_t)(c0_b + t * ((float)c1_b - (float)c0_b));
                    color = (a << 24) | (r << 16) | (g << 8) | b;
                }
                blend_pixel(&row[x], color);
            }
        }
    }
}

static void rasterize_polyline_stroke(const vico_path_context_t *ctx, int target_size,
                                      uint32_t *out_pixels, int stride_px,
                                      uint8_t width8, uint32_t color)
{
    if (ctx->count < 2)
        return;

    float sw = ((float)width8 / 255.0f) * (float)target_size;
    if (sw < 1.0f)
        sw = 1.0f;

    float half_w = sw * 0.5f;
    float half_w2 = half_w * half_w;

    for (int i = 0; i + 1 < ctx->count; i++) {
        float ax = ctx->points[i].x, ay = ctx->points[i].y;
        float bx = ctx->points[i + 1].x, by = ctx->points[i + 1].y;
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;

        int min_x = (int)((ax < bx ? ax : bx) - half_w - 0.5f);
        int max_x = (int)((ax > bx ? ax : bx) + half_w + 1.5f);
        int min_y = (int)((ay < by ? ay : by) - half_w - 0.5f);
        int max_y = (int)((ay > by ? ay : by) + half_w + 1.5f);

        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x > target_size) max_x = target_size;
        if (max_y > target_size) max_y = target_size;

        for (int y = min_y; y < max_y; y++) {
            float py = (float)y + 0.5f;
            uint32_t *row = out_pixels + y * stride_px;
            for (int x = min_x; x < max_x; x++) {
                float px = (float)x + 0.5f;
                float u = 0.0f;
                if (len2 > 0.0001f) {
                    u = ((px - ax) * dx + (py - ay) * dy) / len2;
                    if (u < 0.0f) u = 0.0f;
                    else if (u > 1.0f) u = 1.0f;
                }
                float cx = ax + u * dx;
                float cy = ay + u * dy;
                float dist2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
                if (dist2 <= half_w2) {
                    blend_pixel(&row[x], color);
                }
            }
        }
    }
}

int vico_rasterize(const uint8_t *data, size_t data_len, int target_size,
                   uint32_t *out_pixels, int stride_px)
{
    if (!data || data_len == 0 || target_size <= 0 || !out_pixels || stride_px <= 0)
        return -1;

    size_t offset = 0;

    /* Check optional VICO header */
    if (data_len >= sizeof(vico_hdr_t)) {
        const vico_hdr_t *hdr = (const vico_hdr_t *)data;
        if (hdr->magic == VICO_MAGIC) {
            offset = sizeof(vico_hdr_t);
        }
    }

    vico_path_context_t path;
    memset(&path, 0, sizeof(path));

    while (offset < data_len) {
        uint8_t cmd = data[offset++];

        if (cmd == VCMD_END) {
            break;
        }

        switch (cmd) {
        case VCMD_MOVE: {
            if (offset + 2 > data_len) return -1;
            uint8_t x8 = data[offset++];
            uint8_t y8 = data[offset++];
            float fx = ((float)x8 / 255.0f) * (float)target_size;
            float fy = ((float)y8 / 255.0f) * (float)target_size;
            path.current = (vico_point_t){ fx, fy };
            path.subpath_start = path.current;
            path.count = 0;
            if (path.count < VICO_MAX_POINTS)
                path.points[path.count++] = path.current;
            break;
        }

        case VCMD_LINE: {
            if (offset + 2 > data_len) return -1;
            uint8_t x8 = data[offset++];
            uint8_t y8 = data[offset++];
            float fx = ((float)x8 / 255.0f) * (float)target_size;
            float fy = ((float)y8 / 255.0f) * (float)target_size;
            path.current = (vico_point_t){ fx, fy };
            if (path.count < VICO_MAX_POINTS)
                path.points[path.count++] = path.current;
            break;
        }

        case VCMD_QUAD: {
            if (offset + 4 > data_len) return -1;
            uint8_t cx8 = data[offset++];
            uint8_t cy8 = data[offset++];
            uint8_t ex8 = data[offset++];
            uint8_t ey8 = data[offset++];

            float fcx = ((float)cx8 / 255.0f) * (float)target_size;
            float fcy = ((float)cy8 / 255.0f) * (float)target_size;
            float fex = ((float)ex8 / 255.0f) * (float)target_size;
            float fey = ((float)ey8 / 255.0f) * (float)target_size;

            vico_point_t p0 = path.current;
            for (int s = 1; s <= 4; s++) {
                float t = (float)s / 4.0f;
                float omt = 1.0f - t;
                float qx = omt * omt * p0.x + 2.0f * omt * t * fcx + t * t * fex;
                float qy = omt * omt * p0.y + 2.0f * omt * t * fcy + t * t * fey;
                if (path.count < VICO_MAX_POINTS)
                    path.points[path.count++] = (vico_point_t){ qx, qy };
            }
            path.current = (vico_point_t){ fex, fey };
            break;
        }

        case VCMD_CLOSE: {
            if (path.count > 0 && path.count < VICO_MAX_POINTS) {
                path.points[path.count++] = path.subpath_start;
                path.current = path.subpath_start;
            }
            break;
        }

        case VCMD_FILL_SOLID: {
            if (offset + 4 > data_len) return -1;
            uint8_t a = data[offset++];
            uint8_t r = data[offset++];
            uint8_t g = data[offset++];
            uint8_t b = data[offset++];
            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            rasterize_polygon_fill(&path, target_size, out_pixels, stride_px, 0, 0, 0, 0, 0, color, color);
            path.count = 0;
            break;
        }

        case VCMD_FILL_GRAD: {
            if (offset + 12 > data_len) return -1;
            uint8_t x0_8 = data[offset++];
            uint8_t y0_8 = data[offset++];
            uint8_t x1_8 = data[offset++];
            uint8_t y1_8 = data[offset++];
            uint8_t a0 = data[offset++];
            uint8_t r0 = data[offset++];
            uint8_t g0 = data[offset++];
            uint8_t b0 = data[offset++];
            uint8_t a1 = data[offset++];
            uint8_t r1 = data[offset++];
            uint8_t g1 = data[offset++];
            uint8_t b1 = data[offset++];

            float gx0 = ((float)x0_8 / 255.0f) * (float)target_size;
            float gy0 = ((float)y0_8 / 255.0f) * (float)target_size;
            float gx1 = ((float)x1_8 / 255.0f) * (float)target_size;
            float gy1 = ((float)y1_8 / 255.0f) * (float)target_size;
            uint32_t c0 = ((uint32_t)a0 << 24) | ((uint32_t)r0 << 16) | ((uint32_t)g0 << 8) | b0;
            uint32_t c1 = ((uint32_t)a1 << 24) | ((uint32_t)r1 << 16) | ((uint32_t)g1 << 8) | b1;

            rasterize_polygon_fill(&path, target_size, out_pixels, stride_px, 1, gx0, gy0, gx1, gy1, c0, c1);
            path.count = 0;
            break;
        }

        case VCMD_STROKE: {
            if (offset + 5 > data_len) return -1;
            uint8_t width8 = data[offset++];
            uint8_t a = data[offset++];
            uint8_t r = data[offset++];
            uint8_t g = data[offset++];
            uint8_t b = data[offset++];
            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            rasterize_polyline_stroke(&path, target_size, out_pixels, stride_px, width8, color);
            path.count = 0;
            break;
        }

        case VCMD_CIRCLE: {
            if (offset + 7 > data_len) return -1;
            uint8_t cx = data[offset++];
            uint8_t cy = data[offset++];
            uint8_t rad = data[offset++];
            uint8_t a = data[offset++];
            uint8_t r = data[offset++];
            uint8_t g = data[offset++];
            uint8_t b = data[offset++];
            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            rasterize_circle(target_size, out_pixels, stride_px, cx, cy, rad, color);
            break;
        }

        case VCMD_RECT: {
            if (offset + 8 > data_len) return -1;
            uint8_t rx = data[offset++];
            uint8_t ry = data[offset++];
            uint8_t rw = data[offset++];
            uint8_t rh = data[offset++];
            uint8_t a = data[offset++];
            uint8_t r = data[offset++];
            uint8_t g = data[offset++];
            uint8_t b = data[offset++];
            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            rasterize_rect(target_size, out_pixels, stride_px, rx, ry, rw, rh, color);
            break;
        }

        case VCMD_RRECT: {
            if (offset + 9 > data_len) return -1;
            uint8_t rx = data[offset++];
            uint8_t ry = data[offset++];
            uint8_t rw = data[offset++];
            uint8_t rh = data[offset++];
            uint8_t cr = data[offset++];
            uint8_t a = data[offset++];
            uint8_t r = data[offset++];
            uint8_t g = data[offset++];
            uint8_t b = data[offset++];
            uint32_t color = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            rasterize_rrect(target_size, out_pixels, stride_px, rx, ry, rw, rh, cr, color);
            break;
        }

        default:
            /* Unknown opcode: abort parsing */
            return -1;
        }
    }

    return 0;
}
