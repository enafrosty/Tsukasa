/*
 * Project Tsukasa — libwidget: retained-mode widget kit (guide 16)
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

#include "../include/libwidget.h"

#include <stddef.h>

static int str_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

void ui_gradient_init(ui_gradient_t *g, bool horizontal)
{
    if (!g) return;
    g->horizontal = horizontal;
    g->stop_count = 0;
}

int ui_gradient_add_stop(ui_gradient_t *g, int pos_pct, uint32_t color)
{
    if (!g || g->stop_count >= UI_GRADIENT_MAX_STOPS) return -1;
    if (pos_pct < 0) pos_pct = 0;
    if (pos_pct > 100) pos_pct = 100;
    {
        int i = g->stop_count++;
        while (i > 0 && g->stops[i - 1].pos_pct > pos_pct) {
            g->stops[i] = g->stops[i - 1];
            i--;
        }
        g->stops[i].pos_pct = pos_pct;
        g->stops[i].color = color;
    }
    return 0;
}

static uint32_t lerp_channel(uint32_t a, uint32_t b, int num, int den)
{
    if (den <= 0) return a;
    return a + (uint32_t)(((int)(b - a) * num) / den);
}

uint32_t ui_gradient_sample(const ui_gradient_t *g, int pos_pct)
{
    const ui_gradient_stop_t *lo;
    const ui_gradient_stop_t *hi;
    int i, span, off;
    uint32_t aa, ar, ag, ab, ba, br, bg, bb;

    if (!g || g->stop_count == 0) return 0xFF000000u;
    if (pos_pct < 0) pos_pct = 0;
    if (pos_pct > 100) pos_pct = 100;
    if (g->stop_count == 1 || pos_pct <= g->stops[0].pos_pct)
        return g->stops[0].color;
    if (pos_pct >= g->stops[g->stop_count - 1].pos_pct)
        return g->stops[g->stop_count - 1].color;

    lo = &g->stops[0];
    hi = &g->stops[g->stop_count - 1];
    for (i = 0; i + 1 < g->stop_count; i++) {
        if (pos_pct >= g->stops[i].pos_pct && pos_pct <= g->stops[i + 1].pos_pct) {
            lo = &g->stops[i];
            hi = &g->stops[i + 1];
            break;
        }
    }
    span = hi->pos_pct - lo->pos_pct;
    off = pos_pct - lo->pos_pct;

    aa = (lo->color >> 24) & 0xFF; ba = (hi->color >> 24) & 0xFF;
    ar = (lo->color >> 16) & 0xFF; br = (hi->color >> 16) & 0xFF;
    ag = (lo->color >> 8) & 0xFF;  bg = (hi->color >> 8) & 0xFF;
    ab = lo->color & 0xFF;         bb = hi->color & 0xFF;

    return (lerp_channel(aa, ba, off, span) << 24) |
           (lerp_channel(ar, br, off, span) << 16) |
           (lerp_channel(ag, bg, off, span) << 8) |
           lerp_channel(ab, bb, off, span);
}

void widget_context_init(widget_context_t *ctx,
                         void *user_data,
                         void (*draw_rect)(void *user_data, int x, int y, int w, int h, uint32_t color),
                         void (*draw_rounded_rect_filled)(void *user_data, int x, int y, int w, int h, int radius, uint32_t color),
                         void (*draw_string)(void *user_data, int x, int y, const char *str, uint32_t color),
                         int (*measure_string_width)(void *user_data, const char *str),
                         void (*mark_dirty)(void *user_data, int x, int y, int w, int h),
                         bool use_light_theme)
{
    if (!ctx) return;
    ctx->user_data = user_data;
    ctx->draw_rect = draw_rect;
    ctx->draw_rounded_rect_filled = draw_rounded_rect_filled;
    ctx->draw_string = draw_string;
    ctx->measure_string_width = measure_string_width;
    ctx->mark_dirty = mark_dirty;
    ctx->use_light_theme = use_light_theme;
    ctx->draw_line = 0;
    ctx->fill_ellipse = 0;
    ctx->draw_gradient = 0;
}

void widget_context_set_painter_ex(widget_context_t *ctx,
                                   void (*draw_line)(void *, int, int, int, int, uint32_t),
                                   void (*fill_ellipse)(void *, int, int, int, int, uint32_t),
                                   void (*draw_gradient)(void *, int, int, int, int, const ui_gradient_t *))
{
    if (!ctx) return;
    ctx->draw_line = draw_line;
    ctx->fill_ellipse = fill_ellipse;
    ctx->draw_gradient = draw_gradient;
}

/* widget base + tree */

void ui_widget_base_init(ui_widget_t *w, ui_widget_type_t type,
                         int x, int y, int width, int height)
{
    int i;
    if (!w) return;
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    w->pref_w = width;
    w->pref_h = height;
    w->visible = true;
    w->type = type;
    w->parent = 0;
    for (i = 0; i < UI_WIDGET_MAX_CHILDREN; i++)
        w->children[i] = 0;
    w->child_count = 0;
    w->draw = 0;
    w->handle_event = 0;
    for (i = 0; i < UI_SIGNAL_SLOTS; i++)
        w->slots[i].in_use = false;
    w->user_data = 0;
}

int ui_widget_add_child(ui_widget_t *parent, ui_widget_t *child)
{
    if (!parent || !child || parent->child_count >= UI_WIDGET_MAX_CHILDREN)
        return -1;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return 0;
}

void ui_widget_set_visible(ui_widget_t *w, bool visible)
{
    if (w) w->visible = visible;
}

void ui_widget_draw_tree(ui_widget_t *w, widget_context_t *ctx)
{
    int i;
    if (!w || !w->visible) return;
    if (w->draw) w->draw(w, ctx);
    for (i = 0; i < w->child_count; i++)
        ui_widget_draw_tree(w->children[i], ctx);
}

static bool point_in(const ui_widget_t *w, int px, int py)
{
    return px >= w->x && px < w->x + w->w &&
           py >= w->y && py < w->y + w->h;
}

/* Deepest visible widget under the point; topmost sibling (highest index) wins — NTK's find_widget_at shape. */
static bool dispatch_pointer(ui_widget_t *w, const ui_tk_event_t *ev)
{
    int i;
    if (!w || !w->visible) return false;
    for (i = w->child_count - 1; i >= 0; i--) {
        ui_widget_t *c = w->children[i];
        if (c && c->visible && point_in(c, ev->x, ev->y))
            if (dispatch_pointer(c, ev))
                return true;
    }
    if (point_in(w, ev->x, ev->y) && w->handle_event)
        return w->handle_event(w, ev, w->user_data);
    return false;
}

static bool dispatch_key(ui_widget_t *w, const ui_tk_event_t *ev)
{
    int i;
    if (!w || !w->visible) return false;
    if (w->handle_event && w->handle_event(w, ev, w->user_data))
        return true;
    for (i = 0; i < w->child_count; i++)
        if (dispatch_key(w->children[i], ev))
            return true;
    return false;
}

bool ui_widget_dispatch(ui_widget_t *root, const ui_tk_event_t *ev)
{
    if (!root || !ev) return false;
    if (ev->type == UI_TK_EV_POINTER)
        return dispatch_pointer(root, ev);
    if (ev->type == UI_TK_EV_KEY)
        return dispatch_key(root, ev);
    return false;
}

int ui_signal_connect(ui_widget_t *w, ui_signal_t sig, ui_signal_fn fn, void *userdata)
{
    int i;
    if (!w || !fn || sig >= UI_SIGNAL_COUNT) return -1;
    for (i = 0; i < UI_SIGNAL_SLOTS; i++) {
        if (!w->slots[i].in_use) {
            w->slots[i].in_use = true;
            w->slots[i].sig = sig;
            w->slots[i].fn = fn;
            w->slots[i].userdata = userdata;
            return 0;
        }
    }
    return -1;
}

int ui_signal_disconnect(ui_widget_t *w, ui_signal_t sig, ui_signal_fn fn)
{
    int i;
    if (!w || !fn) return -1;
    for (i = 0; i < UI_SIGNAL_SLOTS; i++) {
        if (w->slots[i].in_use && w->slots[i].sig == sig && w->slots[i].fn == fn) {
            w->slots[i].in_use = false;
            return 0;
        }
    }
    return -1;
}

int ui_signal_emit(ui_widget_t *w, ui_signal_t sig)
{
    int i;
    int fired = 0;
    if (!w) return 0;
    /* Slot order IS registration order (connect always takes the first free slot and disconnect frees it; a... */
    for (i = 0; i < UI_SIGNAL_SLOTS; i++) {
        if (w->slots[i].in_use && w->slots[i].sig == sig) {
            w->slots[i].fn(w, w->slots[i].userdata);
            fired++;
        }
    }
    return fired;
}

static void box_place_child(ui_box_t *box, ui_widget_t *c, bool fill,
                            int main_pos, int main_size)
{
    /* Main axis: the allocated cell is [main_pos, main_pos+main_size). */
    if (box->orientation == UI_HORIZONTAL) {
        c->h = box->base.h;
        c->y = box->base.y;
        if (fill) {
            c->x = main_pos;
            c->w = main_size;
        } else {
            c->w = c->pref_w < main_size ? c->pref_w : main_size;
            c->x = main_pos + (main_size - c->w) / 2;
        }
    } else {
        c->w = box->base.w;
        c->x = box->base.x;
        if (fill) {
            c->y = main_pos;
            c->h = main_size;
        } else {
            c->h = c->pref_h < main_size ? c->pref_h : main_size;
            c->y = main_pos + (main_size - c->h) / 2;
        }
    }
}

void ui_box_relayout(ui_box_t *box)
{
    int n, i, axis, cursor;
    int fixed = 0, expanders = 0, padding_total = 0;
    int leftover, share, extra;

    if (!box) return;
    n = box->base.child_count;
    if (n == 0) return;

    axis = (box->orientation == UI_HORIZONTAL) ? box->base.w : box->base.h;

    for (i = 0; i < n; i++) {
        ui_widget_t *c = box->base.children[i];
        int nat = (box->orientation == UI_HORIZONTAL) ? c->pref_w : c->pref_h;
        padding_total += 2 * box->pack[i].padding;
        if (box->homogeneous || box->pack[i].expand)
            expanders++;
        else
            fixed += nat;
    }
    padding_total += box->spacing * (n - 1);

    if (box->homogeneous) {
        fixed = 0;
    }

    leftover = axis - fixed - padding_total;
    if (leftover < 0) leftover = 0;
    share = expanders > 0 ? leftover / expanders : 0;
    extra = expanders > 0 ? leftover % expanders : 0;

    cursor = (box->orientation == UI_HORIZONTAL) ? box->base.x : box->base.y;
    for (i = 0; i < n; i++) {
        ui_widget_t *c = box->base.children[i];
        int nat = (box->orientation == UI_HORIZONTAL) ? c->pref_w : c->pref_h;
        int cell;
        bool expands = box->homogeneous || box->pack[i].expand;

        if (expands) {
            cell = share;
            expanders--;
            if (expanders == 0) cell += extra;
        } else {
            cell = nat;
        }

        cursor += box->pack[i].padding;
        box_place_child(box, c, box->pack[i].fill || box->homogeneous,
                        cursor, cell);
        cursor += cell + box->pack[i].padding;
        if (i + 1 < n) cursor += box->spacing;
    }
}

void ui_box_init(ui_box_t *box, int orientation, int x, int y, int w, int h)
{
    int i;
    if (!box) return;
    ui_widget_base_init(&box->base, UI_WIDGET_BOX, x, y, w, h);
    box->orientation = orientation;
    box->spacing = 4;
    box->homogeneous = false;
    for (i = 0; i < UI_WIDGET_MAX_CHILDREN; i++) {
        box->pack[i].expand = false;
        box->pack[i].fill = false;
        box->pack[i].padding = 0;
    }
}

int ui_box_pack(ui_box_t *box, ui_widget_t *child, bool expand, bool fill, int padding)
{
    int idx;
    if (!box || !child) return -1;
    idx = box->base.child_count;
    if (ui_widget_add_child(&box->base, child) != 0)
        return -1;
    box->pack[idx].expand = expand;
    box->pack[idx].fill = fill;
    box->pack[idx].padding = padding < 0 ? 0 : padding;
    ui_box_relayout(box);
    return 0;
}

void ui_box_set_geometry(ui_box_t *box, int x, int y, int w, int h)
{
    if (!box) return;
    box->base.x = x;
    box->base.y = y;
    box->base.w = w;
    box->base.h = h;
    ui_box_relayout(box);
}

void ui_grid_relayout(ui_grid_t *grid)
{
    int i, cw, ch;
    if (!grid || grid->rows <= 0 || grid->cols <= 0) return;
    cw = (grid->base.w - (grid->cols - 1) * grid->spacing) / grid->cols;
    ch = (grid->base.h - (grid->rows - 1) * grid->spacing) / grid->rows;
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;
    for (i = 0; i < grid->base.child_count; i++) {
        ui_widget_t *c = grid->base.children[i];
        c->x = grid->base.x + grid->cell[i].col * (cw + grid->spacing);
        c->y = grid->base.y + grid->cell[i].row * (ch + grid->spacing);
        c->w = cw;
        c->h = ch;
    }
}

void ui_grid_init(ui_grid_t *grid, int rows, int cols, int spacing,
                  int x, int y, int w, int h)
{
    int i;
    if (!grid) return;
    ui_widget_base_init(&grid->base, UI_WIDGET_GRID, x, y, w, h);
    grid->rows = rows > 0 ? rows : 1;
    grid->cols = cols > 0 ? cols : 1;
    grid->spacing = spacing < 0 ? 0 : spacing;
    for (i = 0; i < UI_WIDGET_MAX_CHILDREN; i++) {
        grid->cell[i].row = 0;
        grid->cell[i].col = 0;
    }
}

int ui_grid_attach(ui_grid_t *grid, ui_widget_t *child, int row, int col)
{
    int idx;
    if (!grid || !child) return -1;
    if (row < 0 || row >= grid->rows || col < 0 || col >= grid->cols) return -1;
    idx = grid->base.child_count;
    if (ui_widget_add_child(&grid->base, child) != 0)
        return -1;
    grid->cell[idx].row = row;
    grid->cell[idx].col = col;
    ui_grid_relayout(grid);
    return 0;
}

void ui_grid_set_geometry(ui_grid_t *grid, int x, int y, int w, int h)
{
    if (!grid) return;
    grid->base.x = x;
    grid->base.y = y;
    grid->base.w = w;
    grid->base.h = h;
    ui_grid_relayout(grid);
}

static void button_draw_adapter(ui_widget_t *w, widget_context_t *ctx)
{
    widget_button_draw(ctx, (widget_button_t *)w);
}

static bool button_event_adapter(ui_widget_t *w, const ui_tk_event_t *ev, void *user_data)
{
    if (ev->type != UI_TK_EV_POINTER) return false;
    return widget_button_handle_mouse((widget_button_t *)w, ev->x, ev->y,
                                      ev->down, ev->clicked, user_data);
}

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h, const char *text)
{
    if (!btn) return;
    ui_widget_base_init(&btn->base, UI_WIDGET_BUTTON, x, y, w, h);
    btn->base.draw = button_draw_adapter;
    btn->base.handle_event = button_event_adapter;
    btn->text = text;
    btn->pressed = false;
    btn->hovered = false;
    btn->on_click = 0;
}

void widget_button_draw(widget_context_t *ctx, widget_button_t *btn)
{
    uint32_t border = ctx && ctx->use_light_theme ? 0xFFB0B0B0u : 0xFF4A4A4Cu;
    uint32_t bg = 0xFF353537u;
    if (!ctx || !btn) return;
    if (ctx->use_light_theme) bg = 0xFFEAEAEAu;
    if (btn->hovered) bg = ctx->use_light_theme ? 0xFFD8D8D8u : 0xFF454547u;
    if (btn->pressed) bg = ctx->use_light_theme ? 0xFFC8C8C8u : 0xFF555557u;

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->base.x, btn->base.y, btn->base.w, btn->base.h, 6, border);
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->base.x + 1, btn->base.y + 1, btn->base.w - 2, btn->base.h - 2, 5, bg);
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, btn->base.x, btn->base.y, btn->base.w, btn->base.h, border);
    }

    if (ctx->draw_string && btn->text) {
        int tw = ctx->measure_string_width
                     ? ctx->measure_string_width(ctx->user_data, btn->text)
                     : str_len(btn->text) * 8;
        int tx = btn->base.x + (btn->base.w - tw) / 2;
        int ty = btn->base.y + (btn->base.h - 8) / 2;
        ctx->draw_string(ctx->user_data, tx, ty, btn->text,
                         ctx->use_light_theme ? 0xFF222222u : 0xFFFFFFFFu);
    }
}

bool widget_button_handle_mouse(widget_button_t *btn, int mx, int my, bool mouse_down, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    if (!btn) return false;
    in_bounds = (mx >= btn->base.x && mx < btn->base.x + btn->base.w &&
                 my >= btn->base.y && my < btn->base.y + btn->base.h);
    btn->hovered = in_bounds;
    if (mouse_clicked && in_bounds) {
        btn->pressed = true;
        return true;
    }
    if (!mouse_down && btn->pressed) {
        btn->pressed = false;
        if (in_bounds) {
            if (btn->on_click) btn->on_click(user_data);
            ui_signal_emit(&btn->base, UI_SIGNAL_CLICKED);
        }
        return true;
    }
    return in_bounds;
}

static void scrollbar_draw_adapter(ui_widget_t *w, widget_context_t *ctx)
{
    widget_scrollbar_draw(ctx, (widget_scrollbar_t *)w);
}

static bool scrollbar_event_adapter(ui_widget_t *w, const ui_tk_event_t *ev, void *user_data)
{
    if (ev->type != UI_TK_EV_POINTER) return false;
    return widget_scrollbar_handle_mouse((widget_scrollbar_t *)w, ev->x, ev->y,
                                         ev->down, user_data);
}

void widget_scrollbar_init(widget_scrollbar_t *sb, int x, int y, int w, int h)
{
    if (!sb) return;
    ui_widget_base_init(&sb->base, UI_WIDGET_SCROLLBAR, x, y, w, h);
    sb->base.draw = scrollbar_draw_adapter;
    sb->base.handle_event = scrollbar_event_adapter;
    sb->content_height = h;
    sb->scroll_y = 0;
    sb->is_dragging = false;
    sb->drag_start_my = 0;
    sb->drag_start_scroll_y = 0;
    sb->on_scroll = 0;
}

void widget_scrollbar_update(widget_scrollbar_t *sb, int content_height, int scroll_y)
{
    if (!sb) return;
    sb->content_height = content_height;
    sb->scroll_y = scroll_y;
}

void widget_scrollbar_draw(widget_context_t *ctx, widget_scrollbar_t *sb)
{
    int thumb_h, max_scroll, thumb_y;
    if (!ctx || !sb || !ctx->draw_rect) return;

    ctx->draw_rect(ctx->user_data, sb->base.x, sb->base.y, sb->base.w, sb->base.h,
                   ctx->use_light_theme ? 0xFFE6E6E6u : 0xFF2A2A2Au);
    if (sb->content_height <= sb->base.h) return;

    thumb_h = (sb->base.h * sb->base.h) / sb->content_height;
    if (thumb_h < 20) thumb_h = 20;
    max_scroll = sb->content_height - sb->base.h;
    if (max_scroll <= 0) return;
    if (sb->scroll_y < 0) sb->scroll_y = 0;
    if (sb->scroll_y > max_scroll) sb->scroll_y = max_scroll;
    thumb_y = sb->base.y + (sb->scroll_y * (sb->base.h - thumb_h)) / max_scroll;

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, sb->base.x + 1, thumb_y + 1, sb->base.w - 2, thumb_h - 2, 4,
                                      sb->is_dragging ? 0xFF666666u : 0xFF888888u);
    } else {
        ctx->draw_rect(ctx->user_data, sb->base.x + 1, thumb_y + 1, sb->base.w - 2, thumb_h - 2, 0xFF888888u);
    }
}

bool widget_scrollbar_handle_mouse(widget_scrollbar_t *sb, int mx, int my, bool mouse_down, void *user_data)
{
    int thumb_h, max_scroll, thumb_y, track_h, dy, new_scroll;
    bool in_track, in_thumb;
    if (!sb || sb->content_height <= sb->base.h) return false;

    thumb_h = (sb->base.h * sb->base.h) / sb->content_height;
    if (thumb_h < 20) thumb_h = 20;
    max_scroll = sb->content_height - sb->base.h;
    if (max_scroll <= 0) return false;
    thumb_y = sb->base.y + (sb->scroll_y * (sb->base.h - thumb_h)) / max_scroll;

    in_track = (mx >= sb->base.x && mx < sb->base.x + sb->base.w &&
                my >= sb->base.y && my < sb->base.y + sb->base.h);
    in_thumb = (mx >= sb->base.x && mx < sb->base.x + sb->base.w &&
                my >= thumb_y && my < thumb_y + thumb_h);

    if (!mouse_down) {
        sb->is_dragging = false;
        return in_track;
    }

    if (!sb->is_dragging && in_thumb) {
        sb->is_dragging = true;
        sb->drag_start_my = my;
        sb->drag_start_scroll_y = sb->scroll_y;
        return true;
    }

    if (!sb->is_dragging) return in_track;

    track_h = sb->base.h - thumb_h;
    if (track_h <= 0) return true;
    dy = my - sb->drag_start_my;
    new_scroll = sb->drag_start_scroll_y + (dy * max_scroll) / track_h;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    if (new_scroll != sb->scroll_y) {
        sb->scroll_y = new_scroll;
        if (sb->on_scroll) sb->on_scroll(user_data, sb->scroll_y);
        ui_signal_emit(&sb->base, UI_SIGNAL_CHANGED);
    }
    return true;
}

static void textbox_draw_adapter(ui_widget_t *w, widget_context_t *ctx)
{
    widget_textbox_draw(ctx, (widget_textbox_t *)w);
}

static bool textbox_event_adapter(ui_widget_t *w, const ui_tk_event_t *ev, void *user_data)
{
    widget_textbox_t *tb = (widget_textbox_t *)w;
    if (ev->type == UI_TK_EV_POINTER)
        return widget_textbox_handle_mouse(tb, ev->x, ev->y, ev->clicked, user_data);
    if (ev->type == UI_TK_EV_KEY && ev->pressed)
        return widget_textbox_handle_key(tb, (char)(ev->keycode & 0xFF), user_data);
    return false;
}

void widget_textbox_init(widget_textbox_t *tb, int x, int y, int w, int h, char *buffer, int max_len)
{
    if (!tb) return;
    ui_widget_base_init(&tb->base, UI_WIDGET_TEXTBOX, x, y, w, h);
    tb->base.draw = textbox_draw_adapter;
    tb->base.handle_event = textbox_event_adapter;
    tb->text = buffer;
    tb->max_len = max_len;
    tb->cursor_pos = str_len(buffer);
    tb->focused = false;
    tb->on_change = 0;
}

void widget_textbox_draw(widget_context_t *ctx, widget_textbox_t *tb)
{
    uint32_t border = ctx && ctx->use_light_theme ? 0xFFB0B0B0u : 0xFF4A4A4Cu;
    uint32_t bg = ctx && ctx->use_light_theme ? 0xFFFFFFFFu : 0xFF1B1B1Bu;
    if (!ctx || !tb || !ctx->draw_rect) return;

    if (tb->focused)
        border = 0xFF4C8DFFu;

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->base.x, tb->base.y, tb->base.w, tb->base.h, 4, border);
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->base.x + 1, tb->base.y + 1, tb->base.w - 2, tb->base.h - 2, 3, bg);
    } else {
        ctx->draw_rect(ctx->user_data, tb->base.x, tb->base.y, tb->base.w, tb->base.h, border);
    }
    if (ctx->draw_string && tb->text)
        ctx->draw_string(ctx->user_data, tb->base.x + 5, tb->base.y + (tb->base.h - 8) / 2, tb->text,
                         ctx->use_light_theme ? 0xFF202020u : 0xFFFFFFFFu);
}

bool widget_textbox_handle_mouse(widget_textbox_t *tb, int mx, int my, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    (void)user_data;
    if (!tb) return false;
    in_bounds = (mx >= tb->base.x && mx < tb->base.x + tb->base.w &&
                 my >= tb->base.y && my < tb->base.y + tb->base.h);
    if (mouse_clicked) tb->focused = in_bounds;
    return in_bounds;
}

bool widget_textbox_handle_key(widget_textbox_t *tb, char c, void *user_data)
{
    int len, i;
    if (!tb || !tb->focused || !tb->text) return false;
    len = str_len(tb->text);

    if (c == '\b') {
        if (tb->cursor_pos <= 0 || len <= 0) return true;
        for (i = tb->cursor_pos - 1; i < len; i++) tb->text[i] = tb->text[i + 1];
        tb->cursor_pos--;
        if (tb->on_change) tb->on_change(user_data);
        ui_signal_emit(&tb->base, UI_SIGNAL_CHANGED);
        return true;
    }

    if (c < 32 || c > 126) return false;
    if (len >= tb->max_len - 1) return true;

    for (i = len; i >= tb->cursor_pos; i--) tb->text[i + 1] = tb->text[i];
    tb->text[tb->cursor_pos] = c;
    tb->cursor_pos++;
    if (tb->on_change) tb->on_change(user_data);
    ui_signal_emit(&tb->base, UI_SIGNAL_CHANGED);
    return true;
}

static void dropdown_draw_adapter(ui_widget_t *w, widget_context_t *ctx)
{
    widget_dropdown_draw(ctx, (widget_dropdown_t *)w);
}

static bool dropdown_event_adapter(ui_widget_t *w, const ui_tk_event_t *ev, void *user_data)
{
    if (ev->type != UI_TK_EV_POINTER) return false;
    return widget_dropdown_handle_mouse((widget_dropdown_t *)w, ev->x, ev->y,
                                        ev->clicked, user_data);
}

void widget_dropdown_init(widget_dropdown_t *dd, int x, int y, int w, int h, const char **items, int count)
{
    if (!dd) return;
    ui_widget_base_init(&dd->base, UI_WIDGET_DROPDOWN, x, y, w, h);
    dd->base.draw = dropdown_draw_adapter;
    dd->base.handle_event = dropdown_event_adapter;
    dd->items = items;
    dd->item_count = count;
    dd->selected_idx = 0;
    dd->is_open = false;
    dd->on_select = 0;
}

void widget_dropdown_draw(widget_context_t *ctx, widget_dropdown_t *dd)
{
    if (!ctx || !dd || !ctx->draw_rect) return;
    if (ctx->draw_rounded_rect_filled)
        ctx->draw_rounded_rect_filled(ctx->user_data, dd->base.x, dd->base.y, dd->base.w, dd->base.h, 4, 0xFF3A3A3Au);
    else
        ctx->draw_rect(ctx->user_data, dd->base.x, dd->base.y, dd->base.w, dd->base.h, 0xFF3A3A3Au);

    if (ctx->draw_string && dd->items && dd->item_count > 0 && dd->selected_idx >= 0 && dd->selected_idx < dd->item_count)
        ctx->draw_string(ctx->user_data, dd->base.x + 5, dd->base.y + (dd->base.h - 8) / 2, dd->items[dd->selected_idx], 0xFFFFFFFFu);
}

bool widget_dropdown_handle_mouse(widget_dropdown_t *dd, int mx, int my, bool mouse_clicked, void *user_data)
{
    int idx;
    bool in_bounds;
    if (!dd || !mouse_clicked) return false;

    in_bounds = (mx >= dd->base.x && mx < dd->base.x + dd->base.w &&
                 my >= dd->base.y && my < dd->base.y + dd->base.h);
    if (in_bounds) {
        dd->is_open = !dd->is_open;
        return true;
    }
    if (!dd->is_open) return false;
    idx = (my - (dd->base.y + dd->base.h)) / dd->base.h;
    if (idx >= 0 && idx < dd->item_count) {
        dd->selected_idx = idx;
        dd->is_open = false;
        if (dd->on_select) dd->on_select(user_data, idx);
        ui_signal_emit(&dd->base, UI_SIGNAL_SELECTED);
        return true;
    }
    dd->is_open = false;
    return false;
}

static void checkbox_draw_adapter(ui_widget_t *w, widget_context_t *ctx)
{
    widget_checkbox_draw(ctx, (widget_checkbox_t *)w);
}

static bool checkbox_event_adapter(ui_widget_t *w, const ui_tk_event_t *ev, void *user_data)
{
    if (ev->type != UI_TK_EV_POINTER) return false;
    return widget_checkbox_handle_mouse((widget_checkbox_t *)w, ev->x, ev->y,
                                        ev->clicked, user_data);
}

void widget_checkbox_init(widget_checkbox_t *cb, int x, int y, int w, int h, const char *text, bool is_radio)
{
    if (!cb) return;
    ui_widget_base_init(&cb->base, UI_WIDGET_CHECKBOX, x, y, w, h);
    cb->base.draw = checkbox_draw_adapter;
    cb->base.handle_event = checkbox_event_adapter;
    cb->text = text;
    cb->checked = false;
    cb->is_radio = is_radio;
    cb->on_toggle = 0;
}

void widget_checkbox_draw(widget_context_t *ctx, widget_checkbox_t *cb)
{
    int box = 14;
    int by;
    if (!ctx || !cb || !ctx->draw_rect) return;
    by = cb->base.y + (cb->base.h - box) / 2;
    if (ctx->draw_rounded_rect_filled)
        ctx->draw_rounded_rect_filled(ctx->user_data, cb->base.x, by, box, box, cb->is_radio ? 7 : 3, 0xFF404040u);
    else
        ctx->draw_rect(ctx->user_data, cb->base.x, by, box, box, 0xFF404040u);
    if (cb->checked && ctx->draw_rect)
        ctx->draw_rect(ctx->user_data, cb->base.x + 4, by + 4, 6, 6, 0xFFFFFFFFu);

    if (ctx->draw_string && cb->text)
        ctx->draw_string(ctx->user_data, cb->base.x + box + 6, cb->base.y + (cb->base.h - 8) / 2, cb->text, 0xFFFFFFFFu);
}

bool widget_checkbox_handle_mouse(widget_checkbox_t *cb, int mx, int my, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    if (!cb || !mouse_clicked) return false;
    in_bounds = (mx >= cb->base.x && mx < cb->base.x + cb->base.w &&
                 my >= cb->base.y && my < cb->base.y + cb->base.h);
    if (!in_bounds) return false;
    cb->checked = !cb->checked;
    if (cb->on_toggle) cb->on_toggle(user_data, cb->checked);
    ui_signal_emit(&cb->base, UI_SIGNAL_TOGGLED);
    return true;
}
