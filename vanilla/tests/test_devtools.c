/*
 * Project Tsukasa — Developer Tools & Debug Protocol Test Suite
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
#include "../include/protocol.h"
#include "../include/theme.h"
#include "../server/compositor.h"
#include "../server/blitter.h"
#include "../devtools/vendor/microui/microui.h"
#include "../../scripts/test/tsk_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            return -1; \
        } \
    } while (0)

/* Stubs for WM/compositor dependencies in host unit tests */
void wm_invalidate_window(void *srv, void *w) { (void)srv; (void)w; }
void wm_get_frame_rect(const void *w, void *r) { (void)w; (void)r; }
void dnd_check_timeout(void *srv) { (void)srv; }
void dnd_render_ghost(void *srv, const void *dirty) { (void)srv; (void)dirty; }
void cursor_render(uint32_t *buf, uint32_t pitch, uint32_t w, uint32_t h, int32_t cx, int32_t cy, const void *dirty) {
    (void)buf; (void)pitch; (void)w; (void)h; (void)cx; (void)cy; (void)dirty;
}
void vanilla_server_release_buffers(void *srv) { (void)srv; }
void *vanilla_server_find_window(void *srv, uint32_t id) { (void)srv; (void)id; return NULL; }
void vanilla_server_destroy_window_record(void *srv, void *w) { (void)srv; (void)w; }

static int test_protocol_struct_sizes(void)
{
    TEST_ASSERT(sizeof(vanilla_dbg_window_t) == 116, "vanilla_dbg_window_t must be exactly 116 bytes");
    TEST_ASSERT(sizeof(vanilla_dbg_compositor_t) == 20, "vanilla_dbg_compositor_t must be exactly 20 bytes");

    vanilla_dbg_window_t win;
    memset(&win, 0, sizeof(win));
    win.window_id = 42;
    win.pid = 100;
    win.x = 10; win.y = 20; win.w = 300; win.h = 200;
    win.layer = 1;
    win.is_mapped = 1;
    win.is_focused = 1;
    win.damage_w = 50; win.damage_h = 60;
    win.frame_count = 120;
    strncpy(win.title, "Test Window", sizeof(win.title) - 1);

    TEST_ASSERT(win.window_id == 42, "window_id field mismatch");
    TEST_ASSERT(win.pid == 100, "pid field mismatch");
    TEST_ASSERT(strcmp(win.title, "Test Window") == 0, "title field mismatch");
    TEST_ASSERT(win.w == 300 && win.h == 200, "geometry field mismatch");
    TEST_ASSERT(win.frame_count == 120, "frame_count field mismatch");
    return 0;
}

static int test_theme_set_and_mutate(void)
{
    vanilla_theme_t original;
    memcpy(&original, g_theme, sizeof(vanilla_theme_t));

    vanilla_theme_t custom;
    memcpy(&custom, &original, sizeof(vanilla_theme_t));
    custom.bg_base = 0xFF112233u;
    custom.accent = 0xFFAABBCCu;

    int rc = theme_set(&custom);
    TEST_ASSERT(rc == 0, "theme_set returned non-zero");
    TEST_ASSERT(g_theme->bg_base == 0xFF112233u, "g_theme->bg_base not updated");
    TEST_ASSERT(g_theme->accent == 0xFFAABBCCu, "g_theme->accent not updated");

    /* Restore */
    theme_set(&original);
    TEST_ASSERT(g_theme->bg_base == original.bg_base, "g_theme->bg_base not restored");
    return 0;
}

static int test_microui_id_uniqueness(void)
{
    mu_Context ctx;
    mu_init(&ctx);

    const char *token = "accent";
    mu_push_id(&ctx, token, (int)strlen(token));

    mu_push_id(&ctx, "r", 1);
    mu_Id id_r = mu_get_id(&ctx, "slider", 6);
    mu_pop_id(&ctx);

    mu_push_id(&ctx, "g", 1);
    mu_Id id_g = mu_get_id(&ctx, "slider", 6);
    mu_pop_id(&ctx);

    mu_push_id(&ctx, "b", 1);
    mu_Id id_b = mu_get_id(&ctx, "slider", 6);
    mu_pop_id(&ctx);

    mu_pop_id(&ctx);

    TEST_ASSERT(id_r != id_g, "R and G slider IDs must not collide");
    TEST_ASSERT(id_g != id_b, "G and B slider IDs must not collide");
    TEST_ASSERT(id_r != id_b, "R and B slider IDs must not collide");
    return 0;
}

static int test_compositor_overlay_render(void)
{
    vanilla_compositor_t comp;
    int rc = compositor_init_offscreen(&comp, 640, 480);
    TEST_ASSERT(rc == 0, "compositor_init_offscreen failed");
    TEST_ASSERT(comp.backbuffer != NULL, "comp backbuffer is NULL");

    /* Fill initial background */
    uint32_t bg_color = 0xFF2E3440u;
    for (uint32_t i = 0; i < comp.width * comp.height; i++) {
        comp.backbuffer[i] = bg_color;
    }

    g_perf_overlay_enabled = 1;
    compositor_render_overlay(&comp, 8500, 4, 3);

    /* Verify top-left HUD pixels are written */
    int32_t hx = THEME_PX(8) + THEME_PX(10);
    int32_t hy = THEME_PX(8) + THEME_PX(10);
    uint32_t sample = comp.backbuffer[hy * comp.pitch_px + hx];
    TEST_ASSERT(sample != bg_color, "compositor_render_overlay did not modify backbuffer");

    compositor_destroy(&comp);
    g_perf_overlay_enabled = 0;
    return 0;
}

int main(void)
{
    printf("========================================================\n");
    printf(" Project Vanilla - Developer Tools & Debug Protocol\n");
    printf("========================================================\n");

    int passed = 0;
    int total = 4;

    if (test_protocol_struct_sizes() != 0) {
        TSK_TEST_FAIL("devtools", "protocol_struct_sizes", "Structure size assertion failed");
        return 1;
    }
    TSK_TEST_PASS("devtools", "protocol_struct_sizes");
    passed++;

    if (test_theme_set_and_mutate() != 0) {
        TSK_TEST_FAIL("devtools", "theme_set_and_mutate", "Theme mutation failed");
        return 1;
    }
    TSK_TEST_PASS("devtools", "theme_set_and_mutate");
    passed++;

    if (test_microui_id_uniqueness() != 0) {
        TSK_TEST_FAIL("devtools", "microui_id_uniqueness", "Microui ID hashing collided");
        return 1;
    }
    TSK_TEST_PASS("devtools", "microui_id_uniqueness");
    passed++;

    if (test_compositor_overlay_render() != 0) {
        TSK_TEST_FAIL("devtools", "compositor_overlay_render", "Overlay rendering failed");
        return 1;
    }
    TSK_TEST_PASS("devtools", "compositor_overlay_render");
    passed++;

    TSK_TEST_DONE("devtools", passed, total);
    return 0;
}
