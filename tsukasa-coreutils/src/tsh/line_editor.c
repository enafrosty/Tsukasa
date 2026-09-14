/*
 * Project Tsukasa — tsh Interactive Line Editor Implementation
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

#include "line_editor.h"
#include "history.h"
#include <unistd.h>
#include <string.h>

void line_editor_init(void)
{
}

static void redraw_line(const char *prompt, const char *buf, size_t len, size_t cursor)
{
    write(STDOUT_FILENO, "\r", 1);
    if (prompt)
        write(STDOUT_FILENO, prompt, strlen(prompt));
    if (len > 0)
        write(STDOUT_FILENO, buf, len);
    write(STDOUT_FILENO, "\x1b[K", 3);
    for (size_t i = cursor; i < len; i++)
        write(STDOUT_FILENO, "\b", 1);
}

int line_editor_readline(const char *prompt, char *buf, size_t max_len)
{
    if (!buf || max_len == 0)
        return -1;

    size_t len = 0;
    size_t cursor = 0;
    int hist_index = history_count();
    char saved_draft[TSH_HISTORY_LINE_MAX];
    int has_draft = 0;

    saved_draft[0] = '\0';
    buf[0] = '\0';

    if (prompt)
        write(STDOUT_FILENO, prompt, strlen(prompt));

    for (;;) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            if (len == 0)
                return -1;
            buf[len] = '\0';
            return (int)len;
        }

        if (c == '\r' || c == '\n') {
            buf[len] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            return (int)len;
        }

        if (c == 3) {
            /* Ctrl+C: Cancel current line */
            write(STDOUT_FILENO, "^C\n", 3);
            buf[0] = '\0';
            return 0;
        }

        if (c == 4) {
            /* Ctrl+D: EOF on empty line */
            if (len == 0)
                return -1;
            if (cursor < len) {
                memmove(&buf[cursor], &buf[cursor + 1], len - cursor);
                len--;
                redraw_line(prompt, buf, len, cursor);
            }
            continue;
        }

        if (c == 12) {
            /* Ctrl+L: Clear screen and reprint */
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            redraw_line(prompt, buf, len, cursor);
            continue;
        }

        if (c == '\b' || c == 127) {
            /* Backspace handling */
            if (cursor > 0) {
                if (cursor == len) {
                    cursor--;
                    len--;
                    buf[len] = '\0';
                    write(STDOUT_FILENO, "\b \b", 3);
                } else {
                    memmove(&buf[cursor - 1], &buf[cursor], len - cursor + 1);
                    cursor--;
                    len--;
                    redraw_line(prompt, buf, len, cursor);
                }
            }
            continue;
        }

        if (c == 27) {
            /* ANSI escape sequence parsing */
            char seq[4];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0)
                continue;

            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0)
                    continue;

                if (seq[1] == 'A') {
                    /* Up arrow: Navigate to older history */
                    if (hist_index > 0) {
                        if (hist_index == history_count()) {
                            size_t dlen = len < sizeof(saved_draft) - 1 ? len : sizeof(saved_draft) - 1;
                            memcpy(saved_draft, buf, dlen);
                            saved_draft[dlen] = '\0';
                            has_draft = 1;
                        }
                        hist_index--;
                        const char *h = history_get(hist_index);
                        if (h) {
                            len = strlen(h);
                            if (len >= max_len)
                                len = max_len - 1;
                            memcpy(buf, h, len);
                            buf[len] = '\0';
                            cursor = len;
                            redraw_line(prompt, buf, len, cursor);
                        }
                    }
                } else if (seq[1] == 'B') {
                    /* Down arrow: Navigate to newer history */
                    if (hist_index < history_count()) {
                        hist_index++;
                        if (hist_index == history_count()) {
                            if (has_draft) {
                                len = strlen(saved_draft);
                                if (len >= max_len)
                                    len = max_len - 1;
                                memcpy(buf, saved_draft, len);
                                buf[len] = '\0';
                            } else {
                                len = 0;
                                buf[0] = '\0';
                            }
                        } else {
                            const char *h = history_get(hist_index);
                            if (h) {
                                len = strlen(h);
                                if (len >= max_len)
                                    len = max_len - 1;
                                memcpy(buf, h, len);
                                buf[len] = '\0';
                            }
                        }
                        cursor = len;
                        redraw_line(prompt, buf, len, cursor);
                    }
                } else if (seq[1] == 'C') {
                    /* Right arrow: Cursor forward */
                    if (cursor < len) {
                        cursor++;
                        write(STDOUT_FILENO, "\x1b[C", 3);
                    }
                } else if (seq[1] == 'D') {
                    /* Left arrow: Cursor backward */
                    if (cursor > 0) {
                        cursor--;
                        write(STDOUT_FILENO, "\x1b[D", 3);
                    }
                } else if (seq[1] == 'H') {
                    /* Home key */
                    cursor = 0;
                    redraw_line(prompt, buf, len, cursor);
                } else if (seq[1] == 'F') {
                    /* End key */
                    cursor = len;
                    redraw_line(prompt, buf, len, cursor);
                } else if (seq[1] >= '1' && seq[1] <= '4') {
                    char term;
                    if (read(STDIN_FILENO, &term, 1) > 0 && term == '~') {
                        if (seq[1] == '1') {
                            cursor = 0;
                            redraw_line(prompt, buf, len, cursor);
                        } else if (seq[1] == '4') {
                            cursor = len;
                            redraw_line(prompt, buf, len, cursor);
                        } else if (seq[1] == '3') {
                            /* Delete key */
                            if (cursor < len) {
                                memmove(&buf[cursor], &buf[cursor + 1], len - cursor);
                                len--;
                                redraw_line(prompt, buf, len, cursor);
                            }
                        }
                    }
                }
            }
            continue;
        }

        /* Standard printable character */
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            if (len + 1 < max_len) {
                if (cursor == len) {
                    buf[len++] = c;
                    buf[len] = '\0';
                    cursor++;
                    write(STDOUT_FILENO, &c, 1);
                } else {
                    memmove(&buf[cursor + 1], &buf[cursor], len - cursor + 1);
                    buf[cursor] = c;
                    len++;
                    cursor++;
                    redraw_line(prompt, buf, len, cursor);
                }
            }
        }
    }
}
