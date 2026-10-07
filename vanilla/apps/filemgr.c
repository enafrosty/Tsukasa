/*
 * Project Tsukasa — Project Vanilla File Manager
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
#include <sys/stat.h>
#include <time.h>

#define FM_DEFAULT_W    600
#define FM_DEFAULT_H    420
#define HEADER_HEIGHT   36
#define SIDEBAR_WIDTH   130
#define ROW_HEIGHT      24
#define MAX_ENTRIES     128

typedef struct {
    char   name[64];
    int    is_dir;
    size_t size;
} file_entry_t;

typedef struct {
    char         cwd[128];
    file_entry_t entries[MAX_ENTRIES];
    char         item_labels[MAX_ENTRIES][80];
    const char  *item_ptrs[MAX_ENTRIES];
    int          entry_count;
    int          selected_idx;
    int          dirty;

    int          drag_candidate;
    int          drag_start_x;
    int          drag_start_y;
    int          drag_file_idx;

    ui_ctx_t    *ui_ctx;
    ui_widget_t *root;
    ui_widget_t *path_label;
    ui_widget_t *list_widget;
    char         path_buf[160];
} filemgr_state_t;

static void fm_scan_dir(filemgr_state_t *st)
{
    st->entry_count = 0;
    st->selected_idx = 0;

    char names[MAX_ENTRIES][64];
    int n = list_dir(st->cwd, names, MAX_ENTRIES);
    if (n > 0) {
        for (int i = 0; i < n && st->entry_count < MAX_ENTRIES; i++) {
            file_entry_t *e = &st->entries[st->entry_count];
            strncpy(e->name, names[i], sizeof(e->name) - 1);
            e->name[sizeof(e->name) - 1] = '\0';

            char full[256];
            if (strcmp(st->cwd, "/") == 0)
                snprintf(full, sizeof(full), "/%s", names[i]);
            else
                snprintf(full, sizeof(full), "%s/%s", st->cwd, names[i]);

            struct stat sb;
            if (stat(full, &sb) == 0) {
                e->is_dir = S_ISDIR(sb.st_mode);
                e->size = (size_t)sb.st_size;
            } else {
                e->is_dir = 0;
                e->size = 0;
            }

            if (e->is_dir) {
                snprintf(st->item_labels[st->entry_count], sizeof(st->item_labels[0]),
                         "[DIR]  %-30s  <DIR>", e->name);
            } else {
                size_t l = strlen(e->name);
                int is_elf = (l > 4 && strcmp(e->name + l - 4, ".elf") == 0);
                const char *tag = is_elf ? "[APP]" : "[FILE]";
                char sz_str[24];
                if (e->size >= 1024 * 1024)
                    snprintf(sz_str, sizeof(sz_str), "%lu MB", (unsigned long)(e->size / (1024 * 1024)));
                else if (e->size >= 1024)
                    snprintf(sz_str, sizeof(sz_str), "%lu KB", (unsigned long)(e->size / 1024));
                else
                    snprintf(sz_str, sizeof(sz_str), "%lu B", (unsigned long)e->size);

                snprintf(st->item_labels[st->entry_count], sizeof(st->item_labels[0]),
                         "%s  %-30s  %s", tag, e->name, sz_str);
            }

            st->item_ptrs[st->entry_count] = st->item_labels[st->entry_count];
            st->entry_count++;
        }
    }

    snprintf(st->path_buf, sizeof(st->path_buf), "Path: %s", st->cwd);
    if (st->path_label) {
        st->path_label->label.text = st->path_buf;
        ui_widget_invalidate(st->path_label);
    }

    if (st->list_widget) {
        st->list_widget->list.items = st->item_ptrs;
        st->list_widget->list.count = st->entry_count;
        st->list_widget->list.selected = st->selected_idx;
        st->list_widget->list.scroll_top = 0;
        ui_widget_invalidate(st->list_widget);
    }

    st->dirty = 1;
}

static void fm_navigate_to(filemgr_state_t *st, const char *path)
{
    strncpy(st->cwd, path, sizeof(st->cwd) - 1);
    st->cwd[sizeof(st->cwd) - 1] = '\0';
    fm_scan_dir(st);
}

static void fm_open_entry(filemgr_state_t *st, int idx)
{
    if (idx < 0 || idx >= st->entry_count)
        return;

    file_entry_t *e = &st->entries[idx];
    char full[256];
    if (strcmp(st->cwd, "/") == 0)
        snprintf(full, sizeof(full), "/%s", e->name);
    else
        snprintf(full, sizeof(full), "%s/%s", st->cwd, e->name);

    if (e->is_dir) {
        fm_navigate_to(st, full);
    } else {
        size_t len = strlen(e->name);
        int is_elf = 0;
        if (len >= 4) {
            const char *ext = e->name + len - 4;
            if (strcmp(ext, ".elf") == 0 || strcmp(ext, ".ELF") == 0)
                is_elf = 1;
        }
        if (is_elf || strncmp(st->cwd, "/bin", 4) == 0 || strncmp(st->cwd, "/fat12", 6) == 0) {
            char *argv[] = { full, NULL };
            spawn(full, argv, NULL);
        }
    }
}

static void on_btn_up(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    char *last_slash = strrchr(st->cwd, '/');
    if (last_slash && last_slash != st->cwd) {
        *last_slash = '\0';
        fm_scan_dir(st);
    } else {
        strcpy(st->cwd, "/");
        fm_scan_dir(st);
    }
}

static void on_btn_home(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    fm_navigate_to(st, "/bin");
}

static void on_shortcut_click(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_CLICK) return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (w && w->button.label)
        fm_navigate_to(st, w->button.label);
}

static void on_file_selected(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED) {
        st->selected_idx = ev->toggle.state;
        st->dirty = 1;
    }
}

int main(int argc, char **argv)
{
    filemgr_state_t state;
    memset(&state, 0, sizeof(state));
    strcpy(state.cwd, (argc >= 2 && argv[1][0]) ? argv[1] : "/bin");

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
        fprintf(stderr, "filemgr: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "File Manager", 140, 90,
                                                  FM_DEFAULT_W, FM_DEFAULT_H,
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
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF4C566Au;

    state.root = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.root->layout_elem->w_mode = VSIZE_GROW;
    state.root->layout_elem->h_mode = VSIZE_GROW;
    state.root->layout_elem->bg_color = bg;

    /* Header Bar */
    ui_widget_t *header = ui_box(state.ui_ctx, VDIR_ROW);
    header->layout_elem->w_mode = VSIZE_GROW;
    header->layout_elem->h_mode = VSIZE_FIXED;
    header->layout_elem->h_px = HEADER_HEIGHT;
    header->layout_elem->gap = 8;
    header->layout_elem->pad_left = header->layout_elem->pad_right = 8;
    header->layout_elem->align_items = VALIGN_CENTER;
    header->layout_elem->bg_color = elev;
    header->layout_elem->border_color = bdr;
    header->layout_elem->border_width = 1;
    ui_widget_add_child(state.root, header);

    ui_widget_t *btn_up = ui_button(state.ui_ctx, "Up", on_btn_up, &state);
    ui_widget_add_child(header, btn_up);

    ui_widget_t *btn_home = ui_button(state.ui_ctx, "Home", on_btn_home, &state);
    ui_widget_add_child(header, btn_home);

    snprintf(state.path_buf, sizeof(state.path_buf), "Path: %s", state.cwd);
    state.path_label = ui_label(state.ui_ctx, state.path_buf, fg_primary);
    ui_widget_add_child(header, state.path_label);

    /* Body Area (Sidebar + File List) */
    ui_widget_t *body = ui_box(state.ui_ctx, VDIR_ROW);
    body->layout_elem->w_mode = VSIZE_GROW;
    body->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(state.root, body);

    /* Sidebar */
    ui_widget_t *sidebar = ui_box(state.ui_ctx, VDIR_COLUMN);
    sidebar->layout_elem->w_mode = VSIZE_FIXED;
    sidebar->layout_elem->w_px = SIDEBAR_WIDTH;
    sidebar->layout_elem->h_mode = VSIZE_GROW;
    sidebar->layout_elem->pad_left = sidebar->layout_elem->pad_right = 8;
    sidebar->layout_elem->pad_top = sidebar->layout_elem->pad_bottom = 8;
    sidebar->layout_elem->gap = 4;
    sidebar->layout_elem->bg_color = elev;
    sidebar->layout_elem->border_color = bdr;
    sidebar->layout_elem->border_width = 1;
    ui_widget_add_child(body, sidebar);

    ui_widget_t *quick_lbl = ui_label(state.ui_ctx, "QUICK ACCESS", fg_dim);
    ui_widget_add_child(sidebar, quick_lbl);

    static const char *shortcuts[] = { "/", "/bin", "/tmp", "/dev", "/proc", "/sys" };
    for (int i = 0; i < 6; i++) {
        ui_widget_t *sc_btn = ui_button(state.ui_ctx, shortcuts[i], on_shortcut_click, &state);
        sc_btn->layout_elem->w_mode = VSIZE_GROW;
        ui_widget_add_child(sidebar, sc_btn);
    }

    /* Main File List Container */
    ui_widget_t *main_col = ui_box(state.ui_ctx, VDIR_COLUMN);
    main_col->layout_elem->w_mode = VSIZE_GROW;
    main_col->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(body, main_col);

    state.list_widget = ui_list(state.ui_ctx, state.item_ptrs, state.entry_count,
                                on_file_selected, &state);
    state.list_widget->layout_elem->w_mode = VSIZE_GROW;
    state.list_widget->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(main_col, state.list_widget);

    fm_scan_dir(&state);

    int running = 1;
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
            } else if (ev.type == VANILLA_EVENT_DOUBLE_CLICK) {
                fm_open_entry(&state, state.selected_idx);
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                ui_handle_event(state.ui_ctx, state.root, iev);

                if (iev->type == EV_KEY) {
                    if (iev->code == BTN_LEFT) {
                        if (iev->value == 1) {
                            int cx = (int)iev->pad1;
                            int cy = (int)iev->pad2;
                            if (state.list_widget && state.list_widget->layout_elem) {
                                int lx = state.list_widget->layout_elem->computed_x;
                                int ly = state.list_widget->layout_elem->computed_y;
                                int lw = state.list_widget->layout_elem->computed_w;
                                int lh = state.list_widget->layout_elem->computed_h;
                                if (cx >= lx && cx < lx + lw && cy >= ly && cy < ly + lh) {
                                    state.drag_candidate = 1;
                                    state.drag_start_x = cx;
                                    state.drag_start_y = cy;
                                    state.drag_file_idx = state.selected_idx;
                                }
                            }
                        } else {
                            state.drag_candidate = 0;
                        }
                    } else if (iev->value == 1) {
                        if (iev->code == KEY_UP) {
                            if (state.selected_idx > 0) {
                                state.selected_idx--;
                                state.list_widget->list.selected = state.selected_idx;
                                int row_y = state.selected_idx * ROW_HEIGHT;
                                if (row_y < state.list_widget->list.scroll_top)
                                    state.list_widget->list.scroll_top = row_y;
                                ui_widget_invalidate(state.list_widget);
                                state.dirty = 1;
                            }
                        } else if (iev->code == KEY_DOWN) {
                            if (state.selected_idx < state.entry_count - 1) {
                                state.selected_idx++;
                                state.list_widget->list.selected = state.selected_idx;
                                int row_y = state.selected_idx * ROW_HEIGHT;
                                int lh = state.list_widget->layout_elem ? state.list_widget->layout_elem->computed_h : 300;
                                if (row_y + ROW_HEIGHT > state.list_widget->list.scroll_top + lh)
                                    state.list_widget->list.scroll_top = row_y + ROW_HEIGHT - lh;
                                ui_widget_invalidate(state.list_widget);
                                state.dirty = 1;
                            }
                        } else if (iev->code == KEY_PAGEUP) {
                            int lh = state.list_widget->layout_elem ? state.list_widget->layout_elem->computed_h : 300;
                            state.list_widget->list.scroll_top -= lh / 2;
                            if (state.list_widget->list.scroll_top < 0)
                                state.list_widget->list.scroll_top = 0;
                            ui_widget_invalidate(state.list_widget);
                            state.dirty = 1;
                        } else if (iev->code == KEY_PAGEDOWN) {
                            int lh = state.list_widget->layout_elem ? state.list_widget->layout_elem->computed_h : 300;
                            int total_h = state.entry_count * ROW_HEIGHT;
                            state.list_widget->list.scroll_top += lh / 2;
                            if (state.list_widget->list.scroll_top > total_h - lh)
                                state.list_widget->list.scroll_top = total_h > lh ? total_h - lh : 0;
                            ui_widget_invalidate(state.list_widget);
                            state.dirty = 1;
                        } else if (iev->code == KEY_ENTER) {
                            fm_open_entry(&state, state.selected_idx);
                        } else if (iev->code == KEY_BACKSPACE) {
                            char *last_slash = strrchr(state.cwd, '/');
                            if (last_slash && last_slash != state.cwd) {
                                *last_slash = '\0';
                                fm_scan_dir(&state);
                            } else {
                                strcpy(state.cwd, "/");
                                fm_scan_dir(&state);
                            }
                        }
                    }
                } else if (iev->type == EV_REL) {
                    if (state.drag_candidate) {
                        int cx = (int)iev->pad1;
                        int cy = (int)iev->pad2;
                        int dx = cx - state.drag_start_x;
                        int dy = cy - state.drag_start_y;
                        if (dx * dx + dy * dy >= DND_THRESHOLD_PX * DND_THRESHOLD_PX) {
                            if (state.drag_file_idx >= 0 && state.drag_file_idx < state.entry_count) {
                                file_entry_t *e = &state.entries[state.drag_file_idx];
                                char full[256];
                                if (strcmp(state.cwd, "/") == 0)
                                    snprintf(full, sizeof(full), "/%s", e->name);
                                else
                                    snprintf(full, sizeof(full), "%s/%s", state.cwd, e->name);

                                char uri[280];
                                snprintf(uri, sizeof(uri), "file://%s", full);
                                dnd_start_drag(win, "text/uri-list", uri, strlen(uri), NULL);
                                state.drag_candidate = 0;
                            }
                        }
                    }
                }
            }
        }

        ui_render(state.ui_ctx, state.root, &win->surface, NULL);

        /* Draw list scrollbar if entries exceed visible height */
        if (state.list_widget && state.list_widget->layout_elem) {
            int lx = state.list_widget->layout_elem->computed_x;
            int ly = state.list_widget->layout_elem->computed_y;
            int lw = state.list_widget->layout_elem->computed_w;
            int lh = state.list_widget->layout_elem->computed_h;
            int total_h = state.entry_count * ROW_HEIGHT;
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
