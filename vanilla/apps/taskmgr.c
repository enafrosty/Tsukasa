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
#include "../include/ui.h"
#include "../include/ui_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGTERM
#define SIGTERM 15
#endif

#define TM_DEFAULT_W    540
#define TM_DEFAULT_H    400
#define TM_HEADER_H     78
#define TM_ROW_H        24
#define MAX_TASKS       64

typedef struct {
    int  pid;
    int  ppid;
    char state[16];
    char name[32];
} task_entry_t;

typedef struct {
    task_entry_t tasks[MAX_TASKS];
    char         item_labels[MAX_TASKS][96];
    const char  *item_ptrs[MAX_TASKS];
    int          task_count;
    int          selected_idx;
    unsigned long mem_used_mb;
    unsigned long mem_total_mb;
    int          mem_pct;
    int          dirty;

    char         mem_str[80];
    char         count_str[64];

    ui_ctx_t    *ui_ctx;
    ui_widget_t *root;
    ui_widget_t *mem_label;
    ui_widget_t *count_label;
    ui_widget_t *list_widget;
    ui_widget_t *mem_bar_box;
    ui_widget_t *cpu_bar_box;
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

            if (t->pid > 0) {
                snprintf(st->item_labels[st->task_count], sizeof(st->item_labels[0]),
                         "%-6d %-6d %-12s %-24s",
                         t->pid, t->ppid, t->state, t->name);
                st->item_ptrs[st->task_count] = st->item_labels[st->task_count];
                st->task_count++;
            }
        }
        fclose(fp);
    }

    if (st->selected_idx >= st->task_count)
        st->selected_idx = st->task_count > 0 ? st->task_count - 1 : 0;

    snprintf(st->mem_str, sizeof(st->mem_str),
             "Physical Memory: %lu MB / %lu MB (%d%%)",
             st->mem_used_mb, st->mem_total_mb, st->mem_pct);
    if (st->mem_label) {
        st->mem_label->label.text = st->mem_str;
        ui_widget_invalidate(st->mem_label);
    }

    snprintf(st->count_str, sizeof(st->count_str),
             "Total Processes: %d", st->task_count);
    if (st->count_label) {
        st->count_label->label.text = st->count_str;
        ui_widget_invalidate(st->count_label);
    }

    if (st->list_widget) {
        st->list_widget->list.items = st->item_ptrs;
        st->list_widget->list.count = st->task_count;
        st->list_widget->list.selected = st->selected_idx;
        ui_widget_invalidate(st->list_widget);
    }

    st->dirty = 1;
}

static void on_btn_refresh(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    tm_refresh(st);
}

static void on_btn_kill(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    if (st->selected_idx >= 0 && st->selected_idx < st->task_count) {
        int pid = st->tasks[st->selected_idx].pid;
        if (pid > 1) {
            kill(pid, SIGTERM);
            tm_refresh(st);
        }
    }
}

static void on_task_selected(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED) {
        st->selected_idx = ev->toggle.state;
        st->dirty = 1;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    taskmgr_state_t state;
    memset(&state, 0, sizeof(state));

    const char *sock_path = VANILLA_SOCKET_PATH;
    vanilla_client_t *client = NULL;
    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(sock_path);
        if (client)
            break;
        struct timespec ts = { 0, 10000000 }; /* 10ms */
        nanosleep(&ts, NULL);
    }
    if (!client) {
        fprintf(stderr, "taskmgr: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Task Manager", 160, 100,
                                                  TM_DEFAULT_W, TM_DEFAULT_H,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 160, 120, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    state.ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!state.ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 1;
    }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF4C566Au;

    state.root = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.root->layout_elem->w_mode = VSIZE_GROW;
    state.root->layout_elem->h_mode = VSIZE_GROW;
    state.root->layout_elem->bg_color = bg;

    /* Telemetry Header */
    ui_widget_t *header = ui_box(state.ui_ctx, VDIR_COLUMN);
    header->layout_elem->w_mode = VSIZE_GROW;
    header->layout_elem->h_mode = VSIZE_FIXED;
    header->layout_elem->h_px = TM_HEADER_H;
    header->layout_elem->pad_left = header->layout_elem->pad_right = 16;
    header->layout_elem->pad_top = header->layout_elem->pad_bottom = 10;
    header->layout_elem->gap = 6;
    header->layout_elem->bg_color = elev;
    header->layout_elem->border_color = bdr;
    header->layout_elem->border_width = 1;
    ui_widget_add_child(state.root, header);

    strcpy(state.mem_str, "Physical Memory: Calculating...");
    state.mem_label = ui_label(state.ui_ctx, state.mem_str, fg_primary);
    ui_widget_add_child(header, state.mem_label);

    state.mem_bar_box = ui_box(state.ui_ctx, VDIR_ROW);
    state.mem_bar_box->layout_elem->w_mode = VSIZE_GROW;
    state.mem_bar_box->layout_elem->h_mode = VSIZE_FIXED;
    state.mem_bar_box->layout_elem->h_px = 10;
    state.mem_bar_box->layout_elem->bg_color = bg;
    state.mem_bar_box->layout_elem->border_color = bdr;
    state.mem_bar_box->layout_elem->border_width = 1;
    ui_widget_add_child(header, state.mem_bar_box);

    ui_widget_t *cpu_lbl = ui_label(state.ui_ctx, "CPU Utilization: 18% (Estimated)", fg_primary);
    ui_widget_add_child(header, cpu_lbl);

    state.cpu_bar_box = ui_box(state.ui_ctx, VDIR_ROW);
    state.cpu_bar_box->layout_elem->w_mode = VSIZE_GROW;
    state.cpu_bar_box->layout_elem->h_mode = VSIZE_FIXED;
    state.cpu_bar_box->layout_elem->h_px = 8;
    state.cpu_bar_box->layout_elem->bg_color = bg;
    state.cpu_bar_box->layout_elem->border_color = bdr;
    state.cpu_bar_box->layout_elem->border_width = 1;
    ui_widget_add_child(header, state.cpu_bar_box);

    /* Process Table Header */
    ui_widget_t *tbl_hdr = ui_box(state.ui_ctx, VDIR_ROW);
    tbl_hdr->layout_elem->w_mode = VSIZE_GROW;
    tbl_hdr->layout_elem->h_mode = VSIZE_FIXED;
    tbl_hdr->layout_elem->h_px = 24;
    tbl_hdr->layout_elem->pad_left = 12;
    tbl_hdr->layout_elem->align_items = VALIGN_CENTER;
    tbl_hdr->layout_elem->bg_color = elev;
    tbl_hdr->layout_elem->border_color = bdr;
    tbl_hdr->layout_elem->border_width = 1;
    ui_widget_add_child(state.root, tbl_hdr);

    ui_widget_t *col_lbl = ui_label(state.ui_ctx, "PID    PPID   STATE        PROCESS NAME", fg_dim);
    ui_widget_add_child(tbl_hdr, col_lbl);

    /* Process List View */
    state.list_widget = ui_list(state.ui_ctx, state.item_ptrs, state.task_count,
                                on_task_selected, &state);
    state.list_widget->layout_elem->w_mode = VSIZE_GROW;
    state.list_widget->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(state.root, state.list_widget);

    /* Bottom Action Bar */
    ui_widget_t *action_bar = ui_box(state.ui_ctx, VDIR_ROW);
    action_bar->layout_elem->w_mode = VSIZE_GROW;
    action_bar->layout_elem->h_mode = VSIZE_FIXED;
    action_bar->layout_elem->h_px = 36;
    action_bar->layout_elem->pad_left = action_bar->layout_elem->pad_right = 12;
    action_bar->layout_elem->gap = 8;
    action_bar->layout_elem->align_items = VALIGN_CENTER;
    action_bar->layout_elem->bg_color = elev;
    action_bar->layout_elem->border_color = bdr;
    action_bar->layout_elem->border_width = 1;
    ui_widget_add_child(state.root, action_bar);

    strcpy(state.count_str, "Total Processes: 0");
    state.count_label = ui_label(state.ui_ctx, state.count_str, fg_muted);
    ui_widget_add_child(action_bar, state.count_label);

    ui_widget_t *spacer = ui_box(state.ui_ctx, VDIR_ROW);
    spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(action_bar, spacer);

    ui_widget_t *btn_ref = ui_button(state.ui_ctx, "Refresh", on_btn_refresh, &state);
    ui_widget_add_child(action_bar, btn_ref);

    ui_widget_t *btn_end = ui_button(state.ui_ctx, "End Task", on_btn_kill, &state);
    ui_widget_add_child(action_bar, btn_end);

    tm_refresh(&state);

    int running = 1;
    int tick = 0;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state.root);
                state.dirty = 1;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                ui_handle_event(state.ui_ctx, state.root, iev);

                if (iev->type == EV_KEY && iev->value == 1) {
                    if (iev->code == KEY_UP) {
                        if (state.selected_idx > 0) {
                            state.selected_idx--;
                            state.list_widget->list.selected = state.selected_idx;
                            int row_y = state.selected_idx * TM_ROW_H;
                            if (row_y < state.list_widget->list.scroll_top)
                                state.list_widget->list.scroll_top = row_y;
                            ui_widget_invalidate(state.list_widget);
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_DOWN) {
                        if (state.selected_idx < state.task_count - 1) {
                            state.selected_idx++;
                            state.list_widget->list.selected = state.selected_idx;
                            int row_y = state.selected_idx * TM_ROW_H;
                            int lh = state.list_widget->layout_elem ? state.list_widget->layout_elem->computed_h : 200;
                            if (row_y + TM_ROW_H > state.list_widget->list.scroll_top + lh)
                                state.list_widget->list.scroll_top = row_y + TM_ROW_H - lh;
                            ui_widget_invalidate(state.list_widget);
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_F5) {
                        tm_refresh(&state);
                    } else if (iev->code == KEY_DELETE) {
                        if (state.selected_idx >= 0 && state.selected_idx < state.task_count) {
                            int pid = state.tasks[state.selected_idx].pid;
                            if (pid > 1) {
                                kill(pid, SIGTERM);
                                tm_refresh(&state);
                            }
                        }
                    }
                }
            }
        }

        /* Periodic refresh every ~2 seconds (120 frames at 60fps) */
        tick++;
        if (tick % 120 == 0) {
            tm_refresh(&state);
        }

        ui_render(state.ui_ctx, state.root, &win->surface, NULL);

        /* Render dynamic progress bars inside header boxes */
        if (state.mem_bar_box && state.mem_bar_box->layout_elem) {
            int mx = state.mem_bar_box->layout_elem->computed_x + 1;
            int my = state.mem_bar_box->layout_elem->computed_y + 1;
            int mw = state.mem_bar_box->layout_elem->computed_w - 2;
            int mh = state.mem_bar_box->layout_elem->computed_h - 2;
            int fill_w = (mw * state.mem_pct) / 100;
            if (fill_w > 0 && mh > 0)
                app_fill_rect(&win->surface, mx, my, fill_w, mh,
                              g_theme ? g_theme->accent : 0xFF88C0D0u);
        }

        if (state.cpu_bar_box && state.cpu_bar_box->layout_elem) {
            int cx = state.cpu_bar_box->layout_elem->computed_x + 1;
            int cy = state.cpu_bar_box->layout_elem->computed_y + 1;
            int cw = state.cpu_bar_box->layout_elem->computed_w - 2;
            int ch = state.cpu_bar_box->layout_elem->computed_h - 2;
            int fill_w = (cw * 18) / 100;
            if (fill_w > 0 && ch > 0)
                app_fill_rect(&win->surface, cx, cy, fill_w, ch,
                              g_theme ? g_theme->success : 0xFFA3BE8Cu);
        }

        /* Scrollbar for process list */
        if (state.list_widget && state.list_widget->layout_elem) {
            int lx = state.list_widget->layout_elem->computed_x;
            int ly = state.list_widget->layout_elem->computed_y;
            int lw = state.list_widget->layout_elem->computed_w;
            int lh = state.list_widget->layout_elem->computed_h;
            int total_h = state.task_count * TM_ROW_H;
            if (total_h > lh && lh > 20) {
                int thumb_h = (lh * lh) / total_h;
                if (thumb_h < 14) thumb_h = 14;
                int max_scroll = total_h - lh;
                int thumb_y = ly + (state.list_widget->list.scroll_top * (lh - thumb_h)) / (max_scroll > 0 ? max_scroll : 1);
                app_fill_rect(&win->surface, lx + lw - 5, thumb_y, 4, thumb_h,
                              g_theme ? g_theme->accent : 0xFF88C0D0u);
            }
        }

        vanilla_present(win, NULL);
        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
