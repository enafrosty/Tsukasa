/*
 * calc.c - Simple calculator app.
 * Grid of on-screen buttons for 0-9, +, -, *, /, =, C.
 * Supports basic integer arithmetic via mouse clicks.
 */

#include "apps.h"
#include "../wm.h"
#include "../font.h"
#include "../blit.h"
#include "../ui.h"
#include "../theme.h"
#include "../../input/event.h"
#include "../../mm/heap.h"
#include <stddef.h>
#include <stdint.h>

#define CALC_W     220
#define CALC_H     280
#define BTN_W      40
#define BTN_H      28
#define BTN_PAD    4
#define DISPLAY_H  30

struct calc_data {
    int32_t accumulator;
    int32_t current;
    char    op;          /* '+', '-', '*', '/', or 0 = none */
    char    display[16];
    int     display_len;
    int     new_number;  /* 1 = next digit starts a new number */
};

/* Button layout: 4 columns x 5 rows. */
static const char *calc_buttons[5][4] = {
    { "C", "+/-", "%", "/" },
    { "7", "8",   "9", "*" },
    { "4", "5",   "6", "-" },
    { "1", "2",   "3", "+" },
    { "0", "0",   ".", "=" },
};

static void int_to_str(int32_t val, char *buf, int maxlen)
{
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[16];
    int i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0 && i < 14) {
            tmp[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    int p = 0;
    if (neg && p < maxlen - 1) buf[p++] = '-';
    for (int j = i - 1; j >= 0 && p < maxlen - 1; j--)
        buf[p++] = tmp[j];
    buf[p] = '\0';
}

static void calc_update_display(struct calc_data *cd)
{
    int_to_str(cd->current, cd->display, 15);
    cd->display_len = 0;
    while (cd->display[cd->display_len])
        cd->display_len++;
}

static void calc_draw(wm_window_t *win)
{
    struct calc_data *cd = (struct calc_data *)win->app_data;
    if (!cd) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    fb_fill_rect(cx, cy, cw, ch, (color_t)THEME_WIN_BG);

    /* Display area. */
    int dx = cx + 8;
    int dy = cy + 8;
    int dw = cw - 16;
    fb_fill_rect(dx, dy, dw, DISPLAY_H, (color_t)rgba(13, 19, 28, 0xFF));
    fb_draw_hline(dx, dy, dw, (color_t)THEME_WIN_BORDER);
    fb_draw_vline(dx, dy, DISPLAY_H, (color_t)THEME_WIN_BORDER);
    fb_draw_hline(dx, dy + DISPLAY_H - 1, dw, (color_t)THEME_WIN_BORDER);
    fb_draw_vline(dx + dw - 1, dy, DISPLAY_H, (color_t)THEME_WIN_BORDER);

    /* Display text (right-aligned). */
    int text_w = cd->display_len * 8;
    fb_draw_string(dx + dw - text_w - 8, dy + (DISPLAY_H - 8) / 2,
                   cd->display, (color_t)THEME_TEXT, (color_t)rgba(13, 19, 28, 0xFF));

    /* Buttons. */
    int grid_x = cx + 8;
    int grid_y = cy + 8 + DISPLAY_H + 8;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int bx = grid_x + col * (BTN_W + BTN_PAD);
            int by = grid_y + row * (BTN_H + BTN_PAD);
            const char *label = calc_buttons[row][col];
            ui_draw_button(bx, by, BTN_W, BTN_H, label, 0, 0);
        }
    }
}

static void calc_handle_char(struct calc_data *cd, char c)
{
    if (c >= '0' && c <= '9') {
        int digit = c - '0';
        if (cd->new_number) {
            cd->current = digit;
            cd->new_number = 0;
        } else {
            cd->current = cd->current * 10 + digit;
        }
        calc_update_display(cd);
        return;
    }

    if (c == 'C' || c == 'c') {
        cd->accumulator = 0;
        cd->current = 0;
        cd->op = 0;
        cd->new_number = 1;
        calc_update_display(cd);
        return;
    }

    if (c == '\b') {
        cd->current /= 10;
        calc_update_display(cd);
        return;
    }

    if (c == '=' || c == '\n' || c == '\r') {
        switch (cd->op) {
        case '+': cd->accumulator += cd->current; break;
        case '-': cd->accumulator -= cd->current; break;
        case '*': cd->accumulator *= cd->current; break;
        case '/':
            if (cd->current != 0)
                cd->accumulator /= cd->current;
            break;
        }
        cd->current = cd->accumulator;
        cd->op = 0;
        cd->new_number = 1;
        calc_update_display(cd);
        return;
    }

    if (c == '+' || c == '-' || c == '*' || c == '/') {
        /* Apply pending operation first. */
        if (cd->op) {
            switch (cd->op) {
            case '+': cd->accumulator += cd->current; break;
            case '-': cd->accumulator -= cd->current; break;
            case '*': cd->accumulator *= cd->current; break;
            case '/':
                if (cd->current != 0)
                    cd->accumulator /= cd->current;
                break;
            }
            cd->current = cd->accumulator;
        } else {
            cd->accumulator = cd->current;
        }
        cd->op = c;
        cd->new_number = 1;
        calc_update_display(cd);
        return;
    }
}

static void calc_event(wm_window_t *win, const void *event)
{
    struct calc_data *cd = (struct calc_data *)win->app_data;
    if (!cd || !event) return;

    const struct input_event *ev = (const struct input_event *)event;

    /* Handle keyboard numbers. */
    if (ev->type == EVENT_KEY && ev->subtype == KEY_PRESS) {
        char key = (char)(ev->keycode & 0xFF);
        calc_handle_char(cd, key);
        return;
    }

    if (ev->type != EVENT_MOUSE) return;

    int emx = ev->x, emy = ev->y;
    int left_down = (ev->keycode & 1) && (ev->subtype == MOUSE_BTN_DOWN);
    if (!left_down) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    int grid_x = cx + 8;
    int grid_y = cy + 8 + DISPLAY_H + 8;

    /* Find which button was clicked. */
    int col = (emx - grid_x) / (BTN_W + BTN_PAD);
    int row = (emy - grid_y) / (BTN_H + BTN_PAD);

    if (col < 0 || col >= 4 || row < 0 || row >= 5)
        return;

    /* Check within button bounds (not in padding). */
    int bx = grid_x + col * (BTN_W + BTN_PAD);
    int by = grid_y + row * (BTN_H + BTN_PAD);
    if (emx < bx || emx >= bx + BTN_W ||
        emy < by || emy >= by + BTN_H)
        return;

    const char *label = calc_buttons[row][col];
    calc_handle_char(cd, label[0]);
}

void app_calc_open(void)
{
    struct calc_data *cd = (struct calc_data *)kmalloc(sizeof(struct calc_data));
    if (!cd) return;
    cd->accumulator = 0;
    cd->current = 0;
    cd->op = 0;
    cd->new_number = 1;
    calc_update_display(cd);

    wm_create_window(150, 80, CALC_W, CALC_H, "Calculator",
                     calc_draw, calc_event, cd);
}
