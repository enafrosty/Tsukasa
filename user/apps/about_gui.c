#include "../include/app_runtime.h"
#include "../include/libui.h"
#include "../include/stdlib.h"
#include "../include/unistd.h"

static int about_main(int argc, char **argv)
{
    ui_window_t win;
    ui_event_t ev;
    int running = 1;
    (void)argc;
    (void)argv;
    win = ui_window_create("About Tsukasa", 220, 160, 320, 180);
    if ((int64_t)win <= 0)
        return 1;

    while (running) {
        ui_draw_rect(win, 0, 0, 320, 180, 0xFF1A1F2Au);
        ui_draw_string(win, 18, 24, "Tsukasa OS", 0xFFFFFFFFu);
        ui_draw_string(win, 18, 42, "Phase 7 userspace cutover", 0xFFE0ECFFu);
        ui_draw_string(win, 18, 70, "GUI apps now run via libui/libwidget", 0xFFAAC7E6u);
        ui_draw_string(win, 18, 88, "Shell + CLI toolchain lives in userspace", 0xFFAAC7E6u);
        ui_mark_dirty(win, 0, 0, 320, 180);

        if (!ui_get_event(win, &ev)) {
            yield();
            continue;
        }
        if (ev.type == UI_EVENT_CLOSE)
            break;
        if (ev.type == UI_EVENT_KEY && (char)(ev.keycode & 0xFF) == 27)
            break;
    }
    ui_window_destroy(win);
    return 0;
}

void app_about_gui_entry(void)
{
    _exit(app_run_main(about_main));
}
