/*
 * Project Tsukasa — Retained Widget Toolkit Unit Tests
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

#include "ui.h"
#include "ui_widgets.h"
#include "ui_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            return -1; \
        } \
    } while (0)

#define PASS(name) printf("[TEST] ui.%s PASS\n", name)

static uint8_t s_arena[128 * 1024];

static int s_btn_clicked = 0;
static void on_test_btn_click(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    (void)ud;
    if (ev->type == UI_EVENT_CLICK)
        s_btn_clicked++;
}

static int s_checkbox_val = -1;
static void on_test_checkbox(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_checkbox_val = ev->toggle.state;
}

static int test_ui_tree_and_focus(void)
{
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), NULL);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    TEST_ASSERT(root != NULL, "ui_box failed");

    ui_widget_t *btn1 = ui_button(ctx, "Button 1", on_test_btn_click, NULL);
    ui_widget_t *btn2 = ui_button(ctx, "Button 2", on_test_btn_click, NULL);
    ui_widget_t *lbl = ui_label(ctx, "Hello", 0xFFFFFFFF);
    ui_widget_t *tf = ui_text_field(ctx, "init", NULL, NULL);

    ui_widget_add_child(root, btn1);
    ui_widget_add_child(root, lbl);
    ui_widget_add_child(root, btn2);
    ui_widget_add_child(root, tf);

    TEST_ASSERT(root->first_child == btn1, "first child is btn1");
    TEST_ASSERT(btn1->next_sibling == lbl, "btn1 sibling is lbl");
    TEST_ASSERT(lbl->next_sibling == btn2, "lbl sibling is btn2");
    TEST_ASSERT(btn2->next_sibling == tf, "btn2 sibling is tf");

    /* Focus chain tests */
    TEST_ASSERT(btn1->focusable == 1, "btn1 is focusable");
    TEST_ASSERT(lbl->focusable == 0, "label is not focusable");
    TEST_ASSERT(btn2->focusable == 1, "btn2 is focusable");
    TEST_ASSERT(tf->focusable == 1, "text field is focusable");

    /* Initially nothing focused */
    TEST_ASSERT(ui_get_focused(ctx) == NULL, "initially nothing focused");

    /* Next focus -> btn1 */
    ui_focus_next(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == btn1, "focus next -> btn1");
    TEST_ASSERT(btn1->focused == 1, "btn1 focused bit set");

    /* Next focus -> btn2 (skipping lbl) */
    ui_focus_next(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == btn2, "focus next -> btn2");
    TEST_ASSERT(btn1->focused == 0, "btn1 focused bit cleared");
    TEST_ASSERT(btn2->focused == 1, "btn2 focused bit set");

    /* Next focus -> tf */
    ui_focus_next(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == tf, "focus next -> tf");

    /* Next focus -> wrap to btn1 */
    ui_focus_next(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == btn1, "focus wrap -> btn1");

    /* Prev focus -> wrap to tf */
    ui_focus_prev(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == tf, "focus prev -> tf");

    /* Space on focused button */
    ui_widget_set_focus(ctx, btn1);
    s_btn_clicked = 0;
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = KEY_SPACE;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_btn_clicked == 1, "space activates focused button");

    PASS("tree_and_focus");
    return 0;
}

static int test_ui_text_field(void)
{
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), NULL);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    ui_widget_t *tf = ui_text_field(ctx, "hello", NULL, NULL);
    ui_widget_add_child(root, tf);

    ui_widget_set_focus(ctx, tf);
    TEST_ASSERT(strcmp(tf->text_field.buf, "hello") == 0, "initial text");
    TEST_ASSERT(tf->text_field.cursor_pos == 5, "cursor at end");

    /* Key press '!' -> "hello!" */
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = KEY_1;
    iev.value = 1;
    ctx->shift_down = 1;
    ui_handle_event(ctx, root, &iev);
    ctx->shift_down = 0;
    TEST_ASSERT(strcmp(tf->text_field.buf, "hello!") == 0, "inserted char with shift");
    TEST_ASSERT(tf->text_field.cursor_pos == 6, "cursor advanced");

    /* Backspace -> "hello" */
    iev.code = KEY_BACKSPACE;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(strcmp(tf->text_field.buf, "hello") == 0, "backspace removed char");
    TEST_ASSERT(tf->text_field.cursor_pos == 5, "cursor decremented");

    /* Home -> cursor 0 */
    iev.code = KEY_HOME;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tf->text_field.cursor_pos == 0, "home moves cursor to 0");

    /* Right arrow -> cursor 1 */
    iev.code = KEY_RIGHT;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tf->text_field.cursor_pos == 1, "right moves cursor to 1");

    /* Delete -> deletes 'e' -> "hllo" */
    iev.code = KEY_DELETE;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(strcmp(tf->text_field.buf, "hllo") == 0, "delete removed char at cursor");
    TEST_ASSERT(tf->text_field.cursor_pos == 1, "cursor remained at 1");

    /* Selection via Shift+Right */
    ctx->shift_down = 1;
    iev.code = KEY_RIGHT;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    ctx->shift_down = 0;
    TEST_ASSERT(tf->text_field.sel_start == 1, "selection start is 1");
    TEST_ASSERT(tf->text_field.sel_end == 2, "selection end is 2");

    /* Typing 'a' replaces selected char -> "halo" */
    iev.code = KEY_A;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(strcmp(tf->text_field.buf, "halo") == 0, "typing replaced selection");
    TEST_ASSERT(tf->text_field.sel_start == -1, "selection cleared");

    PASS("text_field");
    return 0;
}

static int test_ui_widgets_and_render(void)
{
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), NULL);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    root->layout_elem->pad_left = 10;
    root->layout_elem->pad_top = 10;
    root->layout_elem->pad_right = 10;
    root->layout_elem->pad_bottom = 10;
    root->layout_elem->gap = 8;
    root->layout_elem->bg_color = 0xFF2E3440;

    ui_widget_t *cb = ui_checkbox(ctx, "Check me", 0, on_test_checkbox, NULL);
    ui_widget_add_child(root, cb);

    ui_widget_t *rad1 = ui_radio(ctx, "Radio 1", 1, 1, NULL, NULL);
    ui_widget_t *rad2 = ui_radio(ctx, "Radio 2", 1, 0, NULL, NULL);
    ui_widget_add_child(root, rad1);
    ui_widget_add_child(root, rad2);

    ui_widget_t *sl = ui_slider(ctx, 50.0f, 0.0f, 100.0f, NULL, NULL);
    ui_widget_add_child(root, sl);

    const char *items[] = { "Item 1", "Item 2", "Item 3" };
    ui_widget_t *lst = ui_list(ctx, items, 3, NULL, NULL);
    ui_widget_add_child(root, lst);

    const char *tabs[] = { "Tab A", "Tab B" };
    ui_widget_t *tb = ui_tab_bar(ctx, tabs, 2, NULL, NULL);
    ui_widget_add_child(root, tb);

    ui_widget_t *dlg = ui_modal_dialog(ctx, "Title", "Body", "OK", NULL, NULL);
    ui_widget_add_child(root, dlg);

    ui_widget_t *tst = ui_toast(ctx, "Notice", 5000);
    ui_widget_add_child(root, tst);

    ui_widget_t *mnu = ui_menu(ctx, items, 3, NULL, NULL);
    ui_widget_add_child(root, mnu);

    ui_widget_t *scv = ui_scroll_view(ctx);
    ui_widget_add_child(root, scv);

    /* Allocate dummy surface for rendering */
    uint32_t pixels[640 * 480];
    vanilla_surface_t surf;
    memset(&surf, 0, sizeof(surf));
    surf.width = 640;
    surf.height = 480;
    surf.pitch = 640 * sizeof(uint32_t);
    surf.pixels = pixels;

    /* Render without errors */
    ui_render(ctx, root, &surf, NULL);

    /* Verify root dirty flag was cleared */
    TEST_ASSERT(root->dirty == 0, "root dirty flag cleared after render");

    /* Invalidate and partial render */
    ui_widget_invalidate(cb);
    TEST_ASSERT(cb->dirty == 1, "cb dirty after invalidate");
    TEST_ASSERT(root->dirty == 1, "root dirty bubbled up");

    vanilla_rect_t clip = { 0, 0, 200, 100 };
    ui_render(ctx, root, &surf, &clip);
    TEST_ASSERT(root->dirty == 0, "root dirty cleared after clipped render");

    /* Hit test checkbox */
    int32_t cb_x = cb->layout_elem->computed_x + 5;
    int32_t cb_y = cb->layout_elem->computed_y + 5;
    ui_widget_t *hit = ui_hit_test(root, cb_x, cb_y);
    TEST_ASSERT(hit == cb, "hit test found checkbox");

    /* Click checkbox */
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = BTN_LEFT;
    iev.pad1 = (uint16_t)cb_x;
    iev.pad2 = (uint16_t)cb_y;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(cb->checkbox.checked == 1, "checkbox checked on click");
    TEST_ASSERT(s_checkbox_val == 1, "checkbox callback invoked");

    PASS("widgets_and_render");
    return 0;
}

int main(void)
{
    printf("=== libvanilla-ui unit tests ===\n");
    if (test_ui_tree_and_focus() != 0) return 1;
    if (test_ui_text_field() != 0) return 1;
    if (test_ui_widgets_and_render() != 0) return 1;
    printf("[TEST] ui DONE 3/3\n");
    return 0;
}
