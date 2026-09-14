/*
 * Project Tsukasa — Desktop Shell and Quick Launcher Unit Tests
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>

#include "../server/server.h"
#include "../server/shell.h"
#include "../server/launcher.h"

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: ASSERT failed: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
            exit(1); \
        } \
    } while (0)

static void test_shell_layout_and_clock(void)
{
    printf("[TEST] Shell layout and clock...\n");

    vanilla_shell_t shell;
    shell_init(&shell);

    ASSERT(shell.start_menu_open == 0, "start menu initially closed");
    ASSERT(TASKBAR_HEIGHT == 36, "taskbar height is 36px");
    ASSERT(TASKBAR_START_X == 4, "start button x offset");
    ASSERT(TASKBAR_START_W == 64, "start button width");
    ASSERT(TASKBAR_START_H == 28, "start button height");
    ASSERT(TASKBAR_PILL_START_X == 74, "window pill start x");
    ASSERT(TASKBAR_PILL_W == 120, "window pill width");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);

    shell_update_clock(&shell, &srv);
    ASSERT(strlen(shell.clock_str) == 5, "clock string length is 5 (HH:MM)");
    ASSERT(shell.clock_str[2] == ':', "clock string delimiter is ':'");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

static void test_fuzzy_matching_and_ranking(void)
{
    printf("[TEST] Fuzzy matching and search ranking...\n");

    int score_exact = launcher_fuzzy_match("term", "Terminal");
    ASSERT(score_exact > 0, "fuzzy matches prefix term in Terminal");

    int score_subseq = launcher_fuzzy_match("calc", "Calculator");
    ASSERT(score_subseq > 0, "fuzzy matches calc in Calculator");

    int score_empty = launcher_fuzzy_match("", "Terminal");
    ASSERT(score_empty == 100, "empty pattern yields score 100");

    int score_fail = launcher_fuzzy_match("xyz999", "Terminal");
    ASSERT(score_fail == -1, "unmatched pattern returns -1");

    vanilla_launcher_t launcher;
    launcher_init(&launcher);
    ASSERT(launcher.visible == 0, "launcher initially hidden");
    ASSERT(launcher.match_count > 0, "initial matches populate all apps");

    /* Search for 'calc' */
    strncpy(launcher.query, "calc", sizeof(launcher.query) - 1);
    launcher.query_len = 4;
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);
    srv.launcher = launcher;
    srv.launcher.visible = 1;

    /* Handle backspace and typing */
    launcher_handle_key(&srv, KEY_BACKSPACE, 1);
    ASSERT(srv.launcher.query_len == 3, "backspace removes one char");
    ASSERT(strcmp(srv.launcher.query, "cal") == 0, "query is 'cal'");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

static void test_evdev_to_ascii_and_nav(void)
{
    printf("[TEST] Evdev keycode to ASCII and navigation...\n");

    ASSERT(launcher_evdev_to_ascii(KEY_A, 0) == 'a', "KEY_A lowercase");
    ASSERT(launcher_evdev_to_ascii(KEY_A, 1) == 'A', "KEY_A uppercase with Shift");
    ASSERT(launcher_evdev_to_ascii(KEY_Z, 0) == 'z', "KEY_Z lowercase");
    ASSERT(launcher_evdev_to_ascii(KEY_Z, 1) == 'Z', "KEY_Z uppercase with Shift");
    ASSERT(launcher_evdev_to_ascii(KEY_1, 0) == '1', "KEY_1 digit");
    ASSERT(launcher_evdev_to_ascii(KEY_1, 1) == '!', "KEY_1 exclamation with Shift");
    ASSERT(launcher_evdev_to_ascii(KEY_0, 0) == '0', "KEY_0 digit");
    ASSERT(launcher_evdev_to_ascii(KEY_0, 1) == ')', "KEY_0 paren with Shift");
    ASSERT(launcher_evdev_to_ascii(KEY_SPACE, 0) == ' ', "KEY_SPACE is space");
    ASSERT(launcher_evdev_to_ascii(KEY_MINUS, 0) == '-', "KEY_MINUS is hyphen");
    ASSERT(launcher_evdev_to_ascii(KEY_MINUS, 1) == '_', "KEY_MINUS is underscore with Shift");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);
    launcher_init(&srv.launcher);
    launcher_set_visible(&srv, 1);

    ASSERT(srv.launcher.selected_idx == 0, "selected idx is initially 0");
    launcher_handle_key(&srv, KEY_DOWN, 1);
    ASSERT(srv.launcher.selected_idx == 1, "KEY_DOWN increments selected idx");
    launcher_handle_key(&srv, KEY_UP, 1);
    ASSERT(srv.launcher.selected_idx == 0, "KEY_UP decrements selected idx");

    launcher_handle_key(&srv, KEY_ESC, 1);
    ASSERT(srv.launcher.visible == 0, "KEY_ESC closes launcher");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

static void test_aero_snap_and_work_area(void)
{
    printf("[TEST] Aero-snap window tiling and desktop work area clamping...\n");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);

    vanilla_server_window_t *w = &srv.windows[0];
    w->in_use = 1;
    w->window_id = 1;
    w->x = 100;
    w->y = 100;
    w->width = 400;
    w->height = 300;
    w->flags = WINDOW_FLAG_NONE;
    w->is_mapped = 1;
    w->is_snapped = SNAP_NONE;
    strncpy(w->title, "Test Window", sizeof(w->title) - 1);

    /* Maximize snap */
    wm_snap_window(&srv, 1, SNAP_MAXIMIZE);
    ASSERT(w->is_snapped == SNAP_MAXIMIZE, "window is maximized");

    vanilla_rect_t frame;
    wm_get_frame_rect(w, &frame);
    ASSERT(frame.x == 0, "maximized frame x is 0");
    ASSERT(frame.y == 0, "maximized frame y is 0");
    ASSERT(frame.w == 1024, "maximized frame width is screen width");
    ASSERT(frame.h == 768 - TASKBAR_HEIGHT, "maximized frame height clamped to desktop work area");
    ASSERT(frame.y + frame.h <= 768 - TASKBAR_HEIGHT, "maximized window does not overlap taskbar");

    /* Left half snap */
    wm_snap_window(&srv, 1, SNAP_LEFT);
    ASSERT(w->is_snapped == SNAP_LEFT, "window is left-snapped");
    wm_get_frame_rect(w, &frame);
    ASSERT(frame.x == 0, "left snap frame x is 0");
    ASSERT(frame.w == 512, "left snap frame width is half screen");
    ASSERT(frame.h == 768 - TASKBAR_HEIGHT, "left snap frame height clamped to work area");

    /* Right half snap */
    wm_snap_window(&srv, 1, SNAP_RIGHT);
    ASSERT(w->is_snapped == SNAP_RIGHT, "window is right-snapped");
    wm_get_frame_rect(w, &frame);
    ASSERT(frame.x == 512, "right snap frame x is half screen");
    ASSERT(frame.w == 512, "right snap frame width is half screen");
    ASSERT(frame.h == 768 - TASKBAR_HEIGHT, "right snap frame height clamped to work area");

    /* Unsnap and restore original floating geometry */
    wm_unsnap_window(&srv, 1);
    ASSERT(w->is_snapped == SNAP_NONE, "window is unsnapped");
    ASSERT(w->x == 100, "restored floating x");
    ASSERT(w->y == 100, "restored floating y");
    ASSERT(w->width == 400, "restored floating width");
    ASSERT(w->height == 300, "restored floating height");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

static void test_input_cursor_tracking_and_damage(void)
{
    printf("[TEST] Input cursor tracking and bounding box invalidation...\n");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);

    srv.cursor_x = 512;
    srv.cursor_y = 384;
    srv.compositor.dirty_count = 0;

    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_REL;
    ev.code = REL_X;
    ev.value = 24;

    wm_handle_input_event(&srv, &ev);
    ASSERT(srv.cursor_x == 536, "cursor_x updated by +24");
    ASSERT(srv.compositor.dirty_count >= 2, "both old and new cursor boxes invalidated in damage queue");

    /* Check boundary clamping */
    ev.code = REL_X;
    ev.value = -2000;
    wm_handle_input_event(&srv, &ev);
    ASSERT(srv.cursor_x == 0, "cursor clamped to left screen boundary (0)");

    ev.code = REL_Y;
    ev.value = 5000;
    wm_handle_input_event(&srv, &ev);
    ASSERT(srv.cursor_y == 767, "cursor clamped to bottom screen boundary (767)");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

static void test_shell_click_and_window_toggle(void)
{
    printf("[TEST] Taskbar click interaction and window toggle...\n");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    compositor_init_offscreen(&srv.compositor, 1024, 768);
    shell_init(&srv.shell);
    launcher_init(&srv.launcher);

    /* Click Start button at (10, 750) */
    int handled = shell_handle_click(&srv, 10, 750, BTN_LEFT);
    ASSERT(handled == 1, "taskbar click consumed");
    ASSERT(srv.launcher.visible == 1, "start button click opened quick launcher");

    /* Click Start button again */
    handled = shell_handle_click(&srv, 10, 750, BTN_LEFT);
    ASSERT(handled == 1, "taskbar click consumed");
    ASSERT(srv.launcher.visible == 0, "start button click toggled quick launcher closed");

    /* Create test window */
    vanilla_server_window_t *w = &srv.windows[0];
    w->in_use = 1;
    w->window_id = 1;
    w->x = 50;
    w->y = 50;
    w->width = 300;
    w->height = 200;
    w->is_mapped = 1;
    w->is_focused = 1;
    srv.focused_window_id = 1;
    strncpy(w->title, "Editor", sizeof(w->title) - 1);

    /* Click pill 0 at x = 80, y = 750 */
    handled = shell_handle_click(&srv, 80, 750, BTN_LEFT);
    ASSERT(handled == 1, "pill click consumed");
    ASSERT(w->is_mapped == 0, "clicking active pill toggles window to minimized");

    /* Click pill 0 again to restore */
    handled = shell_handle_click(&srv, 80, 750, BTN_LEFT);
    ASSERT(handled == 1, "pill click consumed");
    ASSERT(w->is_mapped == 1, "clicking pill restores minimized window");
    ASSERT(w->is_focused == 1, "restored window receives focus");

    compositor_destroy(&srv.compositor);
    printf("       Passed.\n");
}

int main(void)
{
    printf("============================================================\n");
    printf("  Project Tsukasa - Desktop Shell & Launcher Unit Tests\n");
    printf("============================================================\n");

    test_shell_layout_and_clock();
    test_fuzzy_matching_and_ranking();
    test_evdev_to_ascii_and_nav();
    test_aero_snap_and_work_area();
    test_input_cursor_tracking_and_damage();
    test_shell_click_and_window_toggle();

    printf("\n[SUCCESS] All Desktop Shell & Launcher unit tests passed!\n");
    return 0;
}
