/*
 * Project Tsukasa — Vanilla Terminal ANSI/VT State Machine Implementation
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

#include "terminal_vt.h"
#include <string.h>
#include <unistd.h>

extern int clipboard_set(const char *text, size_t len);

static const uint32_t ansi_colours[16] = {
    0xFF3B4252u, /* 0: black       - Nord Polar Night 1 */
    0xFFBF616Au, /* 1: red         - Nord Aurora red */
    0xFFA3BE8Cu, /* 2: green       - Nord Aurora green */
    0xFFEBCB8Bu, /* 3: yellow      - Nord Aurora yellow */
    0xFF81A1C1u, /* 4: blue        - Nord Frost blue */
    0xFFB48EADu, /* 5: magenta     - Nord Aurora purple */
    0xFF88C0D0u, /* 6: cyan        - Nord Frost cyan */
    0xFFD8DEE9u, /* 7: white       - Nord Snow */
    0xFF4C566Au, /* 8: bright black (dark grey) */
    0xFFBF616Au, /* 9: bright red */
    0xFFA3BE8Cu, /* 10: bright green */
    0xFFEBCB8Bu, /* 11: bright yellow */
    0xFF81A1C1u, /* 12: bright blue */
    0xFFB48EADu, /* 13: bright magenta */
    0xFF8FBCBBu, /* 14: bright cyan */
    0xFFECEFF4u, /* 15: bright white */
};

void vt_init(terminal_state_t *st, int rows, int cols)
{
    if (!st)
        return;

    memset(st, 0, sizeof(*st));

    if (rows < 10) rows = TERM_DEFAULT_ROWS;
    if (rows > TERM_MAX_ROWS) rows = TERM_MAX_ROWS;
    if (cols < 20) cols = TERM_DEFAULT_COLS;
    if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;

    st->rows = rows;
    st->cols = cols;
    st->cur_fg = COLOR_DEFAULT_FG;
    st->cur_bg = COLOR_DEFAULT_BG;
    st->cursor_visible = 1;
    st->dirty = 1;

    for (int r = 0; r < rows; r++) {
        memset(st->grid[r], ' ', (size_t)cols);
        for (int c = 0; c < cols; c++) {
            st->colors[r][c] = COLOR_DEFAULT_FG;
            st->bg_colors[r][c] = COLOR_DEFAULT_BG;
        }
    }
}

void vt_resize(terminal_state_t *st, int new_rows, int new_cols)
{
    if (!st)
        return;

    if (new_rows < 10) new_rows = 10;
    if (new_rows > TERM_MAX_ROWS) new_rows = TERM_MAX_ROWS;
    if (new_cols < 20) new_cols = 20;
    if (new_cols > TERM_MAX_COLS) new_cols = TERM_MAX_COLS;

    if (new_rows == st->rows && new_cols == st->cols)
        return;

    /* Initialize newly exposed cells if growing */
    for (int r = 0; r < new_rows; r++) {
        for (int c = 0; c < new_cols; c++) {
            if (r >= st->rows || c >= st->cols) {
                st->grid[r][c] = ' ';
                st->colors[r][c] = st->cur_fg;
                st->bg_colors[r][c] = st->cur_bg;
            }
        }
    }

    st->rows = new_rows;
    st->cols = new_cols;

    if (st->cursor_row >= st->rows)
        st->cursor_row = st->rows - 1;
    if (st->cursor_col >= st->cols)
        st->cursor_col = st->cols - 1;

    st->dirty = 1;
}

void vt_scroll_up(terminal_state_t *st)
{
    if (!st || st->rows <= 0)
        return;

    /* Push topmost line to scrollback ring buffer */
    int head = st->scrollback_head;
    memcpy(st->scrollback[head], st->grid[0], (size_t)st->cols);
    memcpy(st->scrollback_fg[head], st->colors[0], (size_t)st->cols * sizeof(uint32_t));
    memcpy(st->scrollback_bg[head], st->bg_colors[0], (size_t)st->cols * sizeof(uint32_t));

    st->scrollback_head = (head + 1) % TERM_SCROLLBACK;
    if (st->scrollback_count < TERM_SCROLLBACK)
        st->scrollback_count++;

    /* Shift grid rows upward */
    for (int r = 0; r < st->rows - 1; r++) {
        memcpy(st->grid[r], st->grid[r + 1], (size_t)st->cols);
        memcpy(st->colors[r], st->colors[r + 1], (size_t)st->cols * sizeof(uint32_t));
        memcpy(st->bg_colors[r], st->bg_colors[r + 1], (size_t)st->cols * sizeof(uint32_t));
    }

    /* Blank bottom row */
    memset(st->grid[st->rows - 1], ' ', (size_t)st->cols);
    for (int c = 0; c < st->cols; c++) {
        st->colors[st->rows - 1][c] = st->cur_fg;
        st->bg_colors[st->rows - 1][c] = st->cur_bg;
    }

    st->dirty = 1;
}

void vt_scroll_down(terminal_state_t *st)
{
    if (!st || st->rows <= 0)
        return;

    for (int r = st->rows - 1; r > 0; r--) {
        memcpy(st->grid[r], st->grid[r - 1], (size_t)st->cols);
        memcpy(st->colors[r], st->colors[r - 1], (size_t)st->cols * sizeof(uint32_t));
        memcpy(st->bg_colors[r], st->bg_colors[r - 1], (size_t)st->cols * sizeof(uint32_t));
    }

    memset(st->grid[0], ' ', (size_t)st->cols);
    for (int c = 0; c < st->cols; c++) {
        st->colors[0][c] = st->cur_fg;
        st->bg_colors[0][c] = st->cur_bg;
    }

    st->dirty = 1;
}

void vt_scroll_history(terminal_state_t *st, int delta)
{
    if (!st)
        return;

    int new_off = st->scroll_offset + delta;
    if (new_off < 0)
        new_off = 0;
    if (new_off > st->scrollback_count)
        new_off = st->scrollback_count;

    if (new_off != st->scroll_offset) {
        st->scroll_offset = new_off;
        st->dirty = 1;
    }
}

void vt_copy_last_output(terminal_state_t *st)
{
    if (!st)
        return;

    int target_row = -1;
    if (st->cursor_row > 0) {
        int last = st->cols - 1;
        while (last >= 0 && st->grid[st->cursor_row - 1][last] == ' ')
            last--;
        if (last >= 0)
            target_row = st->cursor_row - 1;
    }
    if (target_row < 0) {
        int last = st->cols - 1;
        while (last >= 0 && st->grid[st->cursor_row][last] == ' ')
            last--;
        if (last >= 0)
            target_row = st->cursor_row;
    }
    if (target_row >= 0) {
        int last = st->cols - 1;
        while (last >= 0 && st->grid[target_row][last] == ' ')
            last--;
        if (last >= 0) {
            size_t len = (size_t)(last + 1);
            char line_buf[TERM_MAX_COLS + 1];
            memcpy(line_buf, st->grid[target_row], len);
            line_buf[len] = '\0';
            clipboard_set(line_buf, len);
        }
    }
}

static void apply_sgr_code(terminal_state_t *st, int code)
{
    if (code == 0) {
        st->cur_fg = COLOR_DEFAULT_FG;
        st->cur_bg = COLOR_DEFAULT_BG;
        st->cur_bold = 0;
    } else if (code == 1) {
        st->cur_bold = 1;
    } else if (code == 22) {
        st->cur_bold = 0;
    } else if (code >= 30 && code <= 37) {
        st->cur_fg = ansi_colours[code - 30];
    } else if (code == 39) {
        st->cur_fg = COLOR_DEFAULT_FG;
    } else if (code >= 40 && code <= 47) {
        st->cur_bg = ansi_colours[code - 40];
    } else if (code == 49) {
        st->cur_bg = COLOR_DEFAULT_BG;
    } else if (code >= 90 && code <= 97) {
        st->cur_fg = ansi_colours[8 + (code - 90)];
    } else if (code >= 100 && code <= 107) {
        st->cur_bg = ansi_colours[8 + (code - 100)];
    }
}

static void dispatch_sgr(terminal_state_t *st)
{
    if (st->vt_param_count == 0) {
        apply_sgr_code(st, 0);
        return;
    }

    for (int i = 0; i < st->vt_param_count; i++) {
        int code = st->vt_params[i];
        if (code == 38 || code == 48) {
            /* 24-bit RGB: 38/48 ; 2 ; r ; g ; b */
            if (i + 4 < st->vt_param_count && st->vt_params[i + 1] == 2) {
                int r = st->vt_params[i + 2] & 0xFF;
                int g = st->vt_params[i + 3] & 0xFF;
                int b = st->vt_params[i + 4] & 0xFF;
                uint32_t rgb = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                if (code == 38)
                    st->cur_fg = rgb;
                else
                    st->cur_bg = rgb;
                i += 4;
                continue;
            }
            /* 256 colour: 38/48 ; 5 ; idx */
            if (i + 2 < st->vt_param_count && st->vt_params[i + 1] == 5) {
                int idx = st->vt_params[i + 2];
                if (idx >= 0 && idx < 16) {
                    if (code == 38)
                        st->cur_fg = ansi_colours[idx];
                    else
                        st->cur_bg = ansi_colours[idx];
                }
                i += 2;
                continue;
            }
        }
        apply_sgr_code(st, code);
    }
}

static void dispatch_csi(terminal_state_t *st, char cmd)
{
    switch (cmd) {
    case 'A': { /* CUU: Cursor Up */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        st->cursor_row -= n;
        if (st->cursor_row < 0)
            st->cursor_row = 0;
        break;
    }
    case 'B': { /* CUD: Cursor Down */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        st->cursor_row += n;
        if (st->cursor_row >= st->rows)
            st->cursor_row = st->rows - 1;
        break;
    }
    case 'C': { /* CUF: Cursor Forward */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        st->cursor_col += n;
        if (st->cursor_col >= st->cols)
            st->cursor_col = st->cols - 1;
        break;
    }
    case 'D': { /* CUB: Cursor Backward */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        st->cursor_col -= n;
        if (st->cursor_col < 0)
            st->cursor_col = 0;
        break;
    }
    case 'H':   /* CUP: Cursor Position */
    case 'f': { /* HVP: Horizontal and Vertical Position */
        int r = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] - 1 : 0;
        int c = (st->vt_param_count >= 2 && st->vt_params[1] > 0) ? st->vt_params[1] - 1 : 0;
        if (r < 0) r = 0;
        if (r >= st->rows) r = st->rows - 1;
        if (c < 0) c = 0;
        if (c >= st->cols) c = st->cols - 1;
        st->cursor_row = r;
        st->cursor_col = c;
        break;
    }
    case 'J': { /* ED: Erase in Display */
        int mode = (st->vt_param_count >= 1) ? st->vt_params[0] : 0;
        if (mode == 0) {
            /* Erase from cursor to end of screen */
            for (int c = st->cursor_col; c < st->cols; c++) {
                st->grid[st->cursor_row][c] = ' ';
                st->colors[st->cursor_row][c] = st->cur_fg;
                st->bg_colors[st->cursor_row][c] = st->cur_bg;
            }
            for (int r = st->cursor_row + 1; r < st->rows; r++) {
                memset(st->grid[r], ' ', (size_t)st->cols);
                for (int c = 0; c < st->cols; c++) {
                    st->colors[r][c] = st->cur_fg;
                    st->bg_colors[r][c] = st->cur_bg;
                }
            }
        } else if (mode == 1) {
            /* Erase from start of screen to cursor */
            for (int r = 0; r < st->cursor_row; r++) {
                memset(st->grid[r], ' ', (size_t)st->cols);
                for (int c = 0; c < st->cols; c++) {
                    st->colors[r][c] = st->cur_fg;
                    st->bg_colors[r][c] = st->cur_bg;
                }
            }
            for (int c = 0; c <= st->cursor_col && c < st->cols; c++) {
                st->grid[st->cursor_row][c] = ' ';
                st->colors[st->cursor_row][c] = st->cur_fg;
                st->bg_colors[st->cursor_row][c] = st->cur_bg;
            }
        } else if (mode == 2) {
            /* Erase all */
            for (int r = 0; r < st->rows; r++) {
                memset(st->grid[r], ' ', (size_t)st->cols);
                for (int c = 0; c < st->cols; c++) {
                    st->colors[r][c] = st->cur_fg;
                    st->bg_colors[r][c] = st->cur_bg;
                }
            }
        }
        break;
    }
    case 'K': { /* EL: Erase in Line */
        int mode = (st->vt_param_count >= 1) ? st->vt_params[0] : 0;
        if (mode == 0) {
            for (int c = st->cursor_col; c < st->cols; c++) {
                st->grid[st->cursor_row][c] = ' ';
                st->colors[st->cursor_row][c] = st->cur_fg;
                st->bg_colors[st->cursor_row][c] = st->cur_bg;
            }
        } else if (mode == 1) {
            for (int c = 0; c <= st->cursor_col && c < st->cols; c++) {
                st->grid[st->cursor_row][c] = ' ';
                st->colors[st->cursor_row][c] = st->cur_fg;
                st->bg_colors[st->cursor_row][c] = st->cur_bg;
            }
        } else if (mode == 2) {
            memset(st->grid[st->cursor_row], ' ', (size_t)st->cols);
            for (int c = 0; c < st->cols; c++) {
                st->colors[st->cursor_row][c] = st->cur_fg;
                st->bg_colors[st->cursor_row][c] = st->cur_bg;
            }
        }
        break;
    }
    case 'm': { /* SGR */
        dispatch_sgr(st);
        break;
    }
    case 'S': { /* SU: Scroll Up */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        for (int i = 0; i < n; i++)
            vt_scroll_up(st);
        break;
    }
    case 'T': { /* SD: Scroll Down */
        int n = (st->vt_param_count >= 1 && st->vt_params[0] > 0) ? st->vt_params[0] : 1;
        for (int i = 0; i < n; i++)
            vt_scroll_down(st);
        break;
    }
    case 'h': {
        if (st->vt_private && st->vt_param_count >= 1 && st->vt_params[0] == 25)
            st->cursor_visible = 1;
        break;
    }
    case 'l': {
        if (st->vt_private && st->vt_param_count >= 1 && st->vt_params[0] == 25)
            st->cursor_visible = 0;
        break;
    }
    default:
        break;
    }

    st->dirty = 1;
}

void vt_process_byte(terminal_state_t *st, char c)
{
    if (!st)
        return;

    switch (st->vt_state) {
    case VT_NORMAL:
        if (c == 0x1B) {
            st->vt_state = VT_ESC_START;
            return;
        }

        if (c == '\r') {
            st->cursor_col = 0;
            st->dirty = 1;
            return;
        }

        if (c == '\n') {
            st->cursor_col = 0;
            st->cursor_row++;
            if (st->cursor_row >= st->rows) {
                vt_scroll_up(st);
                st->cursor_row = st->rows - 1;
            }
            st->dirty = 1;
            return;
        }

        if (c == '\b') {
            if (st->cursor_col > 0) {
                st->cursor_col--;
                st->dirty = 1;
            }
            return;
        }

        if (c == '\t') {
            int next_tab = (st->cursor_col + 8) & ~7;
            if (next_tab > st->cols)
                next_tab = st->cols;
            while (st->cursor_col < next_tab) {
                st->grid[st->cursor_row][st->cursor_col] = ' ';
                st->colors[st->cursor_row][st->cursor_col] = st->cur_fg;
                st->bg_colors[st->cursor_row][st->cursor_col] = st->cur_bg;
                st->cursor_col++;
            }
            st->dirty = 1;
            return;
        }

        if (c == '\a') {
            /* Visual / audible bell ignored */
            return;
        }

        if ((unsigned char)c < 32)
            return;

        if (st->cursor_col >= st->cols) {
            st->cursor_col = 0;
            st->cursor_row++;
            if (st->cursor_row >= st->rows) {
                vt_scroll_up(st);
                st->cursor_row = st->rows - 1;
            }
        }

        st->grid[st->cursor_row][st->cursor_col] = c;
        st->colors[st->cursor_row][st->cursor_col] = st->cur_fg;
        st->bg_colors[st->cursor_row][st->cursor_col] = st->cur_bg;
        st->cursor_col++;
        st->dirty = 1;
        break;

    case VT_ESC_START:
        if (c == '[') {
            st->vt_state = VT_CSI_PARAMS;
            st->vt_param_count = 0;
            st->vt_private = 0;
            memset(st->vt_params, 0, sizeof(st->vt_params));
        } else if (c == 'c') {
            /* Device attributes query or reset */
            st->vt_state = VT_NORMAL;
            if (st->in_pipe[1] > 0) {
                const char *da = "\033[?1;2c";
                write(st->in_pipe[1], da, strlen(da));
            }
        } else if (c == 0x1B) {
            /* Remain in ESC_START */
        } else {
            /* Unhandled 2-character escape sequence */
            st->vt_state = VT_NORMAL;
        }
        break;

    case VT_CSI_PARAMS:
        if (c == '?') {
            st->vt_private = 1;
        } else if (c >= '0' && c <= '9') {
            if (st->vt_param_count == 0)
                st->vt_param_count = 1;
            st->vt_params[st->vt_param_count - 1] =
                st->vt_params[st->vt_param_count - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (st->vt_param_count < TERM_MAX_PARAMS) {
                st->vt_param_count++;
                st->vt_params[st->vt_param_count - 1] = 0;
            }
        } else if (c >= 0x40 && c <= 0x7E) {
            /* Command byte */
            dispatch_csi(st, c);
            st->vt_state = VT_NORMAL;
        } else if (c == 0x1B) {
            st->vt_state = VT_ESC_START;
        }
        break;
    }
}

void vt_process_bytes(terminal_state_t *st, const char *buf, size_t len)
{
    if (!st || !buf)
        return;

    for (size_t i = 0; i < len; i++)
        vt_process_byte(st, buf[i]);
}
