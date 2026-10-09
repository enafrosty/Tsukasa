/*
 * Project Tsukasa — Display Server Developer Tools & Window Inspector Implementation
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

#include "inspector.h"
#include "../apps/app_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static vanilla_surface_t *g_render_surf = NULL;
static mu_Rect            g_clip_rect   = { 0, 0, DEVTOOLS_DEFAULT_WIDTH, DEVTOOLS_DEFAULT_HEIGHT };

static uint64_t get_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
    }
    return 0;
#endif
}

static int exact_write_fd(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    while (written < count) {
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                usleep(1000);
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static int exact_read_fd(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;
    while (received < count) {
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                usleep(1000);
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static mu_Rect intersect_rects(mu_Rect r1, mu_Rect r2)
{
    int x1 = r1.x > r2.x ? r1.x : r2.x;
    int y1 = r1.y > r2.y ? r1.y : r2.y;
    int x2 = (r1.x + r1.w) < (r2.x + r2.w) ? (r1.x + r1.w) : (r2.x + r2.w);
    int y2 = (r1.y + r1.h) < (r2.y + r2.h) ? (r1.y + r1.h) : (r2.y + r2.h);
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    mu_Rect res;
    res.x = x1; res.y = y1; res.w = x2 - x1; res.h = y2 - y1;
    return res;
}

void mu_renderer_draw_rect(mu_Rect rect, mu_Color color)
{
    if (!g_render_surf || color.a == 0)
        return;

    mu_Rect clipped = intersect_rects(rect, g_clip_rect);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;

    /* microui is RGBA; project Tsukasa blitter is ARGB */
    uint32_t argb = ((uint32_t)color.a << 24) |
                    ((uint32_t)color.r << 16) |
                    ((uint32_t)color.g << 8)  |
                    ((uint32_t)color.b);

    if (color.a == 255) {
        app_fill_rect(g_render_surf, clipped.x, clipped.y, clipped.w, clipped.h, argb);
        return;
    }

    /* Alpha blend into target surface */
    uint32_t alpha = color.a;
    uint32_t inv_a = 255 - alpha;
    uint32_t r = color.r;
    uint32_t g = color.g;
    uint32_t b = color.b;
    uint32_t pitch_px = g_render_surf->pitch / sizeof(uint32_t);

    for (int cy = clipped.y; cy < clipped.y + clipped.h; cy++) {
        uint32_t *row = g_render_surf->pixels + (size_t)cy * pitch_px;
        for (int cx = clipped.x; cx < clipped.x + clipped.w; cx++) {
            uint32_t dst = row[cx];
            uint32_t dr = (dst >> 16) & 0xFF;
            uint32_t dg = (dst >> 8)  & 0xFF;
            uint32_t db = dst & 0xFF;
            uint32_t out_r = (r * alpha + dr * inv_a) / 255;
            uint32_t out_g = (g * alpha + dg * inv_a) / 255;
            uint32_t out_b = (b * alpha + db * inv_a) / 255;
            row[cx] = 0xFF000000u | (out_r << 16) | (out_g << 8) | out_b;
        }
    }
}

void mu_renderer_draw_text(const char *text, mu_Vec2 pos, mu_Color color)
{
    if (!g_render_surf || !text)
        return;

    uint32_t argb = ((uint32_t)color.a << 24) |
                    ((uint32_t)color.r << 16) |
                    ((uint32_t)color.g << 8)  |
                    ((uint32_t)color.b);

    int cur_x = pos.x;
    for (const char *p = text; *p; p++) {
        if (cur_x + 8 >= g_clip_rect.x && cur_x < g_clip_rect.x + g_clip_rect.w &&
            pos.y + 8 >= g_clip_rect.y && pos.y < g_clip_rect.y + g_clip_rect.h) {
            app_draw_char(g_render_surf, cur_x, pos.y, *p, argb);
        }
        cur_x += 8;
    }
}

void mu_renderer_draw_icon(int icon, mu_Rect rect, mu_Color color)
{
    if (!g_render_surf) return;
    char glyph = ' ';
    switch (icon) {
    case MU_ICON_CLOSE:     glyph = 'x'; break;
    case MU_ICON_CHECK:     glyph = 'v'; break;
    case MU_ICON_COLLAPSED: glyph = '>'; break;
    case MU_ICON_EXPANDED:  glyph = 'v'; break;
    default:                glyph = '*'; break;
    }
    int gx = rect.x + (rect.w - 8) / 2;
    int gy = rect.y + (rect.h - 8) / 2;
    uint32_t argb = ((uint32_t)color.a << 24) |
                    ((uint32_t)color.r << 16) |
                    ((uint32_t)color.g << 8)  |
                    ((uint32_t)color.b);
    app_draw_char(g_render_surf, gx, gy, glyph, argb);
}

int mu_renderer_get_text_width(const char *text, int len)
{
    if (!text) return 0;
    int actual_len = len < 0 ? (int)strlen(text) : len;
    return actual_len * 8;
}

int mu_renderer_get_text_height(void)
{
    return 10;
}

static int mu_cb_text_width(mu_Font font, const char *text, int len)
{
    (void)font;
    return mu_renderer_get_text_width(text, len);
}

static int mu_cb_text_height(mu_Font font)
{
    (void)font;
    return mu_renderer_get_text_height();
}

static int dbg_connect_server(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VANILLA_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t hello_hdr;
    vanilla_msg_hello_t hello_msg;
    hello_hdr.magic = VANILLA_IPC_MAGIC;
    hello_hdr.msg_type = MSG_HELLO;
    hello_hdr.payload_len = (uint16_t)sizeof(hello_msg);
    hello_hdr.window_id = 0;

    memset(&hello_msg, 0, sizeof(hello_msg));
    hello_msg.client_version = VANILLA_IPC_VERSION;
    strncpy(hello_msg.client_name, "devtools", sizeof(hello_msg.client_name) - 1);

    if (exact_write_fd(fd, &hello_hdr, sizeof(hello_hdr)) < 0 ||
        exact_write_fd(fd, &hello_msg, sizeof(hello_msg)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack_msg;
    if (exact_read_fd(fd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_read_fd(fd, &ack_msg, sizeof(ack_msg)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void dbg_query_windows(devtools_state_t *st)
{
    if (st->dbg_fd < 0)
        return;

    vanilla_msg_hdr_t qhdr;
    vanilla_msg_debug_query_t qpayload;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(qpayload);
    qhdr.window_id = 0;
    qpayload.query_type = DBGQ_WINDOWS;

    if (exact_write_fd(st->dbg_fd, &qhdr, sizeof(qhdr)) < 0 ||
        exact_write_fd(st->dbg_fd, &qpayload, sizeof(qpayload)) < 0)
        return;

    vanilla_msg_hdr_t rhdr;
    vanilla_msg_debug_query_resp_t rpayload;
    if (exact_read_fd(st->dbg_fd, &rhdr, sizeof(rhdr)) < 0 ||
        exact_read_fd(st->dbg_fd, &rpayload, sizeof(rpayload)) < 0)
        return;

    uint32_t count = rpayload.window_count;
    if (count > VANILLA_MAX_WINDOWS)
        count = VANILLA_MAX_WINDOWS;

    vanilla_dbg_window_t records[VANILLA_MAX_WINDOWS];
    if (count > 0) {
        if (exact_read_fd(st->dbg_fd, records, count * sizeof(vanilla_dbg_window_t)) < 0)
            return;
    }

    uint64_t now = get_now_ms();
    uint64_t delta_ms = (now > st->last_query_time_ms) ? (now - st->last_query_time_ms) : DEVTOOLS_QUERY_INTERVAL;
    if (delta_ms == 0) delta_ms = 1;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t prev_frames = 0;
        for (uint32_t j = 0; j < st->window_count; j++) {
            if (st->windows[j].window_id == records[i].window_id) {
                prev_frames = st->windows[j].frame_count;
                break;
            }
        }
        st->windows[i].window_id = records[i].window_id;
        st->windows[i].pid = records[i].pid;
        strncpy(st->windows[i].title, records[i].title, sizeof(st->windows[i].title) - 1);
        st->windows[i].title[sizeof(st->windows[i].title) - 1] = '\0';
        st->windows[i].x = records[i].x;
        st->windows[i].y = records[i].y;
        st->windows[i].w = records[i].w;
        st->windows[i].h = records[i].h;
        st->windows[i].z_index = records[i].z_index;
        st->windows[i].layer = records[i].layer;
        st->windows[i].is_mapped = records[i].is_mapped;
        st->windows[i].is_focused = records[i].is_focused;
        st->windows[i].damage_x = records[i].damage_x;
        st->windows[i].damage_y = records[i].damage_y;
        st->windows[i].damage_w = records[i].damage_w;
        st->windows[i].damage_h = records[i].damage_h;
        st->windows[i].prev_frame_count = prev_frames;
        st->windows[i].frame_count = records[i].frame_count;

        uint32_t diff = records[i].frame_count >= prev_frames ? (records[i].frame_count - prev_frames) : 0;
        st->windows[i].fps = (uint32_t)((uint64_t)diff * 1000ULL / delta_ms);
    }
    st->window_count = count;
    st->last_query_time_ms = now;
}

static void dbg_query_compositor(devtools_state_t *st)
{
    if (st->dbg_fd < 0)
        return;

    vanilla_msg_hdr_t qhdr;
    vanilla_msg_debug_query_t qpayload;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(qpayload);
    qhdr.window_id = 0;
    qpayload.query_type = DBGQ_COMPOSITOR;

    if (exact_write_fd(st->dbg_fd, &qhdr, sizeof(qhdr)) < 0 ||
        exact_write_fd(st->dbg_fd, &qpayload, sizeof(qpayload)) < 0)
        return;

    vanilla_msg_hdr_t rhdr;
    vanilla_msg_debug_query_resp_t rpayload;
    if (exact_read_fd(st->dbg_fd, &rhdr, sizeof(rhdr)) < 0 ||
        exact_read_fd(st->dbg_fd, &rpayload, sizeof(rpayload)) < 0)
        return;

    if (exact_read_fd(st->dbg_fd, &st->comp_stats, sizeof(st->comp_stats)) < 0)
        return;
}

static void dbg_query_theme(devtools_state_t *st)
{
    if (st->dbg_fd < 0)
        return;

    vanilla_msg_hdr_t qhdr;
    vanilla_msg_debug_query_t qpayload;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(qpayload);
    qhdr.window_id = 0;
    qpayload.query_type = DBGQ_THEME;

    if (exact_write_fd(st->dbg_fd, &qhdr, sizeof(qhdr)) < 0 ||
        exact_write_fd(st->dbg_fd, &qpayload, sizeof(qpayload)) < 0)
        return;

    vanilla_msg_hdr_t rhdr;
    vanilla_msg_debug_query_resp_t rpayload;
    if (exact_read_fd(st->dbg_fd, &rhdr, sizeof(rhdr)) < 0 ||
        exact_read_fd(st->dbg_fd, &rpayload, sizeof(rpayload)) < 0)
        return;

    vanilla_theme_t th;
    if (exact_read_fd(st->dbg_fd, &th, sizeof(th)) == 0) {
        st->active_theme = th;
        if (!st->theme_loaded) {
            st->original_theme = th;
            st->theme_loaded = 1;
        }
    }
}

static void dbg_set_theme(devtools_state_t *st, const vanilla_theme_t *th)
{
    if (st->dbg_fd < 0 || !th)
        return;

    uint8_t buf[sizeof(vanilla_msg_debug_query_t) + sizeof(vanilla_theme_t)];
    vanilla_msg_debug_query_t *q = (vanilla_msg_debug_query_t *)buf;
    q->query_type = DBGQ_SET_THEME;
    memcpy(buf + sizeof(vanilla_msg_debug_query_t), th, sizeof(vanilla_theme_t));

    vanilla_msg_hdr_t qhdr;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(buf);
    qhdr.window_id = 0;

    if (exact_write_fd(st->dbg_fd, &qhdr, sizeof(qhdr)) < 0 ||
        exact_write_fd(st->dbg_fd, buf, sizeof(buf)) < 0)
        return;

    vanilla_msg_hdr_t rhdr;
    vanilla_msg_debug_query_resp_t rpayload;
    if (exact_read_fd(st->dbg_fd, &rhdr, sizeof(rhdr)) < 0 ||
        exact_read_fd(st->dbg_fd, &rpayload, sizeof(rpayload)) < 0)
        return;
}

static void dbg_revert_theme(devtools_state_t *st)
{
    if (st->dbg_fd < 0)
        return;

    vanilla_msg_hdr_t qhdr;
    vanilla_msg_debug_query_t qpayload;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(qpayload);
    qhdr.window_id = 0;
    qpayload.query_type = DBGQ_THEME_RELOAD;

    if (exact_write_fd(st->dbg_fd, &qhdr, sizeof(qhdr)) < 0 ||
        exact_write_fd(st->dbg_fd, &qpayload, sizeof(qpayload)) < 0)
        return;

    vanilla_msg_hdr_t rhdr;
    vanilla_msg_debug_query_resp_t rpayload;
    if (exact_read_fd(st->dbg_fd, &rhdr, sizeof(rhdr)) < 0 ||
        exact_read_fd(st->dbg_fd, &rpayload, sizeof(rpayload)) < 0)
        return;

    dbg_query_theme(st);
}

static const char *layer_name(uint8_t l)
{
    switch (l) {
    case 0: return "BG";
    case 1: return "NRM";
    case 2: return "TOP";
    case 3: return "OVL";
    default: return "UNK";
    }
}

static void render_inspector_panel(devtools_state_t *st)
{
    mu_Context *ctx = &st->mu_ctx;
    char text[128];

    mu_layout_row(ctx, 1, (int[]){ -1 }, 22);
    snprintf(text, sizeof(text), "Window Inspector (%u Windows) - Live 500ms Query [F12=Perf HUD]",
             st->window_count);
    mu_label(ctx, text);

    int col_widths[] = { 50, 45, 175, 145, 45, 45, 45, 45 };
    mu_layout_row(ctx, 8, col_widths, 20);
    mu_label(ctx, "ID");
    mu_label(ctx, "PID");
    mu_label(ctx, "Title");
    mu_label(ctx, "Geometry");
    mu_label(ctx, "Lyr");
    mu_label(ctx, "Map");
    mu_label(ctx, "Foc");
    mu_label(ctx, "FPS");

    if (st->window_count == 0) {
        mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
        mu_label(ctx, "No windows connected.");
        return;
    }

    mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
    mu_begin_panel(ctx, "inspector_table_panel");

    for (uint32_t i = 0; i < st->window_count; i++) {
        const devtools_win_entry_t *w = &st->windows[i];
        mu_layout_row(ctx, 8, col_widths, 20);

        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%04u", w->window_id);
        mu_label(ctx, id_str);

        char pid_str[16];
        if (w->pid >= 0)
            snprintf(pid_str, sizeof(pid_str), "%d", w->pid);
        else
            strncpy(pid_str, "-1", sizeof(pid_str));
        mu_label(ctx, pid_str);

        mu_label(ctx, w->title[0] ? w->title : "(untitled)");

        char geo_str[48];
        snprintf(geo_str, sizeof(geo_str), "%d,%d %ux%u", w->x, w->y, w->w, w->h);
        mu_label(ctx, geo_str);

        mu_label(ctx, layer_name(w->layer));
        mu_label(ctx, w->is_mapped ? "yes" : "no");
        mu_label(ctx, w->is_focused ? "yes" : "no");

        char fps_str[16];
        snprintf(fps_str, sizeof(fps_str), "%u", w->fps);
        mu_label(ctx, fps_str);
    }

    mu_end_panel(ctx);
}

static void render_perf_panel(devtools_state_t *st)
{
    mu_Context *ctx = &st->mu_ctx;
    char text[128];

    mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
    mu_label(ctx, "Compositor Performance & Timing Metrics");

    mu_layout_row(ctx, 2, (int[]){ 200, -1 }, 22);

    mu_label(ctx, "Last Frame Time:");
    float ms = (float)st->comp_stats.frame_time_us / 1000.0f;
    snprintf(text, sizeof(text), "%.2f ms (%u us)", ms, st->comp_stats.frame_time_us);
    mu_label(ctx, text);

    mu_label(ctx, "Dirty Rectangles:");
    snprintf(text, sizeof(text), "%u rects", st->comp_stats.dirty_rect_count);
    mu_label(ctx, text);

    mu_label(ctx, "Damage Area:");
    snprintf(text, sizeof(text), "%u pixels", st->comp_stats.damage_area_px);
    mu_label(ctx, text);

    mu_label(ctx, "Connected Clients:");
    snprintf(text, sizeof(text), "%u", st->comp_stats.client_count);
    mu_label(ctx, text);

    mu_label(ctx, "Managed Windows:");
    snprintf(text, sizeof(text), "%u", st->comp_stats.window_count);
    mu_label(ctx, text);

    mu_layout_row(ctx, 1, (int[]){ -1 }, 30);
    mu_label(ctx, "--------------------------------------------------------");

    mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
    mu_label(ctx, "Press F12 at any time to toggle the transparent heads-up");
    mu_layout_row(ctx, 1, (int[]){ -1 }, 24);
    mu_label(ctx, "performance overlay rendered directly on the compositor screen.");
}

typedef struct {
    const char *name;
    uint32_t   *ptr;
} theme_token_entry_t;

static void render_theme_panel(devtools_state_t *st)
{
    mu_Context *ctx = &st->mu_ctx;
    vanilla_theme_t *t = &st->active_theme;

    mu_layout_row(ctx, 1, (int[]){ -1 }, 22);
    mu_label(ctx, "Live Theme Editor - Modify Design Tokens in Memory");

    if (st->status_msg[0]) {
        mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
        mu_label(ctx, st->status_msg);
    }

    mu_layout_row(ctx, 3, (int[]){ 140, 140, 140 }, 28);
    if (mu_button(ctx, "Apply Theme")) {
        dbg_set_theme(st, &st->active_theme);
        snprintf(st->status_msg, sizeof(st->status_msg), "Theme applied live.");
    }
    if (mu_button(ctx, "Save Theme")) {
        dbg_set_theme(st, &st->active_theme);
        st->theme_saved = 1;
        snprintf(st->status_msg, sizeof(st->status_msg), "Theme saved for session.");
    }
    if (mu_button(ctx, "Revert Theme")) {
        dbg_revert_theme(st);
        st->theme_saved = 0;
        snprintf(st->status_msg, sizeof(st->status_msg), "Theme reverted to boot defaults.");
    }

    theme_token_entry_t tokens[] = {
        { "bg_base",               &t->bg_base },
        { "bg_elevated",           &t->bg_elevated },
        { "bg_overlay",            &t->bg_overlay },
        { "fg_primary",            &t->fg_primary },
        { "fg_muted",              &t->fg_muted },
        { "fg_dim",                &t->fg_dim },
        { "border",                &t->border },
        { "border_focus",          &t->border_focus },
        { "accent",                &t->accent },
        { "accent_hover",          &t->accent_hover },
        { "accent_pressed",        &t->accent_pressed },
        { "selection",             &t->selection },
        { "success",               &t->success },
        { "warning",               &t->warning },
        { "danger",                &t->danger },
        { "titlebar_active",       &t->titlebar_active },
        { "titlebar_inactive",     &t->titlebar_inactive },
        { "titlebar_text_active",  &t->titlebar_text_active },
        { "titlebar_text_inactive",&t->titlebar_text_inactive },
        { "titlebar_btn_bg",       &t->titlebar_btn_bg },
        { "titlebar_btn_bg_hover", &t->titlebar_btn_bg_hover },
        { "titlebar_btn_icon",     &t->titlebar_btn_icon },
        { "taskbar_bg",            &t->taskbar_bg },
        { "taskbar_item_active",   &t->taskbar_item_active },
        { "taskbar_item_open",     &t->taskbar_item_open },
        { "taskbar_text",          &t->taskbar_text },
    };
    size_t num_tokens = sizeof(tokens) / sizeof(tokens[0]);

    mu_layout_row(ctx, 1, (int[]){ -1 }, -1);
    mu_begin_panel(ctx, "theme_tokens_panel");

    int changed = 0;
    for (size_t i = 0; i < num_tokens; i++) {
        uint32_t orig_color = *tokens[i].ptr;
        uint32_t a = (orig_color >> 24) & 0xFF;
        mu_Real r = (orig_color >> 16) & 0xFF;
        mu_Real g = (orig_color >> 8)  & 0xFF;
        mu_Real b = orig_color & 0xFF;

        mu_layout_row(ctx, 5, (int[]){ 160, 36, 85, 85, 85 }, 22);
        mu_label(ctx, tokens[i].name);

        mu_Rect swatch = mu_layout_next(ctx);
        mu_draw_rect(ctx, swatch, mu_color((int)r, (int)g, (int)b, 255));

        mu_push_id(ctx, tokens[i].name, strlen(tokens[i].name));

        mu_push_id(ctx, "r", 1);
        if (mu_slider(ctx, &r, 0, 255) & MU_RES_CHANGE)
            changed = 1;
        mu_pop_id(ctx);

        mu_push_id(ctx, "g", 1);
        if (mu_slider(ctx, &g, 0, 255) & MU_RES_CHANGE)
            changed = 1;
        mu_pop_id(ctx);

        mu_push_id(ctx, "b", 1);
        if (mu_slider(ctx, &b, 0, 255) & MU_RES_CHANGE)
            changed = 1;
        mu_pop_id(ctx);

        mu_pop_id(ctx);

        uint32_t new_color = ((uint32_t)a << 24) |
                             (((uint32_t)r & 0xFF) << 16) |
                             (((uint32_t)g & 0xFF) << 8) |
                             ((uint32_t)b & 0xFF);
        if (new_color != orig_color) {
            *tokens[i].ptr = new_color;
            changed = 1;
        }
    }

    mu_end_panel(ctx);

    if (changed) {
        dbg_set_theme(st, &st->active_theme);
        st->theme_saved = 0;
        snprintf(st->status_msg, sizeof(st->status_msg), "Theme modified (live preview, unsaved)");
    }
}

static void render_devtools_ui(devtools_state_t *st)
{
    mu_Context *ctx = &st->mu_ctx;
    mu_begin(ctx);

    if (mu_begin_window_ex(ctx, "Developer Tools", mu_rect(0, 0, st->win->width, st->win->height),
                           MU_OPT_NOFRAME | MU_OPT_NORESIZE | MU_OPT_NOTITLE)) {
        mu_layout_row(ctx, 3, (int[]){ 160, 160, 160 }, 28);
        if (mu_button(ctx, "1. Window Inspector")) st->active_panel = DEVTOOLS_PANEL_INSPECTOR;
        if (mu_button(ctx, "2. Performance Stats")) st->active_panel = DEVTOOLS_PANEL_PERF;
        if (mu_button(ctx, "3. Theme Editor")) st->active_panel = DEVTOOLS_PANEL_THEME;

        mu_layout_row(ctx, 1, (int[]){ -1 }, 4);
        mu_layout_next(ctx);

        switch (st->active_panel) {
        case DEVTOOLS_PANEL_INSPECTOR:
            render_inspector_panel(st);
            break;
        case DEVTOOLS_PANEL_PERF:
            render_perf_panel(st);
            break;
        case DEVTOOLS_PANEL_THEME:
            render_theme_panel(st);
            break;
        }

        mu_end_window(ctx);
    }

    mu_end(ctx);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    devtools_state_t st;
    memset(&st, 0, sizeof(st));

    st.client = vanilla_connect(NULL);
    if (!st.client) {
        fprintf(stderr, "[devtools] Failed to connect to Vanilla display server\n");
        return 1;
    }

    st.win = vanilla_create_window(st.client, "Developer Tools", 80, 80,
                                   DEVTOOLS_DEFAULT_WIDTH, DEVTOOLS_DEFAULT_HEIGHT,
                                   WINDOW_FLAG_RESIZABLE);
    if (!st.win) {
        vanilla_disconnect(st.client);
        return 1;
    }
    vanilla_map_window(st.win);

    st.dbg_fd = dbg_connect_server();
    if (st.dbg_fd < 0) {
        fprintf(stderr, "[devtools] Warning: failed to connect to debug endpoint\n");
    }

    mu_init(&st.mu_ctx);
    st.mu_ctx.text_width = mu_cb_text_width;
    st.mu_ctx.text_height = mu_cb_text_height;

    st.surface = &st.win->surface;
    st.running = 1;
    st.active_panel = DEVTOOLS_PANEL_INSPECTOR;

    dbg_query_theme(&st);
    dbg_query_windows(&st);
    dbg_query_compositor(&st);

    int mouse_x = 0;
    int mouse_y = 0;
    uint64_t last_query_tick = get_now_ms();

    while (st.running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(st.client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                st.running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                vanilla_ack_configure(st.client, ev.window_id, ev.configure.serial);
                st.surface = &st.win->surface;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                if (ev.input.type == EV_REL) {
                    if (ev.input.code == REL_X) mouse_x += ev.input.value;
                    if (ev.input.code == REL_Y) mouse_y += ev.input.value;
                    if (ev.input.code == REL_WHEEL)
                        mu_input_scroll(&st.mu_ctx, 0, ev.input.value * -20);
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_x >= (int)st.win->width) mouse_x = (int)st.win->width - 1;
                    if (mouse_y >= (int)st.win->height) mouse_y = (int)st.win->height - 1;
                    mu_input_mousemove(&st.mu_ctx, mouse_x, mouse_y);
                } else if (ev.input.type == EV_KEY) {
                    if (ev.input.code == BTN_LEFT) {
                        if (ev.input.value)
                            mu_input_mousedown(&st.mu_ctx, mouse_x, mouse_y, MU_MOUSE_LEFT);
                        else
                            mu_input_mouseup(&st.mu_ctx, mouse_x, mouse_y, MU_MOUSE_LEFT);
                    } else if (ev.input.code == BTN_RIGHT) {
                        if (ev.input.value)
                            mu_input_mousedown(&st.mu_ctx, mouse_x, mouse_y, MU_MOUSE_RIGHT);
                        else
                            mu_input_mouseup(&st.mu_ctx, mouse_x, mouse_y, MU_MOUSE_RIGHT);
                    } else if (ev.input.code == KEY_BACKSPACE) {
                        if (ev.input.value) mu_input_keydown(&st.mu_ctx, MU_KEY_BACKSPACE);
                        else mu_input_keyup(&st.mu_ctx, MU_KEY_BACKSPACE);
                    } else if (ev.input.code == KEY_ENTER || ev.input.code == KEY_KPENTER) {
                        if (ev.input.value) mu_input_keydown(&st.mu_ctx, MU_KEY_RETURN);
                        else mu_input_keyup(&st.mu_ctx, MU_KEY_RETURN);
                    }
                }
            }
        }

        uint64_t now = get_now_ms();
        if (now - last_query_tick >= DEVTOOLS_QUERY_INTERVAL) {
            dbg_query_windows(&st);
            dbg_query_compositor(&st);
            last_query_tick = now;
        }

        /* Build immediate-mode UI */
        render_devtools_ui(&st);

        /* Render microui commands to surface */
        g_render_surf = st.surface;
        g_clip_rect = mu_rect(0, 0, st.win->width, st.win->height);

        /* Fill window base background */
        app_fill_rect(st.surface, 0, 0, st.win->width, st.win->height, 0xFF242933u);

        mu_Command *cmd = NULL;
        while (mu_next_command(&st.mu_ctx, &cmd)) {
            switch (cmd->type) {
            case MU_COMMAND_TEXT:
                mu_renderer_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color);
                break;
            case MU_COMMAND_RECT:
                mu_renderer_draw_rect(cmd->rect.rect, cmd->rect.color);
                break;
            case MU_COMMAND_ICON:
                mu_renderer_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
                break;
            case MU_COMMAND_CLIP:
                g_clip_rect = intersect_rects(cmd->clip.rect,
                                              mu_rect(0, 0, st.win->width, st.win->height));
                break;
            default:
                break;
            }
        }

        vanilla_present(st.win, NULL);
        usleep(16000);
    }

    if (st.theme_loaded && !st.theme_saved) {
        dbg_revert_theme(&st);
    }

    if (st.dbg_fd >= 0)
        close(st.dbg_fd);
    vanilla_destroy_window(st.win);
    vanilla_disconnect(st.client);
    return 0;
}
