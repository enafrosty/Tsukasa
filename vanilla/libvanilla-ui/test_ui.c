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
            printf("[TEST] ui.assert FAIL: %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
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
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_checkbox_val = ev->toggle.state;
}

static int s_tab_val = -1;
static void on_test_tab(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_tab_val = ev->toggle.state;
}

static float s_slider_val = -1.0f;
static void on_test_slider(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_slider_val = ev->slider.value;
}

static int s_list_val = -1;
static void on_test_list(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_list_val = ev->toggle.state;
}

static int s_menu_val = -1;
static void on_test_menu(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        s_menu_val = ev->toggle.state;
}

static int s_modal_clicked = 0;
static void on_test_modal(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w; (void)ud;
    if (ev->type == UI_EVENT_CLICK)
        s_modal_clicked++;
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

    /* Enter on focused button */
    iev.code = KEY_ENTER;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_btn_clicked == 2, "enter activates focused button");

    /* Shift+Tab backwards navigation */
    ctx->shift_down = 1;
    iev.code = KEY_TAB;
    ui_handle_event(ctx, root, &iev);
    ctx->shift_down = 0;
    TEST_ASSERT(ui_get_focused(ctx) == tf, "shift+tab wraps backward to tf");

    /* Collapsed subtree skipping */
    ui_widget_t *hidden_box = ui_box(ctx, VDIR_COLUMN);
    hidden_box->layout_elem->h_mode = VSIZE_FIXED;
    hidden_box->layout_elem->h_px = 0;
    hidden_box->layout_elem->clip_children = 1;
    hidden_box->layout_elem->computed_w = 0;
    hidden_box->layout_elem->computed_h = 0;
    ui_widget_t *hidden_btn = ui_button(ctx, "Hidden", NULL, NULL);
    ui_widget_add_child(hidden_box, hidden_btn);
    ui_widget_add_child(root, hidden_box);

    ui_widget_set_focus(ctx, tf);
    ui_focus_next(ctx, root);
    TEST_ASSERT(ui_get_focused(ctx) == btn1, "tab skips collapsed subtree to btn1");

    /* Tab bar keyboard navigation */
    const char *test_tabs[] = { "Tab0", "Tab1", "Tab2" };
    ui_widget_t *tb = ui_tab_bar(ctx, test_tabs, 3, on_test_tab, NULL);
    ui_widget_add_child(root, tb);
    ui_widget_set_focus(ctx, tb);
    s_tab_val = -1;
    iev.code = KEY_RIGHT;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tb->tab_bar.active_tab == 1, "tab_bar right arrow moves to tab 1");
    TEST_ASSERT(s_tab_val == 1, "tab_bar event fired with tab 1");
    iev.code = KEY_LEFT;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tb->tab_bar.active_tab == 0, "tab_bar left arrow moves to tab 0");
    TEST_ASSERT(s_tab_val == 0, "tab_bar event fired with tab 0");

    /* Slider keyboard navigation */
    ui_widget_t *sl = ui_slider(ctx, 50.0f, 0.0f, 100.0f, on_test_slider, NULL);
    ui_widget_add_child(root, sl);
    ui_widget_set_focus(ctx, sl);
    s_slider_val = -1.0f;
    iev.code = KEY_RIGHT;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(sl->slider.value > 50.0f, "slider right arrow increments value");
    TEST_ASSERT(s_slider_val == sl->slider.value, "slider callback received incremented value");

    /* List keyboard navigation */
    const char *test_items[] = { "A", "B", "C" };
    ui_widget_t *lst = ui_list(ctx, test_items, 3, on_test_list, NULL);
    ui_widget_add_child(root, lst);
    ui_widget_set_focus(ctx, lst);
    s_list_val = -1;
    iev.code = KEY_DOWN;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(lst->list.selected == 1, "list down arrow moves selection to 1");
    TEST_ASSERT(s_list_val == 1, "list callback received selection 1");

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

    /* End key moves cursor to length */
    iev.code = KEY_END;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tf->text_field.cursor_pos == 4, "end key moves cursor to end");

    /* Delete at end of buffer does not crash */
    iev.code = KEY_DELETE;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tf->text_field.cursor_pos == 4, "delete at end keeps cursor");
    TEST_ASSERT(strcmp(tf->text_field.buf, "halo") == 0, "delete at end preserves content");

    /* Home and Backspace at 0 does not underflow */
    iev.code = KEY_HOME;
    ui_handle_event(ctx, root, &iev);
    iev.code = KEY_BACKSPACE;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(tf->text_field.cursor_pos == 0, "backspace at start keeps cursor at 0");
    TEST_ASSERT(strcmp(tf->text_field.buf, "halo") == 0, "backspace at start preserves content");

    /* Buffer capacity test: fill buffer to 255 chars */
    for (int i = (int)strlen(tf->text_field.buf); i < 255; i++) {
        tf->text_field.buf[i] = 'x';
        tf->text_field.cursor_pos = i + 1;
    }
    tf->text_field.buf[255] = '\0';
    TEST_ASSERT(strlen(tf->text_field.buf) == 255, "buffer filled to 255 chars");

    /* Attempt inserting 256th char -> must be rejected without buffer overflow */
    iev.code = KEY_Z;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(strlen(tf->text_field.buf) == 255, "256th char rejected, buffer stayed at 255");
    TEST_ASSERT(tf->text_field.buf[255] == '\0', "buffer null terminator intact");

    /* Mouse click cursor positioning using real TrueType glyph advances */
    tf->text_field.buf[0] = 'A';
    tf->text_field.buf[1] = 'B';
    tf->text_field.buf[2] = 'C';
    tf->text_field.buf[3] = '\0';
    tf->layout_elem->computed_x = 50;
    tf->layout_elem->computed_y = 50;
    tf->layout_elem->computed_w = 200;
    tf->layout_elem->computed_h = 28;
    tf->text_field.scroll_x = 0;
    int32_t ab_w = 0;
    app_measure_text_n("ABC", 2, app_default_font_size(), APP_FONT_REGULAR, &ab_w, NULL);
    ui_text_field_handle_click(tf, 56 + ab_w + 1, 55, 0);
    TEST_ASSERT(tf->text_field.cursor_pos == 2, "click placed cursor at index 2");

    /* Proportional TrueType glyphs: "iiii" is narrower than "MMMM" */
    int32_t narrow_w = 0, wide_w = 0;
    app_measure_text("iiii", &narrow_w, NULL);
    app_measure_text("MMMM", &wide_w, NULL);
    TEST_ASSERT(wide_w > narrow_w, "TrueType M is wider than i");

    strcpy(tf->text_field.buf, "MMMM");
    ui_text_field_handle_click(tf, 56 + narrow_w, 55, 0);
    int32_t pos_wide = tf->text_field.cursor_pos;
    strcpy(tf->text_field.buf, "iiii");
    ui_text_field_handle_click(tf, 56 + narrow_w, 55, 0);
    int32_t pos_narrow = tf->text_field.cursor_pos;
    TEST_ASSERT(pos_narrow > pos_wide, "same click X lands deeper in narrow string");

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

    ui_widget_t *dlg = ui_modal_dialog(ctx, "Title", "Body", "OK", on_test_modal, NULL);
    if (dlg->layout_elem) {
        dlg->layout_elem->h_mode = VSIZE_FIXED;
        dlg->layout_elem->h_px = 200;
    }
    ui_widget_add_child(root, dlg);

    ui_widget_t *tst = ui_toast(ctx, "Notice", 5000);
    ui_widget_add_child(root, tst);

    ui_widget_t *mnu = ui_menu(ctx, items, 3, on_test_menu, NULL);
    ui_widget_add_child(root, mnu);

    ui_widget_t *scv = ui_scroll_view(ctx);
    scv->scroll_view.content_h = 1000;
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

    /* Click checkbox (press + release) */
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = BTN_LEFT;
    iev.pad1 = (uint16_t)cb_x;
    iev.pad2 = (uint16_t)cb_y;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(cb->checkbox.checked == 1, "checkbox checked on press");
    TEST_ASSERT(s_checkbox_val == 1, "checkbox callback invoked");
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(cb->checkbox.checked == 1, "checkbox release maintains state");

    /* Radio exclusivity */
    int32_t rad2_x = rad2->layout_elem->computed_x + 5;
    int32_t rad2_y = rad2->layout_elem->computed_y + 5;
    iev.pad1 = (uint16_t)rad2_x;
    iev.pad2 = (uint16_t)rad2_y;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(rad2->radio.checked == 1, "radio 2 checked on click");
    TEST_ASSERT(rad1->radio.checked == 0, "radio 1 unchecked when radio 2 clicked");
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);

    /* Menu item click */
    int32_t mnu_x = mnu->layout_elem->computed_x + 10;
    int32_t mnu_y = mnu->layout_elem->computed_y + 24 + 10; /* Item 1 (row 1) */
    s_menu_val = -1;
    iev.pad1 = (uint16_t)mnu_x;
    iev.pad2 = (uint16_t)mnu_y;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(mnu->menu.selected == 1, "menu item 1 selected");
    TEST_ASSERT(s_menu_val == 1, "menu callback received index 1");
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);

    /* Modal dialog OK click on release */
    int32_t dw = dlg->layout_elem->computed_w;
    int32_t dh = dlg->layout_elem->computed_h;
    int32_t cx = dlg->layout_elem->computed_x + (dw - 360) / 2;
    int32_t cy = dlg->layout_elem->computed_y + (dh - 180) / 2;
    int32_t ok_x = cx + 360 - 96 + 20;
    int32_t ok_y = cy + 180 - 40 + 10;
    s_modal_clicked = 0;
    iev.pad1 = (uint16_t)ok_x;
    iev.pad2 = (uint16_t)ok_y;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_modal_clicked == 0, "modal not triggered on press alone");
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_modal_clicked == 1, "modal triggered on OK button release");

    PASS("widgets_and_render");
    return 0;
}

static uint32_t s_ext_pixels[640 * 480];
static uint8_t  s_layout_arena[64 * 1024];

static char s_mock_clip[256];
static int  s_mock_clip_len = 0;

static int mock_clip_set(const char *text, size_t len)
{
    if (!text)
        return -1;
    if (len >= sizeof(s_mock_clip))
        len = sizeof(s_mock_clip) - 1;
    memcpy(s_mock_clip, text, len);
    s_mock_clip[len] = '\0';
    s_mock_clip_len = (int)len;
    return 0;
}

static int mock_clip_get(char *out, size_t max_len)
{
    if (!out || max_len == 0)
        return 0;
    size_t n = (size_t)s_mock_clip_len;
    if (n >= max_len)
        n = max_len - 1;
    if (n > 0)
        memcpy(out, s_mock_clip, n);
    out[n] = '\0';
    return (int)n;
}

static int test_ui_scroll_and_list(void)
{
    vanilla_layout_t *lctx = vlayout_init(s_layout_arena, sizeof(s_layout_arena));
    TEST_ASSERT(lctx != NULL, "vlayout_init failed");
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), lctx);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    ui_widget_t *scv = ui_scroll_view(ctx);
    scv->layout_elem->h_mode = VSIZE_FIXED;
    scv->layout_elem->h_px = 100;
    ui_widget_add_child(root, scv);

    ui_widget_t *b1 = ui_button(ctx, "First", NULL, NULL);
    b1->layout_elem->h_mode = VSIZE_FIXED;
    b1->layout_elem->h_px = 120;
    ui_widget_t *b2 = ui_button(ctx, "Second", NULL, NULL);
    b2->layout_elem->h_mode = VSIZE_FIXED;
    b2->layout_elem->h_px = 120;
    ui_widget_add_child(scv, b1);
    ui_widget_add_child(scv, b2);

    ui_widget_t *lst = ui_list(ctx, NULL, 0, NULL, NULL);
    const char *long_items[20] = {
        "Row 00", "Row 01", "Row 02", "Row 03", "Row 04",
        "Row 05", "Row 06", "Row 07", "Row 08", "Row 09",
        "Row 10", "Row 11", "Row 12", "Row 13", "Row 14",
        "Row 15", "Row 16", "Row 17", "Row 18", "Row 19"
    };
    ui_list_set_items(lst, long_items, 20);
    lst->layout_elem->h_mode = VSIZE_FIXED;
    lst->layout_elem->h_px = 100;
    ui_widget_add_child(root, lst);

    vanilla_surface_t surf;
    memset(&surf, 0, sizeof(surf));
    surf.width = 400;
    surf.height = 300;
    surf.pitch = 400 * sizeof(uint32_t);
    surf.pixels = s_ext_pixels;

    ui_render(ctx, root, &surf, NULL);
    int32_t b1_y0 = b1->layout_elem->computed_y;
    int32_t b2_y0 = b2->layout_elem->computed_y;
    TEST_ASSERT(b2_y0 > b1_y0, "second child positioned below first");
    TEST_ASSERT(ui_scroll_view_effective_content_h(scv) >= 240, "scroll_view content height computed");

    /* Wheel scroll down inside scroll_view */
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_REL;
    iev.code = REL_WHEEL;
    iev.pad1 = (uint16_t)(scv->layout_elem->computed_x + 20);
    iev.pad2 = (uint32_t)(scv->layout_elem->computed_y + 20);
    iev.value = -2;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(scv->scroll_view.scroll_y > 0, "wheel down increased scroll_y");

    int32_t scrolled_y = scv->scroll_view.scroll_y;
    ui_render(ctx, root, &surf, NULL);
    TEST_ASSERT(b1->layout_elem->computed_y == b1_y0 - scrolled_y,
                "scroll_view translated child computed_y");

    /* Clipped second child outside viewport must not hit when offscreen, scroll to reveal it */
    ui_scroll_view_scroll_to(scv, 140);
    ui_render(ctx, root, &surf, NULL);
    TEST_ASSERT(scv->scroll_view.scroll_y == 140, "scroll_to clamped within range");
    ui_widget_t *hit = ui_hit_test(root,
                                   scv->layout_elem->computed_x + 20,
                                   scv->layout_elem->computed_y + 50);
    TEST_ASSERT(hit == b2, "scrolled child b2 now hittable inside viewport");

    /* Scrollbar press/drag on scroll_view scrollbar edge */
    int32_t sb_x = scv->layout_elem->computed_x + scv->layout_elem->computed_w - 4;
    int32_t sb_top = scv->layout_elem->computed_y + 5;
    iev.type = EV_KEY;
    iev.code = BTN_LEFT;
    iev.pad1 = (uint16_t)sb_x;
    iev.pad2 = (uint32_t)sb_top;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(scv->scroll_view.scroll_y < 140, "track click above thumb scrolled up");
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);

    /* List keyboard navigation & scroll-to-selection */
    ui_widget_set_focus(ctx, lst);
    iev.type = EV_KEY;
    iev.code = KEY_END;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(lst->list.selected == 19, "KEY_END selected last item");
    TEST_ASSERT(lst->list.scroll_top > 0, "list auto-scrolled to bottom selection");

    iev.code = KEY_HOME;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(lst->list.selected == 0, "KEY_HOME selected first item");
    TEST_ASSERT(lst->list.scroll_top == 0, "list auto-scrolled back to top");

    PASS("scroll_and_list");
    return 0;
}

static int test_ui_menus_and_sharp_corners(void)
{
    vanilla_layout_t *lctx = vlayout_init(s_layout_arena, sizeof(s_layout_arena));
    TEST_ASSERT(lctx != NULL, "vlayout_init failed");
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), lctx);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    const char *bar_labels[] = { "File", "Edit", "Help" };
    ui_widget_t *bar = ui_menubar(ctx, bar_labels, 3, on_test_menu, NULL);
    ui_widget_add_child(root, bar);

    const ui_menu_item_t mitems[] = {
        { "New",      "Ctrl+N", 0,                     1 },
        { NULL,       NULL,     UI_MENU_ITEM_SEPARATOR, 0 },
        { "Save",     "Ctrl+S", UI_MENU_ITEM_CHECKED,  2 },
        { "Disabled", NULL,     UI_MENU_ITEM_DISABLED, 3 },
    };
    s_menu_val = -1;
    ui_widget_t *pop = ui_menu_ex(ctx, mitems, 4, on_test_menu, NULL);
    TEST_ASSERT(pop->layout_elem->corner_radius == 0, "menu corner_radius is 0px sharp");
    TEST_ASSERT(bar->layout_elem->corner_radius == 0, "menubar corner_radius is 0px sharp");

    vanilla_surface_t surf;
    memset(&surf, 0, sizeof(surf));
    surf.width = 480;
    surf.height = 320;
    surf.pitch = 480 * sizeof(uint32_t);
    surf.pixels = s_ext_pixels;

    int32_t fx = ui_menubar_item_x(bar, 0);
    ui_ctx_show_popup(ctx, pop, fx, 28);
    ui_render(ctx, root, &surf, NULL);
    TEST_ASSERT(ui_ctx_get_popup(ctx) == pop, "popup active after show");

    /* Verify menu emit_draw uses zero rounded_rects (sharp 0px corners) */
    vanilla_draw_cmd_t cmd_buf[128];
    vanilla_draw_cmd_array_t cmds = { cmd_buf, 0, 128 };
    ui_widget_emit_draw(pop, &cmds);
    int rounded_count = 0;
    for (int32_t i = 0; i < cmds.count; i++) {
        if (cmd_buf[i].type == VCMD_ROUNDED_RECT)
            rounded_count++;
    }
    TEST_ASSERT(rounded_count == 0, "menu draw emits 0 rounded_rects (sharp 0px corners)");

    /* Clicking separator and disabled item must not trigger selection */
    int32_t sep_y = 0, sep_h = 0;
    ui_menu_row_rect(pop, 1, &sep_y, &sep_h);
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = BTN_LEFT;
    iev.pad1 = (uint16_t)(pop->layout_elem->computed_x + 12);
    iev.pad2 = (uint32_t)(sep_y + sep_h / 2);
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_menu_val == -1, "separator click ignored");
    TEST_ASSERT(ui_ctx_get_popup(ctx) == pop, "popup stays open on separator click");

    int32_t dis_y = 0, dis_h = 0;
    ui_menu_row_rect(pop, 3, &dis_y, &dis_h);
    iev.pad2 = (uint32_t)(dis_y + dis_h / 2);
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_menu_val == -1, "disabled menu item click ignored");

    /* Clicking enabled Save row fires callback and closes popup */
    int32_t sav_y = 0, sav_h = 0;
    ui_menu_row_rect(pop, 2, &sav_y, &sav_h);
    iev.pad2 = (uint32_t)(sav_y + sav_h / 2);
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_menu_val == 2, "Save menu item selected");
    TEST_ASSERT(ui_ctx_get_popup(ctx) == NULL, "popup auto-hidden after item click");

    PASS("menus_and_sharp_corners");
    return 0;
}

static ui_dialog_result_t s_last_dlg_res = UI_DIALOG_RES_NONE;
static void on_test_dialog(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)ud;
    if (w && w->type == UI_WIDGET_MODAL_DIALOG && ev->type == UI_EVENT_CLICK) {
        s_modal_clicked++;
        s_last_dlg_res = w->modal.result;
    }
}

static int test_ui_modals_and_progress(void)
{
    vanilla_layout_t *lctx = vlayout_init(s_layout_arena, sizeof(s_layout_arena));
    TEST_ASSERT(lctx != NULL, "vlayout_init failed");
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), lctx);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    s_btn_clicked = 0;
    ui_widget_t *bg_btn = ui_button(ctx, "Behind", on_test_btn_click, NULL);
    ui_widget_add_child(root, bg_btn);

    ui_widget_t *pb = ui_progress_bar_create(ctx, 42.0f);
    TEST_ASSERT(strcmp(pb->progress_bar.label_buf, "42%") == 0, "progress bar initial 42%");
    ui_progress_bar_set(pb, 75.6f);
    TEST_ASSERT(strcmp(pb->progress_bar.label_buf, "76%") == 0, "progress bar updated to 76%");
    ui_progress_bar_set(pb, 150.0f);
    TEST_ASSERT(pb->progress_bar.value == 100.0f, "progress bar clamped at 100");
    ui_widget_add_child(root, pb);

    vanilla_surface_t surf;
    memset(&surf, 0, sizeof(surf));
    surf.width = 480;
    surf.height = 360;
    surf.pitch = 480 * sizeof(uint32_t);
    surf.pixels = s_ext_pixels;

    /* Confirm modal overlay: blocks background clicks, Cancel sets RES_CANCEL */
    ui_widget_t *cfm = ui_dialog_confirm(ctx, "Delete?", "Confirm action",
                                         "Delete", "Cancel", on_test_dialog, NULL);
    ui_ctx_show_modal(ctx, cfm);
    ui_render(ctx, root, &surf, NULL);
    TEST_ASSERT(ui_ctx_get_modal(ctx) == cfm, "confirm modal shown");

    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = BTN_LEFT;
    iev.pad1 = (uint16_t)(bg_btn->layout_elem->computed_x + 10);
    iev.pad2 = (uint32_t)(bg_btn->layout_elem->computed_y + 10);
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_btn_clicked == 0, "modal blocks background button click");

    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    ui_modal_compute_layout(cfm, &cx, &cy, &cw, &ch);
    s_modal_clicked = 0;
    s_last_dlg_res = UI_DIALOG_RES_NONE;
    iev.pad1 = (uint16_t)(cx + cw - 186 + 20);
    iev.pad2 = (uint32_t)(cy + ch - 40 + 12);
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    iev.value = 0;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_modal_clicked == 1, "Cancel click invoked dialog callback");
    TEST_ASSERT(s_last_dlg_res == UI_DIALOG_RES_CANCEL, "Cancel click returned UI_DIALOG_RES_CANCEL");
    TEST_ASSERT(ui_ctx_get_modal(ctx) == NULL, "modal hidden after Cancel");

    /* Prompt modal: accepts text input and Enter submits OK */
    ui_widget_t *prm = ui_dialog_prompt(ctx, "Rename", "Enter new name",
                                        "doc", "Save", "Cancel", on_test_dialog, NULL);
    ui_ctx_show_modal(ctx, prm);
    ui_render(ctx, root, &surf, NULL);
    TEST_ASSERT(ui_get_focused(ctx) == prm->modal.input_field,
                "prompt modal auto-focused input field");

    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.code = KEY_2;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(strcmp(ui_dialog_get_input(prm), "doc2") == 0, "prompt text appended");

    s_modal_clicked = 0;
    s_last_dlg_res = UI_DIALOG_RES_NONE;
    iev.code = KEY_ENTER;
    iev.value = 1;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(s_modal_clicked == 1, "Enter confirmed prompt modal");
    TEST_ASSERT(s_last_dlg_res == UI_DIALOG_RES_OK, "Enter yielded UI_DIALOG_RES_OK");
    TEST_ASSERT(strcmp(ui_dialog_get_input(prm), "doc2") == 0, "prompt text retained after OK");
    TEST_ASSERT(ui_ctx_get_modal(ctx) == NULL, "prompt modal hidden after Enter");

    PASS("modals_and_progress");
    return 0;
}

static int test_ui_multiline_text_area(void)
{
    vanilla_layout_t *lctx = vlayout_init(s_layout_arena, sizeof(s_layout_arena));
    TEST_ASSERT(lctx != NULL, "vlayout_init failed");
    ui_ctx_t *ctx = ui_ctx_init(s_arena, sizeof(s_arena), lctx);
    TEST_ASSERT(ctx != NULL, "ui_ctx_init failed");
    ui_ctx_set_clipboard_ops(ctx, mock_clip_set, mock_clip_get);

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    ui_widget_t *ta = ui_text_area(ctx, "alpha\nbeta\ngamma", 1, NULL, NULL);
    ui_widget_t *next_btn = ui_button(ctx, "Next", NULL, NULL);
    ui_widget_add_child(root, ta);
    ui_widget_add_child(root, next_btn);

    TEST_ASSERT(ui_text_field_get_line_count(ta) == 3, "multiline count is 3");
    int32_t ln = 0, col = 0;
    ui_text_field_get_cursor_lc(ta, &ln, &col);
    TEST_ASSERT(ln == 1 && col == 1, "initial cursor at line 1 col 1");

    ui_widget_set_focus(ctx, ta);
    struct input_event iev;
    memset(&iev, 0, sizeof(iev));
    iev.type = EV_KEY;
    iev.value = 1;

    /* Move down to line 2, end of line, press Enter, insert Tab (4 spaces) */
    iev.code = KEY_DOWN;
    ui_handle_event(ctx, root, &iev);
    ui_text_field_get_cursor_lc(ta, &ln, &col);
    TEST_ASSERT(ln == 2, "KEY_DOWN moved to line 2");

    iev.code = KEY_END;
    ui_handle_event(ctx, root, &iev);
    iev.code = KEY_ENTER;
    ui_handle_event(ctx, root, &iev);
    iev.code = KEY_TAB;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(ui_get_focused(ctx) == ta, "Tab inside multiline text_area did not blur focus");
    TEST_ASSERT(ui_text_field_get_line_count(ta) == 4, "Enter added a line");
    TEST_ASSERT(strcmp(ui_text_field_get_text(ta), "alpha\nbeta\n    \ngamma") == 0,
                "Enter + Tab inserted newline and 4 spaces");

    /* Clipboard Ctrl+A / Ctrl+C / Ctrl+End / Ctrl+V */
    ctx->ctrl_down = 1;
    iev.code = KEY_A;
    ui_handle_event(ctx, root, &iev);
    iev.code = KEY_C;
    ui_handle_event(ctx, root, &iev);
    ctx->ctrl_down = 0;
    TEST_ASSERT(strcmp(s_mock_clip, "alpha\nbeta\n    \ngamma") == 0,
                "Ctrl+C copied multiline text");

    ui_text_field_set_text(ta, "head:");
    ctx->ctrl_down = 1;
    iev.code = KEY_END;
    ui_handle_event(ctx, root, &iev);
    iev.code = KEY_V;
    ui_handle_event(ctx, root, &iev);
    ctx->ctrl_down = 0;
    TEST_ASSERT(strncmp(ui_text_field_get_text(ta), "head:alpha", 10) == 0,
                "Ctrl+V pasted clipboard content");

    /* Render 30-line text area inside fixed box and verify wheel scrolling */
    ta->layout_elem->h_mode = VSIZE_FIXED;
    ta->layout_elem->h_px = 100;
    ui_text_field_set_text(ta,
                           "L1\nL2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\nL10\n"
                           "L11\nL12\nL13\nL14\nL15\nL16\nL17\nL18\nL19\nL20");
    vanilla_surface_t surf;
    memset(&surf, 0, sizeof(surf));
    surf.width = 480;
    surf.height = 240;
    surf.pitch = 480 * sizeof(uint32_t);
    surf.pixels = s_ext_pixels;
    ui_render(ctx, root, &surf, NULL);

    iev.type = EV_REL;
    iev.code = REL_WHEEL;
    iev.pad1 = (uint16_t)(ta->layout_elem->computed_x + 50);
    iev.pad2 = (uint32_t)(ta->layout_elem->computed_y + 40);
    iev.value = -2;
    ui_handle_event(ctx, root, &iev);
    TEST_ASSERT(ta->text_field.scroll_y > 0, "wheel scrolled multiline text_area");

    /* Shift+Tab leaves multiline editor */
    iev.type = EV_KEY;
    iev.code = KEY_TAB;
    iev.value = 1;
    ctx->shift_down = 1;
    ui_handle_event(ctx, root, &iev);
    ctx->shift_down = 0;
    TEST_ASSERT(ui_get_focused(ctx) != ta, "Shift+Tab moved focus out of multiline text_area");

    ui_text_field_free(ta);
    PASS("multiline_text_area");
    return 0;
}

int main(void)
{
    printf("=== libvanilla-ui unit tests ===\n");
    if (test_ui_tree_and_focus() != 0) return 1;
    if (test_ui_text_field() != 0) return 1;
    if (test_ui_widgets_and_render() != 0) return 1;
    if (test_ui_scroll_and_list() != 0) return 1;
    if (test_ui_menus_and_sharp_corners() != 0) return 1;
    if (test_ui_modals_and_progress() != 0) return 1;
    if (test_ui_multiline_text_area() != 0) return 1;
    printf("[TEST] ui DONE 7/7\n");
    return 0;
}
