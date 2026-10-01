/*
 * Project Tsukasa — Flex Layout Engine Unit Test Suite
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

#include "../include/layout.h"
#include "../include/draw_cmd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __has_include("../../scripts/test/tsk_test.h")
#include "../../scripts/test/tsk_test.h"
#endif

#ifndef TSK_TEST_PASS
#define TSK_TEST_PASS(suite, name) printf("[TEST] %s.%s PASS\n", suite, name)
#define TSK_TEST_FAIL(suite, name, reason) printf("[TEST] %s.%s FAIL: %s\n", suite, name, reason)
#define TSK_TEST_DONE(suite, passed, total) printf("[TEST] %s DONE %d/%d\n", suite, (int)(passed), (int)(total))
#endif

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            return -1; \
        } \
    } while (0)

static uint8_t g_arena[64 * 1024];

static int test_flex_column_and_row(void)
{
    vanilla_layout_t *ctx = vlayout_init(g_arena, sizeof(g_arena));
    TEST_ASSERT(ctx != NULL, "vlayout_init returned NULL");

    /*
     * Root container: Column, bounds 640x480.
     * Padding 10 all sides, gap 10.
     * Inner bounds: width = 620, height = 460.
     */
    vanilla_elem_t *root = vlayout_box(ctx);
    TEST_ASSERT(root != NULL, "vlayout_box returned NULL");
    root->direction = VDIR_COLUMN;
    root->pad_top = 10;
    root->pad_bottom = 10;
    root->pad_left = 10;
    root->pad_right = 10;
    root->gap = 10;
    root->bg_color = 0xFF2E3440;

    /*
     * Row 1: Fixed height 100, Grow width.
     * Padding 0, gap 20.
     */
    vanilla_elem_t *row1 = vlayout_box(ctx);
    row1->direction = VDIR_ROW;
    row1->h_mode = VSIZE_FIXED;
    row1->h_px = 100;
    row1->w_mode = VSIZE_GROW;
    row1->gap = 20;
    row1->bg_color = 0xFF3B4252;
    vlayout_add_child(root, row1);

    /* Inside Row 1: Box A (fixed 100), Box B (grow), Box C (grow) */
    vanilla_elem_t *boxA = vlayout_box(ctx);
    boxA->w_mode = VSIZE_FIXED;
    boxA->w_px = 100;
    boxA->h_mode = VSIZE_GROW;
    boxA->bg_color = 0xFF88C0D0;
    vlayout_add_child(row1, boxA);

    vanilla_elem_t *boxB = vlayout_box(ctx);
    boxB->w_mode = VSIZE_GROW;
    boxB->h_mode = VSIZE_GROW;
    boxB->bg_color = 0xFF81A1C1;
    vlayout_add_child(row1, boxB);

    vanilla_elem_t *boxC = vlayout_box(ctx);
    boxC->w_mode = VSIZE_GROW;
    boxC->h_mode = VSIZE_GROW;
    boxC->bg_color = 0xFF5E81AC;
    vlayout_add_child(row1, boxC);

    /*
     * Row 2: Grow height (available = 460 - 100 - 10 = 350), Grow width.
     * align_items = VALIGN_CENTER, gap 20.
     */
    vanilla_elem_t *row2 = vlayout_box(ctx);
    row2->direction = VDIR_ROW;
    row2->h_mode = VSIZE_GROW;
    row2->w_mode = VSIZE_GROW;
    row2->align_items = VALIGN_CENTER;
    row2->gap = 20;
    row2->bg_color = 0xFF434C5E;
    vlayout_add_child(root, row2);

    /* Inside Row 2: Box D (grow), Box E (fixed 120x50) */
    vanilla_elem_t *boxD = vlayout_box(ctx);
    boxD->w_mode = VSIZE_GROW;
    boxD->h_mode = VSIZE_GROW;
    boxD->bg_color = 0xFFA3BE8C;
    vlayout_add_child(row2, boxD);

    vanilla_elem_t *boxE = vlayout_box(ctx);
    boxE->w_mode = VSIZE_FIXED;
    boxE->w_px = 120;
    boxE->h_mode = VSIZE_FIXED;
    boxE->h_px = 50;
    boxE->bg_color = 0xFFEBCB8B;
    vlayout_add_child(row2, boxE);

    /* Compute layout for 640x480 */
    vlayout_compute(ctx, root, 0, 0, 640, 480);

    /* Check root geometry */
    TEST_ASSERT(root->computed_x == 0, "root x mismatch");
    TEST_ASSERT(root->computed_y == 0, "root y mismatch");
    TEST_ASSERT(root->computed_w == 640, "root w mismatch");
    TEST_ASSERT(root->computed_h == 480, "root h mismatch");

    /* Check Row 1 geometry */
    TEST_ASSERT(row1->computed_x == 10, "row1 x mismatch");
    TEST_ASSERT(row1->computed_y == 10, "row1 y mismatch");
    TEST_ASSERT(row1->computed_w == 620, "row1 w mismatch");
    TEST_ASSERT(row1->computed_h == 100, "row1 h mismatch");

    /* Check Row 1 children: avail w = 620 - 100 - 40 = 480 => B=240, C=240 */
    TEST_ASSERT(boxA->computed_x == 10, "boxA x mismatch");
    TEST_ASSERT(boxA->computed_y == 10, "boxA y mismatch");
    TEST_ASSERT(boxA->computed_w == 100, "boxA w mismatch");
    TEST_ASSERT(boxA->computed_h == 100, "boxA h mismatch");

    TEST_ASSERT(boxB->computed_x == 130, "boxB x mismatch");
    TEST_ASSERT(boxB->computed_y == 10, "boxB y mismatch");
    TEST_ASSERT(boxB->computed_w == 240, "boxB w mismatch");
    TEST_ASSERT(boxB->computed_h == 100, "boxB h mismatch");

    TEST_ASSERT(boxC->computed_x == 390, "boxC x mismatch");
    TEST_ASSERT(boxC->computed_y == 10, "boxC y mismatch");
    TEST_ASSERT(boxC->computed_w == 240, "boxC w mismatch");
    TEST_ASSERT(boxC->computed_h == 100, "boxC h mismatch");

    /* Check Row 2 geometry: y = 10 + 100 + 10 = 120, h = 350 */
    TEST_ASSERT(row2->computed_x == 10, "row2 x mismatch");
    TEST_ASSERT(row2->computed_y == 120, "row2 y mismatch");
    TEST_ASSERT(row2->computed_w == 620, "row2 w mismatch");
    TEST_ASSERT(row2->computed_h == 350, "row2 h mismatch");

    /* Check Row 2 children: avail w = 620 - 120 - 20 = 480 => D=480 */
    TEST_ASSERT(boxD->computed_x == 10, "boxD x mismatch");
    TEST_ASSERT(boxD->computed_y == 120, "boxD y mismatch");
    TEST_ASSERT(boxD->computed_w == 480, "boxD w mismatch");
    TEST_ASSERT(boxD->computed_h == 350, "boxD h mismatch");

    /* Box E: centered vertically in row2: y = 120 + (350 - 50)/2 = 270 */
    TEST_ASSERT(boxE->computed_x == 510, "boxE x mismatch");
    TEST_ASSERT(boxE->computed_y == 270, "boxE y mismatch");
    TEST_ASSERT(boxE->computed_w == 120, "boxE w mismatch");
    TEST_ASSERT(boxE->computed_h == 50, "boxE h mismatch");

    /* Emit draw commands */
    vanilla_draw_cmd_array_t cmds;
    memset(&cmds, 0, sizeof(cmds));
    vlayout_emit(ctx, root, &cmds);

    TEST_ASSERT(cmds.count > 0, "emit produced 0 commands");
    TEST_ASSERT(cmds.cmds[0].type == VCMD_FILL_RECT, "cmd 0 type mismatch");
    TEST_ASSERT(cmds.cmds[0].bounds.w == 640, "cmd 0 bounds w mismatch");
    TEST_ASSERT(cmds.cmds[0].fill_rect.color == 0xFF2E3440, "cmd 0 color mismatch");

    return 0;
}

static int test_draw_command_execution(void)
{
    /* Test execution of all draw command types */
    uint32_t pixels[64 * 64];
    memset(pixels, 0, sizeof(pixels));

    vanilla_surface_t surf;
    surf.width = 64;
    surf.height = 64;
    surf.pitch = 64 * sizeof(uint32_t);
    surf.size = sizeof(pixels);
    surf.pixels = pixels;
    surf.shm_id = -1;

    vanilla_surface_t src_surf;
    uint32_t src_pixels[16 * 16];
    for (int i = 0; i < 16 * 16; i++)
        src_pixels[i] = 0xFFAABBCC;
    src_surf.width = 16;
    src_surf.height = 16;
    src_surf.pitch = 16 * sizeof(uint32_t);
    src_surf.size = sizeof(src_pixels);
    src_surf.pixels = src_pixels;
    src_surf.shm_id = -1;

    vanilla_draw_cmd_t cmd_buffer[10];
    vanilla_draw_cmd_array_t cmds;
    cmds.cmds = cmd_buffer;
    cmds.count = 0;
    cmds.capacity = 10;

    /* 1. FILL_RECT */
    cmd_buffer[cmds.count].type = VCMD_FILL_RECT;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){0, 0, 64, 64};
    cmd_buffer[cmds.count].fill_rect.color = 0xFF112233;
    cmds.count++;

    /* 2. PUSH_CLIP */
    cmd_buffer[cmds.count].type = VCMD_PUSH_CLIP;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){4, 4, 56, 56};
    cmds.count++;

    /* 3. ROUNDED_RECT (stubbed as fill_rect) */
    cmd_buffer[cmds.count].type = VCMD_ROUNDED_RECT;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){8, 8, 20, 20};
    cmd_buffer[cmds.count].rounded_rect.color = 0xFF445566;
    cmd_buffer[cmds.count].rounded_rect.radius = 4;
    cmds.count++;

    /* 4. BORDER */
    cmd_buffer[cmds.count].type = VCMD_BORDER;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){30, 8, 20, 20};
    cmd_buffer[cmds.count].border.color = 0xFF778899;
    cmd_buffer[cmds.count].border.width = 1;
    cmds.count++;

    /* 5. TEXT */
    cmd_buffer[cmds.count].type = VCMD_TEXT;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){8, 35, 40, 10};
    cmd_buffer[cmds.count].text.text = "OK";
    cmd_buffer[cmds.count].text.color = 0xFFFFFFFF;
    cmd_buffer[cmds.count].text.font_size = 8.0f;
    cmd_buffer[cmds.count].text.font_id = 0;
    cmds.count++;

    /* 6. IMAGE */
    cmd_buffer[cmds.count].type = VCMD_IMAGE;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){40, 40, 8, 8};
    cmd_buffer[cmds.count].image.src = &src_surf;
    cmd_buffer[cmds.count].image.src_rect = (vanilla_draw_rect_t){0, 0, 8, 8};
    cmds.count++;

    /* 7. POP_CLIP */
    cmd_buffer[cmds.count].type = VCMD_POP_CLIP;
    cmd_buffer[cmds.count].bounds = (vanilla_draw_rect_t){0, 0, 0, 0};
    cmds.count++;

    /* Execute all commands */
    vanilla_execute_draw_commands(&surf, &cmds, NULL);

    /* Verify background filled */
    TEST_ASSERT(pixels[0] == 0xFF112233, "pixel 0 background color mismatch");
    /* Verify rounded rect painted in clipped area */
    TEST_ASSERT(pixels[8 * 64 + 8] == 0xFF445566, "rounded rect pixel mismatch");
    /* Verify border painted */
    TEST_ASSERT(pixels[8 * 64 + 30] == 0xFF778899, "border pixel mismatch");
    /* Verify image copied */
    TEST_ASSERT(pixels[40 * 64 + 40] == 0xFFAABBCC, "image pixel mismatch");

    return 0;
}

static int test_edge_cases(void)
{
    vanilla_layout_t *ctx = vlayout_init(g_arena, sizeof(g_arena));
    TEST_ASSERT(ctx != NULL, "vlayout_init NULL");

    /* 1. Null / zero-size bounds */
    vlayout_compute(ctx, NULL, 0, 0, 100, 100);

    vanilla_elem_t *elem = vlayout_box(ctx);
    vlayout_compute(ctx, elem, 0, 0, 0, 0);
    TEST_ASSERT(elem->computed_w == 0, "zero w mismatch");
    TEST_ASSERT(elem->computed_h == 0, "zero h mismatch");

    /* 2. Negative bounds clamped to 0 */
    vlayout_compute(ctx, elem, 0, 0, -100, -50);
    TEST_ASSERT(elem->computed_w == 0, "negative w clamp failed");
    TEST_ASSERT(elem->computed_h == 0, "negative h clamp failed");

    /* 3. Text element sizing */
    vlayout_reset(ctx);
    vanilla_elem_t *text_elem = vlayout_text(ctx, "Hello\nWorld", 0xFFFFFFFF, 8.0f);
    vlayout_compute(ctx, text_elem, 0, 0, 200, 200);
    /* 5 chars * 8px = 40px wide, 2 lines * 8px + 2px = 18px high */
    TEST_ASSERT(text_elem->computed_w == 40, "text w mismatch");
    TEST_ASSERT(text_elem->computed_h == 18, "text h mismatch");

    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int passed = 0;
    int total = 3;

    if (test_flex_column_and_row() == 0) {
        TSK_TEST_PASS("layout", "flex_column_and_row");
        passed++;
    } else {
        TSK_TEST_FAIL("layout", "flex_column_and_row", "assertions failed");
    }

    if (test_draw_command_execution() == 0) {
        TSK_TEST_PASS("layout", "draw_command_execution");
        passed++;
    } else {
        TSK_TEST_FAIL("layout", "draw_command_execution", "assertions failed");
    }

    if (test_edge_cases() == 0) {
        TSK_TEST_PASS("layout", "edge_cases");
        passed++;
    } else {
        TSK_TEST_FAIL("layout", "edge_cases", "assertions failed");
    }

    TSK_TEST_DONE("layout", passed, total);

    return (passed == total) ? 0 : 1;
}
