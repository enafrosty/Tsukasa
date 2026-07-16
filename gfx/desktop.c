/*
 * desktop.c - Desktop shell with dirty-region compositing and launcher.
 */

#include "desktop.h"
#include "blit.h"
#include "bmp.h"
#include "cursor.h"
#include "font.h"
#include "gui_srv.h"
#include "theme.h"
#include "ui.h"
#include "wm.h"
#include "../drv/fb.h"
#include "../drv/ps2mouse.h"
#include "../fs/vfs.h"
#include "../include/kprintf.h"
#include "../input/event.h"
#include "../mm/heap.h"
#include "../syscall/syscall.h"

#include <stddef.h>
#include <stdint.h>

#define ICON_SIZE       48
#define ICON_GAP        22
#define ICON_COL_GAP    34
#define ICON_LABEL_GAP   8
#define ICON_START_X    28
#define ICON_START_Y    26
#define MAX_ICONS       10
#define TASKBAR_H       46
#define DESKTOP_FONT_H   8
#define MENU_W         248
#define MENU_ITEM_H     30
#define MENU_HEADER_H   34
#define MENU_PAD         8
#define MENU_ITEMS       8
#define DESKTOP_DIRTY_MAX 128

#define LAUNCHER_W     468
#define LAUNCHER_Q_H    38
#define LAUNCHER_ROW_H  28
#define LAUNCHER_MAX_MATCH 8

typedef void (*app_launcher_fn)(void);

struct desktop_icon {
    int x;
    int y;
    int icon_type;
    const char *label;
    app_launcher_fn launch;
};

struct launcher_entry {
    const char *label;
    app_launcher_fn launch;
};

struct taskbar_pill_hit {
    int x;
    int y;
    int w;
    int h;
    wm_window_t *win;
};

static void launch_notepad(void);
static void launch_filemgr(void);
static void launch_settings(void);
static void launch_calc(void);
static void launch_terminal(void);
static void launch_about(void);
static void launch_taskmgr(void);
static void launch_network(void);

static struct desktop_icon icons[MAX_ICONS];
static int num_icons;

static int taskbar_y;
static int start_btn_x, start_btn_y, start_btn_w, start_btn_h;
static int start_menu_open;

/* Start menu placement. */
static int menu_x, menu_y;

/* Global launcher state. */
static int launcher_open;
static char launcher_query[48];
static int launcher_query_len;
static int launcher_matches[LAUNCHER_MAX_MATCH];
static int launcher_match_count;
static int launcher_selected;
static int launcher_x, launcher_y, launcher_h;
static int launcher_hover;

static int alt_down;
static int cursor_last_x, cursor_last_y;

static uint32_t wallpaper_bg_mode = DESKTOP_BG_MODE_GRADIENT;
static uint32_t wallpaper_solid_color = THEME_BG_BOT;
static uint32_t wallpaper_style = DESKTOP_WALLPAPER_SCALE_FILL;
static char wallpaper_path[128] = "";
static uint32_t *wallpaper_pixels;
static struct taskbar_pill_hit taskbar_pills[WM_MAX_WINDOWS];
static int taskbar_pill_count;
static int start_menu_hover = -1;

static const char *menu_labels[MENU_ITEMS] = {
    "Files", "Notepad", "Settings", "Calculator",
    "Terminal", "Diagnostics", "Network", "About"
};
static app_launcher_fn menu_launchers[MENU_ITEMS];

static const struct launcher_entry launcher_catalog[] = {
    { "Files", launch_filemgr },
    { "Notepad", launch_notepad },
    { "Settings", launch_settings },
    { "Calculator", launch_calc },
    { "Terminal", launch_terminal },
    { "Diagnostics", launch_taskmgr },
    { "Network", launch_network },
    { "About", launch_about },
};

static void launch_user_app(const char *path, const char *args)
{
    struct tsukasa_spawn_request req;
    req.path = path;
    req.args = args ? args : path;
    req.stdin_fd = 0;
    req.stdout_fd = 1;
    req.stderr_fd = 2;
    req.tty_id = -1;
    if ((intptr_t)syscall_handler(SYS_SYSTEM,
                                  SYSTEM_CMD_SPAWN_EX,
                                  (uintptr_t)&req,
                                  0, 0, 0) < 0) {
        kprintf("[desktop] launch failed: %s\n", path ? path : "(null)");
    }
}

static void launch_notepad(void)   { launch_user_app("/apps/notepad", "/apps/notepad"); }
static void launch_filemgr(void)   { launch_user_app("/apps/filemgr", "/apps/filemgr"); }
static void launch_settings(void)  { launch_user_app("/apps/settings", "/apps/settings"); }
static void launch_calc(void)      { launch_user_app("/apps/calc", "/apps/calc"); }
static void launch_terminal(void)  { launch_user_app("/apps/terminal", "/apps/terminal"); }
static void launch_about(void)     { launch_user_app("/apps/about", "/apps/about"); }
static void launch_taskmgr(void)   { launch_user_app("/apps/diagnostics", "/apps/diagnostics"); }
static void launch_network(void)   { launch_user_app("/apps/network", "/apps/network"); }

static void copy_small_string(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_len(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void draw_text_plain(int x, int y, const char *s, color_t fg, color_t bg)
{
    int cx = x;
    if (!s)
        return;
    for (int i = 0; s[i]; i++) {
        fb_draw_char(cx, y, s[i], fg, bg);
        cx += 8;
    }
}

static void draw_text_trunc(int x, int y, int max_w, const char *s, color_t fg, color_t bg)
{
    int len;
    int max_chars;
    int cx = x;
    if (!s || max_w <= 0)
        return;

    len = str_len(s);
    max_chars = max_w / 8;
    if (max_chars <= 0)
        return;

    if (len <= max_chars) {
        draw_text_plain(x, y, s, fg, bg);
        return;
    }

    if (max_chars <= 3) {
        for (int i = 0; i < max_chars; i++) {
            fb_draw_char(cx, y, '.', fg, bg);
            cx += 8;
        }
        return;
    }

    for (int i = 0; i < max_chars - 3; i++) {
        fb_draw_char(cx, y, s[i], fg, bg);
        cx += 8;
    }
    fb_draw_char(cx, y, '.', fg, bg); cx += 8;
    fb_draw_char(cx, y, '.', fg, bg); cx += 8;
    fb_draw_char(cx, y, '.', fg, bg);
}

static int intersects(int ax, int ay, int aw, int ah,
                      int bx, int by, int bw, int bh)
{
    int ax1 = ax + aw;
    int ay1 = ay + ah;
    int bx1 = bx + bw;
    int by1 = by + bh;
    return !(ax1 <= bx || bx1 <= ax || ay1 <= by || by1 <= ay);
}

static void write_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int save_screenshot_bmp(const char *path)
{
    int fd;
    uint8_t hdr[54];
    uint32_t w = fb_info.width;
    uint32_t h = fb_info.height;
    uint32_t row_bytes = w * 4u;
    uint32_t image_bytes = row_bytes * h;
    uint32_t file_bytes = 54u + image_bytes;

    if (!path || !fb_info.addr || fb_info.bpp != 32)
        return -1;

    for (int i = 0; i < 54; i++)
        hdr[i] = 0;
    hdr[0] = 'B';
    hdr[1] = 'M';
    write_u32le(&hdr[2], file_bytes);
    write_u32le(&hdr[10], 54u);
    write_u32le(&hdr[14], 40u);
    write_u32le(&hdr[18], w);
    write_u32le(&hdr[22], h);
    write_u16le(&hdr[26], 1u);
    write_u16le(&hdr[28], 32u);
    write_u32le(&hdr[34], image_bytes);

    fd = vfs_create(path);
    if (fd < 0)
        return -1;

    if (vfs_write(fd, hdr, sizeof(hdr)) != sizeof(hdr)) {
        vfs_close(fd);
        return -1;
    }

    for (int y = (int)h - 1; y >= 0; y--) {
        const void *row = (const uint8_t *)fb_info.addr + (uint32_t)y * fb_info.pitch;
        if (vfs_write(fd, row, row_bytes) != row_bytes) {
            vfs_close(fd);
            return -1;
        }
    }

    vfs_close(fd);
    return 0;
}

/* ---- Wallpaper / background ------------------------------------------ */

static void release_wallpaper_cache(void)
{
    if (wallpaper_pixels) {
        kfree(wallpaper_pixels);
        wallpaper_pixels = NULL;
    }
}

static color_t desktop_gradient_at_y(int py)
{
    uint8_t tr = (THEME_BG_TOP >> 16) & 0xFF;
    uint8_t tg = (THEME_BG_TOP >> 8) & 0xFF;
    uint8_t tb = THEME_BG_TOP & 0xFF;
    uint8_t br = (THEME_BG_BOT >> 16) & 0xFF;
    uint8_t bg = (THEME_BG_BOT >> 8) & 0xFF;
    uint8_t bb = THEME_BG_BOT & 0xFF;
    int sh = (int)fb_info.height;
    uint32_t t = (sh > 1) ? ((uint32_t)py * 255u) / (uint32_t)(sh - 1) : 0u;
    uint32_t it = 255u - t;
    uint8_t r = (uint8_t)((tr * it + br * t) >> 8);
    uint8_t g = (uint8_t)((tg * it + bg * t) >> 8);
    uint8_t b = (uint8_t)((tb * it + bb * t) >> 8);
    return rgb(r, g, b);
}

static void fill_gradient_region(int x, int y, int w, int h)
{
    for (int row = 0; row < h; row++)
        fb_fill_rect(x, y + row, w, 1, desktop_gradient_at_y(y + row));
}

static int build_wallpaper_cache(const char *path)
{
    int bw = 0;
    int bh = 0;
    int sw = (int)fb_info.width;
    int sh = (int)fb_info.height;
    uint32_t *raw_pixels = NULL;

    if (!path || !path[0] || sw <= 0 || sh <= 0)
        return -1;
    if (bmp_load_to_buf(path, &raw_pixels, &bw, &bh) != 0 || !raw_pixels)
        return -1;
    if (bw <= 0 || bh <= 0) {
        kfree(raw_pixels);
        return -1;
    }

    wallpaper_pixels = (uint32_t *)kmalloc((size_t)sw * (size_t)sh * sizeof(uint32_t));
    if (!wallpaper_pixels) {
        kfree(raw_pixels);
        return -1;
    }

    if (wallpaper_style == DESKTOP_WALLPAPER_CENTER) {
        int draw_w = (bw < sw) ? bw : sw;
        int draw_h = (bh < sh) ? bh : sh;
        int dst_x = (sw - draw_w) / 2;
        int dst_y = (sh - draw_h) / 2;
        int src_x0 = (bw - draw_w) / 2;
        int src_y0 = (bh - draw_h) / 2;

        for (int y = 0; y < sh; y++) {
            uint32_t row_color = desktop_gradient_at_y(y);
            uint32_t *dst = &wallpaper_pixels[y * sw];
            for (int x = 0; x < sw; x++)
                dst[x] = row_color;
        }

        for (int y = 0; y < draw_h; y++) {
            uint32_t *dst = &wallpaper_pixels[(dst_y + y) * sw + dst_x];
            const uint32_t *src = &raw_pixels[(src_y0 + y) * bw + src_x0];
            for (int x = 0; x < draw_w; x++)
                dst[x] = src[x] | 0xFF000000u;
        }
    } else {
        int scaled_w;
        int scaled_h;
        int crop_x;
        int crop_y;

        if ((uint64_t)sw * (uint64_t)bh >= (uint64_t)sh * (uint64_t)bw) {
            scaled_w = sw;
            scaled_h = (int)(((uint64_t)bh * (uint64_t)sw + (uint64_t)bw - 1u) / (uint64_t)bw);
        } else {
            scaled_h = sh;
            scaled_w = (int)(((uint64_t)bw * (uint64_t)sh + (uint64_t)bh - 1u) / (uint64_t)bh);
        }
        crop_x = (scaled_w - sw) / 2;
        crop_y = (scaled_h - sh) / 2;

        for (int y = 0; y < sh; y++) {
            int src_y = (int)(((uint64_t)(y + crop_y) * (uint64_t)bh) / (uint64_t)scaled_h);
            uint32_t *dst = &wallpaper_pixels[y * sw];
            if (src_y < 0) src_y = 0;
            if (src_y >= bh) src_y = bh - 1;
            for (int x = 0; x < sw; x++) {
                int src_x = (int)(((uint64_t)(x + crop_x) * (uint64_t)bw) / (uint64_t)scaled_w);
                if (src_x < 0) src_x = 0;
                if (src_x >= bw) src_x = bw - 1;
                dst[x] = raw_pixels[src_y * bw + src_x] | 0xFF000000u;
            }
        }
    }

    kfree(raw_pixels);
    return 0;
}

int desktop_set_wallpaper(const char *path)
{
    release_wallpaper_cache();

    if (!path || path[0] == '\0') {
        wallpaper_path[0] = '\0';
        wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
        return 0;
    }
    if (str_len(path) >= (int)sizeof(wallpaper_path)) {
        wallpaper_path[0] = '\0';
        wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
        return -1;
    }

    if (build_wallpaper_cache(path) != 0) {
        wallpaper_path[0] = '\0';
        wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
        return -1;
    }

    copy_small_string(wallpaper_path, sizeof(wallpaper_path), path);
    wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
    return 0;
}

int desktop_apply_theme(uint32_t accent_color,
                        uint32_t background_mode,
                        uint32_t solid_color,
                        uint32_t requested_wallpaper_style,
                        const char *requested_wallpaper_path)
{
    if (((accent_color >> 24) & 0xFFu) != 0xFFu)
        return -1;
    if (((solid_color >> 24) & 0xFFu) != 0xFFu)
        return -1;
    if (background_mode > DESKTOP_BG_MODE_WALLPAPER)
        return -1;
    if (requested_wallpaper_style > DESKTOP_WALLPAPER_CENTER)
        return -1;

    g_accent_color = accent_color;
    wallpaper_solid_color = solid_color;
    wallpaper_style = requested_wallpaper_style;

    if (requested_wallpaper_path)
        copy_small_string(wallpaper_path, sizeof(wallpaper_path), requested_wallpaper_path);

    if (background_mode == DESKTOP_BG_MODE_WALLPAPER) {
        if (!requested_wallpaper_path || requested_wallpaper_path[0] == '\0') {
            wallpaper_bg_mode = DESKTOP_BG_MODE_GRADIENT;
            release_wallpaper_cache();
            wallpaper_path[0] = '\0';
            wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
            return -1;
        }
        if (desktop_set_wallpaper(requested_wallpaper_path) != 0) {
            wallpaper_bg_mode = DESKTOP_BG_MODE_GRADIENT;
            wallpaper_path[0] = '\0';
            wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
            return -1;
        }
        wallpaper_bg_mode = DESKTOP_BG_MODE_WALLPAPER;
        return 0;
    }

    wallpaper_bg_mode = background_mode;
    release_wallpaper_cache();
    wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
    return 0;
}

static void draw_desktop_bg(void)
{
    if (wallpaper_bg_mode == DESKTOP_BG_MODE_WALLPAPER && wallpaper_pixels) {
        int sw = (int)fb_info.width;
        int sh = (int)fb_info.height;
        for (int y = 0; y < sh; y++) {
            uint32_t *fb_row = (uint32_t *)((char *)fb_info.addr + (uint32_t)y * fb_info.pitch);
            uint32_t *src_row = &wallpaper_pixels[y * sw];
            for (int x = 0; x < sw; x++)
                fb_row[x] = src_row[x];
        }
        return;
    }

    if (wallpaper_bg_mode == DESKTOP_BG_MODE_SOLID) {
        fb_fill_rect(0, 0, (int)fb_info.width, (int)fb_info.height, (color_t)wallpaper_solid_color);
        return;
    }

    fb_fill_gradient_v(0, 0, (int)fb_info.width, (int)fb_info.height,
                       (color_t)THEME_BG_TOP, (color_t)THEME_BG_BOT);
}

static void draw_desktop_bg_region(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (wallpaper_bg_mode == DESKTOP_BG_MODE_WALLPAPER && wallpaper_pixels) {
        int sw = (int)fb_info.width;
        for (int row = 0; row < h; row++) {
            int py = y + row;
            uint32_t *fb_row = (uint32_t *)((char *)fb_info.addr + (uint32_t)py * fb_info.pitch) + x;
            uint32_t *src_row = &wallpaper_pixels[py * sw + x];
            for (int col = 0; col < w; col++)
                fb_row[col] = src_row[col];
        }
        return;
    }

    if (wallpaper_bg_mode == DESKTOP_BG_MODE_SOLID) {
        fb_fill_rect(x, y, w, h, (color_t)wallpaper_solid_color);
        return;
    }

    fill_gradient_region(x, y, w, h);
}

/* ---- Icons ------------------------------------------------------------ */

static void add_icon(const char *label, int icon_type, app_launcher_fn fn)
{
    struct desktop_icon *ic;
    int row_h = ICON_SIZE + ICON_GAP + DESKTOP_FONT_H + ICON_LABEL_GAP;
    int usable_h = (int)fb_info.height - TASKBAR_H - ICON_START_Y - 18;
    int per_col = usable_h / row_h;
    int col;
    int row;
    if (num_icons >= MAX_ICONS)
        return;
    if (per_col < 1)
        per_col = 1;

    col = num_icons / per_col;
    row = num_icons % per_col;

    ic = &icons[num_icons];
    ic->x = ICON_START_X + col * (ICON_SIZE + ICON_COL_GAP + 36);
    ic->y = ICON_START_Y + row * row_h;
    ic->icon_type = icon_type;
    ic->label = label;
    ic->launch = fn;
    num_icons++;
}

static void setup_icons(void)
{
    num_icons = 0;
    add_icon("Files", 0, launch_filemgr);
    add_icon("Notepad", 1, launch_notepad);
    add_icon("Settings", 4, launch_settings);
    add_icon("Calc", 5, launch_calc);
    add_icon("Terminal", 3, launch_terminal);
    add_icon("Diagnostics", 3, launch_taskmgr);
    add_icon("Network", 3, launch_network);
    add_icon("About", 6, launch_about);
}

static void draw_icon_item(const struct desktop_icon *ic)
{
    int lx, ly;
    const char *lbl = ic->label;
    fb_fill_rounded_rect(ic->x - 8, ic->y - 8,
                         ICON_SIZE + 16, ICON_SIZE + DESKTOP_FONT_H + ICON_LABEL_GAP + 16,
                         8, rgba(10, 18, 28, 128));
    fb_draw_rounded_rect(ic->x - 8, ic->y - 8,
                         ICON_SIZE + 16, ICON_SIZE + DESKTOP_FONT_H + ICON_LABEL_GAP + 16,
                         8, rgba(120, 145, 170, 45));
    ui_draw_icon(ic->x, ic->y, ICON_SIZE, ic->icon_type);
    lx = ic->x - 10;
    ly = ic->y + ICON_SIZE + ICON_LABEL_GAP;
    draw_text_trunc(lx, ly, ICON_SIZE + 20, lbl, (color_t)THEME_TEXT, 0x00000000u);
}

static void draw_icons(void)
{
    for (int i = 0; i < num_icons; i++)
        draw_icon_item(&icons[i]);
}

static void draw_icons_region(int x, int y, int w, int h)
{
    for (int i = 0; i < num_icons; i++) {
        const struct desktop_icon *ic = &icons[i];
        int iw = ICON_SIZE + 16;
        int ih = ICON_SIZE + DESKTOP_FONT_H + ICON_LABEL_GAP + 16;
        if (!intersects(x, y, w, h, ic->x - 8, ic->y - 8, iw, ih))
            continue;
        draw_icon_item(ic);
    }
}

/* ---- Taskbar / start menu -------------------------------------------- */

static int start_menu_height(void)
{
    return MENU_HEADER_H + MENU_PAD * 2 + MENU_ITEMS * MENU_ITEM_H;
}

static wm_window_t *hit_taskbar_pill(int mx, int my)
{
    for (int i = 0; i < taskbar_pill_count; i++) {
        const struct taskbar_pill_hit *pill = &taskbar_pills[i];
        if (mx >= pill->x && mx < pill->x + pill->w &&
            my >= pill->y && my < pill->y + pill->h)
            return pill->win;
    }
    return NULL;
}

static void draw_taskbar(void)
{
    int right_zone_w = 152;
    int pill_h;
    int px;

    taskbar_pill_count = 0;
    taskbar_y = (int)fb_info.height - TASKBAR_H;
    fb_fill_rect_alpha(0, taskbar_y, (int)fb_info.width, TASKBAR_H, rgba(12, 18, 28, 228));
    fb_draw_hline(0, taskbar_y, (int)fb_info.width, rgba(120, 148, 176, 64));
    fb_draw_hline(0, taskbar_y + 1, (int)fb_info.width, rgba(20, 28, 40, 180));

    start_btn_w = 82;
    start_btn_h = 30;
    start_btn_x = 10;
    start_btn_y = taskbar_y + (TASKBAR_H - start_btn_h) / 2;
    fb_fill_rounded_rect(start_btn_x, start_btn_y, start_btn_w, start_btn_h, 10,
                         start_menu_open ? (color_t)g_accent_color : rgba(26, 39, 56, 240));
    fb_draw_rounded_rect(start_btn_x, start_btn_y, start_btn_w, start_btn_h, 10,
                         rgba(140, 170, 200, 90));

    {
        const char *sl = "Tsukasa";
        int tx = start_btn_x + (start_btn_w - 7 * 8) / 2;
        int ty = start_btn_y + (start_btn_h - 8) / 2;
        draw_text_plain(tx, ty, sl, (color_t)THEME_TEXT, 0x00000000u);
    }

    pill_h = TASKBAR_H - 14;
    px = start_btn_x + start_btn_w + 14;
    for (wm_window_t *w = wm_get_bottom(); w; w = w->next) {
        int active;
        int tw;
        int pill_w;
        int ty;
        color_t bg;
        if (!(w->flags & WM_FLAG_VISIBLE))
            continue;

        tw = str_len(w->title) * 8 + 28;
        if (tw < 92) tw = 92;
        if (tw > 184) tw = 184;
        pill_w = tw;

        if (px + pill_w > (int)fb_info.width - right_zone_w - 10)
            break;

        active = (w->flags & WM_FLAG_ACTIVE) != 0;
        bg = active ? alpha_blend_color((color_t)g_accent_color, rgba(18, 26, 36, 255), 200)
                    : rgba(30, 42, 58, 225);

        ty = taskbar_y + (TASKBAR_H - pill_h) / 2;
        fb_fill_rounded_rect(px, ty, pill_w, pill_h, 9, bg);
        fb_draw_rounded_rect(px, ty, pill_w, pill_h, 9,
                             active ? rgba(195, 232, 255, 90) : rgba(110, 135, 162, 56));
        draw_text_trunc(px + 12, ty + (pill_h - 8) / 2, pill_w - 18,
                        w->title[0] ? w->title : "Untitled",
                        (color_t)THEME_TEXT, 0x00000000u);

        if (taskbar_pill_count < WM_MAX_WINDOWS) {
            taskbar_pills[taskbar_pill_count].x = px;
            taskbar_pills[taskbar_pill_count].y = ty;
            taskbar_pills[taskbar_pill_count].w = pill_w;
            taskbar_pills[taskbar_pill_count].h = pill_h;
            taskbar_pills[taskbar_pill_count].win = w;
            taskbar_pill_count++;
        }

        px += pill_w + 8;
    }

    {
        const char *clk = "Alt+Space";
        int cx = (int)fb_info.width - right_zone_w + 42;
        int cy = taskbar_y + (TASKBAR_H - 8) / 2;
        fb_fill_rounded_rect((int)fb_info.width - right_zone_w, taskbar_y + 8,
                             right_zone_w - 10, TASKBAR_H - 16, 9, rgba(24, 33, 48, 220));
        draw_text_plain(cx, cy, clk, (color_t)THEME_TEXT_DIM, 0x00000000u);
    }
}

static void draw_start_menu(void)
{
    int mh;
    if (!start_menu_open)
        return;

    mh = start_menu_height();
    menu_x = start_btn_x;
    menu_y = taskbar_y - mh - 8;

    fb_fill_rounded_rect(menu_x, menu_y, MENU_W, mh, 10, rgba(12, 18, 28, 238));
    fb_draw_rounded_rect(menu_x, menu_y, MENU_W, mh, 10, rgba(130, 158, 188, 72));
    fb_fill_rounded_rect(menu_x + 1, menu_y + 1, MENU_W - 2, MENU_HEADER_H, 9,
                         alpha_blend_color((color_t)g_accent_color, rgba(8, 14, 24, 255), 178));

    {
        draw_text_plain(menu_x + 14, menu_y + (MENU_HEADER_H - 8) / 2,
                        "Launch Apps", (color_t)THEME_TEXT, 0x00000000u);
    }

    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = menu_y + MENU_HEADER_H + MENU_PAD + i * MENU_ITEM_H;
        int ix = menu_x + 16;
        const char *lbl = menu_labels[i];
        color_t row_bg = (i == start_menu_hover) ? rgba(34, 62, 88, 220) : rgba(0, 0, 0, 0);
        if (i == start_menu_hover)
            fb_fill_rounded_rect(menu_x + 8, iy + 1, MENU_W - 16, MENU_ITEM_H - 2, 7, row_bg);
        fb_fill_rounded_rect(ix - 8, iy + 6, 12, 12, 3,
                             (i == start_menu_hover) ? (color_t)g_accent_color : rgba(86, 108, 132, 210));
        draw_text_trunc(ix + 12, iy + (MENU_ITEM_H - 8) / 2,
                        MENU_W - 44, lbl,
                        (color_t)THEME_TEXT, 0x00000000u);
    }
}

/* ---- Launcher --------------------------------------------------------- */

static int fuzzy_score(const char *query, const char *candidate)
{
    int qi = 0;
    int score = 0;
    if (!query || !candidate)
        return -1;
    if (query[0] == '\0')
        return 0;
    for (int ci = 0; candidate[ci]; ci++) {
        char qc = query[qi];
        char cc = candidate[ci];
        if (qc >= 'A' && qc <= 'Z') qc = (char)(qc + 32);
        if (cc >= 'A' && cc <= 'Z') cc = (char)(cc + 32);
        if (qc == cc) {
            score += ci;
            qi++;
            if (!query[qi])
                return score;
        }
    }
    return -1;
}

static void launcher_update_layout(void)
{
    int rows = (launcher_match_count > 0) ? launcher_match_count : 1;
    launcher_x = ((int)fb_info.width - LAUNCHER_W) / 2;
    launcher_h = LAUNCHER_Q_H + rows * LAUNCHER_ROW_H + 14;
    launcher_y = ((int)fb_info.height - launcher_h) / 4;
}

static void launcher_update_matches(void)
{
    int scores[LAUNCHER_MAX_MATCH];
    int total = (int)(sizeof(launcher_catalog) / sizeof(launcher_catalog[0]));
    launcher_match_count = 0;
    launcher_hover = -1;

    for (int i = 0; i < LAUNCHER_MAX_MATCH; i++)
        scores[i] = 0x7FFFFFFF;

    for (int i = 0; i < total; i++) {
        int score = fuzzy_score(launcher_query, launcher_catalog[i].label);
        if (score < 0)
            continue;
        if (launcher_match_count < LAUNCHER_MAX_MATCH) {
            int pos = launcher_match_count++;
            while (pos > 0 && scores[pos - 1] > score) {
                scores[pos] = scores[pos - 1];
                launcher_matches[pos] = launcher_matches[pos - 1];
                pos--;
            }
            scores[pos] = score;
            launcher_matches[pos] = i;
        } else if (score < scores[launcher_match_count - 1]) {
            int pos = launcher_match_count - 1;
            while (pos > 0 && scores[pos - 1] > score) {
                scores[pos] = scores[pos - 1];
                launcher_matches[pos] = launcher_matches[pos - 1];
                pos--;
            }
            scores[pos] = score;
            launcher_matches[pos] = i;
        }
    }

    if (launcher_match_count <= 0) {
        launcher_selected = -1;
    } else {
        if (launcher_selected >= launcher_match_count)
            launcher_selected = launcher_match_count - 1;
        if (launcher_selected < 0)
            launcher_selected = 0;
    }
    launcher_update_layout();
}

static void launcher_set_open(int open)
{
    if (open) {
        if (start_menu_open) {
            start_menu_open = 0;
            start_menu_hover = -1;
        }
        launcher_open = 1;
        launcher_query[0] = '\0';
        launcher_query_len = 0;
        launcher_selected = 0;
        launcher_hover = -1;
        launcher_update_matches();
    } else {
        launcher_open = 0;
    }
    wm_mark_dirty_rect(0, 0, (int)fb_info.width, (int)fb_info.height);
}

static void draw_launcher(void)
{
    if (!launcher_open)
        return;

    launcher_update_layout();
    fb_fill_rect_alpha(0, 0, (int)fb_info.width, (int)fb_info.height - TASKBAR_H, rgba(2, 4, 8, 96));
    fb_fill_rounded_rect(launcher_x, launcher_y, LAUNCHER_W, launcher_h, 12, rgba(10, 16, 26, 246));
    fb_draw_rounded_rect(launcher_x, launcher_y, LAUNCHER_W, launcher_h, 12, rgba(140, 170, 204, 86));
    fb_fill_rounded_rect(launcher_x + 8, launcher_y + 8, LAUNCHER_W - 16, LAUNCHER_Q_H - 10, 8,
                         rgba(4, 8, 14, 255));
    fb_draw_rounded_rect(launcher_x + 8, launcher_y + 8, LAUNCHER_W - 16, LAUNCHER_Q_H - 10, 8,
                         alpha_blend_color((color_t)g_accent_color, rgba(60, 90, 120, 255), 110));

    {
        const char *prefix = "Search";
        int px = launcher_x + 14;
        int py = launcher_y + 19;
        draw_text_plain(px, py, prefix, (color_t)THEME_TEXT_DIM, 0x00000000u);
        draw_text_plain(px + 58, py, ":", (color_t)THEME_TEXT_DIM, 0x00000000u);
        draw_text_trunc(px + 72, py, LAUNCHER_W - 94,
                        launcher_query_len > 0 ? launcher_query : "(type to filter)",
                        launcher_query_len > 0 ? (color_t)THEME_TEXT : (color_t)THEME_TEXT_DIM,
                        0x00000000u);
    }

    if (launcher_match_count <= 0) {
        int iy = launcher_y + LAUNCHER_Q_H + (LAUNCHER_ROW_H - 8) / 2;
        draw_text_plain(launcher_x + 20, iy, "No matches", (color_t)THEME_TEXT_DIM, 0x00000000u);
    } else {
        for (int i = 0; i < launcher_match_count; i++) {
            int idx = launcher_matches[i];
            int iy = launcher_y + LAUNCHER_Q_H + i * LAUNCHER_ROW_H;
            int highlighted = (i == launcher_selected) || (i == launcher_hover);
            color_t bg = highlighted ? rgba(32, 60, 88, 235) : rgba(12, 20, 32, 220);
            fb_fill_rounded_rect(launcher_x + 10, iy + 1, LAUNCHER_W - 20, LAUNCHER_ROW_H - 2, 6, bg);
            if (i == launcher_selected)
                fb_draw_rounded_rect(launcher_x + 10, iy + 1, LAUNCHER_W - 20, LAUNCHER_ROW_H - 2, 6,
                                     alpha_blend_color((color_t)g_accent_color, rgba(140, 176, 214, 255), 110));
            draw_text_trunc(launcher_x + 18, iy + 10, LAUNCHER_W - 36,
                            launcher_catalog[idx].label,
                            highlighted ? (color_t)THEME_TEXT : (color_t)THEME_TEXT_DIM,
                            0x00000000u);
        }
    }
}

static int launcher_hit_item(int mx, int my)
{
    if (!launcher_open)
        return -1;
    if (mx < launcher_x + 10 || mx >= launcher_x + LAUNCHER_W - 10)
        return -1;
    if (my < launcher_y + LAUNCHER_Q_H)
        return -1;
    {
        int rel = my - (launcher_y + LAUNCHER_Q_H);
        int row = rel / LAUNCHER_ROW_H;
        if (row >= 0 && row < launcher_match_count)
            return row;
    }
    return -1;
}

static void launcher_execute_selected(void)
{
    if (launcher_selected < 0 || launcher_selected >= launcher_match_count)
        return;
    launcher_catalog[launcher_matches[launcher_selected]].launch();
    launcher_set_open(0);
}

static void launcher_handle_key(uint32_t keycode)
{
    if (!launcher_open)
        return;
    if (keycode == 27u) {
        launcher_set_open(0);
        return;
    }
    if (keycode == '\n' || keycode == '\r') {
        launcher_execute_selected();
        return;
    }
    if (keycode == '\b') {
        if (launcher_query_len > 0) {
            launcher_query_len--;
            launcher_query[launcher_query_len] = '\0';
            launcher_update_matches();
        }
        return;
    }
    if (keycode == 0x48u || keycode == 0xE048u) {
        if (launcher_selected > 0)
            launcher_selected--;
        launcher_hover = -1;
        return;
    }
    if (keycode == 0x50u || keycode == 0xE050u) {
        if (launcher_selected + 1 < launcher_match_count)
            launcher_selected++;
        launcher_hover = -1;
        return;
    }
    if (keycode >= 32u && keycode <= 126u) {
        if (launcher_query_len + 1 < (int)sizeof(launcher_query)) {
            launcher_query[launcher_query_len++] = (char)keycode;
            launcher_query[launcher_query_len] = '\0';
            launcher_update_matches();
        }
    }
}

/* ---- Hit testing ------------------------------------------------------ */

static int hit_icon(int mx, int my)
{
    for (int i = 0; i < num_icons; i++) {
        const struct desktop_icon *ic = &icons[i];
        if (mx >= ic->x && mx < ic->x + ICON_SIZE &&
            my >= ic->y && my < ic->y + ICON_SIZE)
            return i;
    }
    return -1;
}

static int hit_start_btn(int mx, int my)
{
    return (mx >= start_btn_x && mx < start_btn_x + start_btn_w &&
            my >= start_btn_y && my < start_btn_y + start_btn_h);
}

static int hit_menu_item(int mx, int my)
{
    int mh;
    if (!start_menu_open)
        return -1;
    mh = start_menu_height();
    if (mx < menu_x + 8 || mx >= menu_x + MENU_W - 8 ||
        my < menu_y + MENU_HEADER_H + MENU_PAD || my >= menu_y + mh - MENU_PAD)
        return -1;
    {
        int idx = (my - (menu_y + MENU_HEADER_H + MENU_PAD)) / MENU_ITEM_H;
        return (idx >= 0 && idx < MENU_ITEMS) ? idx : -1;
    }
}

/* ---- Redraw ----------------------------------------------------------- */

static void do_full_redraw(void)
{
    draw_desktop_bg();
    draw_icons();
    wm_redraw_all();
    draw_taskbar();
    draw_start_menu();
    draw_launcher();
    cursor_draw();
}

static void redraw_dirty_regions(void)
{
    wm_dirty_rect_t wm_dirty_regions[DESKTOP_DIRTY_MAX];
    int wm_dirty = wm_collect_dirty_regions(wm_dirty_regions, DESKTOP_DIRTY_MAX);
    int rx0;
    int ry0;
    int rx1;
    int ry1;
    int rw;
    int rh;

    if (wm_dirty <= 0)
        return;

    /*
     * Under very bursty input, region stitching can get fragmented.
     * Falling back to one full redraw keeps visual correctness stable.
     */
    if (wm_dirty > (DESKTOP_DIRTY_MAX / 2)) {
        do_full_redraw();
        return;
    }

    /*
     * Merge dirty rectangles into one repaint region. This avoids ordering
     * artifacts where later small region redraws can punch visual holes into
     * overlays or freshly-redrawn windows.
     */
    rx0 = wm_dirty_regions[0].x;
    ry0 = wm_dirty_regions[0].y;
    rx1 = wm_dirty_regions[0].x + wm_dirty_regions[0].w;
    ry1 = wm_dirty_regions[0].y + wm_dirty_regions[0].h;
    for (int i = 1; i < wm_dirty; i++) {
        wm_dirty_rect_t r = wm_dirty_regions[i];
        int r1x = r.x + r.w;
        int r1y = r.y + r.h;
        if (r.x < rx0) rx0 = r.x;
        if (r.y < ry0) ry0 = r.y;
        if (r1x > rx1) rx1 = r1x;
        if (r1y > ry1) ry1 = r1y;
    }

    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;
    if (rx1 > (int)fb_info.width) rx1 = (int)fb_info.width;
    if (ry1 > (int)fb_info.height) ry1 = (int)fb_info.height;

    rw = rx1 - rx0;
    rh = ry1 - ry0;
    if (rw <= 0 || rh <= 0)
        return;

    draw_desktop_bg_region(rx0, ry0, rw, rh);
    draw_icons_region(rx0, ry0, rw, rh);
    wm_redraw_region(rx0, ry0, rw, rh);

    if (intersects(rx0, ry0, rw, rh, 0, taskbar_y, (int)fb_info.width, TASKBAR_H))
        draw_taskbar();
    if (start_menu_open &&
        intersects(rx0, ry0, rw, rh, menu_x, menu_y, MENU_W, start_menu_height()))
        draw_start_menu();

    /*
     * Launcher draws a full-screen dim layer (except taskbar), so any redraw
     * on the desktop region must reapply it to remain visually stable.
     */
    if (launcher_open && ry0 < taskbar_y)
        draw_launcher();

    cursor_draw();
}

/* ---- Main loop -------------------------------------------------------- */

void desktop_run(void)
{
    if (!fb_info.addr || fb_info.bpp != 32)
        return;

    cursor_init();
    cursor_set((int)fb_info.width / 2, (int)fb_info.height / 2);
    wm_init();
    gui_srv_init();
    ps2mouse_init();

    setup_icons();
    menu_launchers[0] = launch_filemgr;
    menu_launchers[1] = launch_notepad;
    menu_launchers[2] = launch_settings;
    menu_launchers[3] = launch_calc;
    menu_launchers[4] = launch_terminal;
    menu_launchers[5] = launch_taskmgr;
    menu_launchers[6] = launch_network;
    menu_launchers[7] = launch_about;

    start_menu_open = 0;
    launcher_open = 0;
    launcher_query[0] = '\0';
    launcher_query_len = 0;
    launcher_match_count = 0;
    launcher_selected = -1;
    launcher_hover = -1;
    start_menu_hover = -1;
    taskbar_pill_count = 0;
    alt_down = 0;
    cursor_last_x = cursor_x();
    cursor_last_y = cursor_y();

    {
        struct tsukasa_theme_state boot_theme;
        (void)syscall_handler(SYS_SYSTEM,
                              SYSTEM_CMD_THEME_GET_STATE,
                              (uintptr_t)&boot_theme,
                              0, 0, 0);
    }

    do_full_redraw();

    for (;;) {
        struct input_event ev;

        while (event_dequeue(&ev)) {
            if (ev.event_id == INPUT_EVENT_MOUSE_MOVE ||
                ev.event_id == INPUT_EVENT_MOUSE_DOWN ||
                ev.event_id == INPUT_EVENT_MOUSE_UP ||
                ev.event_id == INPUT_EVENT_RIGHT_CLICK ||
                ev.event_id == INPUT_EVENT_CLICK ||
                ev.event_id == INPUT_EVENT_MOUSE_WHEEL) {
                int left_down = (ev.event_id == INPUT_EVENT_MOUSE_DOWN) &&
                                ((ev.keycode & MOUSE_BUTTON_LEFT) != 0);
                int cx = cursor_x();
                int cy = cursor_y();

                if (cursor_last_x != cx || cursor_last_y != cy) {
                    wm_mark_dirty_rect(cursor_last_x - 1, cursor_last_y - 1,
                                       CURSOR_W + 2, CURSOR_HEIGHT + 2);
                    wm_mark_dirty_rect(cx - 1, cy - 1, CURSOR_W + 2, CURSOR_HEIGHT + 2);
                    cursor_last_x = cx;
                    cursor_last_y = cy;
                } else {
                    wm_mark_dirty_rect(cx - 1, cy - 1, CURSOR_W + 2, CURSOR_HEIGHT + 2);
                }

                if (launcher_open) {
                    if (ev.event_id == INPUT_EVENT_MOUSE_MOVE) {
                        int li = launcher_hit_item(ev.x, ev.y);
                        if (li != launcher_hover) {
                            launcher_hover = li;
                            if (li >= 0)
                                launcher_selected = li;
                            wm_mark_dirty_rect(launcher_x, launcher_y, LAUNCHER_W, launcher_h);
                        }
                    }
                    if (left_down) {
                        int li = launcher_hit_item(ev.x, ev.y);
                        if (li >= 0) {
                            launcher_selected = li;
                            launcher_execute_selected();
                        } else if (!(ev.x >= launcher_x && ev.x < launcher_x + LAUNCHER_W &&
                                     ev.y >= launcher_y && ev.y < launcher_y + launcher_h)) {
                            launcher_set_open(0);
                        }
                        if (launcher_open)
                            wm_mark_dirty_rect(launcher_x, launcher_y, LAUNCHER_W, launcher_h);
                    }
                    continue;
                }

                if (start_menu_open && ev.event_id == INPUT_EVENT_MOUSE_MOVE) {
                    int new_hover = hit_menu_item(ev.x, ev.y);
                    if (new_hover != start_menu_hover) {
                        start_menu_hover = new_hover;
                        wm_mark_dirty_rect(menu_x, menu_y, MENU_W, start_menu_height());
                    }
                }

                if (left_down) {
                    int menu_h = start_menu_height();

                    if (hit_start_btn(ev.x, ev.y)) {
                        int was_open = start_menu_open;
                        start_menu_open = !start_menu_open;
                        start_menu_hover = -1;
                        if (start_menu_open) {
                            menu_x = start_btn_x;
                            menu_y = taskbar_y - menu_h - 8;
                        }
                        wm_mark_dirty_rect(0, taskbar_y, (int)fb_info.width, TASKBAR_H);
                        if (was_open || start_menu_open)
                            wm_mark_dirty_rect(menu_x, menu_y, MENU_W, menu_h);
                        continue;
                    }

                    if (start_menu_open) {
                        int mi = hit_menu_item(ev.x, ev.y);
                        if (mi >= 0 && menu_launchers[mi]) {
                            start_menu_open = 0;
                            start_menu_hover = -1;
                            menu_launchers[mi]();
                            wm_mark_dirty_rect(menu_x, menu_y, MENU_W, menu_h);
                            wm_mark_dirty_rect(0, taskbar_y, (int)fb_info.width, TASKBAR_H);
                            continue;
                        }
                        if (!(ev.x >= menu_x && ev.x < menu_x + MENU_W &&
                              ev.y >= menu_y && ev.y < menu_y + menu_h)) {
                            start_menu_open = 0;
                            start_menu_hover = -1;
                            wm_mark_dirty_rect(menu_x, menu_y, MENU_W, menu_h);
                            wm_mark_dirty_rect(0, taskbar_y, (int)fb_info.width, TASKBAR_H);
                            continue;
                        }
                    }

                    {
                        wm_window_t *pill = hit_taskbar_pill(ev.x, ev.y);
                        if (pill) {
                            wm_bring_to_front(pill);
                            wm_mark_dirty_rect(0, taskbar_y, (int)fb_info.width, TASKBAR_H);
                            continue;
                        }
                    }

                    if (!wm_handle_input(&ev)) {
                        int icon_idx = hit_icon(ev.x, ev.y);
                        if (icon_idx >= 0)
                            icons[icon_idx].launch();
                        wm_mark_dirty_rect(0, taskbar_y, (int)fb_info.width, TASKBAR_H);
                    }
                } else {
                    wm_handle_input(&ev);
                }

                continue;
            }

            if (ev.event_id == INPUT_EVENT_KEY || ev.event_id == INPUT_EVENT_KEYUP) {
                int is_down = (ev.event_id == INPUT_EVENT_KEY);
                if (ev.keycode == 56u)
                    alt_down = is_down;

                if (is_down && ev.keycode == INPUT_KEY_PRINTSCREEN) {
                    if (save_screenshot_bmp("/tmp/screenshot.bmp") == 0)
                        kprintf("[desktop] screenshot saved to /tmp/screenshot.bmp\n");
                    else
                        kprintf("[desktop] screenshot failed\n");
                    continue;
                }

                if (is_down && alt_down && ev.keycode == ' ') {
                    if (start_menu_open) {
                        start_menu_open = 0;
                        start_menu_hover = -1;
                        wm_mark_dirty_rect(menu_x, menu_y, MENU_W, start_menu_height());
                    }
                    launcher_set_open(!launcher_open);
                    continue;
                }

                if (launcher_open && is_down) {
                    launcher_handle_key(ev.keycode);
                    wm_mark_dirty_rect(launcher_x, launcher_y, LAUNCHER_W, launcher_h);
                    continue;
                }

                if (is_down && ev.keycode == 27u && start_menu_open) {
                    start_menu_open = 0;
                    start_menu_hover = -1;
                    wm_mark_dirty_rect(menu_x, menu_y, MENU_W, start_menu_height());
                    continue;
                }

                wm_handle_input(&ev);
            }
        }

        redraw_dirty_regions();

        __asm__ volatile ("hlt");
    }
}
