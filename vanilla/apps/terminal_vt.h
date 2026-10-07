/*
 * Project Tsukasa — Vanilla Terminal ANSI/VT State Machine Header
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

#ifndef _VANILLA_TERMINAL_VT_H
#define _VANILLA_TERMINAL_VT_H

#include <stdint.h>
#include <stddef.h>

#define TERM_MAX_PARAMS    16
#define TERM_SCROLLBACK    500
#define TERM_DEFAULT_ROWS  25
#define TERM_DEFAULT_COLS  80
#define TERM_MAX_ROWS      128
#define TERM_MAX_COLS      256
#define CELL_WIDTH         8
#define CELL_HEIGHT        16

#define COLOR_DEFAULT_FG   0xFFD8DEE9u
#define COLOR_DEFAULT_BG   0x00000000u

typedef enum {
    VT_NORMAL = 0,
    VT_ESC_START,
    VT_CSI_PARAMS,
    VT_OSC,
} vt_state_t;

typedef struct terminal_state_t {
    int      rows;
    int      cols;

    char     grid[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t colors[TERM_MAX_ROWS][TERM_MAX_COLS];
    uint32_t bg_colors[TERM_MAX_ROWS][TERM_MAX_COLS];

    /* Scrollback: ring buffer of complete rows */
    char     scrollback[TERM_SCROLLBACK][TERM_MAX_COLS];
    uint32_t scrollback_fg[TERM_SCROLLBACK][TERM_MAX_COLS];
    uint32_t scrollback_bg[TERM_SCROLLBACK][TERM_MAX_COLS];
    int      scrollback_head;
    int      scrollback_count;
    int      scroll_offset;

    int      cursor_row;
    int      cursor_col;
    uint32_t cur_fg;
    uint32_t cur_bg;
    int      cur_bold;
    int      cursor_visible;

    /* Parser state */
    vt_state_t vt_state;
    int        vt_params[TERM_MAX_PARAMS];
    int        vt_param_count;
    int        vt_private;

    int        in_pipe[2];
    int        out_pipe[2];
    int        shell_pid;
    int        shift_down;
    int        ctrl_down;
    int        dirty;
} terminal_state_t;

void vt_init(terminal_state_t *st, int rows, int cols);
void vt_resize(terminal_state_t *st, int new_rows, int new_cols);
void vt_process_byte(terminal_state_t *st, char c);
void vt_process_bytes(terminal_state_t *st, const char *buf, size_t len);
void vt_scroll_up(terminal_state_t *st);
void vt_scroll_down(terminal_state_t *st);
void vt_scroll_history(terminal_state_t *st, int delta);
void vt_copy_last_output(terminal_state_t *st);

#endif /* _VANILLA_TERMINAL_VT_H */
