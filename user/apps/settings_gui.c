#include "../include/app_runtime.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#include "../lib/syscall.h"

#define ST_W 460
#define ST_H 300

static const uint32_t g_accents[] = {
    0xFF4FC3F7u, 0xFF34D399u, 0xFFF59E0Bu,
    0xFFF43F5Eu, 0xFF8B5CF6u, 0xFF64748Bu
};

typedef struct settings_state {
    ui_window_t win;
    int selected;
    int running;
    char status[96];
} settings_state_t;

static void st_set_status(settings_state_t *st, const char *msg)
{
    if (!st)
        return;
    strncpy(st->status, msg ? msg : "", sizeof(st->status) - 1);
    st->status[sizeof(st->status) - 1] = '\0';
}

static void st_draw(settings_state_t *st)
{
    ui_draw_rect(st->win, 0, 0, ST_W, ST_H, 0xFF1D2630u);
    ui_draw_rect(st->win, 0, 0, ST_W, 20, 0xFF34495Eu);
    ui_draw_string(st->win, 8, 6, "Settings (1-6 accent | G gradient | S solid | W wallpaper)", 0xFFFFFFFFu);

    for (int i = 0; i < (int)(sizeof(g_accents) / sizeof(g_accents[0])); i++) {
        int x = 18 + i * 70;
        ui_draw_rect(st->win, x, 44, 56, 36, g_accents[i]);
        if (i == st->selected)
            ui_draw_rect(st->win, x - 2, 42, 60, 2, 0xFFFFFFFFu);
    }
    ui_draw_string(st->win, 16, 100, "W: /tmp/screenshot.bmp  F: fill  C: center", 0xFFE3EDF7u);
    ui_draw_string(st->win, 16, 116, "R refreshes theme state from kernel.", 0xFFE3EDF7u);
    ui_draw_string(st->win, 16, ST_H - 18, st->status, 0xFF9AD1FFu);
    ui_mark_dirty(st->win, 0, 0, ST_W, ST_H);
}

static int settings_main(int argc, char **argv)
{
    settings_state_t st;
    ui_event_t ev;
    struct tsukasa_theme_state theme;
    (void)argc;
    (void)argv;
    memset(&st, 0, sizeof(st));
    st.win = ui_window_create("Settings", 130, 90, ST_W, ST_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;
    st.selected = 0;
    st_set_status(&st, "Ready");
    st_draw(&st);

    while (st.running) {
        if (!ui_get_event(st.win, &ev)) {
            yield();
            continue;
        }
        if (ev.type == UI_EVENT_CLOSE) {
            st.running = 0;
            break;
        }
        if (ev.type == UI_EVENT_PAINT || ev.type == UI_EVENT_RESIZE) {
            st_draw(&st);
            continue;
        }
        if (ev.type != UI_EVENT_KEY)
            continue;

        {
            char c = (char)(ev.keycode & 0xFF);
            if (c >= '1' && c <= '6') {
                st.selected = c - '1';
                theme_set_accent(g_accents[st.selected]);
                st_set_status(&st, "Accent updated.");
            } else if (c == 'g' || c == 'G') {
                if (theme_set_bg_mode(TSUKASA_THEME_BG_GRADIENT) == 0)
                    st_set_status(&st, "Background mode: gradient.");
                else
                    st_set_status(&st, "Unable to set gradient background.");
            } else if (c == 's' || c == 'S') {
                if (theme_set_bg_mode_ex(TSUKASA_THEME_BG_SOLID, g_accents[st.selected]) == 0)
                    st_set_status(&st, "Background mode: solid (selected accent).");
                else
                    st_set_status(&st, "Unable to set solid background.");
            } else if (c == 'w' || c == 'W') {
                if (theme_set_wallpaper("/tmp/screenshot.bmp") == 0)
                    st_set_status(&st, "Wallpaper set to /tmp/screenshot.bmp.");
                else
                    st_set_status(&st, "Wallpaper update failed.");
            } else if (c == 'f' || c == 'F') {
                if (theme_set_bg_mode_ex(TSUKASA_THEME_BG_WALLPAPER, TSUKASA_THEME_WP_SCALE_FILL) == 0)
                    st_set_status(&st, "Wallpaper layout: scale-fill.");
                else
                    st_set_status(&st, "Unable to set wallpaper fill mode.");
            } else if (c == 'c' || c == 'C') {
                if (theme_set_bg_mode_ex(TSUKASA_THEME_BG_WALLPAPER, TSUKASA_THEME_WP_CENTER) == 0)
                    st_set_status(&st, "Wallpaper layout: center.");
                else
                    st_set_status(&st, "Unable to set wallpaper center mode.");
            } else if (c == 'r' || c == 'R') {
                if (theme_get_state(&theme) == 0) {
                    st.selected = 0;
                    for (int i = 0; i < (int)(sizeof(g_accents) / sizeof(g_accents[0])); i++) {
                        if (theme.accent_color == g_accents[i]) {
                            st.selected = i;
                            break;
                        }
                    }
                    st_set_status(&st, "Theme state refreshed.");
                } else {
                    st_set_status(&st, "Unable to read theme state.");
                }
            }
        }
        st_draw(&st);
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_settings_gui_entry(void)
{
    _exit(app_run_main(settings_main));
}
