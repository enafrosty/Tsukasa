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
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define FM_DEFAULT_W    590
#define FM_DEFAULT_H    332
#define FM_TOOLBAR_H    34
#define FM_PATHBAR_H    34
#define FM_STATUS_H     24
#define FM_SIDEBAR_W    128
#define FM_LIST_ROW_H   24
#define FM_SCROLLBAR_W  10
#define FM_MAX_ENTRIES  128
#define FM_PATH_MAX     256

enum {
    FM_MENU_FILE = 0,
    FM_MENU_VIEW = 1,
    FM_MENU_HELP = 2,
    FM_MENU_COUNT = 3
};

enum {
    FM_ACT_OPEN = 1,
    FM_ACT_NEW_FOLDER,
    FM_ACT_RENAME,
    FM_ACT_DELETE,
    FM_ACT_PROPERTIES,
    FM_ACT_QUIT,
    FM_ACT_REFRESH,
    FM_ACT_UP,
    FM_ACT_TOGGLE_HIDDEN,
    FM_ACT_TOGGLE_DIRS_FIRST,
    FM_ACT_ABOUT
};

typedef enum {
    FM_MODAL_NONE = 0,
    FM_MODAL_NEW_FOLDER,
    FM_MODAL_RENAME,
    FM_MODAL_CONFIRM_DELETE,
    FM_MODAL_INFO
} fm_modal_mode_t;

typedef struct {
    char   name[64];
    int    is_dir;
    int    is_sock;
    int    is_dev;
    size_t size;
} file_entry_t;

typedef struct {
    char            cwd[FM_PATH_MAX];
    file_entry_t    entries[FM_MAX_ENTRIES];
    char            item_labels[FM_MAX_ENTRIES][96];
    const char     *item_ptrs[FM_MAX_ENTRIES];
    int             entry_count;
    int             selected_idx;

    int             show_hidden;
    int             dirs_first;
    int             running;
    int             win_focused;
    int             ctrl_down;
    int             alt_down;

    int             drag_candidate;
    int             drag_start_x;
    int             drag_start_y;
    int             drag_file_idx;

    char            status_left_buf[256];
    char            status_right_buf[128];
    char            modal_title[64];
    char            modal_body[256];
    char            target_path[FM_PATH_MAX];
    char            target_name[64];

    fm_modal_mode_t modal_mode;

    ui_ctx_t       *ui_ctx;
    ui_widget_t    *root;
    ui_widget_t    *menubar;
    ui_widget_t    *path_field;
    ui_widget_t    *list_widget;
    ui_widget_t    *status_left;
    ui_widget_t    *status_right;

    ui_widget_t    *menus[FM_MENU_COUNT];
    ui_menu_item_t  file_items[8];
    ui_menu_item_t  view_items[5];
    ui_menu_item_t  help_items[1];
    ui_widget_t    *prev_popup;

    ui_widget_t    *prompt_modal;
    ui_widget_t    *confirm_modal;
    ui_widget_t    *info_modal;
} filemgr_state_t;

static int64_t fm_now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
    return 0;
}

static void fm_join_path(const char *dir, const char *name, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    if (!dir || dir[0] == '\0' || strcmp(dir, "/") == 0)
        snprintf(out, cap, "/%s", name ? name : "");
    else
        snprintf(out, cap, "%s/%s", dir, name ? name : "");
}

static void fm_format_size(size_t size, char *out, size_t cap)
{
    if (size >= 1024UL * 1024UL) {
        snprintf(out, cap, "%lu MB", (unsigned long)(size / (1024UL * 1024UL)));
    } else if (size >= 1024UL) {
        snprintf(out, cap, "%lu KB", (unsigned long)(size / 1024UL));
    } else {
        snprintf(out, cap, "%lu B", (unsigned long)size);
    }
}

static int fm_is_elf_name(const char *name)
{
    if (!name)
        return 0;
    size_t len = strlen(name);
    if (len < 4)
        return 0;
    const char *ext = name + len - 4;
    return (strcmp(ext, ".elf") == 0 || strcmp(ext, ".ELF") == 0);
}

static void fm_normalize_path(const char *in, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;

    if (!in) {
        out[0] = '\0';
        return;
    }

    while (*in && isspace((unsigned char)*in))
        in++;

    size_t len = strlen(in);
    while (len > 0 && isspace((unsigned char)in[len - 1]))
        len--;

    if (len == 0) {
        out[0] = '\0';
        return;
    }

    size_t j = 0;
    if (in[0] != '/' && j + 1 < cap)
        out[j++] = '/';

    int prev_slash = 0;
    for (size_t i = 0; i < len && j + 1 < cap; i++) {
        char c = in[i];
        if (c == '/') {
            if (prev_slash)
                continue;
            prev_slash = 1;
        } else {
            prev_slash = 0;
        }
        out[j++] = c;
    }
    while (j > 1 && out[j - 1] == '/')
        j--;
    out[j] = '\0';
}

static int fm_validate_leaf_name(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/' || (unsigned char)name[i] < 0x20)
            return 0;
    }
    return 1;
}

static void fm_show_info(filemgr_state_t *st, const char *title, const char *body)
{
    if (!st || !st->info_modal)
        return;

    snprintf(st->modal_title, sizeof(st->modal_title), "%s", title ? title : "Notice");
    snprintf(st->modal_body, sizeof(st->modal_body), "%s", body ? body : "");
    st->info_modal->modal.title = st->modal_title;
    st->info_modal->modal.body = st->modal_body;
    st->info_modal->modal.ok_label = "OK";
    st->modal_mode = FM_MODAL_INFO;
    ui_ctx_show_modal(st->ui_ctx, st->info_modal);
}

static void fm_show_prompt(filemgr_state_t *st, fm_modal_mode_t mode,
                           const char *title, const char *body,
                           const char *initial, const char *ok_label)
{
    if (!st || !st->prompt_modal)
        return;

    snprintf(st->modal_title, sizeof(st->modal_title), "%s", title ? title : "Input");
    snprintf(st->modal_body, sizeof(st->modal_body), "%s", body ? body : "");
    st->prompt_modal->modal.title = st->modal_title;
    st->prompt_modal->modal.body = st->modal_body;
    st->prompt_modal->modal.ok_label = ok_label ? ok_label : "OK";
    st->prompt_modal->modal.cancel_label = "Cancel";
    ui_text_field_set_text(st->prompt_modal->modal.input_field, initial ? initial : "");
    st->modal_mode = mode;
    ui_ctx_show_modal(st->ui_ctx, st->prompt_modal);
}

static void fm_update_status(filemgr_state_t *st, const char *right_msg)
{
    if (!st)
        return;

    if (st->entry_count <= 0) {
        snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                 "0 items  |  (empty directory)");
    } else if (st->selected_idx >= 0 && st->selected_idx < st->entry_count) {
        const file_entry_t *e = &st->entries[st->selected_idx];
        if (e->is_dir) {
            snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                     "%d %s  |  %s (Directory)",
                     st->entry_count, st->entry_count == 1 ? "item" : "items", e->name);
        } else if (e->is_sock) {
            snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                     "%d %s  |  %s (Socket)",
                     st->entry_count, st->entry_count == 1 ? "item" : "items", e->name);
        } else if (e->is_dev) {
            snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                     "%d %s  |  %s (Device)",
                     st->entry_count, st->entry_count == 1 ? "item" : "items", e->name);
        } else {
            char sz_str[24];
            fm_format_size(e->size, sz_str, sizeof(sz_str));
            snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                     "%d %s  |  %s (%s)",
                     st->entry_count, st->entry_count == 1 ? "item" : "items",
                     e->name, sz_str);
        }
    } else {
        snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                 "%d %s", st->entry_count, st->entry_count == 1 ? "item" : "items");
    }

    if (right_msg)
        snprintf(st->status_right_buf, sizeof(st->status_right_buf), "%s", right_msg);

    if (st->status_left)
        ui_label_set_text(st->status_left, st->status_left_buf);
    if (st->status_right && right_msg)
        ui_label_set_text(st->status_right, st->status_right_buf);
}

static int fm_entry_less(const file_entry_t *a, const file_entry_t *b, int dirs_first)
{
    if (dirs_first && a->is_dir != b->is_dir)
        return a->is_dir > b->is_dir;
    return strcasecmp(a->name, b->name) < 0;
}

static void fm_sort_entries(filemgr_state_t *st)
{
    for (int i = 1; i < st->entry_count; i++) {
        file_entry_t key = st->entries[i];
        int j = i - 1;
        while (j >= 0 && fm_entry_less(&key, &st->entries[j], st->dirs_first)) {
            st->entries[j + 1] = st->entries[j];
            j--;
        }
        st->entries[j + 1] = key;
    }
}

static void fm_format_rows(filemgr_state_t *st)
{
    for (int i = 0; i < st->entry_count; i++) {
        const file_entry_t *e = &st->entries[i];
        if (e->is_dir) {
            snprintf(st->item_labels[i], sizeof(st->item_labels[0]),
                     "[DIR]   %s  <DIR>", e->name);
        } else if (e->is_sock) {
            snprintf(st->item_labels[i], sizeof(st->item_labels[0]),
                     "[SOCK]  %s", e->name);
        } else if (e->is_dev) {
            snprintf(st->item_labels[i], sizeof(st->item_labels[0]),
                     "[DEV]   %s", e->name);
        } else {
            const char *tag = fm_is_elf_name(e->name) ? "[APP]" : "[FILE]";
            char sz_str[24];
            fm_format_size(e->size, sz_str, sizeof(sz_str));
            snprintf(st->item_labels[i], sizeof(st->item_labels[0]),
                     "%-6s  %s  (%s)", tag, e->name, sz_str);
        }
        st->item_ptrs[i] = st->item_labels[i];
    }
}

static int fm_scan_dir(filemgr_state_t *st, const char *select_name)
{
    char names[FM_MAX_ENTRIES][64];
    int n = list_dir(st->cwd, names, FM_MAX_ENTRIES);
    if (n < 0) {
        fm_update_status(st, "Directory unreadable");
        return -1;
    }

    st->entry_count = 0;
    st->selected_idx = 0;
    st->drag_candidate = 0;

    for (int i = 0; i < n && st->entry_count < FM_MAX_ENTRIES; i++) {
        if (names[i][0] == '\0' ||
            strcmp(names[i], ".") == 0 ||
            strcmp(names[i], "..") == 0) {
            continue;
        }
        if (!st->show_hidden && names[i][0] == '.')
            continue;

        file_entry_t *e = &st->entries[st->entry_count];
        memset(e, 0, sizeof(*e));
        snprintf(e->name, sizeof(e->name), "%s", names[i]);

        char full[FM_PATH_MAX];
        fm_join_path(st->cwd, e->name, full, sizeof(full));

        struct stat sb;
        if (stat(full, &sb) == 0) {
            e->is_dir = S_ISDIR(sb.st_mode);
            e->is_sock = S_ISSOCK(sb.st_mode);
            e->is_dev = S_ISCHR(sb.st_mode);
            e->size = (size_t)sb.st_size;
        }
        st->entry_count++;
    }

    fm_sort_entries(st);
    fm_format_rows(st);

    if (select_name && select_name[0] != '\0') {
        for (int i = 0; i < st->entry_count; i++) {
            if (strcmp(st->entries[i].name, select_name) == 0) {
                st->selected_idx = i;
                break;
            }
        }
    }

    if (st->path_field)
        ui_text_field_set_text(st->path_field, st->cwd);

    if (st->list_widget) {
        st->list_widget->list.selected = st->selected_idx;
        st->list_widget->list.scroll_top = 0;
        ui_list_set_items(st->list_widget, st->item_ptrs, st->entry_count);
    }

    fm_update_status(st, "Ready");
    return 0;
}

static int fm_navigate_to(filemgr_state_t *st, const char *path)
{
    if (!st || !path || path[0] == '\0')
        return -1;

    char norm[FM_PATH_MAX];
    fm_normalize_path(path, norm, sizeof(norm));
    if (norm[0] == '\0')
        return -1;

    char probe[FM_MAX_ENTRIES][64];
    if (list_dir(norm, probe, FM_MAX_ENTRIES) < 0) {
        fm_update_status(st, "Path not found");
        snprintf(st->modal_body, sizeof(st->modal_body),
                 "Cannot open folder:\n%s", norm);
        fm_show_info(st, "Navigation Error", st->modal_body);
        return -1;
    }

    snprintf(st->cwd, sizeof(st->cwd), "%s", norm);
    return fm_scan_dir(st, NULL);
}

static void fm_go_up(filemgr_state_t *st)
{
    if (!st || strcmp(st->cwd, "/") == 0)
        return;

    char prev_leaf[64];
    prev_leaf[0] = '\0';

    char *last_slash = strrchr(st->cwd, '/');
    if (last_slash)
        snprintf(prev_leaf, sizeof(prev_leaf), "%s", last_slash + 1);

    if (last_slash && last_slash != st->cwd)
        *last_slash = '\0';
    else
        snprintf(st->cwd, sizeof(st->cwd), "/");

    (void)fm_scan_dir(st, prev_leaf[0] ? prev_leaf : NULL);
}

static pid_t fm_spawn_notepad_file(const char *filepath)
{
    static const char *notepad_bins[] = {
        "/bin/notepad.elf",
        "/fat12/NOTEPAD.ELF",
        "/NOTEPAD.ELF",
        "/bin/NOTEPAD.ELF",
        NULL
    };

    for (int i = 0; notepad_bins[i] != NULL; i++) {
        char *argv[] = { (char *)notepad_bins[i], (char *)filepath, NULL };
        pid_t pid = spawn(notepad_bins[i], argv, NULL);
        if (pid > 0)
            return pid;
    }
    return -1;
}

static void fm_open_entry(filemgr_state_t *st, int idx)
{
    if (!st || idx < 0 || idx >= st->entry_count) {
        fm_update_status(st, "No item selected");
        return;
    }

    const file_entry_t *e = &st->entries[idx];
    char full[FM_PATH_MAX];
    fm_join_path(st->cwd, e->name, full, sizeof(full));

    if (e->is_dir) {
        (void)fm_navigate_to(st, full);
        return;
    }

    if (e->is_sock || e->is_dev) {
        fm_update_status(st, "Cannot open special file");
        return;
    }

    if (fm_is_elf_name(e->name)) {
        char *argv[] = { full, NULL };
        pid_t pid = spawn(full, argv, NULL);
        if (pid > 0) {
            fm_update_status(st, "Launched application");
        } else {
            snprintf(st->modal_body, sizeof(st->modal_body),
                     "Failed to launch:\n%s", full);
            fm_show_info(st, "Launch Error", st->modal_body);
        }
        return;
    }

    if (strncmp(st->cwd, "/bin", 4) == 0 || strncmp(st->cwd, "/fat12", 6) == 0) {
        char *argv[] = { full, NULL };
        pid_t pid = spawn(full, argv, NULL);
        if (pid > 0) {
            fm_update_status(st, "Launched binary");
            return;
        }
    }

    if (fm_spawn_notepad_file(full) > 0) {
        fm_update_status(st, "Opened in Notepad");
    } else {
        snprintf(st->modal_body, sizeof(st->modal_body),
                 "Could not open %s in Notepad.", full);
        fm_show_info(st, "Open Failed", st->modal_body);
    }
}

static void fm_cmd_new_folder(filemgr_state_t *st)
{
    fm_show_prompt(st, FM_MODAL_NEW_FOLDER,
                   "New Folder", "Enter new folder name:",
                   "", "Create");
}

static void fm_cmd_rename(filemgr_state_t *st)
{
    if (!st || st->selected_idx < 0 || st->selected_idx >= st->entry_count) {
        fm_update_status(st, "No item selected");
        return;
    }

    const file_entry_t *e = &st->entries[st->selected_idx];
    snprintf(st->target_name, sizeof(st->target_name), "%s", e->name);
    fm_join_path(st->cwd, e->name, st->target_path, sizeof(st->target_path));

    fm_show_prompt(st, FM_MODAL_RENAME,
                   "Rename Item", "Enter new name:",
                   e->name, "Rename");
}

static void fm_cmd_delete(filemgr_state_t *st)
{
    if (!st || !st->confirm_modal ||
        st->selected_idx < 0 || st->selected_idx >= st->entry_count) {
        fm_update_status(st, "No item selected");
        return;
    }

    const file_entry_t *e = &st->entries[st->selected_idx];
    snprintf(st->target_name, sizeof(st->target_name), "%s", e->name);
    fm_join_path(st->cwd, e->name, st->target_path, sizeof(st->target_path));

    snprintf(st->modal_title, sizeof(st->modal_title), "Confirm Delete");
    snprintf(st->modal_body, sizeof(st->modal_body),
             "Delete %s '%s'?\nThis action cannot be undone.",
             e->is_dir ? "folder" : "file", e->name);
    st->confirm_modal->modal.title = st->modal_title;
    st->confirm_modal->modal.body = st->modal_body;
    st->confirm_modal->modal.ok_label = "Delete";
    st->confirm_modal->modal.cancel_label = "Cancel";
    st->modal_mode = FM_MODAL_CONFIRM_DELETE;
    ui_ctx_show_modal(st->ui_ctx, st->confirm_modal);
}

static void fm_cmd_properties(filemgr_state_t *st)
{
    if (!st || st->selected_idx < 0 || st->selected_idx >= st->entry_count) {
        fm_update_status(st, "No item selected");
        return;
    }

    const file_entry_t *e = &st->entries[st->selected_idx];
    char full[FM_PATH_MAX];
    fm_join_path(st->cwd, e->name, full, sizeof(full));

    const char *kind = e->is_dir ? "Directory"
                     : (e->is_sock ? "Unix Socket"
                     : (e->is_dev ? "Character Device"
                     : (fm_is_elf_name(e->name) ? "ELF Executable" : "Regular File")));

    snprintf(st->modal_body, sizeof(st->modal_body),
             "Name: %s\nType: %s\nSize: %lu bytes\nPath: %s",
             e->name, kind, (unsigned long)e->size, full);
    fm_show_info(st, "Properties", st->modal_body);
}

static void fm_sync_menus(filemgr_state_t *st)
{
    if (!st)
        return;

    int has_sel = (st->entry_count > 0 && st->selected_idx >= 0 && st->selected_idx < st->entry_count);
    for (int i = 0; i < 6; i++) {
        uint32_t id = st->file_items[i].id;
        if (id == FM_ACT_OPEN || id == FM_ACT_RENAME ||
            id == FM_ACT_DELETE || id == FM_ACT_PROPERTIES) {
            if (has_sel)
                st->file_items[i].flags &= ~UI_MENU_ITEM_DISABLED;
            else
                st->file_items[i].flags |= UI_MENU_ITEM_DISABLED;
        }
    }
    if (st->menus[FM_MENU_FILE])
        ui_menu_set_rich_items(st->menus[FM_MENU_FILE], st->file_items, 8);

    if (st->show_hidden)
        st->view_items[3].flags |= UI_MENU_ITEM_CHECKED;
    else
        st->view_items[3].flags &= ~UI_MENU_ITEM_CHECKED;

    if (st->dirs_first)
        st->view_items[4].flags |= UI_MENU_ITEM_CHECKED;
    else
        st->view_items[4].flags &= ~UI_MENU_ITEM_CHECKED;

    if (st->menus[FM_MENU_VIEW])
        ui_menu_set_rich_items(st->menus[FM_MENU_VIEW], st->view_items, 5);
}

static void on_btn_up(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    fm_go_up(st);
    ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static void fm_refresh_dir(filemgr_state_t *st)
{
    if (!st)
        return;
    char keep_buf[64];
    keep_buf[0] = '\0';
    if (st->selected_idx >= 0 && st->selected_idx < st->entry_count)
        snprintf(keep_buf, sizeof(keep_buf), "%s", st->entries[st->selected_idx].name);
    (void)fm_scan_dir(st, keep_buf[0] ? keep_buf : NULL);
}

static void on_btn_refresh(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    fm_refresh_dir(st);
    ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static void on_btn_open(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    fm_open_entry(st, st->selected_idx);
    if (!ui_ctx_get_modal(st->ui_ctx))
        ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static void on_btn_new_folder(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    fm_cmd_new_folder((filemgr_state_t *)ud);
}

static void on_btn_rename(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    fm_cmd_rename((filemgr_state_t *)ud);
}

static void on_btn_delete(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    fm_cmd_delete((filemgr_state_t *)ud);
}

static void on_btn_go(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (fm_navigate_to(st, ui_text_field_get_text(st->path_field)) == 0)
        ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static void on_shortcut_click(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_CLICK)
        return;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (w && w->button.label) {
        if (fm_navigate_to(st, w->button.label) == 0)
            ui_widget_set_focus(st->ui_ctx, st->list_widget);
    }
}

static void on_file_selected(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (!st || !ev || ev->type != UI_EVENT_VALUE_CHANGED)
        return;

    st->selected_idx = ev->toggle.state;
    fm_update_status(st, NULL);
}

static void on_menubar_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (!st || !ev || ev->type != UI_EVENT_VALUE_CHANGED)
        return;

    int32_t idx = ev->toggle.state;
    if (idx < 0 || idx >= FM_MENU_COUNT || !st->menus[idx])
        return;

    if (st->prev_popup == st->menus[idx]) {
        w->menu.selected = -1;
        ui_widget_invalidate(w);
        if (!ui_ctx_get_modal(st->ui_ctx))
            ui_widget_set_focus(st->ui_ctx, st->list_widget);
        return;
    }

    fm_sync_menus(st);
    int32_t fx = ui_menubar_item_x(w, idx);
    int32_t fy = w->layout_elem ? (w->layout_elem->computed_y + w->layout_elem->computed_h) : 28;
    ui_ctx_show_popup(st->ui_ctx, st->menus[idx], fx, fy);
}

static void on_popup_menu_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (!st || !w || !ev || ev->type != UI_EVENT_VALUE_CHANGED)
        return;

    int32_t idx = ev->toggle.state;
    if (idx < 0 || idx >= w->menu.count || !w->menu.rich_items)
        return;

    uint32_t action = w->menu.rich_items[idx].id;
    ui_ctx_hide_popup(st->ui_ctx);
    if (st->menubar) {
        st->menubar->menu.selected = -1;
        ui_widget_invalidate(st->menubar);
    }

    switch (action) {
    case FM_ACT_OPEN:
        fm_open_entry(st, st->selected_idx);
        break;
    case FM_ACT_NEW_FOLDER:
        fm_cmd_new_folder(st);
        break;
    case FM_ACT_RENAME:
        fm_cmd_rename(st);
        break;
    case FM_ACT_DELETE:
        fm_cmd_delete(st);
        break;
    case FM_ACT_PROPERTIES:
        fm_cmd_properties(st);
        break;
    case FM_ACT_QUIT:
        st->running = 0;
        break;
    case FM_ACT_REFRESH:
        fm_refresh_dir(st);
        break;
    case FM_ACT_UP:
        fm_go_up(st);
        break;
    case FM_ACT_TOGGLE_HIDDEN:
        st->show_hidden = !st->show_hidden;
        fm_sync_menus(st);
        (void)fm_scan_dir(st, NULL);
        break;
    case FM_ACT_TOGGLE_DIRS_FIRST:
        st->dirs_first = !st->dirs_first;
        fm_sync_menus(st);
        (void)fm_scan_dir(st, NULL);
        break;
    case FM_ACT_ABOUT:
        fm_show_info(st, "About File Manager",
                     "Project Tsukasa File Manager\n"
                     "Double-click or Enter to open\n"
                     "F2 Rename  |  Del Delete  |  Ctrl+N New Folder");
        break;
    default:
        break;
    }

    if (!ui_ctx_get_modal(st->ui_ctx) && st->running)
        ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static void on_modal_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    filemgr_state_t *st = (filemgr_state_t *)ud;
    if (!st || !w || !ev || ev->type != UI_EVENT_CLICK)
        return;

    fm_modal_mode_t mode = st->modal_mode;
    ui_dialog_result_t res = w->modal.result;
    st->modal_mode = FM_MODAL_NONE;

    if (res == UI_DIALOG_RES_OK) {
        if (mode == FM_MODAL_NEW_FOLDER) {
            const char *name = ui_dialog_get_input(w);
            if (!fm_validate_leaf_name(name)) {
                fm_show_info(st, "Invalid Name",
                             "Folder name cannot be empty or contain '/'.");
            } else {
                char full[FM_PATH_MAX];
                fm_join_path(st->cwd, name, full, sizeof(full));
                if (mkdir(full, 0755) < 0) {
                    snprintf(st->modal_body, sizeof(st->modal_body),
                             "Could not create folder '%s':\n%s",
                             name, strerror(errno));
                    fm_show_info(st, "New Folder Failed", st->modal_body);
                } else {
                    (void)fm_scan_dir(st, name);
                    fm_update_status(st, "Folder created");
                }
            }
        } else if (mode == FM_MODAL_RENAME) {
            const char *new_name = ui_dialog_get_input(w);
            if (!fm_validate_leaf_name(new_name)) {
                fm_show_info(st, "Invalid Name",
                             "Name cannot be empty or contain '/'.");
            } else if (strcmp(new_name, st->target_name) != 0) {
                char new_full[FM_PATH_MAX];
                fm_join_path(st->cwd, new_name, new_full, sizeof(new_full));
                if (rename(st->target_path, new_full) < 0) {
                    snprintf(st->modal_body, sizeof(st->modal_body),
                             "Could not rename '%s':\n%s",
                             st->target_name, strerror(errno));
                    fm_show_info(st, "Rename Failed", st->modal_body);
                } else {
                    (void)fm_scan_dir(st, new_name);
                    fm_update_status(st, "Renamed");
                }
            }
        } else if (mode == FM_MODAL_CONFIRM_DELETE) {
            struct stat sb;
            int is_dir = (stat(st->target_path, &sb) == 0 && S_ISDIR(sb.st_mode));
            int rc = is_dir ? rmdir(st->target_path) : unlink(st->target_path);
            if (rc < 0) {
                snprintf(st->modal_body, sizeof(st->modal_body),
                         "Could not delete '%s':\n%s",
                         st->target_name, strerror(errno));
                fm_show_info(st, "Delete Failed", st->modal_body);
            } else {
                int prev_sel = st->selected_idx;
                (void)fm_scan_dir(st, NULL);
                if (st->entry_count > 0) {
                    if (prev_sel >= st->entry_count)
                        prev_sel = st->entry_count - 1;
                    st->selected_idx = prev_sel;
                    st->list_widget->list.selected = prev_sel;
                    ui_list_scroll_to_selected(st->list_widget);
                    ui_widget_invalidate(st->list_widget);
                }
                fm_update_status(st, "Deleted");
            }
        }
    }

    if (st->modal_mode == FM_MODAL_NONE && st->running)
        ui_widget_set_focus(st->ui_ctx, st->list_widget);
}

static ui_widget_t *fm_compact_button(ui_ctx_t *ctx, const char *label,
                                      ui_event_cb_t cb, void *ud)
{
    ui_widget_t *b = ui_button(ctx, label, cb, ud);
    if (b && b->layout_elem && label) {
        int32_t tw = 0;
        app_measure_text_ex(label, app_default_font_size(), APP_FONT_MEDIUM, &tw, NULL);
        b->layout_elem->pad_left = 8;
        b->layout_elem->pad_right = 8;
        b->layout_elem->pad_top = 4;
        b->layout_elem->pad_bottom = 4;
        b->layout_elem->w_px = (tw + 16 > 44) ? (tw + 16) : 44;
        b->layout_elem->h_px = 26;
    }
    return b;
}

static void fm_init_menus(filemgr_state_t *st)
{
    static const char *bar_labels[FM_MENU_COUNT] = { "File", "View", "Help" };
    st->menubar = ui_menubar(st->ui_ctx, bar_labels, FM_MENU_COUNT, on_menubar_event, st);
    ui_widget_add_child(st->root, st->menubar);

    st->file_items[0] = (ui_menu_item_t){ "Open Selected", "Enter",     0,                      FM_ACT_OPEN };
    st->file_items[1] = (ui_menu_item_t){ "New Folder...", "Ctrl+N",    0,                      FM_ACT_NEW_FOLDER };
    st->file_items[2] = (ui_menu_item_t){ "Rename...",     "F2",        0,                      FM_ACT_RENAME };
    st->file_items[3] = (ui_menu_item_t){ "Delete...",     "Del",       0,                      FM_ACT_DELETE };
    st->file_items[4] = (ui_menu_item_t){ NULL,            NULL,        UI_MENU_ITEM_SEPARATOR, 0 };
    st->file_items[5] = (ui_menu_item_t){ "Properties",    "Alt+Enter", 0,                      FM_ACT_PROPERTIES };
    st->file_items[6] = (ui_menu_item_t){ NULL,            NULL,        UI_MENU_ITEM_SEPARATOR, 0 };
    st->file_items[7] = (ui_menu_item_t){ "Quit",          "Ctrl+Q",    0,                      FM_ACT_QUIT };
    st->menus[FM_MENU_FILE] = ui_menu_ex(st->ui_ctx, st->file_items, 8, on_popup_menu_event, st);

    st->view_items[0] = (ui_menu_item_t){ "Refresh",          "F5",   0,                      FM_ACT_REFRESH };
    st->view_items[1] = (ui_menu_item_t){ "Parent Folder",    "Bksp", 0,                      FM_ACT_UP };
    st->view_items[2] = (ui_menu_item_t){ NULL,               NULL,   UI_MENU_ITEM_SEPARATOR, 0 };
    st->view_items[3] = (ui_menu_item_t){ "Show Hidden",      NULL,   0,                      FM_ACT_TOGGLE_HIDDEN };
    st->view_items[4] = (ui_menu_item_t){ "Directories First", NULL,  UI_MENU_ITEM_CHECKED,   FM_ACT_TOGGLE_DIRS_FIRST };
    st->menus[FM_MENU_VIEW] = ui_menu_ex(st->ui_ctx, st->view_items, 5, on_popup_menu_event, st);

    st->help_items[0] = (ui_menu_item_t){ "About File Manager", NULL, 0, FM_ACT_ABOUT };
    st->menus[FM_MENU_HELP] = ui_menu_ex(st->ui_ctx, st->help_items, 1, on_popup_menu_event, st);
}

static int fm_point_in_list_row(const filemgr_state_t *st, int32_t mx, int32_t my, int *out_row)
{
    if (!st || !st->list_widget || !st->list_widget->layout_elem)
        return 0;

    const vanilla_elem_t *le = st->list_widget->layout_elem;
    int32_t lx = le->computed_x;
    int32_t ly = le->computed_y;
    int32_t lw = le->computed_w;
    int32_t lh = le->computed_h;
    if (mx < lx || mx >= lx + lw || my < ly || my >= ly + lh)
        return 0;

    int32_t inner_h = lh > 2 ? lh - 2 : 0;
    int32_t total_h = st->entry_count * FM_LIST_ROW_H;
    int has_sb = (total_h > inner_h && inner_h > 20 && lw > FM_SCROLLBAR_W + 8);
    if (has_sb && mx >= lx + lw - FM_SCROLLBAR_W - 2)
        return 0;

    int row = (my - ly + st->list_widget->list.scroll_top) / FM_LIST_ROW_H;
    if (row < 0 || row >= st->entry_count)
        return 0;

    if (out_row)
        *out_row = row;
    return 1;
}

static int fm_handle_shortcut(filemgr_state_t *st, const vanilla_event_t *ev)
{
    const struct input_event *iev = &ev->input;
    if (iev->type != EV_KEY || iev->value != 1)
        return 0;

    int is_ctrl = st->ctrl_down || ((ev->mod_state & MOD_CTRL) != 0);
    int is_alt = st->alt_down || ((ev->mod_state & MOD_ALT) != 0);
    ui_widget_t *fw = ui_get_focused(st->ui_ctx);

    if (is_ctrl) {
        if (iev->code == KEY_N) {
            fm_cmd_new_folder(st);
            return 1;
        }
        if (iev->code == KEY_R) {
            fm_refresh_dir(st);
            return 1;
        }
        if (iev->code == KEY_Q) {
            st->running = 0;
            return 1;
        }
        if (iev->code == KEY_L && st->path_field) {
            ui_widget_set_focus(st->ui_ctx, st->path_field);
            ui_text_field_select_all(st->path_field);
            return 1;
        }
        return 0;
    }

    if (iev->code == KEY_F5) {
        fm_refresh_dir(st);
        return 1;
    }

    if (fw == st->path_field) {
        if (iev->code == KEY_ENTER || iev->code == KEY_KPENTER) {
            if (fm_navigate_to(st, ui_text_field_get_text(st->path_field)) == 0)
                ui_widget_set_focus(st->ui_ctx, st->list_widget);
            return 1;
        }
        if (iev->code == KEY_ESC) {
            ui_text_field_set_text(st->path_field, st->cwd);
            ui_widget_set_focus(st->ui_ctx, st->list_widget);
            return 1;
        }
        return 0;
    }

    if (iev->code == KEY_F2) {
        fm_cmd_rename(st);
        return 1;
    }
    if (iev->code == KEY_DELETE && fw == st->list_widget) {
        fm_cmd_delete(st);
        return 1;
    }
    if (iev->code == KEY_BACKSPACE && fw == st->list_widget) {
        fm_go_up(st);
        return 1;
    }
    if ((iev->code == KEY_ENTER || iev->code == KEY_KPENTER) && is_alt && fw == st->list_widget) {
        fm_cmd_properties(st);
        return 1;
    }
    if ((iev->code == KEY_ENTER || iev->code == KEY_KPENTER) && fw == st->list_widget) {
        fm_open_entry(st, st->selected_idx);
        return 1;
    }

    return 0;
}

static int fm_poll_timeout_ms(const filemgr_state_t *st)
{
    if (!st || !st->win_focused)
        return -1;

    ui_widget_t *fw = ui_get_focused(st->ui_ctx);
    if (!fw || fw->type != UI_WIDGET_TEXT_FIELD || fw->text_field.read_only)
        return -1;

    int64_t now_ms = fm_now_ms();
    int64_t rem = 530 - (now_ms % 530);
    if (rem < 50)
        rem = 50;
    return (int)rem;
}

int main(int argc, char **argv)
{
    filemgr_state_t *state = (filemgr_state_t *)calloc(1, sizeof(filemgr_state_t));
    if (!state) {
        fprintf(stderr, "filemgr: memory allocation failed\n");
        return 1;
    }

    state->running = 1;
    state->win_focused = 1;
    state->dirs_first = 1;

    const char *start_dir = (argc >= 2 && argv[1] && argv[1][0] != '\0') ? argv[1] : "/bin";
    fm_normalize_path(start_dir, state->cwd, sizeof(state->cwd));
    if (state->cwd[0] == '\0')
        snprintf(state->cwd, sizeof(state->cwd), "/bin");

    char probe[FM_MAX_ENTRIES][64];
    if (list_dir(state->cwd, probe, FM_MAX_ENTRIES) < 0)
        snprintf(state->cwd, sizeof(state->cwd), "/");

    vanilla_client_t *client = NULL;
    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(VANILLA_SOCKET_PATH);
        if (client)
            break;
        poll(NULL, 0, 10);
    }
    if (!client) {
        fprintf(stderr, "filemgr: failed to connect to display server\n");
        free(state);
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "File Manager", 36, 390,
                                                  FM_DEFAULT_W, FM_DEFAULT_H,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 320, 220, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    state->ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!state->ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF98A2B3u;

    state->root = ui_box(state->ui_ctx, VDIR_COLUMN);
    state->root->layout_elem->w_mode = VSIZE_GROW;
    state->root->layout_elem->h_mode = VSIZE_GROW;
    state->root->layout_elem->bg_color = bg;

    fm_init_menus(state);

    /* Toolbar */
    ui_widget_t *toolbar = ui_box(state->ui_ctx, VDIR_ROW);
    toolbar->layout_elem->w_mode = VSIZE_GROW;
    toolbar->layout_elem->h_mode = VSIZE_FIXED;
    toolbar->layout_elem->h_px = FM_TOOLBAR_H;
    toolbar->layout_elem->gap = 6;
    toolbar->layout_elem->pad_left = 8;
    toolbar->layout_elem->pad_right = 8;
    toolbar->layout_elem->align_items = VALIGN_CENTER;
    toolbar->layout_elem->bg_color = elev;
    toolbar->layout_elem->border_color = bdr;
    toolbar->layout_elem->border_width = 1;
    toolbar->layout_elem->clip_children = 1;
    ui_widget_add_child(state->root, toolbar);

    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "Up", on_btn_up, state));
    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "Refresh", on_btn_refresh, state));
    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "Open", on_btn_open, state));
    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "New Folder", on_btn_new_folder, state));
    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "Rename", on_btn_rename, state));
    ui_widget_add_child(toolbar, fm_compact_button(state->ui_ctx, "Delete", on_btn_delete, state));

    /* Editable Path Bar */
    ui_widget_t *pathbar = ui_box(state->ui_ctx, VDIR_ROW);
    pathbar->layout_elem->w_mode = VSIZE_GROW;
    pathbar->layout_elem->h_mode = VSIZE_FIXED;
    pathbar->layout_elem->h_px = FM_PATHBAR_H;
    pathbar->layout_elem->gap = 8;
    pathbar->layout_elem->pad_left = 8;
    pathbar->layout_elem->pad_right = 8;
    pathbar->layout_elem->align_items = VALIGN_CENTER;
    pathbar->layout_elem->bg_color = bg;
    pathbar->layout_elem->border_color = bdr;
    pathbar->layout_elem->border_width = 1;
    ui_widget_add_child(state->root, pathbar);

    ui_widget_t *loc_lbl = ui_label_ex(state->ui_ctx, "Location:", fg_muted, 13.0f, APP_FONT_MEDIUM);
    ui_widget_add_child(pathbar, loc_lbl);

    state->path_field = ui_text_field(state->ui_ctx, state->cwd, NULL, state);
    state->path_field->layout_elem->w_mode = VSIZE_GROW;
    state->path_field->layout_elem->h_mode = VSIZE_FIXED;
    state->path_field->layout_elem->h_px = 26;
    ui_widget_add_child(pathbar, state->path_field);

    ui_widget_t *btn_go = fm_compact_button(state->ui_ctx, "Go", on_btn_go, state);
    ui_widget_add_child(pathbar, btn_go);

    /* Body Area (Sidebar + Directory List) */
    ui_widget_t *body = ui_box(state->ui_ctx, VDIR_ROW);
    body->layout_elem->w_mode = VSIZE_GROW;
    body->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(state->root, body);

    ui_widget_t *sidebar = ui_box(state->ui_ctx, VDIR_COLUMN);
    sidebar->layout_elem->w_mode = VSIZE_FIXED;
    sidebar->layout_elem->w_px = FM_SIDEBAR_W;
    sidebar->layout_elem->h_mode = VSIZE_GROW;
    sidebar->layout_elem->pad_left = 8;
    sidebar->layout_elem->pad_right = 8;
    sidebar->layout_elem->pad_top = 8;
    sidebar->layout_elem->pad_bottom = 8;
    sidebar->layout_elem->gap = 4;
    sidebar->layout_elem->bg_color = elev;
    sidebar->layout_elem->border_color = bdr;
    sidebar->layout_elem->border_width = 1;
    ui_widget_add_child(body, sidebar);

    ui_widget_t *quick_lbl = ui_label_ex(state->ui_ctx, "PLACES", fg_dim, 11.0f, APP_FONT_BOLD);
    ui_widget_add_child(sidebar, quick_lbl);

    static const char *shortcuts[] = { "/", "/bin", "/tmp", "/etc", "/dev", "/proc", "/sys" };
    for (int i = 0; i < 7; i++) {
        ui_widget_t *sc_btn = fm_compact_button(state->ui_ctx, shortcuts[i], on_shortcut_click, state);
        sc_btn->layout_elem->w_mode = VSIZE_GROW;
        ui_widget_add_child(sidebar, sc_btn);
    }

    state->list_widget = ui_list(state->ui_ctx, state->item_ptrs, state->entry_count,
                                 on_file_selected, state);
    state->list_widget->layout_elem->w_mode = VSIZE_GROW;
    state->list_widget->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(body, state->list_widget);

    /* Status Bar */
    ui_widget_t *statusbar = ui_box(state->ui_ctx, VDIR_ROW);
    statusbar->layout_elem->w_mode = VSIZE_GROW;
    statusbar->layout_elem->h_mode = VSIZE_FIXED;
    statusbar->layout_elem->h_px = FM_STATUS_H;
    statusbar->layout_elem->pad_left = 10;
    statusbar->layout_elem->pad_right = 10;
    statusbar->layout_elem->gap = 8;
    statusbar->layout_elem->align_items = VALIGN_CENTER;
    statusbar->layout_elem->bg_color = elev;
    statusbar->layout_elem->border_color = bdr;
    statusbar->layout_elem->border_width = 1;
    statusbar->layout_elem->clip_children = 1;
    ui_widget_add_child(state->root, statusbar);

    state->status_left = ui_label_ex(state->ui_ctx, "", fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(statusbar, state->status_left);

    ui_widget_t *spacer = ui_box(state->ui_ctx, VDIR_ROW);
    spacer->layout_elem->w_mode = VSIZE_GROW;
    spacer->layout_elem->h_mode = VSIZE_FIXED;
    spacer->layout_elem->h_px = 1;
    ui_widget_add_child(statusbar, spacer);

    state->status_right = ui_label_ex(state->ui_ctx, "Ready", fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(statusbar, state->status_right);

    state->prompt_modal = ui_dialog_prompt(state->ui_ctx, "", "", "", "OK", "Cancel",
                                           on_modal_event, state);
    state->confirm_modal = ui_dialog_confirm(state->ui_ctx, "", "", "Delete", "Cancel",
                                             on_modal_event, state);
    state->info_modal = ui_modal_dialog(state->ui_ctx, "", "", "OK",
                                        on_modal_event, state);

    (void)fm_scan_dir(state, NULL);
    ui_widget_set_focus(state->ui_ctx, state->list_widget);

    int client_fd = vanilla_client_get_fd(client);
    int need_present = 1;

    while (state->running) {
        vanilla_event_t ev;
        int poll_rc;
        while ((poll_rc = vanilla_poll_event(client, &ev)) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                state->running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_FOCUS) {
                state->win_focused = ev.focus.focused ? 1 : 0;
                need_present = 1;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                if (ev.configure.serial != 0)
                    vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state->root);
                need_present = 1;
            } else if (ev.type == VANILLA_EVENT_DOUBLE_CLICK) {
                if (ev.input.code == BTN_LEFT &&
                    !ui_ctx_get_modal(state->ui_ctx) &&
                    !ui_ctx_get_popup(state->ui_ctx)) {
                    int32_t mx = (int32_t)ev.input.pad1;
                    int32_t my = (int32_t)ev.input.pad2;
                    int row = -1;
                    ui_handle_event(state->ui_ctx, state->root, &ev.input);
                    if (ui_hit_test(state->root, mx, my) == state->list_widget &&
                        fm_point_in_list_row(state, mx, my, &row)) {
                        state->drag_candidate = 0;
                        fm_open_entry(state, row);
                        need_present = 1;
                    }
                }
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY) {
                    if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        /* Handled inside ui_handle_event as well */
                    } else if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL) {
                        state->ctrl_down = (iev->value != 0);
                    } else if (iev->code == KEY_LEFTALT || iev->code == KEY_RIGHTALT) {
                        state->alt_down = (iev->value != 0);
                    }
                }

                ui_widget_t *modal_before = ui_ctx_get_modal(state->ui_ctx);
                ui_widget_t *popup_before = ui_ctx_get_popup(state->ui_ctx);
                state->prev_popup = popup_before;

                if (!modal_before && !popup_before && fm_handle_shortcut(state, &ev)) {
                    need_present = 1;
                } else {
                    ui_handle_event(state->ui_ctx, state->root, iev);
                }

                if (!modal_before && !popup_before) {
                    if (iev->type == EV_KEY && iev->code == BTN_LEFT) {
                        if (iev->value == 1) {
                            int32_t cx = (int32_t)iev->pad1;
                            int32_t cy = (int32_t)iev->pad2;
                            int row = -1;
                            if (ui_hit_test(state->root, cx, cy) == state->list_widget &&
                                fm_point_in_list_row(state, cx, cy, &row)) {
                                state->drag_candidate = 1;
                                state->drag_start_x = cx;
                                state->drag_start_y = cy;
                                state->drag_file_idx = row;
                            } else {
                                state->drag_candidate = 0;
                            }
                        } else {
                            state->drag_candidate = 0;
                        }
                    } else if ((iev->type == EV_REL || iev->type == EV_ABS) &&
                               state->drag_candidate &&
                               !state->list_widget->list.sb_dragging) {
                        int cx = (int)iev->pad1;
                        int cy = (int)iev->pad2;
                        int dx = cx - state->drag_start_x;
                        int dy = cy - state->drag_start_y;
                        if (dx * dx + dy * dy >= DND_THRESHOLD_PX * DND_THRESHOLD_PX) {
                            if (state->drag_file_idx >= 0 && state->drag_file_idx < state->entry_count) {
                                const file_entry_t *e = &state->entries[state->drag_file_idx];
                                char full[FM_PATH_MAX];
                                fm_join_path(state->cwd, e->name, full, sizeof(full));
                                char uri[FM_PATH_MAX + 16];
                                snprintf(uri, sizeof(uri), "file://%s", full);
                                dnd_start_drag(win, "text/uri-list", uri, strlen(uri), NULL);
                            }
                            state->drag_candidate = 0;
                        }
                    }
                } else {
                    state->drag_candidate = 0;
                }

                ui_widget_t *modal_after = ui_ctx_get_modal(state->ui_ctx);
                ui_widget_t *popup_after = ui_ctx_get_popup(state->ui_ctx);
                if (popup_before != popup_after || modal_before != modal_after)
                    need_present = 1;

                if (!popup_after && state->menubar && state->menubar->menu.selected != -1) {
                    state->menubar->menu.selected = -1;
                    ui_widget_invalidate(state->menubar);
                }

                if (!modal_after && !popup_after && state->running) {
                    ui_widget_t *fw = ui_get_focused(state->ui_ctx);
                    if (!fw || fw == state->menubar ||
                        fw == state->menus[0] || fw == state->menus[1] || fw == state->menus[2]) {
                        ui_widget_set_focus(state->ui_ctx, state->list_widget);
                    }
                }
            }
        }

        if (!state->running || poll_rc < 0)
            break;

        ui_widget_t *fw = ui_get_focused(state->ui_ctx);
        int caret_blink_due = 0;
        if (state->win_focused && fw && fw->type == UI_WIDGET_TEXT_FIELD && !fw->text_field.read_only) {
            int64_t now_ms = fm_now_ms();
            if (((now_ms / 530) % 2) != ((fw->text_field.caret_ms / 530) % 2))
                caret_blink_due = 1;
        }

        ui_widget_t *cur_popup = ui_ctx_get_popup(state->ui_ctx);
        ui_widget_t *cur_modal = ui_ctx_get_modal(state->ui_ctx);
        int should_render = need_present ||
                            state->root->dirty ||
                            (cur_popup && !cur_popup->hidden && cur_popup->dirty) ||
                            (cur_modal && !cur_modal->hidden && cur_modal->dirty) ||
                            caret_blink_due;

        if (should_render) {
            ui_render(state->ui_ctx, state->root, &win->surface, NULL);
            vanilla_present(win, NULL);
            need_present = 0;
        }

        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int timeout_ms = fm_poll_timeout_ms(state);
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret < 0 && errno != EINTR)
            break;
        if (pret > 0 && !(pfd.revents & POLLIN) &&
            (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    free(state);
    return 0;
}
