/*
 * Project Tsukasa — Compositor & SIMD Blitter Test Suite
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

#include "../include/vanilla.h"
#include "../server/server.h"
#include "../server/blitter.h"
#include "../server/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../scripts/test/tsk_test.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            return -1; \
        } \
    } while (0)

static int test_offscreen_init(void)
{
    vanilla_compositor_t comp;
    int rc = compositor_init_offscreen(&comp, 640, 480);
    TEST_ASSERT(rc == 0, "compositor_init_offscreen failed");
    TEST_ASSERT(comp.width == 640, "comp width mismatch");
    TEST_ASSERT(comp.height == 480, "comp height mismatch");
    TEST_ASSERT(comp.pitch_px == 640, "comp pitch mismatch");
    TEST_ASSERT(comp.bpp == 32, "comp bpp mismatch");
    TEST_ASSERT(comp.backbuffer != NULL, "comp backbuffer is null");
    TEST_ASSERT(comp.is_offscreen == 1, "comp is_offscreen != 1");
    TEST_ASSERT(comp.dirty_count == 1, "initial dirty_count != 1");
    TEST_ASSERT(comp.dirty_rects[0].x == 0, "initial dirty x != 0");
    TEST_ASSERT(comp.dirty_rects[0].y == 0, "initial dirty y != 0");
    TEST_ASSERT(comp.dirty_rects[0].w == 640, "initial dirty w != 640");
    TEST_ASSERT(comp.dirty_rects[0].h == 480, "initial dirty h != 480");

    compositor_destroy(&comp);
    TEST_ASSERT(comp.backbuffer == NULL, "backbuffer not freed");
    TEST_ASSERT(comp.dirty_count == 0, "dirty_count not reset");
    return 0;
}

static int test_blt_fill_and_copy(void)
{
    const uint32_t buf_w = 64;
    const uint32_t buf_h = 64;
    size_t sz = (size_t)buf_w * buf_h * sizeof(uint32_t);

    uint32_t *bufA = (uint32_t *)malloc(sz);
    uint32_t *bufB = (uint32_t *)malloc(sz);
    TEST_ASSERT(bufA != NULL && bufB != NULL, "malloc failed");

    memset(bufA, 0, sz);
    memset(bufB, 0, sz);

    vanilla_rect_t fill_r = { 10, 12, 32, 24 };
    blt_fill_rect(bufA, buf_w, &fill_r, 0xFFAABBCC);

    for (uint32_t y = 0; y < buf_h; y++) {
        for (uint32_t x = 0; x < buf_w; x++) {
            uint32_t val = bufA[y * buf_w + x];
            if ((int32_t)x >= fill_r.x && (int32_t)x < fill_r.x + fill_r.w &&
                (int32_t)y >= fill_r.y && (int32_t)y < fill_r.y + fill_r.h) {
                TEST_ASSERT(val == 0xFFAABBCC, "fill rect pixel mismatch");
            } else {
                TEST_ASSERT(val == 0, "pixel outside fill rect was modified");
            }
        }
    }

    blt_copy_rect(bufB, buf_w, bufA, buf_w, &fill_r);
    for (uint32_t y = 0; y < buf_h; y++) {
        for (uint32_t x = 0; x < buf_w; x++) {
            uint32_t valA = bufA[y * buf_w + x];
            uint32_t valB = bufB[y * buf_w + x];
            if ((int32_t)x >= fill_r.x && (int32_t)x < fill_r.x + fill_r.w &&
                (int32_t)y >= fill_r.y && (int32_t)y < fill_r.y + fill_r.h) {
                TEST_ASSERT(valB == valA, "copied pixel mismatch");
            } else {
                TEST_ASSERT(valB == 0, "unrelated pixel in bufB modified");
            }
        }
    }

    blt_copy_subrect(bufB, buf_w, 0, 0, bufA, buf_w, 10, 12, 8, 8);
    for (int32_t y = 0; y < 8; y++) {
        for (int32_t x = 0; x < 8; x++) {
            TEST_ASSERT(bufB[y * buf_w + x] == 0xFFAABBCC, "copy_subrect pixel mismatch");
        }
    }

    free(bufA);
    free(bufB);
    return 0;
}

static int test_blt_alpha_blending(void)
{
    const int32_t w = 16;
    const int32_t h = 4;
    size_t sz = (size_t)w * h * sizeof(uint32_t);

    uint32_t *dst = (uint32_t *)malloc(sz);
    uint32_t *src = (uint32_t *)malloc(sz);
    TEST_ASSERT(dst != NULL && src != NULL, "malloc failed");

    /* Case 1: 50% transparent white (alpha=128) over opaque black */
    for (int i = 0; i < w * h; i++) {
        dst[i] = 0xFF000000;
        src[i] = 0x80FFFFFF;
    }

    blt_blend_rect(dst, (uint32_t)w, src, (uint32_t)w, w, h, 255);

    for (int i = 0; i < w * h; i++) {
        uint32_t p = dst[i];
        uint32_t a = (p >> 24) & 0xFF;
        uint32_t r = (p >> 16) & 0xFF;
        uint32_t g = (p >> 8) & 0xFF;
        uint32_t b = p & 0xFF;

        TEST_ASSERT(a == 255, "blended alpha should be 255");
        TEST_ASSERT(r >= 127 && r <= 129, "blended red out of expected range");
        TEST_ASSERT(g >= 127 && g <= 129, "blended green out of expected range");
        TEST_ASSERT(b >= 127 && b <= 129, "blended blue out of expected range");
    }

    /* Case 2: Fully transparent source (alpha=0) must not modify dst */
    for (int i = 0; i < w * h; i++) {
        dst[i] = 0xFF112233;
        src[i] = 0x00AABBCC;
    }
    blt_blend_rect(dst, (uint32_t)w, src, (uint32_t)w, w, h, 255);
    for (int i = 0; i < w * h; i++)
        TEST_ASSERT(dst[i] == 0xFF112233, "transparent src altered destination");

    /* Case 3: Fully opaque source (alpha=255) must replace dst */
    for (int i = 0; i < w * h; i++) {
        dst[i] = 0xFF112233;
        src[i] = 0xFF445566;
    }
    blt_blend_rect(dst, (uint32_t)w, src, (uint32_t)w, w, h, 255);
    for (int i = 0; i < w * h; i++)
        TEST_ASSERT(dst[i] == 0xFF445566, "opaque src failed to replace destination");

    /* Case 4: Global alpha attenuation */
    for (int i = 0; i < w * h; i++) {
        dst[i] = 0xFF000000;
        src[i] = 0xFFFFFFFF;
    }
    blt_blend_rect(dst, (uint32_t)w, src, (uint32_t)w, w, h, 128);
    for (int i = 0; i < w * h; i++) {
        uint32_t p = dst[i];
        uint32_t r = (p >> 16) & 0xFF;
        TEST_ASSERT(r >= 127 && r <= 129, "global alpha blend mismatch");
    }

    free(dst);
    free(src);
    return 0;
}

static int test_damage_tracking_and_coalescing(void)
{
    vanilla_compositor_t comp;
    compositor_init_offscreen(&comp, 800, 600);
    comp.dirty_count = 0;

    /* Out of bounds rect must be clipped out completely */
    vanilla_rect_t oob = { -100, -100, 50, 50 };
    compositor_add_damage(&comp, &oob);
    TEST_ASSERT(comp.dirty_count == 0, "OOB damage was not dropped");

    /* Partially offscreen rect must be clipped to screen */
    vanilla_rect_t part = { -20, -10, 50, 40 };
    compositor_add_damage(&comp, &part);
    TEST_ASSERT(comp.dirty_count == 1, "partially offscreen damage not added");
    TEST_ASSERT(comp.dirty_rects[0].x == 0, "clipped x mismatch");
    TEST_ASSERT(comp.dirty_rects[0].y == 0, "clipped y mismatch");
    TEST_ASSERT(comp.dirty_rects[0].w == 30, "clipped w mismatch");
    TEST_ASSERT(comp.dirty_rects[0].h == 30, "clipped h mismatch");

    /* Overlapping damage should be merged */
    vanilla_rect_t overlap = { 20, 20, 30, 30 };
    compositor_add_damage(&comp, &overlap);
    TEST_ASSERT(comp.dirty_count == 2, "second damage rect not added");
    compositor_merge_damage(&comp);
    TEST_ASSERT(comp.dirty_count == 1, "overlapping damage was not merged");
    TEST_ASSERT(comp.dirty_rects[0].x == 0, "merged x mismatch");
    TEST_ASSERT(comp.dirty_rects[0].y == 0, "merged y mismatch");
    TEST_ASSERT(comp.dirty_rects[0].w == 50, "merged w mismatch");
    TEST_ASSERT(comp.dirty_rects[0].h == 50, "merged h mismatch");

    /* Coalescing test when exceeding MAX_DIRTY_RECTS (32) */
    comp.dirty_count = 0;
    for (int i = 0; i < MAX_DIRTY_RECTS; i++) {
        vanilla_rect_t r = { i * 20, 100, 10, 10 };
        compositor_add_damage(&comp, &r);
    }
    TEST_ASSERT(comp.dirty_count == MAX_DIRTY_RECTS, "failed to fill MAX_DIRTY_RECTS");

    /* 33rd rect exceeds limit and triggers bounding coalescing */
    vanilla_rect_t overflow_rect = { 700, 100, 20, 10 };
    compositor_add_damage(&comp, &overflow_rect);
    TEST_ASSERT(comp.dirty_count == 1, "coalesced count != 1");
    TEST_ASSERT(comp.dirty_rects[0].x == 0, "coalesced bounding min_x != 0");
    TEST_ASSERT(comp.dirty_rects[0].y == 100, "coalesced bounding min_y != 100");
    TEST_ASSERT(comp.dirty_rects[0].w == 720, "coalesced bounding width mismatch");
    TEST_ASSERT(comp.dirty_rects[0].h == 10, "coalesced bounding height mismatch");

    compositor_destroy(&comp);
    return 0;
}

static int test_z_order_and_wm_raise(void)
{
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));

    int rc = compositor_init_offscreen(&srv.compositor, 400, 300);
    TEST_ASSERT(rc == 0, "compositor_init_offscreen failed");
    srv.compositor.bg_color = 0xFF101010;

    /* Window 1: 100x100 at (50, 50), filled with Red (0xFFFF0000) */
    vanilla_server_window_t *w1 = &srv.windows[0];
    w1->in_use = 1;
    w1->window_id = 1;
    w1->x = 50;
    w1->y = 50;
    w1->width = 100;
    w1->height = 100;
    w1->flags = WINDOW_FLAG_BORDERLESS;
    w1->layer = LAYER_NORMAL;
    w1->z_index = 10;
    w1->is_mapped = 1;
    w1->surface.width = 100;
    w1->surface.height = 100;
    w1->surface.pitch = 100;
    w1->surface.size = 100 * 100 * sizeof(uint32_t);
    w1->surface.pixels = (uint32_t *)malloc(w1->surface.size);
    TEST_ASSERT(w1->surface.pixels != NULL, "malloc w1 pixels failed");
    for (size_t i = 0; i < 100 * 100; i++)
        w1->surface.pixels[i] = 0xFFFF0000;

    /* Window 2: 100x100 at (100, 50), filled with Green (0xFF00FF00) */
    vanilla_server_window_t *w2 = &srv.windows[1];
    w2->in_use = 1;
    w2->window_id = 2;
    w2->x = 100;
    w2->y = 50;
    w2->width = 100;
    w2->height = 100;
    w2->flags = WINDOW_FLAG_BORDERLESS;
    w2->layer = LAYER_NORMAL;
    w2->z_index = 20;
    w2->is_mapped = 1;
    w2->surface.width = 100;
    w2->surface.height = 100;
    w2->surface.pitch = 100;
    w2->surface.size = 100 * 100 * sizeof(uint32_t);
    w2->surface.pixels = (uint32_t *)malloc(w2->surface.size);
    TEST_ASSERT(w2->surface.pixels != NULL, "malloc w2 pixels failed");
    for (size_t i = 0; i < 100 * 100; i++)
        w2->surface.pixels[i] = 0xFF00FF00;

    srv.next_window_id = 3;
    srv.next_z_index = 20;

    /* Damage full screen and render frame */
    compositor_damage_all(&srv.compositor);
    compositor_render_frame(&srv);

    uint32_t *fb = srv.compositor.backbuffer;
    uint32_t pitch = srv.compositor.pitch_px;

    /* Verify background outside windows */
    TEST_ASSERT(fb[10 * pitch + 10] == 0xFF101010, "background pixel mismatch");

    /* Verify Window 1 non-overlapping region: (75, 75) is Red */
    TEST_ASSERT(fb[75 * pitch + 75] == 0xFFFF0000, "Window 1 non-overlap region mismatch");

    /* Verify Window 2 non-overlapping region: (175, 75) is Green */
    TEST_ASSERT(fb[75 * pitch + 175] == 0xFF00FF00, "Window 2 non-overlap region mismatch");

    /* Verify overlap region (125, 75): Window 2 (Z=20) occludes Window 1 (Z=10) */
    TEST_ASSERT(fb[75 * pitch + 125] == 0xFF00FF00, "Window 2 failed to occlude Window 1");

    /* Test wm_raise_window: raise Window 1 above Window 2 */
    wm_raise_window(&srv, 1);
    TEST_ASSERT(w1->z_index > w2->z_index, "wm_raise_window did not increase Z-index above w2");

    compositor_render_frame(&srv);

    /* Overlap region (125, 75) must now be Red since Window 1 is on top */
    TEST_ASSERT(fb[75 * pitch + 125] == 0xFFFF0000, "Window 1 did not render on top after raise");

    /* Test wm_lower_window: lower Window 1 below Window 2 */
    wm_lower_window(&srv, 1);
    TEST_ASSERT(w1->z_index < w2->z_index, "wm_lower_window did not decrease Z-index below w2");

    compositor_render_frame(&srv);

    /* Overlap region (125, 75) must revert to Green */
    TEST_ASSERT(fb[75 * pitch + 125] == 0xFF00FF00, "Window 2 did not occlude Window 1 after lower");

    free(w1->surface.pixels);
    free(w2->surface.pixels);
    compositor_destroy(&srv.compositor);
    return 0;
}

static int test_negative_coordinate_clipping(void)
{
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));

    int rc = compositor_init_offscreen(&srv.compositor, 400, 300);
    TEST_ASSERT(rc == 0, "compositor_init_offscreen failed");
    srv.compositor.bg_color = 0xFF050505;

    /* Window partially off-screen at negative coordinates: (-25, -15), size 80x60 */
    vanilla_server_window_t *w = &srv.windows[0];
    w->in_use = 1;
    w->window_id = 1;
    w->x = -25;
    w->y = -15;
    w->width = 80;
    w->height = 60;
    w->flags = WINDOW_FLAG_BORDERLESS;
    w->layer = LAYER_NORMAL;
    w->z_index = 5;
    w->is_mapped = 1;
    w->surface.width = 80;
    w->surface.height = 60;
    w->surface.pitch = 80;
    w->surface.size = 80 * 60 * sizeof(uint32_t);
    w->surface.pixels = (uint32_t *)malloc(w->surface.size);
    TEST_ASSERT(w->surface.pixels != NULL, "malloc w pixels failed");

    for (int32_t y = 0; y < 60; y++) {
        for (int32_t x = 0; x < 80; x++) {
            /* Unique signature pixel at client coord (25, 15) which maps to screen (0, 0) */
            if (x == 25 && y == 15)
                w->surface.pixels[y * 80 + x] = 0xFF123456;
            else
                w->surface.pixels[y * 80 + x] = 0xFF0000FF;
        }
    }

    compositor_damage_all(&srv.compositor);
    compositor_render_frame(&srv);

    uint32_t *fb = srv.compositor.backbuffer;
    uint32_t pitch = srv.compositor.pitch_px;

    /* Screen (0, 0) maps to client (25, 15) */
    TEST_ASSERT(fb[0] == 0xFF123456, "pixel at screen (0,0) does not match client (25,15)");

    /* Screen (10, 10) maps to client (35, 25) which is blue */
    TEST_ASSERT(fb[10 * pitch + 10] == 0xFF0000FF, "pixel inside visible subrect is not blue");

    /* Screen (60, 50) is outside window bounds (max x is -25 + 80 = 55) */
    TEST_ASSERT(fb[50 * pitch + 60] == 0xFF050505, "pixel outside window was modified");

    free(w->surface.pixels);
    compositor_destroy(&srv.compositor);
    return 0;
}

static int test_drop_shadow_and_decorations(void)
{
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));

    int rc = compositor_init_offscreen(&srv.compositor, 400, 300);
    TEST_ASSERT(rc == 0, "compositor_init_offscreen failed");
    srv.compositor.bg_color = 0xFFFFFFFF;

    /* Window with decorations: at (100, 100), 80x60 */
    vanilla_server_window_t *w = &srv.windows[0];
    w->in_use = 1;
    w->window_id = 1;
    w->x = 100;
    w->y = 100;
    w->width = 80;
    w->height = 60;
    w->flags = 0; /* Not borderless: decorations enabled */
    w->layer = LAYER_NORMAL;
    w->z_index = 10;
    w->is_mapped = 1;
    w->is_focused = 1;
    w->surface.width = 80;
    w->surface.height = 60;
    w->surface.pitch = 80;
    w->surface.size = 80 * 60 * sizeof(uint32_t);
    w->surface.pixels = (uint32_t *)malloc(w->surface.size);
    TEST_ASSERT(w->surface.pixels != NULL, "malloc w pixels failed");
    for (size_t i = 0; i < 80 * 60; i++)
        w->surface.pixels[i] = 0xFFEEEEEE;

    compositor_damage_all(&srv.compositor);
    compositor_render_frame(&srv);

    uint32_t *fb = srv.compositor.backbuffer;
    uint32_t pitch = srv.compositor.pitch_px;

    /* Titlebar check: y in [100 - TITLEBAR_HEIGHT - 1, 100 - 1], x in [100, 170] */
    /* Focused titlebar color: 0xFF3B4252 */
    uint32_t tb_pixel = fb[(100 - 10) * pitch + 120];
    TEST_ASSERT(tb_pixel == 0xFF3B4252, "focused titlebar color mismatch");

    /* Client surface interior check: (120, 120) */
    TEST_ASSERT(fb[120 * pitch + 120] == 0xFFEEEEEE, "client surface interior color mismatch");

    /* Drop shadow check: immediately below window frame, background was 0xFFFFFFFF */
    /* Shadow should have darkened the background */
    int32_t shadow_y = 100 + 60 + WINDOW_BORDER_WIDTH + 2;
    uint32_t shadow_p = fb[shadow_y * pitch + 120];
    uint32_t sr = (shadow_p >> 16) & 0xFF;
    TEST_ASSERT(sr < 255, "drop shadow did not darken background below window");

    free(w->surface.pixels);
    compositor_destroy(&srv.compositor);
    return 0;
}

int main(void)
{
    printf("========================================================\n");
    printf(" Project Vanilla - Phase 4, Step 4.2 Compositor & Blitter\n");
    printf("========================================================\n");
    int passed = 0;
    int total = 7;

    if (test_offscreen_init() != 0) {
        TSK_TEST_FAIL("compositor", "offscreen_init", "test_offscreen_init failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "offscreen_init");
    passed++;

    if (test_blt_fill_and_copy() != 0) {
        TSK_TEST_FAIL("compositor", "blt_fill_copy", "test_blt_fill_and_copy failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "blt_fill_copy");
    passed++;

    if (test_blt_alpha_blending() != 0) {
        TSK_TEST_FAIL("compositor", "alpha_blending", "test_blt_alpha_blending failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "alpha_blending");
    passed++;

    if (test_damage_tracking_and_coalescing() != 0) {
        TSK_TEST_FAIL("compositor", "damage_tracking", "test_damage_tracking_and_coalescing failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "damage_tracking");
    passed++;

    if (test_z_order_and_wm_raise() != 0) {
        TSK_TEST_FAIL("compositor", "z_order", "test_z_order_and_wm_raise failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "z_order");
    passed++;

    if (test_negative_coordinate_clipping() != 0) {
        TSK_TEST_FAIL("compositor", "negative_clipping", "test_negative_coordinate_clipping failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "negative_clipping");
    passed++;

    if (test_drop_shadow_and_decorations() != 0) {
        TSK_TEST_FAIL("compositor", "decorations", "test_drop_shadow_and_decorations failed");
        return 1;
    }
    TSK_TEST_PASS("compositor", "decorations");
    passed++;

    TSK_TEST_DONE("compositor", passed, total);
    return 0;
}
