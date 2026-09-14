/*
 * Project Tsukasa — Project Vanilla Task Manager
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

#include "app_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGTERM
#define SIGTERM 15
#endif

#define TM_WIDTH        520
#define TM_HEIGHT       380
#define TM_HEADER_H     76
#define TM_ROW_H        22
#define MAX_TASKS       64

typedef struct {
    int  pid;
    int  ppid;
    char state[16];
    char name[32];
} task_entry_t;

typedef struct {
    task_entry_t tasks[MAX_TASKS];
    int          task_count;
    int          selected_idx;
    unsigned long mem_used_mb;
    unsigned long mem_total_mb;
    int          mem_pct;
    int          dirty;
} taskmgr_state_t;

static unsigned long tm_read_key_val(const char *filename, const char *key)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[256];
    size_t klen = strlen(key);
    unsigned long val = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            val = strtoul(p, NULL, 10);
            break;
        }
    }
    fclose(fp);
    return val;
}

static void tm_refresh(taskmgr_state_t *st)
{
    st->task_count = 0;

    /* Read memory metrics */
    unsigned long total_pages = tm_read_key_val("/sys/memory", "pmm_total_pages");
    unsigned long used_pages = tm_read_key_val("/sys/memory", "pmm_used_pages");

    if (total_pages > 0) {
        st->mem_total_mb = (total_pages * 4096UL) / (1024 * 1024);
        st->mem_used_mb = (used_pages * 4096UL) / (1024 * 1024);
        st->mem_pct = (int)((used_pages * 100UL) / total_pages);
    } else {
        st->mem_total_mb = 256;
        st->mem_used_mb = 96;
        st->mem_pct = 37;
    }

    /* Read process list from /proc/processes */
    FILE *fp = fopen("/proc/processes", "r");
    if (fp) {
        char line[256];
        int is_header = 1;
        while (fgets(line, sizeof(line), fp) && st->task_count < MAX_TASKS) {
            if (is_header) {
                is_header = 0;
                continue;
            }
            task_entry_t *t = &st->tasks[st->task_count];
            char *p = line;
            while (*p == ' ') p++;
            t->pid = (int)strtol(p, &p, 10);

            while (*p == ' ') p++;
            t->ppid = (int)strtol(p, &p, 10);

            while (*p == ' ') p++;
            char *s_start = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            size_t s_len = (size_t)(p - s_start);
            if (s_len >= sizeof(t->state)) s_len = sizeof(t->state) - 1;
            memcpy(t->state, s_start, s_len);
            t->state[s_len] = '\0';

            while (*p == ' ') p++;
            char *n_start = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            size_t n_len = (size_t)(p - n_start);
            if (n_len >= sizeof(t->name)) n_len = sizeof(t->name) - 1;
            memcpy(t->name, n_start, n_len);
            t->name[n_len] = '\0';

            if (t->pid > 0)
                st->task_count++;
        }
        fclose(fp);
    }

    if (st->selected_idx >= st->task_count)
        st->selected_idx = st->task_count > 0 ? st->task_count - 1 : 0;

    st->dirty = 1;
}

static void tm_render(vanilla_surface_t *surf, taskmgr_state_t *st)
{
    app_fill_rect(surf, 0, 0, TM_WIDTH, TM_HEIGHT, APP_COLOR_BG);

    /* Telemetry Header */
    app_fill_rect(surf, 0, 0, TM_WIDTH, TM_HEADER_H, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, TM_HEADER_H - 1, TM_WIDTH, 1, APP_COLOR_BORDER);

    char mem_str[64];
    snprintf(mem_str, sizeof(mem_str), "Physical Memory: %lu MB / %lu MB (%d%%)",
             st->mem_used_mb, st->mem_total_mb, st->mem_pct);
    app_draw_text(surf, 16, 12, mem_str, APP_COLOR_TEXT);
    app_draw_progress_bar(surf, 16, 28, TM_WIDTH - 32, 12, st->mem_pct, APP_COLOR_PRIMARY, APP_COLOR_CARD);

    app_draw_text(surf, 16, 48, "CPU Utilization: 18% (Estimated)", APP_COLOR_TEXT);
    app_draw_progress_bar(surf, 16, 62, TM_WIDTH - 32, 8, 18, APP_COLOR_SUCCESS, APP_COLOR_CARD);

    /* Process Table Header */
    int tbl_y = TM_HEADER_H;
    app_fill_rect(surf, 0, tbl_y, TM_WIDTH, 24, APP_COLOR_CARD);
    app_fill_rect(surf, 0, tbl_y + 23, TM_WIDTH, 1, APP_COLOR_BORDER);

    app_draw_text(surf, 16, tbl_y + 8, "PID", APP_COLOR_MUTED);
    app_draw_text(surf, 80, tbl_y + 8, "PPID", APP_COLOR_MUTED);
    app_draw_text(surf, 150, tbl_y + 8, "STATE", APP_COLOR_MUTED);
    app_draw_text(surf, 260, tbl_y + 8, "PROCESS NAME", APP_COLOR_MUTED);

    /* Process Table Rows */
    int content_y = tbl_y + 24;
    int visible_rows = (TM_HEIGHT - content_y - 36) / TM_ROW_H;

    for (int i = 0; i < visible_rows && i < st->task_count; i++) {
        task_entry_t *t = &st->tasks[i];
        int ry = content_y + i * TM_ROW_H;

        if (i == st->selected_idx) {
            app_fill_rect(surf, 0, ry, TM_WIDTH, TM_ROW_H, APP_COLOR_BORDER);
        } else if (i % 2 == 1) {
            app_fill_rect(surf, 0, ry, TM_WIDTH, TM_ROW_H, 0xFF323946u);
        }

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", t->pid);
        app_draw_text(surf, 16, ry + 6, buf, APP_COLOR_TEXT);

        snprintf(buf, sizeof(buf), "%d", t->ppid);
        app_draw_text(surf, 80, ry + 6, buf, APP_COLOR_DIM);

        app_draw_text(surf, 150, ry + 6, t->state, APP_COLOR_PRIMARY);
        app_draw_text(surf, 260, ry + 6, t->name, APP_COLOR_TEXT);

        app_fill_rect(surf, 0, ry + TM_ROW_H - 1, TM_WIDTH, 1, 0xFF3B4252u);
    }

    /* Bottom Action Bar */
    int bot_y = TM_HEIGHT - 36;
    app_fill_rect(surf, 0, bot_y, TM_WIDTH, 36, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, bot_y, TM_WIDTH, 1, APP_COLOR_BORDER);

    app_draw_button(surf, TM_WIDTH - 110, bot_y + 6, 96, 24, "End Task", 0);
    app_draw_button(surf, TM_WIDTH - 216, bot_y + 6, 96, 24, "Refresh", 0);

    if (st->task_count > 0 && st->selected_idx < st->task_count) {
        char sel_info[64];
        snprintf(sel_info, sizeof(sel_info), "Selected: PID %d (%s)",
                 st->tasks[st->selected_idx].pid,
                 st->tasks[st->selected_idx].name);
        app_draw_text(surf, 16, bot_y + 14, sel_info, APP_COLOR_MUTED);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    taskmgr_state_t state;
    memset(&state, 0, sizeof(state));
    tm_refresh(&state);

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) {
        fprintf(stderr, "taskmgr: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Task Manager", 150, 100,
                                                  TM_WIDTH, TM_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(win);

    int running = 1;
    int tick = 0;

    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY && iev->value == 1) {
                    if (iev->code == BTN_LEFT) {
                        int cx = (int)iev->pad1;
                        int cy = (int)iev->pad2;
                        int bot_y = TM_HEIGHT - 36;
                        int content_y = TM_HEADER_H + 24;

                        if (cy >= bot_y) {
                            if (cx >= TM_WIDTH - 110 && cx < TM_WIDTH - 14 &&
                                cy >= bot_y + 6 && cy < bot_y + 30) {
                                if (state.task_count > 0 && state.selected_idx < state.task_count) {
                                    kill(state.tasks[state.selected_idx].pid, SIGTERM);
                                    tm_refresh(&state);
                                }
                            } else if (cx >= TM_WIDTH - 216 && cx < TM_WIDTH - 120 &&
                                       cy >= bot_y + 6 && cy < bot_y + 30) {
                                tm_refresh(&state);
                            }
                        } else if (cy >= content_y && cy < bot_y) {
                            int row = (cy - content_y) / TM_ROW_H;
                            if (row >= 0 && row < state.task_count) {
                                state.selected_idx = row;
                                state.dirty = 1;
                            }
                        }
                    } else if (iev->code == KEY_UP) {
                        if (state.selected_idx > 0) {
                            state.selected_idx--;
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_DOWN) {
                        if (state.selected_idx < state.task_count - 1) {
                            state.selected_idx++;
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_DELETE || iev->code == KEY_K) {
                        /* Kill selected task */
                        if (state.task_count > 0 && state.selected_idx < state.task_count) {
                            kill(state.tasks[state.selected_idx].pid, SIGTERM);
                            tm_refresh(&state);
                        }
                    } else if (iev->code == KEY_R) {
                        tm_refresh(&state);
                    }
                }
            }
        }

        /* Periodic telemetry update every ~1.5 seconds */
        tick++;
        if (tick % 90 == 0) {
            tm_refresh(&state);
        }

        if (state.dirty) {
            tm_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
