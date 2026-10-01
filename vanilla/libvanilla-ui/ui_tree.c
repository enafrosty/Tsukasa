/*
 * Project Tsukasa — Display Server Widget Tree and Invalidation
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

void ui_widget_add_child(ui_widget_t *parent, ui_widget_t *child)
{
    if (!parent || !child)
        return;

    child->parent = parent;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        ui_widget_t *cur = parent->first_child;
        while (cur->next_sibling)
            cur = cur->next_sibling;
        cur->next_sibling = child;
    }

    if (parent->layout_elem && child->layout_elem)
        vlayout_add_child(parent->layout_elem, child->layout_elem);

    ui_widget_invalidate(child);
}

void ui_widget_invalidate(ui_widget_t *w)
{
    if (!w)
        return;

    w->dirty = 1;

    if (w->layout_elem) {
        w->dirty_rect.x = w->layout_elem->computed_x;
        w->dirty_rect.y = w->layout_elem->computed_y;
        w->dirty_rect.w = w->layout_elem->computed_w;
        w->dirty_rect.h = w->layout_elem->computed_h;
    }

    for (ui_widget_t *p = w->parent; p; p = p->parent) {
        p->dirty = 1;
        if (w->dirty_rect.w > 0 && w->dirty_rect.h > 0) {
            if (p->dirty_rect.w <= 0 || p->dirty_rect.h <= 0) {
                p->dirty_rect = w->dirty_rect;
            } else {
                int32_t x1 = p->dirty_rect.x < w->dirty_rect.x ? p->dirty_rect.x : w->dirty_rect.x;
                int32_t y1 = p->dirty_rect.y < w->dirty_rect.y ? p->dirty_rect.y : w->dirty_rect.y;
                int32_t rx1 = p->dirty_rect.x + p->dirty_rect.w;
                int32_t rx2 = w->dirty_rect.x + w->dirty_rect.w;
                int32_t x2 = rx1 > rx2 ? rx1 : rx2;
                int32_t ry1 = p->dirty_rect.y + p->dirty_rect.h;
                int32_t ry2 = w->dirty_rect.y + w->dirty_rect.h;
                int32_t y2 = ry1 > ry2 ? ry1 : ry2;
                p->dirty_rect.x = x1;
                p->dirty_rect.y = y1;
                p->dirty_rect.w = x2 - x1;
                p->dirty_rect.h = y2 - y1;
            }
        }
    }
}
