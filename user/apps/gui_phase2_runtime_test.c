#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

int spawn(const char *path);

#define TEST_WIN_W 560
#define TEST_WIN_H 320

#define GUI_PHASE2_HELPER_PATH "/bin/gui-phase2-isolation-helper"
#define GUI_PHASE2_HANDLE_FILE "/tmp/gui_phase2_owner_handle.txt"
#define GUI_PHASE2_RESULT_FILE "/tmp/gui_phase2_isolation_result.txt"

enum {
    STAGE_PAINT_CONFIRM = 0,
    STAGE_EVENT_ORDER = 1,
    STAGE_DONE = 2,
};

static int write_int_file(const char *path, int value)
{
    char buf[64];
    int fd;
    int len;

    if (!path)
        return -1;
    len = snprintf(buf, sizeof(buf), "%d\n", value);
    if (len <= 0)
        return -1;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
        return -1;
    if (write(fd, buf, (size_t)len) != len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int read_int_file(const char *path, int *out_value)
{
    char buf[64];
    int fd;
    int n;

    if (!path || !out_value)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    *out_value = atoi(buf);
    return 0;
}

static const char *event_name(int type)
{
    switch (type) {
    case UI_EVENT_PAINT: return "PAINT";
    case UI_EVENT_CLICK: return "CLICK";
    case UI_EVENT_RIGHT_CLICK: return "RIGHT_CLICK";
    case UI_EVENT_CLOSE: return "CLOSE";
    case UI_EVENT_KEY: return "KEY";
    case UI_EVENT_KEYUP: return "KEYUP";
    case UI_EVENT_MOUSE_DOWN: return "MOUSE_DOWN";
    case UI_EVENT_MOUSE_UP: return "MOUSE_UP";
    case UI_EVENT_MOUSE_MOVE: return "MOUSE_MOVE";
    case UI_EVENT_MOUSE_WHEEL: return "MOUSE_WHEEL";
    case UI_EVENT_RESIZE: return "RESIZE";
    default: return "UNKNOWN";
    }
}

static int run_abi_sanity(ui_window_t win)
{
    uint64_t sw = 0;
    uint64_t sh = 0;
    static const uint32_t img4[4] = {
        0xFFFF0000u, 0xFF00FF00u,
        0xFF0000FFu, 0xFFFFFF00u
    };
    int ok = 1;
    int rc;
    uint32_t width;
    uint32_t font_h;

    rc = ui_window_set_title_ex(win, "Phase2 GUI Runtime Test");
    if (rc != UI_OK)
        ok = 0;
    rc = ui_window_set_resizable_ex(win, true);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_window_set_resizable_ex(win, false);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_window_set_resizable_ex(win, true);
    if (rc != UI_OK)
        ok = 0;

    rc = ui_draw_rect_ex(win, 14, 14, 120, 32, 0xFF233142u);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_draw_rounded_rect_filled_ex(win, 150, 14, 120, 32, 6, 0xFF285D85u);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_draw_string_ex(win, 22, 24, "ABI SANITY", 0xFFFFFFFFu);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_draw_image_ex(win, 290, 18, 2, 2, img4);
    if (rc != UI_OK)
        ok = 0;
    rc = ui_mark_dirty_ex(win, 0, 0, TEST_WIN_W, TEST_WIN_H);
    if (rc != UI_OK)
        ok = 0;

    width = ui_get_string_width("ABI");
    font_h = ui_get_font_height();
    if (width == 0 || font_h == 0)
        ok = 0;
    if (ui_get_screen_size_ex(&sw, &sh) != UI_OK || sw == 0 || sh == 0)
        ok = 0;

    rc = ui_draw_image_ex(win, 0, 0, 2, 2, NULL);
    if (rc != UI_ERR_INVALID)
        ok = 0;

    return ok;
}

static int run_process_isolation(ui_window_t owner_win)
{
    int child_pid;
    int wait_status = 0;
    int helper_rc = 0;

    if (write_int_file(GUI_PHASE2_HANDLE_FILE, (int)owner_win) != 0)
        return 0;

    child_pid = spawn(GUI_PHASE2_HELPER_PATH);
    if (child_pid <= 0)
        return 0;
    if (waitpid(child_pid, &wait_status, 0) != child_pid)
        return 0;
    if (read_int_file(GUI_PHASE2_RESULT_FILE, &helper_rc) != 0)
        return 0;

    return helper_rc == UI_ERR_PERM;
}

static int run_overlap_stage(ui_window_t base_win)
{
    ui_window_t overlay;
    int draw_ok = 1;

    draw_ok &= (ui_draw_rect_ex(base_win, 30, 80, 220, 120, 0xFF304860u) == UI_OK);
    draw_ok &= (ui_draw_rect_ex(base_win, 60, 110, 220, 120, 0xFF476E94u) == UI_OK);
    draw_ok &= (ui_draw_rect_ex(base_win, 90, 140, 220, 120, 0xFF5E90C2u) == UI_OK);
    draw_ok &= (ui_draw_string_ex(base_win, 30, 66, "Backbuffer persistence test pattern", 0xFFFFFFFFu) == UI_OK);
    draw_ok &= (ui_mark_dirty_ex(base_win, 20, 60, 340, 220) == UI_OK);

    overlay = ui_window_create("Phase2 Overlay", 170, 120, 280, 180);
    if ((int64_t)overlay <= 0)
        return 0;
    draw_ok &= (ui_draw_rect_ex(overlay, 0, 0, 280, 180, 0xFF181A28u) == UI_OK);
    draw_ok &= (ui_draw_string_ex(overlay, 16, 20, "Overlay window", 0xFFFFFFFFu) == UI_OK);
    draw_ok &= (ui_draw_string_ex(overlay, 16, 40, "This should not erase", 0xFFB9D0FFu) == UI_OK);
    draw_ok &= (ui_draw_string_ex(overlay, 16, 56, "the base window content.", 0xFFB9D0FFu) == UI_OK);
    draw_ok &= (ui_mark_dirty_ex(overlay, 0, 0, 280, 180) == UI_OK);

    for (int i = 0; i < 20; i++)
        yield();

    if (ui_window_destroy(overlay) != UI_OK)
        draw_ok = 0;
    if (ui_mark_dirty_ex(base_win, 0, 0, TEST_WIN_W, TEST_WIN_H) != UI_OK)
        draw_ok = 0;
    return draw_ok;
}

static void draw_test_ui(ui_window_t win,
                         int abi_ok,
                         int isolation_ok,
                         int overlay_stage_ok,
                         int paint_confirmed,
                         int stage,
                         const int *trace,
                         int trace_len)
{
    char line[96];
    int y = 14;

    ui_draw_rect(win, 0, 0, TEST_WIN_W, TEST_WIN_H, 0xFF141A24u);
    ui_draw_string(win, 16, y, "Phase 2 GUI Runtime Validation", 0xFFFFFFFFu);
    y += 18;

    snprintf(line, sizeof(line), "ABI sanity: %s", abi_ok ? "PASS" : "FAIL");
    ui_draw_string(win, 16, y, line, abi_ok ? 0xFF7CF8A4u : 0xFFFF9D9Du);
    y += 14;

    snprintf(line, sizeof(line), "Process isolation: %s", isolation_ok ? "PASS" : "FAIL");
    ui_draw_string(win, 16, y, line, isolation_ok ? 0xFF7CF8A4u : 0xFFFF9D9Du);
    y += 14;

    snprintf(line, sizeof(line), "Overlap/backbuffer stage: %s", overlay_stage_ok ? "PASS" : "FAIL");
    ui_draw_string(win, 16, y, line, overlay_stage_ok ? 0xFF7CF8A4u : 0xFFFF9D9Du);
    y += 18;

    if (stage == STAGE_PAINT_CONFIRM) {
        ui_draw_string(win, 16, y, "Manual paint persistence check:", 0xFFE8ECFFu);
        y += 14;
        ui_draw_string(win, 16, y, "1) Verify layered blue pattern is still visible.", 0xFFB9C7E2u);
        y += 14;
        ui_draw_string(win, 16, y, "2) Press Y to confirm, N to fail.", 0xFFB9C7E2u);
        y += 18;
        snprintf(line, sizeof(line), "Paint persistence confirmed: %s", paint_confirmed ? "YES" : "NO");
        ui_draw_string(win, 16, y, line, paint_confirmed ? 0xFF7CF8A4u : 0xFFFFD37Au);
    } else if (stage == STAGE_EVENT_ORDER) {
        ui_draw_string(win, 16, y, "Event order test:", 0xFFE8ECFFu);
        y += 14;
        ui_draw_string(win, 16, y, "Click once, press+release 'a', right-click once, then close window.", 0xFFB9C7E2u);
        y += 14;
        snprintf(line, sizeof(line), "Captured events: %d", trace_len);
        ui_draw_string(win, 16, y, line, 0xFFB9C7E2u);
        y += 14;
        for (int i = 0; i < trace_len && i < 8; i++) {
            snprintf(line, sizeof(line), "%d: %s", i + 1, event_name(trace[i]));
            ui_draw_string(win, 32, y, line, 0xFFCFE3FFu);
            y += 12;
        }
    } else {
        ui_draw_string(win, 16, y, "Validation complete. See console output for final verdict.", 0xFFE8ECFFu);
    }

    ui_mark_dirty(win, 0, 0, TEST_WIN_W, TEST_WIN_H);
}

static int validate_event_trace(const int *trace, int len)
{
    static const int expected[] = {
        UI_EVENT_MOUSE_DOWN,
        UI_EVENT_CLICK,
        UI_EVENT_MOUSE_UP,
        UI_EVENT_KEY,
        UI_EVENT_KEYUP,
        UI_EVENT_RIGHT_CLICK,
        UI_EVENT_CLOSE
    };
    int expected_len = (int)(sizeof(expected) / sizeof(expected[0]));

    if (!trace || len != expected_len)
        return 0;
    for (int i = 0; i < expected_len; i++) {
        if (trace[i] != expected[i])
            return 0;
    }
    return 1;
}

static int gui_phase2_runtime_test_main(int argc, char **argv)
{
    ui_window_t win;
    ui_event_t ev;
    int stage = STAGE_PAINT_CONFIRM;
    int abi_ok;
    int isolation_ok;
    int overlay_stage_ok;
    int paint_confirmed = 0;
    int event_trace[16];
    int trace_len = 0;
    int event_order_ok = 0;
    int running = 1;
    int overall_ok;
    (void)argc;
    (void)argv;

    win = ui_window_create("Phase2 GUI Runtime Test", 90, 80, TEST_WIN_W, TEST_WIN_H);
    if ((int64_t)win <= 0) {
        dprintf(2, "gui-phase2-runtime-test: failed to create window rc=%d\n", (int)win);
        return 1;
    }

    abi_ok = run_abi_sanity(win);
    isolation_ok = run_process_isolation(win);
    overlay_stage_ok = run_overlap_stage(win);
    draw_test_ui(win, abi_ok, isolation_ok, overlay_stage_ok, paint_confirmed, stage, event_trace, trace_len);

    while (running) {
        int rc = ui_get_event_ex(win, &ev);
        if (rc == UI_ERR_AGAIN) {
            yield();
            continue;
        }
        if (rc != UI_OK) {
            running = 0;
            break;
        }

        if (ev.type == UI_EVENT_PAINT || ev.type == UI_EVENT_RESIZE) {
            draw_test_ui(win, abi_ok, isolation_ok, overlay_stage_ok, paint_confirmed, stage, event_trace, trace_len);
            continue;
        }

        if (stage == STAGE_PAINT_CONFIRM) {
            if (ev.type == UI_EVENT_CLOSE) {
                running = 0;
                break;
            }
            if (ev.type == UI_EVENT_KEY) {
                char ch = (char)(ev.keycode & 0xFF);
                if (ch == 'y' || ch == 'Y') {
                    paint_confirmed = 1;
                    stage = STAGE_EVENT_ORDER;
                    trace_len = 0;
                } else if (ch == 'n' || ch == 'N') {
                    paint_confirmed = 0;
                    stage = STAGE_EVENT_ORDER;
                    trace_len = 0;
                }
            }
            continue;
        }

        if (stage == STAGE_EVENT_ORDER) {
            if (ev.type == UI_EVENT_MOUSE_MOVE || ev.type == UI_EVENT_MOUSE_WHEEL)
                continue;
            if (trace_len < (int)(sizeof(event_trace) / sizeof(event_trace[0])))
                event_trace[trace_len++] = ev.type;
            if (ev.type == UI_EVENT_CLOSE) {
                event_order_ok = validate_event_trace(event_trace, trace_len);
                stage = STAGE_DONE;
                running = 0;
            }
        }
    }

    overall_ok = (abi_ok && isolation_ok && overlay_stage_ok && paint_confirmed && event_order_ok);

    dprintf(1, "gui-phase2-runtime-test: ABI %s\n", abi_ok ? "PASS" : "FAIL");
    dprintf(1, "gui-phase2-runtime-test: EVENT_ORDER %s\n", event_order_ok ? "PASS" : "FAIL");
    dprintf(1, "gui-phase2-runtime-test: PAINT_PERSISTENCE %s (manual confirm)\n",
            paint_confirmed ? "PASS" : "FAIL");
    dprintf(1, "gui-phase2-runtime-test: PROCESS_ISOLATION %s\n", isolation_ok ? "PASS" : "FAIL");
    dprintf(1, "gui-phase2-runtime-test: OVERALL %s\n", overall_ok ? "PASS" : "FAIL");

    if (!event_order_ok) {
        dprintf(1, "gui-phase2-runtime-test: captured event trace (%d):\n", trace_len);
        for (int i = 0; i < trace_len; i++)
            dprintf(1, "  %d: %s (%d)\n", i + 1, event_name(event_trace[i]), event_trace[i]);
    }

    ui_window_destroy(win);
    return overall_ok ? 0 : 1;
}

void app_gui_phase2_runtime_test_entry(void)
{
    _exit(app_run_main(gui_phase2_runtime_test_main));
}
