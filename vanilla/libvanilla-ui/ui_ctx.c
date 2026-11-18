/*
 * Project Tsukasa — Display Server Widget Toolkit Context & Arena Manager
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

#include "ui_internal.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static inline size_t align_up(size_t val, size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

int64_t ui_current_time_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
    }
    return 0;
}

ui_ctx_t *ui_ctx_init(void *arena, size_t arena_size, vanilla_layout_t *layout_ctx)
{
    if (!arena || arena_size < sizeof(ui_ctx_t) + 1024)
        return NULL;

    uintptr_t base_addr = (uintptr_t)arena;
    uintptr_t aligned_base = align_up(base_addr, 8);
    size_t pad = (size_t)(aligned_base - base_addr);
    if (arena_size <= pad + sizeof(ui_ctx_t) + 1024)
        return NULL;

    size_t total_usable = arena_size - pad;

    if (!layout_ctx) {
        size_t lsize = 64 * 1024;
        if (lsize >= total_usable / 2)
            lsize = total_usable / 2;
        if (lsize < 4096)
            return NULL;

        void *lmem = (uint8_t *)aligned_base + (total_usable - lsize);
        layout_ctx = vlayout_init(lmem, lsize);
        if (!layout_ctx)
            return NULL;

        total_usable -= lsize;
    }

    ui_ctx_t *ctx = (ui_ctx_t *)aligned_base;
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena_base = (uint8_t *)aligned_base;
    ctx->arena_size = total_usable;
    ctx->offset = align_up(sizeof(ui_ctx_t), 8);
    ctx->layout_ctx = layout_ctx;

    return ctx;
}

void *ui_alloc(ui_ctx_t *ctx, size_t size)
{
    if (!ctx)
        return NULL;

    size_t cur = align_up(ctx->offset, 8);
    size_t next = cur + size;
    if (next > ctx->arena_size) {
        printf("[ui] arena full\n");
        return NULL;
    }

    void *p = ctx->arena_base + cur;
    ctx->offset = next;
    return p;
}

ui_widget_t *ui_alloc_widget(ui_ctx_t *ctx, ui_widget_type_t type)
{
    if (!ctx)
        return NULL;

    ui_widget_t *w = (ui_widget_t *)ui_alloc(ctx, sizeof(ui_widget_t));
    if (!w)
        return NULL;

    memset(w, 0, sizeof(*w));
    w->type = type;
    w->dirty = 1;

    if (ctx->layout_ctx) {
        w->layout_elem = vlayout_alloc_elem(ctx->layout_ctx);
        if (!w->layout_elem)
            return NULL;
    }

    switch (type) {
    case UI_WIDGET_BOX:
        w->focusable = 0;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_GROW;
            w->layout_elem->direction = VDIR_COLUMN;
            w->layout_elem->align_items = VALIGN_START;
        }
        break;

    case UI_WIDGET_BUTTON:
        w->focusable = 1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_FIXED;
            w->layout_elem->w_px = 64;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 32;
            w->layout_elem->pad_left = 12;
            w->layout_elem->pad_right = 12;
            w->layout_elem->pad_top = 6;
            w->layout_elem->pad_bottom = 6;
            w->layout_elem->corner_radius = 4;
        }
        break;

    case UI_WIDGET_LABEL:
        w->focusable = 0;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_TEXT;
            w->layout_elem->w_mode = VSIZE_FIT;
            w->layout_elem->h_mode = VSIZE_FIT;
            w->layout_elem->font_size = 0.0f;
            w->layout_elem->font_weight = APP_FONT_REGULAR;
        }
        break;

    case UI_WIDGET_TEXT_FIELD:
        w->focusable = 1;
        w->text_field.sel_start = -1;
        w->text_field.sel_end = -1;
        w->text_field.sel_anchor = -1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 28;
            w->layout_elem->pad_left = 6;
            w->layout_elem->pad_right = 6;
            w->layout_elem->pad_top = 4;
            w->layout_elem->pad_bottom = 4;
            w->layout_elem->corner_radius = 4;
        }
        break;

    case UI_WIDGET_CHECKBOX:
        w->focusable = 1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_FIXED;
            w->layout_elem->w_px = 100;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 24;
            w->layout_elem->direction = VDIR_ROW;
        }
        break;

    case UI_WIDGET_RADIO:
        w->focusable = 1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_FIXED;
            w->layout_elem->w_px = 100;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 24;
            w->layout_elem->direction = VDIR_ROW;
        }
        break;

    case UI_WIDGET_SLIDER:
        w->focusable = 1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 24;
        }
        break;

    case UI_WIDGET_LIST:
        w->focusable = 1;
        w->list.hovered = -1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_GROW;
            w->layout_elem->corner_radius = 4;
        }
        break;

    case UI_WIDGET_SCROLL_VIEW:
        w->focusable = 0;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_GROW;
            w->layout_elem->direction = VDIR_COLUMN;
            w->layout_elem->align_items = VALIGN_START;
            w->layout_elem->clip_children = 1;
        }
        break;

    case UI_WIDGET_MENU:
        w->focusable = 1;
        w->menu.selected = -1;
        w->menu.hovered = -1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_FIT;
            w->layout_elem->h_mode = VSIZE_FIT;
            w->layout_elem->corner_radius = 0;
        }
        break;

    case UI_WIDGET_TAB_BAR:
        w->focusable = 1;
        w->tab_bar.hovered_tab = -1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 36;
            w->layout_elem->direction = VDIR_ROW;
        }
        break;

    case UI_WIDGET_MODAL_DIALOG:
        w->focusable = 0;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_GROW;
        }
        break;

    case UI_WIDGET_TOAST:
        w->focusable = 0;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_FIT;
            w->layout_elem->h_mode = VSIZE_FIT;
        }
        break;

    case UI_WIDGET_PROGRESS_BAR:
        w->focusable = 0;
        w->progress_bar.max_val = 100.0f;
        w->progress_bar.show_label = 1;
        if (w->layout_elem) {
            w->layout_elem->type = VELEM_BOX;
            w->layout_elem->w_mode = VSIZE_GROW;
            w->layout_elem->h_mode = VSIZE_FIXED;
            w->layout_elem->h_px = 18;
            w->layout_elem->corner_radius = 4;
        }
        break;
    }

    return w;
}

void ui_ctx_set_clipboard_ops(ui_ctx_t *ctx,
                              ui_clipboard_set_fn_t set_fn,
                              ui_clipboard_get_fn_t get_fn)
{
    if (!ctx)
        return;
    ctx->clipboard_set_fn = set_fn;
    ctx->clipboard_get_fn = get_fn;
}

void ui_ctx_show_popup(ui_ctx_t *ctx, ui_widget_t *menu, int32_t x, int32_t y)
{
    if (!ctx || !menu)
        return;

    if (ctx->active_popup && ctx->active_popup != menu)
        ctx->active_popup->hidden = 1;

    if (menu->type == UI_WIDGET_MENU) {
        menu->menu.floating = 1;
        menu->menu.float_x = x;
        menu->menu.float_y = y;
        menu->menu.hovered = -1;
        menu->menu.selected = -1;
    }
    menu->hidden = 0;
    ctx->active_popup = menu;
    ctx->overlay_dirty = 1;
    ui_widget_invalidate(menu);
}

void ui_ctx_hide_popup(ui_ctx_t *ctx)
{
    if (!ctx || !ctx->active_popup)
        return;

    ctx->active_popup->hidden = 1;
    if (ctx->active_popup->type == UI_WIDGET_MENU)
        ctx->active_popup->menu.hovered = -1;
    ctx->active_popup = NULL;
    ctx->overlay_dirty = 1;
}

ui_widget_t *ui_ctx_get_popup(ui_ctx_t *ctx)
{
    return ctx ? ctx->active_popup : NULL;
}

void ui_ctx_show_modal(ui_ctx_t *ctx, ui_widget_t *modal)
{
    if (!ctx || !modal)
        return;

    if (ctx->active_popup)
        ui_ctx_hide_popup(ctx);

    if (ctx->active_modal && ctx->active_modal != modal)
        ctx->active_modal->hidden = 1;

    modal->hidden = 0;
    if (modal->type == UI_WIDGET_MODAL_DIALOG) {
        modal->modal.result = UI_DIALOG_RES_NONE;
        modal->modal.pressed_btn = 0;
        modal->modal.hovered_btn = 0;
    }
    ctx->active_modal = modal;
    ctx->overlay_dirty = 1;
    ui_widget_invalidate(modal);

    if (modal->type == UI_WIDGET_MODAL_DIALOG && modal->modal.input_field)
        ui_widget_set_focus(ctx, modal->modal.input_field);
}

void ui_ctx_hide_modal(ui_ctx_t *ctx)
{
    if (!ctx || !ctx->active_modal)
        return;

    ui_widget_t *m = ctx->active_modal;
    if (m->type == UI_WIDGET_MODAL_DIALOG &&
        m->modal.input_field &&
        ctx->focused_widget == m->modal.input_field) {
        ui_widget_set_focus(ctx, NULL);
    }
    m->hidden = 1;
    ctx->active_modal = NULL;
    ctx->overlay_dirty = 1;
}

ui_widget_t *ui_ctx_get_modal(ui_ctx_t *ctx)
{
    return ctx ? ctx->active_modal : NULL;
}
