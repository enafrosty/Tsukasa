/*
 * gui_srv.c - SYS_GUI service implementation.
 */

#include "gui_srv.h"

#include "font_8x8.h"
#include "wm.h"
#include "../drv/fb.h"
#include "../include/spinlock.h"
#include "../input/event.h"
#include "../mm/heap.h"

#include <stddef.h>
#include <stdint.h>

#define GUI_SRV_MAX_WINDOWS 32
#define GUI_SRV_EVENT_Q     128

typedef struct gui_window {
    int used;
    int handle;
    int owner_pid;
    wm_window_t *wm_win;
    uint32_t *pixels;
    int client_w;
    int client_h;
    char title[WM_TITLE_MAX];
    int resizable;
    int dirty_pending;
    int close_policy_autoclose;
    int close_requested;
    uint32_t dropped_events;
    uint32_t dropped_low_priority;
    uint32_t dropped_oldest;
    uint32_t enqueued_events;

    struct tsukasa_gui_event events[GUI_SRV_EVENT_Q];
    int ev_head;
    int ev_tail;
    int ev_count;
} gui_window_t;

static gui_window_t g_windows[GUI_SRV_MAX_WINDOWS];
static int g_next_handle = 1;
static spinlock_t g_gui_lock = SPINLOCK_INIT;
static uint64_t g_drop_events_total;
static uint64_t g_drop_low_priority_total;
static uint64_t g_drop_oldest_total;

static int str_len(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void copy_title(char *dst, int cap, const char *src)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int clip_rect_to_client(gui_window_t *gw,
                               int *x, int *y, int *w, int *h)
{
    int x0, y0, x1, y1;
    if (!gw || !x || !y || !w || !h)
        return 0;
    if (*w <= 0 || *h <= 0)
        return 0;

    x0 = *x;
    y0 = *y;
    x1 = x0 + *w;
    y1 = y0 + *h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > gw->client_w) x1 = gw->client_w;
    if (y1 > gw->client_h) y1 = gw->client_h;
    if (x0 >= x1 || y0 >= y1)
        return 0;
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

static int evt_is_low_priority(const struct tsukasa_gui_event *ev)
{
    if (!ev)
        return 0;
    return (ev->type == INPUT_EVENT_MOUSE_MOVE ||
            ev->type == INPUT_EVENT_PAINT) ? 1 : 0;
}

static int evt_enqueue(gui_window_t *gw, const struct tsukasa_gui_event *ev)
{
    if (!gw || !ev)
        return GUI_ERR_INVALID;

    if (gw->ev_count > 0 && evt_is_low_priority(ev)) {
        int tail = (gw->ev_tail + GUI_SRV_EVENT_Q - 1) % GUI_SRV_EVENT_Q;
        if (gw->events[tail].type == ev->type &&
            gw->events[tail].window == ev->window) {
            gw->events[tail] = *ev;
            return GUI_OK;
        }
    }

    if (gw->ev_count >= GUI_SRV_EVENT_Q) {
        int dropped = 0;
        for (int rel = 0; rel < gw->ev_count; rel++) {
            int idx = (gw->ev_head + rel) % GUI_SRV_EVENT_Q;
            if (!evt_is_low_priority(&gw->events[idx]))
                continue;
            for (int i = rel; i + 1 < gw->ev_count; i++) {
                int dst = (gw->ev_head + i) % GUI_SRV_EVENT_Q;
                int src = (gw->ev_head + i + 1) % GUI_SRV_EVENT_Q;
                gw->events[dst] = gw->events[src];
            }
            gw->ev_tail = (gw->ev_tail + GUI_SRV_EVENT_Q - 1) % GUI_SRV_EVENT_Q;
            gw->ev_count--;
            gw->dropped_events++;
            gw->dropped_low_priority++;
            g_drop_events_total++;
            g_drop_low_priority_total++;
            dropped = 1;
            break;
        }
        if (!dropped) {
            gw->ev_head = (gw->ev_head + 1) % GUI_SRV_EVENT_Q;
            gw->ev_count--;
            gw->dropped_events++;
            gw->dropped_oldest++;
            g_drop_events_total++;
            g_drop_oldest_total++;
        }
    }

    gw->events[gw->ev_tail] = *ev;
    gw->ev_tail = (gw->ev_tail + 1) % GUI_SRV_EVENT_Q;
    gw->ev_count++;
    gw->enqueued_events++;
    return GUI_OK;
}

static gui_window_t *find_slot_by_handle(int handle)
{
    for (int i = 0; i < GUI_SRV_MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].handle == handle)
            return &g_windows[i];
    }
    return NULL;
}

static gui_window_t *find_slot_by_wm(wm_window_t *win)
{
    for (int i = 0; i < GUI_SRV_MAX_WINDOWS; i++) {
        if (g_windows[i].used && g_windows[i].wm_win == win)
            return &g_windows[i];
    }
    return NULL;
}

static void queue_paint_event(gui_window_t *gw, int x, int y, int w, int h)
{
    struct tsukasa_gui_event ev;
    if (!gw)
        return;
    ev.type = INPUT_EVENT_PAINT;
    ev.window = gw->handle;
    ev.x = x;
    ev.y = y;
    ev.keycode = 0;
    ev.data1 = w;
    ev.data2 = h;
    gw->dirty_pending = 1;
    evt_enqueue(gw, &ev);
}

static void draw_char_to_buffer(gui_window_t *gw, int x, int y, char c, uint32_t color)
{
    uint8_t row_bits;
    if (!gw || !gw->pixels)
        return;
    if ((unsigned char)c >= 128)
        return;
    for (int row = 0; row < FONT_HEIGHT; row++) {
        int py = y + row;
        if (py < 0 || py >= gw->client_h)
            continue;
        row_bits = font_8x8[(unsigned char)c][row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int px = x + col;
            if (px < 0 || px >= gw->client_w)
                continue;
            if (row_bits & (1u << (7 - col)))
                gw->pixels[py * gw->client_w + px] = color | 0xFF000000u;
        }
    }
}

static void gui_window_draw(wm_window_t *win)
{
    gui_window_t *gw;
    int cx, cy, cw, ch;
    int draw_w, draw_h;

    if (!win || !fb_info.addr || fb_info.bpp != 32)
        return;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_wm(win);
    if (!gw || !gw->pixels) {
        spin_unlock(&g_gui_lock);
        return;
    }

    wm_client_rect(win, &cx, &cy, &cw, &ch);
    draw_w = (gw->client_w < cw) ? gw->client_w : cw;
    draw_h = (gw->client_h < ch) ? gw->client_h : ch;
    if (draw_w <= 0 || draw_h <= 0) {
        spin_unlock(&g_gui_lock);
        return;
    }

    for (int row = 0; row < draw_h; row++) {
        uint32_t *dst = (uint32_t *)((char *)fb_info.addr +
                                     (uint32_t)(cy + row) * fb_info.pitch +
                                     (uint32_t)cx * 4u);
        uint32_t *src = &gw->pixels[row * gw->client_w];
        for (int col = 0; col < draw_w; col++)
            dst[col] = src[col] | 0xFF000000u;
    }
    spin_unlock(&g_gui_lock);
}

static int reallocate_client_buffer(gui_window_t *gw, int new_w, int new_h)
{
    uint32_t *new_pixels;
    int copy_w;
    int copy_h;

    if (!gw || new_w <= 0 || new_h <= 0)
        return GUI_ERR_INVALID;

    new_pixels = (uint32_t *)kmalloc((size_t)new_w * (size_t)new_h * sizeof(uint32_t));
    if (!new_pixels)
        return GUI_ERR_NOMEM;

    for (int i = 0; i < new_w * new_h; i++)
        new_pixels[i] = 0xFF202226u;

    if (gw->pixels) {
        copy_w = (new_w < gw->client_w) ? new_w : gw->client_w;
        copy_h = (new_h < gw->client_h) ? new_h : gw->client_h;
        for (int row = 0; row < copy_h; row++) {
            for (int col = 0; col < copy_w; col++) {
                new_pixels[row * new_w + col] = gw->pixels[row * gw->client_w + col];
            }
        }
        kfree(gw->pixels);
    }

    gw->pixels = new_pixels;
    gw->client_w = new_w;
    gw->client_h = new_h;
    return GUI_OK;
}

static void gui_window_event(wm_window_t *win, const void *event)
{
    const struct input_event *in = (const struct input_event *)event;
    gui_window_t *gw;
    struct tsukasa_gui_event out;

    if (!win || !in)
        return;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_wm(win);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return;
    }

    out.type = in->event_id;
    out.window = gw->handle;
    out.x = in->x;
    out.y = in->y;
    out.keycode = (int32_t)in->keycode;
    out.data1 = in->wheel_delta;
    out.data2 = (int32_t)in->modifiers;

    if (in->event_id == INPUT_EVENT_RESIZE) {
        int new_w = in->x;
        int new_h = in->y;
        if (new_w > 0 && new_h > 0) {
            if (reallocate_client_buffer(gw, new_w, new_h) == GUI_OK) {
                out.x = 0;
                out.y = 0;
                out.data1 = new_w;
                out.data2 = new_h;
                gw->dirty_pending = 1;
                queue_paint_event(gw, 0, 0, new_w, new_h);
            }
        }
    }
    if (in->event_id == INPUT_EVENT_CLOSE)
        gw->close_requested = 1;

    evt_enqueue(gw, &out);
    spin_unlock(&g_gui_lock);
}

void gui_srv_init(void)
{
    spin_lock(&g_gui_lock);
    for (int i = 0; i < GUI_SRV_MAX_WINDOWS; i++) {
        g_windows[i].used = 0;
        g_windows[i].handle = 0;
        g_windows[i].owner_pid = -1;
        g_windows[i].wm_win = NULL;
        g_windows[i].pixels = NULL;
        g_windows[i].client_w = 0;
        g_windows[i].client_h = 0;
        g_windows[i].title[0] = '\0';
        g_windows[i].resizable = 1;
        g_windows[i].dirty_pending = 0;
        g_windows[i].close_policy_autoclose = 0;
        g_windows[i].close_requested = 0;
        g_windows[i].dropped_events = 0;
        g_windows[i].dropped_low_priority = 0;
        g_windows[i].dropped_oldest = 0;
        g_windows[i].enqueued_events = 0;
        g_windows[i].ev_head = 0;
        g_windows[i].ev_tail = 0;
        g_windows[i].ev_count = 0;
    }
    g_next_handle = 1;
    g_drop_events_total = 0;
    g_drop_low_priority_total = 0;
    g_drop_oldest_total = 0;
    spin_unlock(&g_gui_lock);
}

void gui_srv_process_cleanup(int pid)
{
    for (;;) {
        int handle = -1;
        spin_lock(&g_gui_lock);
        for (int i = 0; i < GUI_SRV_MAX_WINDOWS; i++) {
            if (g_windows[i].used && g_windows[i].owner_pid == pid) {
                handle = g_windows[i].handle;
                break;
            }
        }
        spin_unlock(&g_gui_lock);
        if (handle < 0)
            break;
        gui_srv_window_destroy(pid, handle);
    }
}

int gui_srv_window_create(int pid,
                          const char *title,
                          int x, int y,
                          int client_w, int client_h)
{
    gui_window_t *slot = NULL;
    wm_window_t *win;
    int handle;
    int outer_w;
    int outer_h;

    if (client_w <= 0 || client_h <= 0)
        return GUI_ERR_INVALID;

    spin_lock(&g_gui_lock);
    for (int i = 0; i < GUI_SRV_MAX_WINDOWS; i++) {
        if (!g_windows[i].used) {
            slot = &g_windows[i];
            slot->used = 2; /* reserved */
            break;
        }
    }
    if (!slot) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_FULL;
    }
    spin_unlock(&g_gui_lock);

    outer_w = client_w + (2 * WM_BORDER_PX);
    outer_h = client_h + (2 * WM_BORDER_PX) + WM_TITLE_BAR_H;
    win = wm_create_window(x, y, outer_w, outer_h, title ? title : "App",
                           gui_window_draw, gui_window_event, NULL);
    if (!win) {
        spin_lock(&g_gui_lock);
        slot->used = 0;
        spin_unlock(&g_gui_lock);
        return GUI_ERR_FULL;
    }

    spin_lock(&g_gui_lock);
    handle = g_next_handle++;
    slot->used = 1;
    slot->handle = handle;
    slot->owner_pid = pid;
    slot->wm_win = win;
    slot->pixels = NULL;
    slot->client_w = 0;
    slot->client_h = 0;
    copy_title(slot->title, WM_TITLE_MAX, title ? title : "App");
    slot->resizable = 1;
    slot->dirty_pending = 0;
    slot->close_policy_autoclose = 0;
    slot->close_requested = 0;
    slot->dropped_events = 0;
    slot->dropped_low_priority = 0;
    slot->dropped_oldest = 0;
    slot->enqueued_events = 0;
    slot->ev_head = 0;
    slot->ev_tail = 0;
    slot->ev_count = 0;
    if (reallocate_client_buffer(slot, client_w, client_h) != GUI_OK) {
        slot->used = 0;
        slot->wm_win = NULL;
        spin_unlock(&g_gui_lock);
        wm_destroy_window(win);
        return GUI_ERR_NOMEM;
    }
    queue_paint_event(slot, 0, 0, client_w, client_h);
    spin_unlock(&g_gui_lock);

    wm_set_autoclose(win, slot->close_policy_autoclose);
    wm_set_resizable(win, slot->resizable);
    wm_mark_dirty_rect(win->x, win->y, win->w, win->h);
    return handle;
}

int gui_srv_window_destroy(int pid, int handle)
{
    gui_window_t *gw;
    wm_window_t *win;
    uint32_t *pixels;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    win = gw->wm_win;
    pixels = gw->pixels;
    gw->used = 0;
    gw->wm_win = NULL;
    gw->pixels = NULL;
    gw->client_w = 0;
    gw->client_h = 0;
    gw->title[0] = '\0';
    gw->resizable = 1;
    gw->dirty_pending = 0;
    gw->close_policy_autoclose = 0;
    gw->close_requested = 0;
    gw->dropped_events = 0;
    gw->dropped_low_priority = 0;
    gw->dropped_oldest = 0;
    gw->enqueued_events = 0;
    gw->ev_count = 0;
    spin_unlock(&g_gui_lock);

    if (win)
        wm_destroy_window(win);
    if (pixels)
        kfree(pixels);
    return GUI_OK;
}

int gui_srv_window_set_title(int pid, int handle, const char *title)
{
    gui_window_t *gw;
    if (!title)
        return GUI_ERR_INVALID;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    copy_title(gw->title, WM_TITLE_MAX, title);
    wm_set_title(gw->wm_win, title);
    spin_unlock(&g_gui_lock);
    return GUI_OK;
}

int gui_srv_window_set_resizable(int pid, int handle, int resizable)
{
    gui_window_t *gw;
    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    gw->resizable = resizable ? 1 : 0;
    wm_set_resizable(gw->wm_win, resizable ? 1 : 0);
    spin_unlock(&g_gui_lock);
    return GUI_OK;
}

int gui_srv_draw_rect(int pid, int handle, int x, int y, int w, int h, uint32_t color)
{
    gui_window_t *gw;
    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    if (!clip_rect_to_client(gw, &x, &y, &w, &h)) {
        spin_unlock(&g_gui_lock);
        return GUI_OK;
    }
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++)
            gw->pixels[(y + row) * gw->client_w + (x + col)] = color | 0xFF000000u;
    }
    spin_unlock(&g_gui_lock);
    return gui_srv_mark_dirty(pid, handle, x, y, w, h);
}

int gui_srv_draw_rounded_rect(int pid, int handle,
                              int x, int y, int w, int h, int radius,
                              uint32_t color)
{
    gui_window_t *gw;
    int draw_x = x;
    int draw_y = y;
    int draw_w = w;
    int draw_h = h;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    if (!clip_rect_to_client(gw, &draw_x, &draw_y, &draw_w, &draw_h)) {
        spin_unlock(&g_gui_lock);
        return GUI_OK;
    }
    if (radius < 0)
        radius = 0;
    if (radius > draw_w / 2)
        radius = draw_w / 2;
    if (radius > draw_h / 2)
        radius = draw_h / 2;

    for (int py = draw_y; py < draw_y + draw_h; py++) {
        for (int px = draw_x; px < draw_x + draw_w; px++) {
            int dx = 0;
            int dy = 0;
            if (px < draw_x + radius)
                dx = draw_x + radius - px;
            else if (px >= draw_x + draw_w - radius)
                dx = px - (draw_x + draw_w - radius - 1);
            if (py < draw_y + radius)
                dy = draw_y + radius - py;
            else if (py >= draw_y + draw_h - radius)
                dy = py - (draw_y + draw_h - radius - 1);
            if (dx == 0 || dy == 0 || (dx * dx + dy * dy) <= radius * radius) {
                gw->pixels[py * gw->client_w + px] = color | 0xFF000000u;
            }
        }
    }
    spin_unlock(&g_gui_lock);
    return gui_srv_mark_dirty(pid, handle, draw_x, draw_y, draw_w, draw_h);
}

int gui_srv_draw_text(int pid, int handle, int x, int y, const char *text, uint32_t color)
{
    gui_window_t *gw;
    if (!text)
        return GUI_ERR_INVALID;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    for (int i = 0; text[i]; i++)
        draw_char_to_buffer(gw, x + i * FONT_WIDTH, y, text[i], color);
    spin_unlock(&g_gui_lock);
    return gui_srv_mark_dirty(pid, handle, x, y, str_len(text) * FONT_WIDTH, FONT_HEIGHT);
}

int gui_srv_draw_image(int pid, int handle,
                       int x, int y, int w, int h,
                       const uint32_t *pixels)
{
    gui_window_t *gw;
    int draw_x = x;
    int draw_y = y;
    int draw_w = w;
    int draw_h = h;
    int src_x = 0;
    int src_y = 0;

    if (!pixels || w <= 0 || h <= 0)
        return GUI_ERR_INVALID;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }

    if (draw_x < 0) {
        src_x = -draw_x;
        draw_w += draw_x;
        draw_x = 0;
    }
    if (draw_y < 0) {
        src_y = -draw_y;
        draw_h += draw_y;
        draw_y = 0;
    }
    if (draw_x + draw_w > gw->client_w)
        draw_w = gw->client_w - draw_x;
    if (draw_y + draw_h > gw->client_h)
        draw_h = gw->client_h - draw_y;
    if (draw_w <= 0 || draw_h <= 0) {
        spin_unlock(&g_gui_lock);
        return GUI_OK;
    }

    for (int row = 0; row < draw_h; row++) {
        for (int col = 0; col < draw_w; col++) {
            uint32_t c = pixels[(src_y + row) * w + (src_x + col)];
            gw->pixels[(draw_y + row) * gw->client_w + (draw_x + col)] = c | 0xFF000000u;
        }
    }
    spin_unlock(&g_gui_lock);
    return gui_srv_mark_dirty(pid, handle, draw_x, draw_y, draw_w, draw_h);
}

int gui_srv_mark_dirty(int pid, int handle, int x, int y, int w, int h)
{
    gui_window_t *gw;
    int cx, cy, cw, ch;
    int mx = x;
    int my = y;
    int mw = w;
    int mh = h;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    if (!clip_rect_to_client(gw, &mx, &my, &mw, &mh)) {
        spin_unlock(&g_gui_lock);
        return GUI_OK;
    }
    gw->dirty_pending = 1;
    wm_client_rect(gw->wm_win, &cx, &cy, &cw, &ch);
    spin_unlock(&g_gui_lock);

    wm_mark_dirty_rect(cx + mx, cy + my, mw, mh);
    return GUI_OK;
}

int gui_srv_get_event(int pid, int handle, struct tsukasa_gui_event *out)
{
    gui_window_t *gw;
    if (!out)
        return GUI_ERR_INVALID;

    spin_lock(&g_gui_lock);
    gw = find_slot_by_handle(handle);
    if (!gw) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_NOTFOUND;
    }
    if (gw->owner_pid != pid) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_PERM;
    }
    if (gw->ev_count == 0) {
        spin_unlock(&g_gui_lock);
        return GUI_ERR_AGAIN;
    }
    *out = gw->events[gw->ev_head];
    gw->ev_head = (gw->ev_head + 1) % GUI_SRV_EVENT_Q;
    gw->ev_count--;
    if (out->type == INPUT_EVENT_PAINT)
        gw->dirty_pending = 0;
    spin_unlock(&g_gui_lock);
    return GUI_OK;
}

int gui_srv_get_string_width(const char *str)
{
    return str_len(str) * FONT_WIDTH;
}

int gui_srv_get_font_height(void)
{
    return FONT_HEIGHT;
}

int gui_srv_get_screen_size(uint64_t *out_w, uint64_t *out_h)
{
    if (!out_w || !out_h)
        return GUI_ERR_INVALID;
    *out_w = fb_info.width;
    *out_h = fb_info.height;
    return GUI_OK;
}
