/*
 * Project Tsukasa — Project Vanilla Text Editor
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define NOTE_DEFAULT_W      620
#define NOTE_DEFAULT_H      420
#define NOTE_STATUS_H       24
#define NOTE_MAX_FILE_BYTES (4 * 1024 * 1024)
#define NOTE_PATH_MAX       256

enum {
    MENU_IDX_FILE = 0,
    MENU_IDX_EDIT = 1,
    MENU_IDX_VIEW = 2,
    MENU_IDX_HELP = 3,
    MENU_COUNT    = 4
};

enum {
    ACT_FILE_NEW = 1,
    ACT_FILE_OPEN,
    ACT_FILE_SAVE,
    ACT_FILE_SAVE_AS,
    ACT_FILE_QUIT,
    ACT_EDIT_CUT,
    ACT_EDIT_COPY,
    ACT_EDIT_PASTE,
    ACT_EDIT_SELECT_ALL,
    ACT_VIEW_LINE_NUMS,
    ACT_VIEW_STATUS_BAR,
    ACT_VIEW_ZOOM_IN,
    ACT_VIEW_ZOOM_OUT,
    ACT_VIEW_ZOOM_RESET,
    ACT_HELP_ABOUT
};

typedef enum {
    NOTE_MODAL_NONE = 0,
    NOTE_MODAL_OPEN_PATH,
    NOTE_MODAL_SAVE_AS,
    NOTE_MODAL_CONFIRM_DISCARD,
    NOTE_MODAL_INFO
} note_modal_mode_t;

typedef enum {
    NOTE_PENDING_NONE = 0,
    NOTE_PENDING_NEW,
    NOTE_PENDING_OPEN,
    NOTE_PENDING_LOAD_PATH,
    NOTE_PENDING_QUIT
} note_pending_action_t;

typedef struct {
    char                  filename[NOTE_PATH_MAX];
    char                  pending_path[NOTE_PATH_MAX];
    char                  status_left_buf[320];
    char                  status_right_buf[96];
    char                  status_note[64];
    char                  modal_title[64];
    char                  modal_body[256];

    int                   has_path;
    int                   modified;
    int                   show_line_numbers;
    int                   show_statusbar;
    float                 editor_font_size;

    int                   running;
    int                   win_focused;
    int                   ctrl_down;
    int                   shift_down;

    int32_t               last_line;
    int32_t               last_col;
    int32_t               last_line_count;
    int                   last_modified;

    note_modal_mode_t     modal_mode;
    note_pending_action_t pending_action;

    ui_ctx_t             *ui_ctx;
    ui_widget_t          *root;
    ui_widget_t          *menubar;
    ui_widget_t          *editor;
    ui_widget_t          *statusbar;
    ui_widget_t          *status_left;
    ui_widget_t          *status_right;

    ui_widget_t          *menus[MENU_COUNT];
    ui_menu_item_t        file_items[7];
    ui_menu_item_t        edit_items[5];
    ui_menu_item_t        view_items[6];
    ui_menu_item_t        help_items[1];
    ui_widget_t          *prev_popup;

    ui_widget_t          *prompt_modal;
    ui_widget_t          *confirm_modal;
    ui_widget_t          *info_modal;
} notepad_state_t;

static char   g_local_clip[VCLIP_TEXT_MAX + 1];
static size_t g_local_clip_len = 0;

static int64_t note_now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
    return 0;
}

static int note_clip_set(const char *text, size_t len)
{
    if (!text && len > 0)
        return -1;

    size_t copy_len = len;
    if (copy_len > VCLIP_TEXT_MAX)
        copy_len = VCLIP_TEXT_MAX;
    if (copy_len > 0)
        memcpy(g_local_clip, text, copy_len);
    g_local_clip[copy_len] = '\0';
    g_local_clip_len = copy_len;

    (void)clipboard_set(text, len);
    return 0;
}

static int note_clip_get(char *out_buf, size_t max_len)
{
    if (!out_buf || max_len == 0)
        return 0;

    int rc = clipboard_get(out_buf, max_len);
    if (rc >= 0) {
        size_t n = (size_t)rc;
        if (n > VCLIP_TEXT_MAX)
            n = VCLIP_TEXT_MAX;
        if (n > 0)
            memcpy(g_local_clip, out_buf, n);
        g_local_clip[n] = '\0';
        g_local_clip_len = n;
        return rc;
    }

    size_t n = g_local_clip_len;
    if (n >= max_len)
        n = max_len - 1;
    if (n > 0)
        memcpy(out_buf, g_local_clip, n);
    out_buf[n] = '\0';
    return (int)n;
}

static void note_trim_path(char *s)
{
    if (!s)
        return;

    size_t start = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

static void note_show_info(notepad_state_t *st, const char *title, const char *body)
{
    if (!st || !st->info_modal)
        return;

    snprintf(st->modal_title, sizeof(st->modal_title), "%s", title ? title : "Notice");
    snprintf(st->modal_body, sizeof(st->modal_body), "%s", body ? body : "");
    st->info_modal->modal.title = st->modal_title;
    st->info_modal->modal.body = st->modal_body;
    st->info_modal->modal.ok_label = "OK";
    st->modal_mode = NOTE_MODAL_INFO;
    ui_ctx_show_modal(st->ui_ctx, st->info_modal);
}

static void note_show_prompt(notepad_state_t *st, note_modal_mode_t mode,
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

static void note_show_discard_confirm(notepad_state_t *st, note_pending_action_t next_act)
{
    if (!st || !st->confirm_modal)
        return;

    st->pending_action = next_act;
    snprintf(st->modal_title, sizeof(st->modal_title), "Unsaved Changes");
    snprintf(st->modal_body, sizeof(st->modal_body),
             "Discard unsaved changes to %s?", st->filename);
    st->confirm_modal->modal.title = st->modal_title;
    st->confirm_modal->modal.body = st->modal_body;
    st->confirm_modal->modal.ok_label = "Discard";
    st->confirm_modal->modal.cancel_label = "Cancel";
    st->modal_mode = NOTE_MODAL_CONFIRM_DISCARD;
    ui_ctx_show_modal(st->ui_ctx, st->confirm_modal);
}

static void note_update_status_labels(notepad_state_t *st, int force)
{
    if (!st || !st->editor)
        return;

    int32_t ln = 1;
    int32_t col = 1;
    ui_text_field_get_cursor_lc(st->editor, &ln, &col);
    int32_t lines = ui_text_field_get_line_count(st->editor);

    if (!force &&
        ln == st->last_line &&
        col == st->last_col &&
        lines == st->last_line_count &&
        st->modified == st->last_modified &&
        st->status_note[0] == '\0') {
        return;
    }

    st->last_line = ln;
    st->last_col = col;
    st->last_line_count = lines;
    st->last_modified = st->modified;

    if (st->status_note[0] != '\0') {
        snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                 "%s%s  -  %s",
                 st->filename,
                 st->modified ? " [Modified]" : "",
                 st->status_note);
    } else {
        snprintf(st->status_left_buf, sizeof(st->status_left_buf),
                 "%s%s",
                 st->filename,
                 st->modified ? " [Modified]" : "");
    }

    snprintf(st->status_right_buf, sizeof(st->status_right_buf),
             "Ln %d, Col %d  |  %d %s",
             ln, col, lines, (lines == 1) ? "line" : "lines");

    if (st->status_left)
        ui_label_set_text(st->status_left, st->status_left_buf);
    if (st->status_right)
        ui_label_set_text(st->status_right, st->status_right_buf);
}

static void note_set_status_note(notepad_state_t *st, const char *note)
{
    if (!st)
        return;
    snprintf(st->status_note, sizeof(st->status_note), "%s", note ? note : "");
    note_update_status_labels(st, 1);
}

static void note_new_document(notepad_state_t *st)
{
    snprintf(st->filename, sizeof(st->filename), "/tmp/untitled.txt");
    st->has_path = 0;
    st->modified = 0;
    ui_text_field_set_text(st->editor, "");
    note_set_status_note(st, "New file");
    ui_widget_set_focus(st->ui_ctx, st->editor);
}

static int note_load_file(notepad_state_t *st, const char *path, int allow_missing)
{
    if (!st || !path || path[0] == '\0')
        return -1;

    char clean[NOTE_PATH_MAX];
    snprintf(clean, sizeof(clean), "%s", path);
    note_trim_path(clean);
    if (clean[0] == '\0')
        return -1;

    int fd = open(clean, O_RDONLY);
    if (fd < 0) {
        if (allow_missing && errno == ENOENT) {
            snprintf(st->filename, sizeof(st->filename), "%s", clean);
            st->has_path = 1;
            st->modified = 0;
            ui_text_field_set_text(st->editor, "");
            note_set_status_note(st, "New file");
            ui_widget_set_focus(st->ui_ctx, st->editor);
            return 0;
        }
        snprintf(st->modal_body, sizeof(st->modal_body),
                 "Could not open %s:\n%s", clean, strerror(errno));
        note_show_info(st, "Open Failed", st->modal_body);
        return -1;
    }

    struct stat sb;
    if (fstat(fd, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        close(fd);
        snprintf(st->modal_body, sizeof(st->modal_body),
                 "Cannot open directory:\n%s", clean);
        note_show_info(st, "Open Failed", st->modal_body);
        return -1;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        close(fd);
        note_show_info(st, "Open Failed", "Out of memory while reading file.");
        return -1;
    }

    int truncated = 0;
    char tmp[1024];
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            snprintf(st->modal_body, sizeof(st->modal_body),
                     "Error reading %s:\n%s", clean, strerror(errno));
            note_show_info(st, "Open Failed", st->modal_body);
            return -1;
        }
        if (n == 0)
            break;

        for (ssize_t i = 0; i < n; i++) {
            char c = tmp[i];
            if (c == '\r')
                continue;
            if (c == '\0')
                c = ' ';
            if (len >= NOTE_MAX_FILE_BYTES) {
                truncated = 1;
                break;
            }
            if (len + 2 > cap) {
                size_t new_cap = cap * 2;
                if (new_cap > NOTE_MAX_FILE_BYTES + 1)
                    new_cap = NOTE_MAX_FILE_BYTES + 1;
                char *nb = (char *)realloc(buf, new_cap);
                if (!nb) {
                    free(buf);
                    close(fd);
                    note_show_info(st, "Open Failed", "Out of memory while loading file.");
                    return -1;
                }
                buf = nb;
                cap = new_cap;
            }
            buf[len++] = c;
        }
        if (truncated)
            break;
    }
    close(fd);

    buf[len] = '\0';
    ui_text_field_set_text(st->editor, buf);
    free(buf);

    snprintf(st->filename, sizeof(st->filename), "%s", clean);
    st->has_path = 1;
    st->modified = 0;
    note_set_status_note(st, truncated ? "Truncated at 4 MiB" : "Loaded");
    ui_widget_set_focus(st->ui_ctx, st->editor);
    return 0;
}

static int note_save_to_path(notepad_state_t *st, const char *path)
{
    if (!st || !st->editor || !path || path[0] == '\0')
        return -1;

    char clean[NOTE_PATH_MAX];
    snprintf(clean, sizeof(clean), "%s", path);
    note_trim_path(clean);
    if (clean[0] == '\0') {
        note_show_info(st, "Save Failed", "Please enter a valid file path.");
        return -1;
    }

    int fd = open(clean, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(st->modal_body, sizeof(st->modal_body),
                 "Could not save %s:\n%s", clean, strerror(errno));
        note_show_info(st, "Save Failed", st->modal_body);
        return -1;
    }

    const char *text = ui_text_field_get_text(st->editor);
    size_t total = text ? strlen(text) : 0;
    size_t written = 0;
    while (written < total) {
        ssize_t n = write(fd, text + written, total - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            int err = errno;
            close(fd);
            snprintf(st->modal_body, sizeof(st->modal_body),
                     "Write failed for %s:\n%s", clean, strerror(err));
            note_show_info(st, "Save Failed", st->modal_body);
            return -1;
        }
        if (n == 0)
            break;
        written += (size_t)n;
    }

    close(fd);
    snprintf(st->filename, sizeof(st->filename), "%s", clean);
    st->has_path = 1;
    st->modified = 0;
    note_set_status_note(st, "Saved");
    ui_widget_set_focus(st->ui_ctx, st->editor);
    return 0;
}

static void note_cmd_new(notepad_state_t *st)
{
    if (st->modified) {
        note_show_discard_confirm(st, NOTE_PENDING_NEW);
        return;
    }
    note_new_document(st);
}

static void note_cmd_open(notepad_state_t *st)
{
    if (st->modified) {
        note_show_discard_confirm(st, NOTE_PENDING_OPEN);
        return;
    }
    note_show_prompt(st, NOTE_MODAL_OPEN_PATH,
                     "Open File", "Enter file path to open:",
                     st->filename, "Open");
}

static void note_cmd_save_as(notepad_state_t *st)
{
    note_show_prompt(st, NOTE_MODAL_SAVE_AS,
                     "Save File As", "Enter destination path:",
                     st->filename, "Save");
}

static void note_cmd_save(notepad_state_t *st)
{
    if (!st->has_path) {
        note_cmd_save_as(st);
        return;
    }
    (void)note_save_to_path(st, st->filename);
}

static void note_cmd_quit(notepad_state_t *st)
{
    if (st->modified) {
        note_show_discard_confirm(st, NOTE_PENDING_QUIT);
        return;
    }
    st->running = 0;
}

static void note_sync_view_menu(notepad_state_t *st)
{
    if (!st || !st->menus[MENU_IDX_VIEW])
        return;

    if (st->show_line_numbers)
        st->view_items[0].flags |= UI_MENU_ITEM_CHECKED;
    else
        st->view_items[0].flags &= ~UI_MENU_ITEM_CHECKED;

    if (st->show_statusbar)
        st->view_items[1].flags |= UI_MENU_ITEM_CHECKED;
    else
        st->view_items[1].flags &= ~UI_MENU_ITEM_CHECKED;

    ui_menu_set_rich_items(st->menus[MENU_IDX_VIEW], st->view_items, 6);
}

static void note_sync_edit_menu(notepad_state_t *st)
{
    if (!st || !st->menus[MENU_IDX_EDIT] || !st->editor)
        return;

    int has_sel = (st->editor->text_field.sel_start >= 0 &&
                   st->editor->text_field.sel_end > st->editor->text_field.sel_start);

    if (has_sel) {
        st->edit_items[0].flags &= ~UI_MENU_ITEM_DISABLED;
        st->edit_items[1].flags &= ~UI_MENU_ITEM_DISABLED;
    } else {
        st->edit_items[0].flags |= UI_MENU_ITEM_DISABLED;
        st->edit_items[1].flags |= UI_MENU_ITEM_DISABLED;
    }
    ui_menu_set_rich_items(st->menus[MENU_IDX_EDIT], st->edit_items, 5);
}

static void note_toggle_line_numbers(notepad_state_t *st)
{
    st->show_line_numbers = !st->show_line_numbers;
    st->editor->text_field.show_line_numbers = st->show_line_numbers ? 1 : 0;
    ui_widget_invalidate(st->editor);
    note_sync_view_menu(st);
    note_set_status_note(st, st->show_line_numbers ? "Line numbers on" : "Line numbers off");
}

static void note_toggle_statusbar(notepad_state_t *st)
{
    st->show_statusbar = !st->show_statusbar;
    if (st->statusbar) {
        ui_widget_set_hidden(st->statusbar, !st->show_statusbar);
        if (st->statusbar->layout_elem)
            st->statusbar->layout_elem->h_px = st->show_statusbar ? NOTE_STATUS_H : 0;
        ui_widget_invalidate(st->root);
    }
    note_sync_view_menu(st);
}

static void note_dispatch_editor_ctrl_key(notepad_state_t *st, uint16_t code)
{
    if (!st || !st->ui_ctx || !st->editor)
        return;

    ui_widget_set_focus(st->ui_ctx, st->editor);

    struct input_event kev;
    memset(&kev, 0, sizeof(kev));
    kev.type = EV_KEY;

    if (!st->ctrl_down) {
        kev.code = KEY_LEFTCTRL;
        kev.value = 1;
        ui_handle_event(st->ui_ctx, st->root, &kev);
    }

    kev.code = code;
    kev.value = 1;
    ui_handle_event(st->ui_ctx, st->root, &kev);

    if (!st->ctrl_down) {
        kev.code = KEY_LEFTCTRL;
        kev.value = 0;
        ui_handle_event(st->ui_ctx, st->root, &kev);
    }
}

static void on_editor_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    notepad_state_t *st = (notepad_state_t *)ud;
    if (!st || !ev)
        return;

    if (ev->type == UI_EVENT_TEXT_CHANGED) {
        st->modified = 1;
        st->status_note[0] = '\0';
        note_update_status_labels(st, 1);
    }
}

static void on_menubar_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    notepad_state_t *st = (notepad_state_t *)ud;
    if (!st || !ev || ev->type != UI_EVENT_VALUE_CHANGED)
        return;

    int32_t idx = ev->toggle.state;
    if (idx < 0 || idx >= MENU_COUNT || !st->menus[idx])
        return;

    if (st->prev_popup == st->menus[idx]) {
        w->menu.selected = -1;
        ui_widget_invalidate(w);
        if (!ui_ctx_get_modal(st->ui_ctx))
            ui_widget_set_focus(st->ui_ctx, st->editor);
        return;
    }

    if (idx == MENU_IDX_EDIT)
        note_sync_edit_menu(st);
    else if (idx == MENU_IDX_VIEW)
        note_sync_view_menu(st);

    int32_t fx = ui_menubar_item_x(w, idx);
    int32_t fy = w->layout_elem ? (w->layout_elem->computed_y + w->layout_elem->computed_h) : 28;
    ui_ctx_show_popup(st->ui_ctx, st->menus[idx], fx, fy);
}

static void on_popup_menu_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    notepad_state_t *st = (notepad_state_t *)ud;
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
    case ACT_FILE_NEW:
        note_cmd_new(st);
        break;
    case ACT_FILE_OPEN:
        note_cmd_open(st);
        break;
    case ACT_FILE_SAVE:
        note_cmd_save(st);
        break;
    case ACT_FILE_SAVE_AS:
        note_cmd_save_as(st);
        break;
    case ACT_FILE_QUIT:
        note_cmd_quit(st);
        break;
    case ACT_EDIT_CUT:
        note_dispatch_editor_ctrl_key(st, KEY_X);
        break;
    case ACT_EDIT_COPY:
        note_dispatch_editor_ctrl_key(st, KEY_C);
        break;
    case ACT_EDIT_PASTE:
        note_dispatch_editor_ctrl_key(st, KEY_V);
        break;
    case ACT_EDIT_SELECT_ALL:
        ui_widget_set_focus(st->ui_ctx, st->editor);
        ui_text_field_select_all(st->editor);
        break;
    case ACT_VIEW_LINE_NUMS:
        note_toggle_line_numbers(st);
        break;
    case ACT_VIEW_STATUS_BAR:
        note_toggle_statusbar(st);
        break;
    case ACT_VIEW_ZOOM_IN:
        if (st->editor_font_size < 24.0f) {
            st->editor_font_size += 2.0f;
            ui_widget_set_font(st->editor, st->editor_font_size, APP_FONT_REGULAR);
            note_set_status_note(st, "Zoomed in");
        }
        break;
    case ACT_VIEW_ZOOM_OUT:
        if (st->editor_font_size > 10.0f) {
            st->editor_font_size -= 2.0f;
            ui_widget_set_font(st->editor, st->editor_font_size, APP_FONT_REGULAR);
            note_set_status_note(st, "Zoomed out");
        }
        break;
    case ACT_VIEW_ZOOM_RESET:
        st->editor_font_size = app_default_font_size();
        ui_widget_set_font(st->editor, 0.0f, APP_FONT_REGULAR);
        note_set_status_note(st, "Zoom reset");
        break;
    case ACT_HELP_ABOUT:
        note_show_info(st, "About Tsukasa Notepad",
                       "Project Tsukasa Notepad\n"
                       "TrueType text editor on libvanilla-ui\n"
                       "Shortcuts: Ctrl+N/O/S/A/C/X/V/Q");
        break;
    default:
        break;
    }

    if (!ui_ctx_get_modal(st->ui_ctx))
        ui_widget_set_focus(st->ui_ctx, st->editor);
}

static void on_modal_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    notepad_state_t *st = (notepad_state_t *)ud;
    if (!st || !w || !ev || ev->type != UI_EVENT_CLICK)
        return;

    note_modal_mode_t mode = st->modal_mode;
    ui_dialog_result_t res = w->modal.result;
    st->modal_mode = NOTE_MODAL_NONE;

    if (res == UI_DIALOG_RES_OK) {
        if (mode == NOTE_MODAL_OPEN_PATH) {
            const char *path = ui_dialog_get_input(w);
            (void)note_load_file(st, path, 0);
        } else if (mode == NOTE_MODAL_SAVE_AS) {
            const char *path = ui_dialog_get_input(w);
            (void)note_save_to_path(st, path);
        } else if (mode == NOTE_MODAL_CONFIRM_DISCARD) {
            note_pending_action_t act = st->pending_action;
            st->pending_action = NOTE_PENDING_NONE;
            if (act == NOTE_PENDING_NEW) {
                note_new_document(st);
            } else if (act == NOTE_PENDING_OPEN) {
                note_show_prompt(st, NOTE_MODAL_OPEN_PATH,
                                 "Open File", "Enter file path to open:",
                                 st->filename, "Open");
            } else if (act == NOTE_PENDING_LOAD_PATH) {
                (void)note_load_file(st, st->pending_path, 0);
            } else if (act == NOTE_PENDING_QUIT) {
                st->running = 0;
            }
        }
    } else {
        if (mode == NOTE_MODAL_CONFIRM_DISCARD)
            st->pending_action = NOTE_PENDING_NONE;
    }

    if (st->modal_mode == NOTE_MODAL_NONE && st->running)
        ui_widget_set_focus(st->ui_ctx, st->editor);
}

static void note_init_menus(notepad_state_t *st)
{
    static const char *bar_labels[MENU_COUNT] = { "File", "Edit", "View", "Help" };
    st->menubar = ui_menubar(st->ui_ctx, bar_labels, MENU_COUNT, on_menubar_event, st);
    ui_widget_add_child(st->root, st->menubar);

    st->file_items[0] = (ui_menu_item_t){ "New",        "Ctrl+N",       0,                      ACT_FILE_NEW };
    st->file_items[1] = (ui_menu_item_t){ "Open...",    "Ctrl+O",       0,                      ACT_FILE_OPEN };
    st->file_items[2] = (ui_menu_item_t){ NULL,         NULL,           UI_MENU_ITEM_SEPARATOR, 0 };
    st->file_items[3] = (ui_menu_item_t){ "Save",       "Ctrl+S",       0,                      ACT_FILE_SAVE };
    st->file_items[4] = (ui_menu_item_t){ "Save As...", "Ctrl+Shift+S", 0,                      ACT_FILE_SAVE_AS };
    st->file_items[5] = (ui_menu_item_t){ NULL,         NULL,           UI_MENU_ITEM_SEPARATOR, 0 };
    st->file_items[6] = (ui_menu_item_t){ "Quit",       "Ctrl+Q",       0,                      ACT_FILE_QUIT };
    st->menus[MENU_IDX_FILE] = ui_menu_ex(st->ui_ctx, st->file_items, 7, on_popup_menu_event, st);

    st->edit_items[0] = (ui_menu_item_t){ "Cut",        "Ctrl+X", 0,                      ACT_EDIT_CUT };
    st->edit_items[1] = (ui_menu_item_t){ "Copy",       "Ctrl+C", 0,                      ACT_EDIT_COPY };
    st->edit_items[2] = (ui_menu_item_t){ "Paste",      "Ctrl+V", 0,                      ACT_EDIT_PASTE };
    st->edit_items[3] = (ui_menu_item_t){ NULL,         NULL,     UI_MENU_ITEM_SEPARATOR, 0 };
    st->edit_items[4] = (ui_menu_item_t){ "Select All", "Ctrl+A", 0,                      ACT_EDIT_SELECT_ALL };
    st->menus[MENU_IDX_EDIT] = ui_menu_ex(st->ui_ctx, st->edit_items, 5, on_popup_menu_event, st);

    st->view_items[0] = (ui_menu_item_t){ "Line Numbers", "Ctrl+L", UI_MENU_ITEM_CHECKED,   ACT_VIEW_LINE_NUMS };
    st->view_items[1] = (ui_menu_item_t){ "Status Bar",   NULL,     UI_MENU_ITEM_CHECKED,   ACT_VIEW_STATUS_BAR };
    st->view_items[2] = (ui_menu_item_t){ NULL,           NULL,     UI_MENU_ITEM_SEPARATOR, 0 };
    st->view_items[3] = (ui_menu_item_t){ "Zoom In",      NULL,     0,                      ACT_VIEW_ZOOM_IN };
    st->view_items[4] = (ui_menu_item_t){ "Zoom Out",     NULL,     0,                      ACT_VIEW_ZOOM_OUT };
    st->view_items[5] = (ui_menu_item_t){ "Reset Zoom",   NULL,     0,                      ACT_VIEW_ZOOM_RESET };
    st->menus[MENU_IDX_VIEW] = ui_menu_ex(st->ui_ctx, st->view_items, 6, on_popup_menu_event, st);

    st->help_items[0] = (ui_menu_item_t){ "About Notepad", NULL, 0, ACT_HELP_ABOUT };
    st->menus[MENU_IDX_HELP] = ui_menu_ex(st->ui_ctx, st->help_items, 1, on_popup_menu_event, st);
}

static int note_handle_shortcut(notepad_state_t *st, const vanilla_event_t *ev)
{
    const struct input_event *iev = &ev->input;
    if (iev->type != EV_KEY || iev->value != 1)
        return 0;

    int is_ctrl = st->ctrl_down || ((ev->mod_state & MOD_CTRL) != 0);
    int is_shift = st->shift_down || ((ev->mod_state & MOD_SHIFT) != 0);

    if (iev->code == KEY_F2) {
        note_cmd_save(st);
        return 1;
    }

    if (!is_ctrl)
        return 0;

    switch (iev->code) {
    case KEY_N:
        note_cmd_new(st);
        return 1;
    case KEY_O:
        note_cmd_open(st);
        return 1;
    case KEY_S:
        if (is_shift)
            note_cmd_save_as(st);
        else
            note_cmd_save(st);
        return 1;
    case KEY_Q:
        note_cmd_quit(st);
        return 1;
    case KEY_L:
        note_toggle_line_numbers(st);
        return 1;
    default:
        return 0;
    }
}

static int note_poll_timeout_ms(const notepad_state_t *st)
{
    if (!st || !st->win_focused)
        return -1;

    ui_widget_t *fw = ui_get_focused(st->ui_ctx);
    if (!fw || fw->type != UI_WIDGET_TEXT_FIELD || fw->text_field.read_only)
        return -1;

    int64_t now_ms = note_now_ms();
    int64_t rem = 530 - (now_ms % 530);
    if (rem < 50)
        rem = 50;
    return (int)rem;
}

int main(int argc, char **argv)
{
    notepad_state_t *state = (notepad_state_t *)calloc(1, sizeof(notepad_state_t));
    if (!state) {
        fprintf(stderr, "notepad: memory allocation failed\n");
        return 1;
    }

    state->running = 1;
    state->win_focused = 1;
    state->show_line_numbers = 1;
    state->show_statusbar = 1;
    state->editor_font_size = app_default_font_size();
    state->last_line = -1;
    state->last_col = -1;
    state->last_line_count = -1;
    state->last_modified = -1;

    vanilla_client_t *client = NULL;
    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(VANILLA_SOCKET_PATH);
        if (client)
            break;
        poll(NULL, 0, 10);
    }
    if (!client) {
        fprintf(stderr, "notepad: failed to connect to display server\n");
        free(state);
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Notepad", 120, 90,
                                                  NOTE_DEFAULT_W, NOTE_DEFAULT_H,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 260, 180, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    state->ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!state->ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    ui_ctx_set_clipboard_ops(state->ui_ctx, note_clip_set, note_clip_get);

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;

    state->root = ui_box(state->ui_ctx, VDIR_COLUMN);
    state->root->layout_elem->w_mode = VSIZE_GROW;
    state->root->layout_elem->h_mode = VSIZE_GROW;
    state->root->layout_elem->bg_color = bg;

    note_init_menus(state);

    state->editor = ui_text_area(state->ui_ctx, "", 1, on_editor_event, state);
    state->editor->layout_elem->w_mode = VSIZE_GROW;
    state->editor->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(state->root, state->editor);

    state->statusbar = ui_box(state->ui_ctx, VDIR_ROW);
    state->statusbar->layout_elem->w_mode = VSIZE_GROW;
    state->statusbar->layout_elem->h_mode = VSIZE_FIXED;
    state->statusbar->layout_elem->h_px = NOTE_STATUS_H;
    state->statusbar->layout_elem->pad_left = 10;
    state->statusbar->layout_elem->pad_right = 10;
    state->statusbar->layout_elem->gap = 8;
    state->statusbar->layout_elem->align_items = VALIGN_CENTER;
    state->statusbar->layout_elem->bg_color = elev;
    state->statusbar->layout_elem->border_color = bdr;
    state->statusbar->layout_elem->border_width = 1;
    state->statusbar->layout_elem->clip_children = 1;
    ui_widget_add_child(state->root, state->statusbar);

    state->status_left = ui_label_ex(state->ui_ctx, "", fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(state->statusbar, state->status_left);

    ui_widget_t *spacer = ui_box(state->ui_ctx, VDIR_ROW);
    spacer->layout_elem->w_mode = VSIZE_GROW;
    spacer->layout_elem->h_mode = VSIZE_FIXED;
    spacer->layout_elem->h_px = 1;
    ui_widget_add_child(state->statusbar, spacer);

    state->status_right = ui_label_ex(state->ui_ctx, "", fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(state->statusbar, state->status_right);

    state->prompt_modal = ui_dialog_prompt(state->ui_ctx, "", "", "", "OK", "Cancel",
                                           on_modal_event, state);
    state->confirm_modal = ui_dialog_confirm(state->ui_ctx, "", "", "Discard", "Cancel",
                                             on_modal_event, state);
    state->info_modal = ui_modal_dialog(state->ui_ctx, "", "", "OK",
                                        on_modal_event, state);

    if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        (void)note_load_file(state, argv[1], 1);
    else
        note_new_document(state);

    ui_widget_set_focus(state->ui_ctx, state->editor);
    note_update_status_labels(state, 1);

    int client_fd = vanilla_client_get_fd(client);
    int need_present = 1;

    while (state->running) {
        vanilla_event_t ev;
        int poll_rc;
        while ((poll_rc = vanilla_poll_event(client, &ev)) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                if (state->modified && state->modal_mode != NOTE_MODAL_CONFIRM_DISCARD) {
                    note_cmd_quit(state);
                    need_present = 1;
                } else {
                    state->running = 0;
                    break;
                }
            } else if (ev.type == VANILLA_EVENT_FOCUS) {
                state->win_focused = ev.focus.focused ? 1 : 0;
                ui_widget_invalidate(state->editor);
                need_present = 1;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                if (ev.configure.serial != 0)
                    vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state->root);
                need_present = 1;
            } else if (ev.type == VANILLA_EVENT_DND_ENTER) {
                if (strcmp(ev.dnd_enter.mime, "text/uri-list") == 0)
                    dnd_set_accept(win, 1);
                else
                    dnd_set_accept(win, 0);
            } else if (ev.type == VANILLA_EVENT_DND_DROP) {
                if (strcmp(ev.dnd_drop.mime, "text/uri-list") == 0) {
                    const char *path = ev.dnd_drop.data;
                    if (strncmp(path, "file://", 7) == 0)
                        path += 7;
                    snprintf(state->pending_path, sizeof(state->pending_path), "%s", path);
                    note_trim_path(state->pending_path);
                    if (state->pending_path[0] != '\0') {
                        if (state->modified)
                            note_show_discard_confirm(state, NOTE_PENDING_LOAD_PATH);
                        else
                            (void)note_load_file(state, state->pending_path, 0);
                        need_present = 1;
                    }
                    dnd_set_accept(win, 1);
                }
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY) {
                    if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT)
                        state->shift_down = (iev->value != 0);
                    else if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL)
                        state->ctrl_down = (iev->value != 0);
                }

                ui_widget_t *modal_before = ui_ctx_get_modal(state->ui_ctx);
                ui_widget_t *popup_before = ui_ctx_get_popup(state->ui_ctx);
                state->prev_popup = popup_before;

                if (!modal_before && !popup_before && note_handle_shortcut(state, &ev)) {
                    need_present = 1;
                } else {
                    ui_handle_event(state->ui_ctx, state->root, iev);
                }

                ui_widget_t *modal_after = ui_ctx_get_modal(state->ui_ctx);
                ui_widget_t *popup_after = ui_ctx_get_popup(state->ui_ctx);
                if (popup_before != popup_after || modal_before != modal_after)
                    need_present = 1;

                if (!popup_after && state->menubar && state->menubar->menu.selected != -1) {
                    state->menubar->menu.selected = -1;
                    ui_widget_invalidate(state->menubar);
                }

                if (!modal_after && !popup_after && state->running &&
                    ui_get_focused(state->ui_ctx) != state->editor) {
                    ui_widget_set_focus(state->ui_ctx, state->editor);
                }

                note_update_status_labels(state, 0);
            }
        }

        if (!state->running || poll_rc < 0)
            break;

        ui_widget_t *fw = ui_get_focused(state->ui_ctx);
        int caret_blink_due = 0;
        if (state->win_focused && fw && fw->type == UI_WIDGET_TEXT_FIELD && !fw->text_field.read_only) {
            int64_t now_ms = note_now_ms();
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

        int timeout_ms = note_poll_timeout_ms(state);
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret < 0 && errno != EINTR)
            break;
        if (pret > 0 && !(pfd.revents & POLLIN) &&
            (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
    }

    ui_text_field_free(state->editor);
    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    free(state);
    return 0;
}
