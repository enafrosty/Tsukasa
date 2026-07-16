#include "../include/libwidget.h"

static int str_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
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
}

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h, const char *text)
{
    if (!btn) return;
    btn->x = x;
    btn->y = y;
    btn->w = w;
    btn->h = h;
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
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->x, btn->y, btn->w, btn->h, 6, border);
        ctx->draw_rounded_rect_filled(ctx->user_data, btn->x + 1, btn->y + 1, btn->w - 2, btn->h - 2, 5, bg);
    } else if (ctx->draw_rect) {
        ctx->draw_rect(ctx->user_data, btn->x, btn->y, btn->w, btn->h, border);
    }

    if (ctx->draw_string && btn->text) {
        int tw = str_len(btn->text) * 8;
        int tx = btn->x + (btn->w - tw) / 2;
        int ty = btn->y + (btn->h - 8) / 2;
        ctx->draw_string(ctx->user_data, tx, ty, btn->text,
                         ctx->use_light_theme ? 0xFF222222u : 0xFFFFFFFFu);
    }
}

bool widget_button_handle_mouse(widget_button_t *btn, int mx, int my, bool mouse_down, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    if (!btn) return false;
    in_bounds = (mx >= btn->x && mx < btn->x + btn->w &&
                 my >= btn->y && my < btn->y + btn->h);
    btn->hovered = in_bounds;
    if (mouse_clicked && in_bounds) {
        btn->pressed = true;
        return true;
    }
    if (!mouse_down && btn->pressed) {
        btn->pressed = false;
        if (in_bounds && btn->on_click) btn->on_click(user_data);
        return true;
    }
    return in_bounds;
}

void widget_scrollbar_init(widget_scrollbar_t *sb, int x, int y, int w, int h)
{
    if (!sb) return;
    sb->x = x;
    sb->y = y;
    sb->w = w;
    sb->h = h;
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

    ctx->draw_rect(ctx->user_data, sb->x, sb->y, sb->w, sb->h,
                   ctx->use_light_theme ? 0xFFE6E6E6u : 0xFF2A2A2Au);
    if (sb->content_height <= sb->h) return;

    thumb_h = (sb->h * sb->h) / sb->content_height;
    if (thumb_h < 20) thumb_h = 20;
    max_scroll = sb->content_height - sb->h;
    if (max_scroll <= 0) return;
    if (sb->scroll_y < 0) sb->scroll_y = 0;
    if (sb->scroll_y > max_scroll) sb->scroll_y = max_scroll;
    thumb_y = sb->y + (sb->scroll_y * (sb->h - thumb_h)) / max_scroll;

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, sb->x + 1, thumb_y + 1, sb->w - 2, thumb_h - 2, 4,
                                      sb->is_dragging ? 0xFF666666u : 0xFF888888u);
    } else {
        ctx->draw_rect(ctx->user_data, sb->x + 1, thumb_y + 1, sb->w - 2, thumb_h - 2, 0xFF888888u);
    }
}

bool widget_scrollbar_handle_mouse(widget_scrollbar_t *sb, int mx, int my, bool mouse_down, void *user_data)
{
    int thumb_h, max_scroll, thumb_y, track_h, dy, new_scroll;
    bool in_track, in_thumb;
    if (!sb || sb->content_height <= sb->h) return false;

    thumb_h = (sb->h * sb->h) / sb->content_height;
    if (thumb_h < 20) thumb_h = 20;
    max_scroll = sb->content_height - sb->h;
    if (max_scroll <= 0) return false;
    thumb_y = sb->y + (sb->scroll_y * (sb->h - thumb_h)) / max_scroll;

    in_track = (mx >= sb->x && mx < sb->x + sb->w && my >= sb->y && my < sb->y + sb->h);
    in_thumb = (mx >= sb->x && mx < sb->x + sb->w && my >= thumb_y && my < thumb_y + thumb_h);

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

    track_h = sb->h - thumb_h;
    if (track_h <= 0) return true;
    dy = my - sb->drag_start_my;
    new_scroll = sb->drag_start_scroll_y + (dy * max_scroll) / track_h;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;
    if (new_scroll != sb->scroll_y) {
        sb->scroll_y = new_scroll;
        if (sb->on_scroll) sb->on_scroll(user_data, sb->scroll_y);
    }
    return true;
}

void widget_textbox_init(widget_textbox_t *tb, int x, int y, int w, int h, char *buffer, int max_len)
{
    if (!tb) return;
    tb->x = x;
    tb->y = y;
    tb->w = w;
    tb->h = h;
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

    if (ctx->draw_rounded_rect_filled) {
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->x, tb->y, tb->w, tb->h, 4, border);
        ctx->draw_rounded_rect_filled(ctx->user_data, tb->x + 1, tb->y + 1, tb->w - 2, tb->h - 2, 3, bg);
    } else {
        ctx->draw_rect(ctx->user_data, tb->x, tb->y, tb->w, tb->h, border);
    }
    if (ctx->draw_string && tb->text)
        ctx->draw_string(ctx->user_data, tb->x + 5, tb->y + (tb->h - 8) / 2, tb->text,
                         ctx->use_light_theme ? 0xFF202020u : 0xFFFFFFFFu);
}

bool widget_textbox_handle_mouse(widget_textbox_t *tb, int mx, int my, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    (void)user_data;
    if (!tb) return false;
    in_bounds = (mx >= tb->x && mx < tb->x + tb->w &&
                 my >= tb->y && my < tb->y + tb->h);
    if (mouse_clicked) tb->focused = in_bounds;
    return in_bounds;
}

bool widget_textbox_handle_key(widget_textbox_t *tb, char c, void *user_data)
{
    int len, i;
    (void)user_data;
    if (!tb || !tb->focused || !tb->text) return false;
    len = str_len(tb->text);

    if (c == '\b') {
        if (tb->cursor_pos <= 0 || len <= 0) return true;
        for (i = tb->cursor_pos - 1; i < len; i++) tb->text[i] = tb->text[i + 1];
        tb->cursor_pos--;
        if (tb->on_change) tb->on_change(user_data);
        return true;
    }

    if (c < 32 || c > 126) return false;
    if (len >= tb->max_len - 1) return true;

    for (i = len; i >= tb->cursor_pos; i--) tb->text[i + 1] = tb->text[i];
    tb->text[tb->cursor_pos] = c;
    tb->cursor_pos++;
    if (tb->on_change) tb->on_change(user_data);
    return true;
}

void widget_dropdown_init(widget_dropdown_t *dd, int x, int y, int w, int h, const char **items, int count)
{
    if (!dd) return;
    dd->x = x;
    dd->y = y;
    dd->w = w;
    dd->h = h;
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
        ctx->draw_rounded_rect_filled(ctx->user_data, dd->x, dd->y, dd->w, dd->h, 4, 0xFF3A3A3Au);
    else
        ctx->draw_rect(ctx->user_data, dd->x, dd->y, dd->w, dd->h, 0xFF3A3A3Au);

    if (ctx->draw_string && dd->items && dd->item_count > 0 && dd->selected_idx >= 0 && dd->selected_idx < dd->item_count)
        ctx->draw_string(ctx->user_data, dd->x + 5, dd->y + (dd->h - 8) / 2, dd->items[dd->selected_idx], 0xFFFFFFFFu);
}

bool widget_dropdown_handle_mouse(widget_dropdown_t *dd, int mx, int my, bool mouse_clicked, void *user_data)
{
    int idx;
    bool in_bounds;
    if (!dd || !mouse_clicked) return false;

    in_bounds = (mx >= dd->x && mx < dd->x + dd->w &&
                 my >= dd->y && my < dd->y + dd->h);
    if (in_bounds) {
        dd->is_open = !dd->is_open;
        return true;
    }
    if (!dd->is_open) return false;
    idx = (my - (dd->y + dd->h)) / dd->h;
    if (idx >= 0 && idx < dd->item_count) {
        dd->selected_idx = idx;
        dd->is_open = false;
        if (dd->on_select) dd->on_select(user_data, idx);
        return true;
    }
    dd->is_open = false;
    return false;
}

void widget_checkbox_init(widget_checkbox_t *cb, int x, int y, int w, int h, const char *text, bool is_radio)
{
    if (!cb) return;
    cb->x = x;
    cb->y = y;
    cb->w = w;
    cb->h = h;
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
    by = cb->y + (cb->h - box) / 2;
    if (ctx->draw_rounded_rect_filled)
        ctx->draw_rounded_rect_filled(ctx->user_data, cb->x, by, box, box, cb->is_radio ? 7 : 3, 0xFF404040u);
    else
        ctx->draw_rect(ctx->user_data, cb->x, by, box, box, 0xFF404040u);
    if (cb->checked && ctx->draw_rect)
        ctx->draw_rect(ctx->user_data, cb->x + 4, by + 4, 6, 6, 0xFFFFFFFFu);

    if (ctx->draw_string && cb->text)
        ctx->draw_string(ctx->user_data, cb->x + box + 6, cb->y + (cb->h - 8) / 2, cb->text, 0xFFFFFFFFu);
}

bool widget_checkbox_handle_mouse(widget_checkbox_t *cb, int mx, int my, bool mouse_clicked, void *user_data)
{
    bool in_bounds;
    if (!cb || !mouse_clicked) return false;
    in_bounds = (mx >= cb->x && mx < cb->x + cb->w &&
                 my >= cb->y && my < cb->y + cb->h);
    if (!in_bounds) return false;
    cb->checked = !cb->checked;
    if (cb->on_toggle) cb->on_toggle(user_data, cb->checked);
    return true;
}
