/*
 * Project Tsukasa — Terminal ANSI/VT State Machine Unit Tests
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

#include "../apps/terminal_vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

/* Stub for clipboard_set on host tests */
int clipboard_set(const char *text, size_t len)
{
    (void)text;
    (void)len;
    return 0;
}

static void test_vt_basic_text(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    const char *msg = "Hello Tsukasa\r\nLine 2";
    vt_process_bytes(&st, msg, strlen(msg));

    TEST_ASSERT(memcmp(st.grid[0], "Hello Tsukasa", 13) == 0);
    TEST_ASSERT(memcmp(st.grid[1], "Line 2", 6) == 0);
    TEST_ASSERT(st.cursor_row == 1);
    TEST_ASSERT(st.cursor_col == 6);

    printf("[TEST] terminal_vt.basic_text PASS\n");
}

static void test_vt_sgr_colours(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* Test 16-color ANSI foreground */
    const char *seq_red = "\033[31mRED\033[0m NORMAL";
    vt_process_bytes(&st, seq_red, strlen(seq_red));

    TEST_ASSERT(st.colors[0][0] == 0xFFBF616Au);
    TEST_ASSERT(st.colors[0][1] == 0xFFBF616Au);
    TEST_ASSERT(st.colors[0][2] == 0xFFBF616Au);
    TEST_ASSERT(st.colors[0][4] == COLOR_DEFAULT_FG); /* 'N' in NORMAL */

    /* Test bold */
    const char *seq_bold = "\r\n\033[1;32mBOLD GREEN\033[0m";
    vt_process_bytes(&st, seq_bold, strlen(seq_bold));
    TEST_ASSERT(st.colors[1][0] == 0xFFA3BE8Cu);

    /* Test 24-bit RGB */
    const char *seq_rgb = "\r\n\033[38;2;12;34;56mRGB\033[0m";
    vt_process_bytes(&st, seq_rgb, strlen(seq_rgb));
    uint32_t expected_rgb = 0xFF000000u | (12u << 16) | (34u << 8) | 56u;
    TEST_ASSERT(st.colors[2][0] == expected_rgb);

    /* Test background color */
    const char *seq_bg = "\r\n\033[44mBLUE BG\033[0m";
    vt_process_bytes(&st, seq_bg, strlen(seq_bg));
    TEST_ASSERT(st.bg_colors[3][0] == 0xFF81A1C1u);
    TEST_ASSERT(st.bg_colors[3][7] == COLOR_DEFAULT_BG);

    printf("[TEST] terminal_vt.sgr_colours PASS\n");
}

static void test_vt_cursor_addressing(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* CUP to row 10, col 20 (1-indexed: 10;20 -> 9, 19) */
    const char *cup = "\033[10;20H";
    vt_process_bytes(&st, cup, strlen(cup));
    TEST_ASSERT(st.cursor_row == 9);
    TEST_ASSERT(st.cursor_col == 19);

    /* CUU (Cursor Up 2) */
    const char *cuu = "\033[2A";
    vt_process_bytes(&st, cuu, strlen(cuu));
    TEST_ASSERT(st.cursor_row == 7);

    /* CUD (Cursor Down 4) */
    const char *cud = "\033[4B";
    vt_process_bytes(&st, cud, strlen(cud));
    TEST_ASSERT(st.cursor_row == 11);

    /* CUF (Cursor Forward 5) */
    const char *cuf = "\033[5C";
    vt_process_bytes(&st, cuf, strlen(cuf));
    TEST_ASSERT(st.cursor_col == 24);

    /* CUB (Cursor Backward 6) */
    const char *cub = "\033[6D";
    vt_process_bytes(&st, cub, strlen(cub));
    TEST_ASSERT(st.cursor_col == 18);

    /* Write at addressed cell */
    vt_process_byte(&st, 'X');
    TEST_ASSERT(st.grid[11][18] == 'X');

    printf("[TEST] terminal_vt.cursor_addressing PASS\n");
}

static void test_vt_erase(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* Fill row 0 with ABCDEFGH */
    vt_process_bytes(&st, "ABCDEFGH", 8);

    /* Move cursor to col 4 and erase to end of line (CSI 0 K) */
    const char *cup = "\033[1;5H\033[K";
    vt_process_bytes(&st, cup, strlen(cup));

    TEST_ASSERT(st.grid[0][0] == 'A');
    TEST_ASSERT(st.grid[0][1] == 'B');
    TEST_ASSERT(st.grid[0][2] == 'C');
    TEST_ASSERT(st.grid[0][3] == 'D');
    TEST_ASSERT(st.grid[0][4] == ' ');
    TEST_ASSERT(st.grid[0][7] == ' ');

    /* Erase entire display (CSI 2 J) */
    vt_process_bytes(&st, "\033[2J", 4);
    for (int r = 0; r < 25; r++) {
        for (int c = 0; c < 80; c++) {
            TEST_ASSERT(st.grid[r][c] == ' ');
        }
    }

    printf("[TEST] terminal_vt.erase PASS\n");
}

static void test_vt_scrollback(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* Push 600 lines */
    for (int i = 0; i < 600; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Line %d\n", i);
        vt_process_bytes(&st, buf, strlen(buf));
    }

    /* Buffer capacity is capped at TERM_SCROLLBACK (500) */
    TEST_ASSERT(st.scrollback_count == TERM_SCROLLBACK);

    /* Scroll history */
    vt_scroll_history(&st, 10);
    TEST_ASSERT(st.scroll_offset == 10);

    vt_scroll_history(&st, 600);
    TEST_ASSERT(st.scroll_offset == TERM_SCROLLBACK);

    vt_scroll_history(&st, -700);
    TEST_ASSERT(st.scroll_offset == 0);

    printf("[TEST] terminal_vt.scrollback PASS\n");
}

static void test_vt_cursor_visibility(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    TEST_ASSERT(st.cursor_visible == 1);

    vt_process_bytes(&st, "\033[?25l", 6);
    TEST_ASSERT(st.cursor_visible == 0);

    vt_process_bytes(&st, "\033[?25h", 6);
    TEST_ASSERT(st.cursor_visible == 1);

    printf("[TEST] terminal_vt.cursor_visibility PASS\n");
}

static void test_vt_resize(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    vt_resize(&st, 40, 100);
    TEST_ASSERT(st.rows == 40);
    TEST_ASSERT(st.cols == 100);

    /* Move cursor near boundary */
    st.cursor_row = 38;
    st.cursor_col = 95;

    /* Shrink */
    vt_resize(&st, 20, 60);
    TEST_ASSERT(st.rows == 20);
    TEST_ASSERT(st.cols == 60);
    TEST_ASSERT(st.cursor_row == 19);
    TEST_ASSERT(st.cursor_col == 59);

    printf("[TEST] terminal_vt.resize PASS\n");
}

static void test_vt_omitted_params(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* CUP with omitted row: \033[;5H -> row 1, col 5 (0-indexed: 0, 4) */
    vt_process_bytes(&st, "\033[;5H", 5);
    TEST_ASSERT(st.cursor_row == 0);
    TEST_ASSERT(st.cursor_col == 4);

    /* CUP with omitted col: \033[7;H -> row 7, col 1 (0-indexed: 6, 0) */
    vt_process_bytes(&st, "\033[7;H", 5);
    TEST_ASSERT(st.cursor_row == 6);
    TEST_ASSERT(st.cursor_col == 0);

    /* CUP with both omitted: \033[;H -> row 1, col 1 (0-indexed: 0, 0) */
    vt_process_bytes(&st, "\033[;H", 4);
    TEST_ASSERT(st.cursor_row == 0);
    TEST_ASSERT(st.cursor_col == 0);

    /* CUP with no params: \033[H -> row 1, col 1 (0-indexed: 0, 0) */
    st.cursor_row = 10;
    st.cursor_col = 15;
    vt_process_bytes(&st, "\033[H", 3);
    TEST_ASSERT(st.cursor_row == 0);
    TEST_ASSERT(st.cursor_col == 0);

    printf("[TEST] terminal_vt.omitted_params PASS\n");
}

static void test_vt_osc(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* OSC sequence to set title, BEL terminated: should be swallowed */
    const char *osc_bel = "\033]0;tsh prompt title\007OK";
    vt_process_bytes(&st, osc_bel, strlen(osc_bel));
    TEST_ASSERT(st.grid[0][0] == 'O');
    TEST_ASSERT(st.grid[0][1] == 'K');
    TEST_ASSERT(st.cursor_col == 2);

    /* OSC sequence, ST (ESC \) terminated: should be swallowed */
    const char *osc_st = "\r\n\033]2;window title\033\\DONE";
    vt_process_bytes(&st, osc_st, strlen(osc_st));
    TEST_ASSERT(st.grid[1][0] == 'D');
    TEST_ASSERT(st.grid[1][1] == 'O');
    TEST_ASSERT(st.grid[1][2] == 'N');
    TEST_ASSERT(st.grid[1][3] == 'E');

    printf("[TEST] terminal_vt.osc PASS\n");
}

static void test_vt_can_sub(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    /* Partial escape aborted by CAN (0x18) */
    const char *seq = "\033[12;\030ABC";
    vt_process_bytes(&st, seq, strlen(seq));
    TEST_ASSERT(st.vt_state == VT_NORMAL);
    TEST_ASSERT(st.grid[0][0] == 'A');
    TEST_ASSERT(st.grid[0][1] == 'B');
    TEST_ASSERT(st.grid[0][2] == 'C');

    printf("[TEST] terminal_vt.can_sub PASS\n");
}

static void test_vt_erase_scrollback(void)
{
    terminal_state_t st;
    vt_init(&st, 25, 80);

    for (int i = 0; i < 30; i++)
        vt_process_bytes(&st, "Row\n", 4);

    TEST_ASSERT(st.scrollback_count > 0);

    /* ED 3: Erase scrollback */
    vt_process_bytes(&st, "\033[3J", 4);
    TEST_ASSERT(st.scrollback_count == 0);
    TEST_ASSERT(st.scroll_offset == 0);

    printf("[TEST] terminal_vt.erase_scrollback PASS\n");
}

int main(void)
{
    printf("=== terminal_vt unit tests ===\n");
    test_vt_basic_text();
    test_vt_sgr_colours();
    test_vt_cursor_addressing();
    test_vt_erase();
    test_vt_scrollback();
    test_vt_cursor_visibility();
    test_vt_resize();
    test_vt_omitted_params();
    test_vt_osc();
    test_vt_can_sub();
    test_vt_erase_scrollback();
    printf("[TEST] terminal_vt DONE 11/11\n");
    return 0;
}
