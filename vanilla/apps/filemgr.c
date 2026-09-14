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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define FM_WIDTH        600
#define FM_HEIGHT       420
#define HEADER_HEIGHT   36
#define SIDEBAR_WIDTH   140
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
    int          entry_count;
    int          selected_idx;
    int          scroll_offset;
    int          dirty;
} filemgr_state_t;

static void fm_scan_dir(filemgr_state_t *st)
{
    st->entry_count = 0;
    st->selected_idx = 0;
    st->scroll_offset = 0;

    char names[MAX_ENTRIES][64];
    int n = list_dir(st->cwd, names, MAX_ENTRIES);
    if (n <= 0) {
        st->dirty = 1;
        return;
    }

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
        st->entry_count++;
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
        /* If executable or ends in .elf / .ELF, spawn it */
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

static void fm_render(vanilla_surface_t *surf, filemgr_state_t *st)
{
    app_fill_rect(surf, 0, 0, FM_WIDTH, FM_HEIGHT, APP_COLOR_BG);

    /* Header Bar */
    app_fill_rect(surf, 0, 0, FM_WIDTH, HEADER_HEIGHT, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, HEADER_HEIGHT - 1, FM_WIDTH, 1, APP_COLOR_BORDER);

    app_draw_button(surf, 8, 6, 40, 24, "Up", 0);
    app_draw_button(surf, 54, 6, 48, 24, "Home", 0);

    char path_lbl[160];
    snprintf(path_lbl, sizeof(path_lbl), "Path: %s", st->cwd);
    app_draw_text(surf, 114, 14, path_lbl, APP_COLOR_TEXT);

    /* Sidebar */
    int content_y = HEADER_HEIGHT;
    int content_h = FM_HEIGHT - HEADER_HEIGHT;
    app_fill_rect(surf, 0, content_y, SIDEBAR_WIDTH, content_h, 0xFF242933u);
    app_fill_rect(surf, SIDEBAR_WIDTH - 1, content_y, 1, content_h, APP_COLOR_BORDER);

    app_draw_text(surf, 12, content_y + 12, "QUICK ACCESS", APP_COLOR_DIM);
    static const char *shortcuts[] = { "/", "/bin", "/tmp", "/dev", "/proc", "/sys" };
    for (int i = 0; i < 6; i++) {
        int sy = content_y + 34 + i * 26;
        app_draw_text(surf, 16, sy, shortcuts[i], APP_COLOR_MUTED);
    }

    /* Main File List */
    int main_x = SIDEBAR_WIDTH;
    int main_w = FM_WIDTH - SIDEBAR_WIDTH;
    int visible_rows = content_h / ROW_HEIGHT;

    for (int i = 0; i < visible_rows; i++) {
        int e_idx = st->scroll_offset + i;
        if (e_idx >= st->entry_count)
            break;

        file_entry_t *e = &st->entries[e_idx];
        int row_y = content_y + i * ROW_HEIGHT;

        if (e_idx == st->selected_idx) {
            app_fill_rect(surf, main_x, row_y, main_w, ROW_HEIGHT, APP_COLOR_CARD);
        }

        /* Icon tag */
        if (e->is_dir) {
            app_draw_text(surf, main_x + 12, row_y + 8, "[DIR]", APP_COLOR_PRIMARY);
            app_draw_text(surf, main_x + 60, row_y + 8, e->name, APP_COLOR_TEXT);
            app_draw_text(surf, main_x + main_w - 70, row_y + 8, "<DIR>", APP_COLOR_DIM);
        } else {
            size_t l = strlen(e->name);
            int is_elf = (l > 4 && strcmp(e->name + l - 4, ".elf") == 0);
            uint32_t icon_col = is_elf ? APP_COLOR_SUCCESS : APP_COLOR_MUTED;
            const char *tag = is_elf ? "[APP]" : "[FILE]";

            app_draw_text(surf, main_x + 12, row_y + 8, tag, icon_col);
            app_draw_text(surf, main_x + 60, row_y + 8, e->name, APP_COLOR_TEXT);

            char sz_str[24];
            if (e->size >= 1024 * 1024)
                snprintf(sz_str, sizeof(sz_str), "%lu MB", (unsigned long)(e->size / (1024 * 1024)));
            else if (e->size >= 1024)
                snprintf(sz_str, sizeof(sz_str), "%lu KB", (unsigned long)(e->size / 1024));
            else
                snprintf(sz_str, sizeof(sz_str), "%lu B", (unsigned long)e->size);

            app_draw_text(surf, main_x + main_w - 70, row_y + 8, sz_str, APP_COLOR_DIM);
        }

        app_fill_rect(surf, main_x, row_y + ROW_HEIGHT - 1, main_w, 1, 0xFF353B49u);
    }
}

int main(int argc, char **argv)
{
    filemgr_state_t state;
    memset(&state, 0, sizeof(state));
    strcpy(state.cwd, (argc >= 2 && argv[1][0]) ? argv[1] : "/bin");
    fm_scan_dir(&state);

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) {
        fprintf(stderr, "filemgr: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "File Manager", 140, 90,
                                                  FM_WIDTH, FM_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(win);

    int running = 1;
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

                        if (cy < HEADER_HEIGHT) {
                            /* Header buttons */
                            if (cx >= 8 && cx < 48 && cy >= 6 && cy < 30) {
                                char *last_slash = strrchr(state.cwd, '/');
                                if (last_slash && last_slash != state.cwd) {
                                    *last_slash = '\0';
                                    fm_scan_dir(&state);
                                } else {
                                    strcpy(state.cwd, "/");
                                    fm_scan_dir(&state);
                                }
                            } else if (cx >= 54 && cx < 102 && cy >= 6 && cy < 30) {
                                strcpy(state.cwd, "/bin");
                                fm_scan_dir(&state);
                            }
                        } else if (cx < SIDEBAR_WIDTH) {
                            /* Sidebar shortcuts */
                            int content_y = HEADER_HEIGHT;
                            static const char *shortcuts[] = { "/", "/bin", "/tmp", "/dev", "/proc", "/sys" };
                            for (int i = 0; i < 6; i++) {
                                int sy = content_y + 34 + i * 26;
                                if (cy >= sy && cy < sy + 22) {
                                    fm_navigate_to(&state, shortcuts[i]);
                                    break;
                                }
                            }
                        } else {
                            /* File list row selection / activation */
                            int row = (cy - HEADER_HEIGHT) / ROW_HEIGHT;
                            int clicked_idx = state.scroll_offset + row;
                            if (clicked_idx >= 0 && clicked_idx < state.entry_count) {
                                if (clicked_idx == state.selected_idx) {
                                    fm_open_entry(&state, clicked_idx);
                                } else {
                                    state.selected_idx = clicked_idx;
                                    state.dirty = 1;
                                }
                            }
                        }
                    } else if (iev->code == KEY_UP) {
                        if (state.selected_idx > 0) {
                            state.selected_idx--;
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_DOWN) {
                        if (state.selected_idx < state.entry_count - 1) {
                            state.selected_idx++;
                            state.dirty = 1;
                        }
                    } else if (iev->code == KEY_ENTER) {
                        fm_open_entry(&state, state.selected_idx);
                    } else if (iev->code == KEY_BACKSPACE) {
                        /* Go to parent directory */
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
            }
        }

        if (state.dirty) {
            fm_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
