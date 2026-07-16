/*
 * hello_gui.c - Phase 6 userspace GUI sample using libui + libwidget only.
 */

#include "../include/libui.h"
#include "../include/libwidget.h"
#include "../lib/syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct demo_state {
    ui_window_t win;
    int running;

    widget_button_t button;
    widget_textbox_t textbox;
    widget_scrollbar_t scrollbar;
    widget_dropdown_t dropdown;
    widget_checkbox_t checkbox;

    int click_count;
    int mouse_down;
    int mouse_x;
    int mouse_y;
    int scroll_value;
    int selected_theme;
    int checked;
    char text[64];
    const char *theme_items[3];
} demo_state_t;

static int str_len(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void int_to_str(int v, char *out, int cap)
{
    char rev[16];
    int ri = 0;
    int oi = 0;
    if (cap <= 0)
        return;
    if (v == 0) {
        if (cap > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }
    if (v < 0) {
        out[oi++] = '-';
        v = -v;
    }
    while (v > 0 && ri < (int)sizeof(rev)) {
        rev[ri++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (ri > 0 && oi < cap - 1)
        out[oi++] = rev[--ri];
    out[oi] = '\0';
}

static void ctx_draw_rect(void *user_data, int x, int y, int w, int h, uint32_t color)
{
    demo_state_t *st = (demo_state_t *)user_data;
    ui_draw_rect(st->win, x, y, w, h, color);
}

static void ctx_draw_rr(void *user_data, int x, int y, int w, int h, int radius, uint32_t color)
{
    demo_state_t *st = (demo_state_t *)user_data;
    ui_draw_rounded_rect_filled(st->win, x, y, w, h, radius, color);
}

static void ctx_draw_string(void *user_data, int x, int y, const char *str, uint32_t color)
{
    demo_state_t *st = (demo_state_t *)user_data;
    ui_draw_string(st->win, x, y, str, color);
}

static int ctx_measure(void *user_data, const char *str)
{
    (void)user_data;
    return (int)ui_get_string_width(str);
}

static void ctx_mark_dirty(void *user_data, int x, int y, int w, int h)
{
    demo_state_t *st = (demo_state_t *)user_data;
    ui_mark_dirty(st->win, x, y, w, h);
}

static void on_button_click(void *user_data)
{
    demo_state_t *st = (demo_state_t *)user_data;
    st->click_count++;
}

static void on_scroll(void *user_data, int new_scroll_y)
{
    demo_state_t *st = (demo_state_t *)user_data;
    st->scroll_value = new_scroll_y;
}

static void on_select(void *user_data, int new_idx)
{
    demo_state_t *st = (demo_state_t *)user_data;
    st->selected_theme = new_idx;
}

static void on_toggle(void *user_data, bool new_state)
{
    demo_state_t *st = (demo_state_t *)user_data;
    st->checked = new_state ? 1 : 0;
}

static void draw_ui(demo_state_t *st)
{
    widget_context_t ctx;
    char clicks_buf[16];
    char scroll_buf[16];
    uint32_t bg = 0xFF1A1D24u;
    uint32_t panel = 0xFF232A35u;
    uint32_t fg = 0xFFFFFFFFu;
    const char *theme_name = "Dark";

    if (st->selected_theme == 1) {
        bg = 0xFFF2F2F2u;
        panel = 0xFFE3E3E3u;
        fg = 0xFF1F1F1Fu;
        theme_name = "Light";
    } else if (st->selected_theme == 2) {
        bg = 0xFF162233u;
        panel = 0xFF20324Bu;
        fg = 0xFFE4F2FFu;
        theme_name = "Ocean";
    }

    ui_draw_rect(st->win, 0, 0, 720, 420, bg);
    ui_draw_rounded_rect_filled(st->win, 16, 16, 688, 388, 8, panel);
    ui_draw_string(st->win, 28, 28, "Phase 6 GUI Syscall Demo", fg);
    ui_draw_string(st->win, 28, 44, "All rendering/event flow goes through SYS_GUI", fg);

    int_to_str(st->click_count, clicks_buf, sizeof(clicks_buf));
    int_to_str(st->scroll_value, scroll_buf, sizeof(scroll_buf));

    ui_draw_string(st->win, 28, 70, "Clicks:", fg);
    ui_draw_string(st->win, 84, 70, clicks_buf, fg);
    ui_draw_string(st->win, 140, 70, "Scroll:", fg);
    ui_draw_string(st->win, 204, 70, scroll_buf, fg);
    ui_draw_string(st->win, 260, 70, "Theme:", fg);
    ui_draw_string(st->win, 316, 70, theme_name, fg);
    ui_draw_string(st->win, 412, 70, st->checked ? "Checked" : "Unchecked", fg);

    widget_context_init(&ctx,
                        st,
                        ctx_draw_rect,
                        ctx_draw_rr,
                        ctx_draw_string,
                        ctx_measure,
                        ctx_mark_dirty,
                        (st->selected_theme == 1));

    widget_button_draw(&ctx, &st->button);
    widget_textbox_draw(&ctx, &st->textbox);
    widget_scrollbar_draw(&ctx, &st->scrollbar);
    widget_dropdown_draw(&ctx, &st->dropdown);
    widget_checkbox_draw(&ctx, &st->checkbox);

    ui_mark_dirty(st->win, 0, 0, 720, 420);
}

void hello_gui_main(void)
{
    demo_state_t st;
    ui_event_t ev;

    for (size_t i = 0; i < sizeof(st); i++)
        ((uint8_t *)&st)[i] = 0;

    st.running = 1;
    st.scroll_value = 0;
    st.selected_theme = 0;
    st.checked = 0;
    st.text[0] = '\0';
    st.theme_items[0] = "Dark";
    st.theme_items[1] = "Light";
    st.theme_items[2] = "Ocean";

    st.win = ui_window_create("SYS_GUI Widgets", 120, 70, 720, 420);
    if ((int64_t)st.win <= 0)
        exit(1);

    widget_button_init(&st.button, 28, 100, 164, 34, "Click Me");
    st.button.on_click = on_button_click;

    widget_textbox_init(&st.textbox, 28, 148, 320, 34, st.text, sizeof(st.text));

    widget_scrollbar_init(&st.scrollbar, 664, 100, 18, 220);
    widget_scrollbar_update(&st.scrollbar, 900, st.scroll_value);
    st.scrollbar.on_scroll = on_scroll;

    widget_dropdown_init(&st.dropdown, 360, 100, 180, 34, st.theme_items, 3);
    st.dropdown.on_select = on_select;

    widget_checkbox_init(&st.checkbox, 360, 148, 220, 30, "Enable Option", false);
    st.checkbox.on_toggle = on_toggle;

    draw_ui(&st);

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
            draw_ui(&st);
            continue;
        }
        if (ev.type == UI_EVENT_KEY) {
            if ((uint32_t)ev.keycode == 27u) {
                st.running = 0;
                break;
            }
            widget_textbox_handle_key(&st.textbox, (char)(ev.keycode & 0xFF), &st);
            draw_ui(&st);
            continue;
        }
        if (ev.type == UI_EVENT_MOUSE_MOVE) {
            st.mouse_x = ev.x;
            st.mouse_y = ev.y;
            widget_button_handle_mouse(&st.button, st.mouse_x, st.mouse_y,
                                       st.mouse_down, false, &st);
            widget_scrollbar_handle_mouse(&st.scrollbar, st.mouse_x, st.mouse_y,
                                          st.mouse_down, &st);
            draw_ui(&st);
            continue;
        }
        if (ev.type == UI_EVENT_MOUSE_DOWN || ev.type == UI_EVENT_CLICK ||
            ev.type == UI_EVENT_RIGHT_CLICK) {
            bool clicked = (ev.type == UI_EVENT_CLICK || ev.type == UI_EVENT_RIGHT_CLICK);
            st.mouse_down = 1;
            st.mouse_x = ev.x;
            st.mouse_y = ev.y;
            widget_button_handle_mouse(&st.button, st.mouse_x, st.mouse_y, true, clicked, &st);
            widget_textbox_handle_mouse(&st.textbox, st.mouse_x, st.mouse_y, clicked, &st);
            widget_dropdown_handle_mouse(&st.dropdown, st.mouse_x, st.mouse_y, clicked, &st);
            widget_checkbox_handle_mouse(&st.checkbox, st.mouse_x, st.mouse_y, clicked, &st);
            widget_scrollbar_handle_mouse(&st.scrollbar, st.mouse_x, st.mouse_y, true, &st);
            draw_ui(&st);
            continue;
        }
        if (ev.type == UI_EVENT_MOUSE_UP) {
            st.mouse_down = 0;
            st.mouse_x = ev.x;
            st.mouse_y = ev.y;
            widget_button_handle_mouse(&st.button, st.mouse_x, st.mouse_y, false, false, &st);
            widget_scrollbar_handle_mouse(&st.scrollbar, st.mouse_x, st.mouse_y, false, &st);
            draw_ui(&st);
            continue;
        }
    }

    ui_window_destroy(st.win);
    exit(0);
}
