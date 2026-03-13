/*
 * desktop.c  -  Desktop shell: taskbar, icons, Start menu, event loop.
 *               Modern dark-glass theme. Runs as the main kernel task.
 */

#include "desktop.h"
#include "wm.h"
#include "ui.h"
#include "theme.h"
#include "blit.h"
#include "font.h"
#include "cursor.h"
#include "bmp.h"
#include "apps/apps.h"
#include "apps/apps.h"
#include "../drv/fb.h"
#include "../drv/ps2mouse.h"
#include "../input/event.h"
#include "../mm/heap.h"
#include "../input/event.h"
#include <stddef.h>
#include <stdint.h>

/* ---- Desktop icon definitions ---------------------------------------- */

#define ICON_SIZE      48
#define ICON_GAP       16
#define ICON_LABEL_GAP  6
#define ICON_START_X   24
#define ICON_START_Y   24
#define MAX_ICONS       8
#define TASKBAR_H      40
#define FONT_HEIGHT     8   /* from font.h (8x8 font) */

typedef void (*app_launcher_fn)(void);

struct desktop_icon {
    int  x, y;
    int  icon_type;      /* ui_draw_icon type: 0-4 */
    const char *label;
    app_launcher_fn launch;
};

/* ---- Forward declarations -------------------------------------------- */
static void launch_notepad(void);
static void launch_filemgr(void);
static void launch_settings(void);
static void launch_calc(void);
static void launch_terminal(void);
static void launch_about(void);

static struct desktop_icon icons[MAX_ICONS];
static int num_icons = 0;

static int taskbar_y;
static int start_btn_x, start_btn_y, start_btn_w, start_btn_h;
static int start_menu_open = 0;
static int mouse_buttons_prev = 0;

/* Wallpaper path (empty = use gradient). */
static char wallpaper_path[128] = "";
static uint32_t *wallpaper_pixels = NULL;

/* ---- Icon setup ------------------------------------------------------- */

static void add_icon(const char *label, int icon_type, app_launcher_fn fn)
{
    if (num_icons >= MAX_ICONS) return;
    struct desktop_icon *ic = &icons[num_icons];
    /* Icons laid out vertically on the left. */
    ic->x          = ICON_START_X;
    ic->y          = ICON_START_Y + num_icons * (ICON_SIZE + ICON_GAP + FONT_HEIGHT + ICON_LABEL_GAP);
    ic->icon_type  = icon_type;
    ic->label      = label;
    ic->launch     = fn;
    num_icons++;
}

static void setup_icons(void)
{
    num_icons = 0;
    add_icon("Files",     0, launch_filemgr);
    add_icon("Notepad",   1, launch_notepad);
    add_icon("Settings",  4, launch_settings);
    add_icon("Calc",      5, launch_calc);
    add_icon("Terminal",  3, launch_terminal);
    add_icon("About",     6, launch_about);
}

/* ---- Wallpaper / background ------------------------------------------ */

/* Integer division that rounds to nearest (for scaling). */
static inline int desk_scale(int src, int src_max, int dst_max)
{
    return (int)((uint32_t)src * (uint32_t)src_max / (uint32_t)dst_max);
}

void desktop_set_wallpaper(const char *path)
{
    if (wallpaper_pixels) {
        kfree(wallpaper_pixels);
        wallpaper_pixels = NULL;
    }

    if (!path || path[0] == '\0') {
        wallpaper_path[0] = '\0';
        return;
    }

    /* Load and cache the image immediately. */
    int bw, bh;
    uint32_t *raw_pixels = NULL;
    if (bmp_load_to_buf(path, &raw_pixels, &bw, &bh) == 0 && raw_pixels) {
        int sw = (int)fb_info.width;
        int sh = (int)fb_info.height;
        
        wallpaper_pixels = (uint32_t *)kmalloc((size_t)sw * sh * sizeof(uint32_t));
        if (wallpaper_pixels) {
            for (int y = 0; y < sh; y++) {
                int src_y = desk_scale(y, bh, sh);
                if (src_y >= bh) src_y = bh - 1;
                for (int x = 0; x < sw; x++) {
                    int src_x = desk_scale(x, bw, sw);
                    if (src_x >= bw) src_x = bw - 1;
                    wallpaper_pixels[y * sw + x] = raw_pixels[src_y * bw + src_x] | 0xFF000000u;
                }
            }
        }
        kfree(raw_pixels);
        
        int i = 0;
        while (path[i] && i < 127) { wallpaper_path[i] = path[i]; i++; }
        wallpaper_path[i] = '\0';
    } else {
        wallpaper_path[0] = '\0';
    }
}

static void draw_desktop_bg(void)
{
    if (wallpaper_pixels) {
        int sw = (int)fb_info.width;
        int sh = (int)fb_info.height;

        for (int y = 0; y < sh; y++) {
            uint32_t *fb_row = (uint32_t *)((char *)fb_info.addr +
                                            (uint32_t)y * fb_info.pitch);
            uint32_t *src_row = &wallpaper_pixels[y * sw];
            for (int x = 0; x < sw; x++) {
                fb_row[x] = src_row[x];
            }
        }
        return;
    }
    
    /* Gradient fallback: dark-blue top → darker bottom. */
    fb_fill_gradient_v(0, 0, (int)fb_info.width, (int)fb_info.height,
                       (color_t)THEME_BG_TOP, (color_t)THEME_BG_BOT);
}

/* ---- Desktop icons ---------------------------------------------------- */

static void draw_icons(void)
{
    for (int i = 0; i < num_icons; i++) {
        struct desktop_icon *ic = &icons[i];

        /* Subtle dark hover-region behind each icon. */
        fb_fill_rect_alpha(ic->x - 4, ic->y - 4,
                           ICON_SIZE + 8, ICON_SIZE + FONT_HEIGHT + ICON_LABEL_GAP + 8,
                           rgba(0, 0, 0, 40));

        ui_draw_icon(ic->x, ic->y, ICON_SIZE, ic->icon_type);

        /* Label below icon. */
        const char *lbl = ic->label;
        int len = 0;
        while (lbl[len]) len++;
        int lx = ic->x + (ICON_SIZE - len * 8) / 2;
        int ly = ic->y + ICON_SIZE + ICON_LABEL_GAP;
        for (int c = 0; lbl[c]; c++) {
            fb_draw_char(lx, ly, lbl[c], (color_t)THEME_TEXT, 0x00000000u);
            lx += 8;
        }
    }
}

/* ---- Taskbar ---------------------------------------------------------- */

static void draw_taskbar(void)
{
    taskbar_y = (int)fb_info.height - TASKBAR_H;

    /* Semi-transparent dark bar. */
    fb_fill_rect_alpha(0, taskbar_y, (int)fb_info.width, TASKBAR_H,
                       (color_t)THEME_TASKBAR_BG);

    /* Top edge highlight. */
    fb_draw_hline(0, taskbar_y, (int)fb_info.width,
                  rgba(79, 195, 247, 60));

    /* Start button (gradient). */
    start_btn_w = 72;
    start_btn_h = 28;
    start_btn_x = 6;
    start_btn_y = taskbar_y + (TASKBAR_H - start_btn_h) / 2;
    fb_fill_gradient_v(start_btn_x, start_btn_y,
                       start_btn_w, start_btn_h,
                       (color_t)THEME_START_TOP, (color_t)THEME_START_BOT);
    fb_draw_rounded_rect(start_btn_x, start_btn_y,
                         start_btn_w, start_btn_h, 5,
                         rgba(79, 195, 247, 100));

    /* "Start" label. */
    int tx = start_btn_x + (start_btn_w - 5 * 8) / 2;
    int ty = start_btn_y + (start_btn_h - 8) / 2;
    const char *sl = "Start";
    for (int i = 0; sl[i]; i++) {
        fb_draw_char(tx, ty, sl[i], (color_t)THEME_TEXT,
                     (color_t)THEME_START_TOP);
        tx += 8;
    }

    /* Active window pills (right side of start button). */
    {
        int px = start_btn_x + start_btn_w + 8;
        for (wm_window_t *w = wm_get_bottom(); w; w = w->next) {
            if (!(w->flags & WM_FLAG_VISIBLE)) continue;
            int active = (w->flags & WM_FLAG_ACTIVE);
            color_t pc = active ? (color_t)g_accent_color
                                : rgba(40, 55, 75, 220);
            fb_fill_rounded_rect(px, taskbar_y + 6, 120, TASKBAR_H - 12, 4, pc);
            /* Window title in pill. */
            int tlx = px + 8;
            for (int i = 0; w->title[i] && tlx + 8 <= px + 112; i++) {
                fb_draw_char(tlx, taskbar_y + (TASKBAR_H - 8) / 2,
                             w->title[i], (color_t)THEME_TEXT, pc);
                tlx += 8;
            }
            px += 128;
            if (px + 128 > (int)fb_info.width - 80) break;
        }
    }

    /* Clock area placeholder (right side). */
    {
        const char *clk = "Tsukasa OS";
        int cx = (int)fb_info.width - 90;
        int cy = taskbar_y + (TASKBAR_H - 8) / 2;
        for (int i = 0; clk[i]; i++) {
            fb_draw_char(cx, cy, clk[i], (color_t)THEME_TEXT_DIM,
                         (color_t)THEME_TASKBAR_BG);
            cx += 8;
        }
    }
}

/* ---- Start menu ------------------------------------------------------- */

#define MENU_W        160
#define MENU_ITEM_H    28
#define MENU_ITEMS      6

static const char *menu_labels[MENU_ITEMS] = {
    "Files", "Notepad", "Settings", "Calculator", "Terminal", "About"
};
static app_launcher_fn menu_launchers[MENU_ITEMS];
static int menu_x, menu_y;

static void draw_start_menu(void)
{
    if (!start_menu_open) return;

    int mh = MENU_ITEMS * MENU_ITEM_H + 8;
    menu_x = start_btn_x;
    menu_y = taskbar_y - mh - 4;

    /* Menu panel. */
    fb_fill_rounded_rect(menu_x, menu_y, MENU_W, mh, 8,
                         rgba(16, 24, 36, 230));
    fb_draw_rounded_rect(menu_x, menu_y, MENU_W, mh, 8,
                         (color_t)THEME_WIN_BORDER_ACTIVE);

    /* Header accent bar. */
    fb_fill_rounded_rect(menu_x, menu_y, MENU_W, 24, 8,
                         (color_t)g_accent_color);

    const char *hdr = "Tsukasa";
    int hx = menu_x + (MENU_W - 7 * 8) / 2;
    int hy = menu_y + (24 - 8) / 2;
    for (int i = 0; hdr[i]; i++) {
        fb_draw_char(hx, hy, hdr[i], (color_t)THEME_TEXT,
                     (color_t)g_accent_color);
        hx += 8;
    }

    /* Items. */
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = menu_y + 28 + i * MENU_ITEM_H;
        int ix = menu_x + 16;
        const char *lbl = menu_labels[i];
        for (int c = 0; lbl[c]; c++) {
            fb_draw_char(ix, iy + (MENU_ITEM_H - 8) / 2, lbl[c],
                         (color_t)THEME_TEXT, rgba(0, 0, 0, 0));
            ix += 8;
        }
    }
}

/* ---- Full redraw ------------------------------------------------------ */

static void do_full_redraw(void)
{
    draw_desktop_bg();
    draw_icons();
    draw_taskbar();
    wm_redraw_all();
    draw_start_menu();
    cursor_draw();
}

/* ---- Hit testing ------------------------------------------------------ */

static int hit_icon(int mx, int my)
{
    for (int i = 0; i < num_icons; i++) {
        struct desktop_icon *ic = &icons[i];
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
    if (!start_menu_open) return -1;
    int mh = MENU_ITEMS * MENU_ITEM_H + 8;
    if (mx < menu_x || mx >= menu_x + MENU_W ||
        my < menu_y + 28 || my >= menu_y + mh)
        return -1;
    int idx = (my - (menu_y + 28)) / MENU_ITEM_H;
    return (idx >= 0 && idx < MENU_ITEMS) ? idx : -1;
}

/* ---- App launchers ---------------------------------------------------- */

static void launch_notepad(void)   { app_notepad_open(); }
static void launch_filemgr(void)   { app_filemgr_open(); }
static void launch_settings(void)  { app_settings_open(); }
static void launch_calc(void)      { app_calc_open(); }
static void launch_terminal(void)  { app_terminal_open(); }
static void launch_about(void)     { app_about_open(); }

/* ---- Main event loop -------------------------------------------------- */

void desktop_run(void)
{
    if (!fb_info.addr || fb_info.bpp != 32)
        return;

    cursor_init();
    cursor_set((int)fb_info.width / 2, (int)fb_info.height / 2);
    wm_init();
    ps2mouse_init();

    setup_icons();

    menu_launchers[0] = launch_filemgr;
    menu_launchers[1] = launch_notepad;
    menu_launchers[2] = launch_settings;
    menu_launchers[3] = launch_calc;
    menu_launchers[4] = launch_terminal;
    menu_launchers[5] = launch_about;

    start_menu_open    = 0;
    mouse_buttons_prev = 0;

    do_full_redraw();

    for (;;) {
        struct input_event ev;
        int redraw = 0;

        while (event_dequeue(&ev)) {
            if (ev.type == EVENT_MOUSE) {
                int mx = ev.x, my = ev.y;
                int buttons = ev.keycode & 0x07;
                int changed = buttons ^ mouse_buttons_prev;
                int left_down = (buttons & 1) && (changed & 1);

                if (ev.subtype == MOUSE_MOVE) {
                    wm_handle_mouse(mx, my, buttons, 0);
                    redraw = 1;
                }

                if (ev.subtype == MOUSE_BTN_DOWN ||
                    ev.subtype == MOUSE_BTN_UP) {
                    if (left_down) {
                        /* Start Menu overrides everything else */
                        if (start_menu_open) {
                            int mi = hit_menu_item(mx, my);
                            if (mi >= 0 && menu_launchers[mi]) {
                                start_menu_open = 0;
                                menu_launchers[mi]();
                                redraw = 1;
                                mouse_buttons_prev = buttons;
                                continue;
                            }
                            
                            /* Clicked outside menu, close it */
                            start_menu_open = 0;
                            redraw = 1;
                        }

                        /* Taskbar Top Level */
                        if (hit_start_btn(mx, my)) {
                            start_menu_open = !start_menu_open;
                            redraw = 1;
                            mouse_buttons_prev = buttons;
                            continue;
                        }
                    }

                    /* Window Manager (Windows natively above Desktop) */
                    if (wm_handle_mouse(mx, my, buttons, changed)) {
                        redraw = 1;
                    } 
                    /* Desktop Layer */
                    else if (left_down) {
                        int icon_idx = hit_icon(mx, my);
                        if (icon_idx >= 0) {
                            icons[icon_idx].launch();
                            redraw = 1;
                            mouse_buttons_prev = buttons;
                            continue;
                        }
                    }

                    mouse_buttons_prev = buttons;
                }
            }

            if (ev.type == EVENT_KEY && ev.subtype == KEY_PRESS) {
                wm_window_t *top = wm_get_top();
                if (top && top->handle_event) {
                    top->handle_event(top, &ev);
                    redraw = 1;
                }
            }
        }

        if (redraw) do_full_redraw();

        __asm__ volatile ("hlt");
    }
}
