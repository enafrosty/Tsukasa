/*
 * filemgr.c  -  File Manager application.
 *
 * Enumerates the FAT12 root directory (and /tmp/) via vfs_list.
 * Displays files in an icon grid.
 * Click to select, double-click to open (.txt → Notepad, .bmp → wallpaper).
 */

#include "apps.h"
#include "../wm.h"
#include "../ui.h"
#include "../theme.h"
#include "../font.h"
#include "../blit.h"
#include "../../input/event.h"
#include "../../fs/vfs.h"
#include "../../mm/heap.h"
#include <stddef.h>
#include <stdint.h>

/* Defined in desktop.c, declared as extern. */
extern void desktop_set_wallpaper(const char *path);

/* ---- Layout ----------------------------------------------------------- */

#define FM_W           480
#define FM_H           340
#define FM_COLS          4
#define FM_CELL_W       96
#define FM_CELL_H       80
#define FM_MAX_FILES    VFS_NAME_MAX   /* reuse constant for max listing */
#define FM_MAX_ENTRIES  64
#define FM_TOOLBAR_H    24

/* ---- File entry type detection ---------------------------------------- */

typedef enum { FT_TEXT, FT_BMP, FT_DIR, FT_OTHER } file_type_t;

typedef struct {
    char        name[VFS_NAME_MAX];
    file_type_t type;
} fm_entry_t;

/* ---- App state -------------------------------------------------------- */

typedef struct {
    fm_entry_t entries[FM_MAX_ENTRIES];
    int        count;
    int        selected;          /* -1 = none                       */
    int        scroll_row;        /* first visible icon row          */
    /* Simple double-click tracking: store last click index + a counter. */
    int        last_click;
    int        click_timer;       /* decrements on each draw; click if >0 */
    char       status_msg[64];
} fm_data_t;

/* ---- String helpers --------------------------------------------------- */

static int fm_strlen(const char *s)
{ int n=0; while(s&&s[n])n++; return n; }

static int fm_streq_suffix(const char *name, const char *suffix)
{
    int nl = fm_strlen(name), sl = fm_strlen(suffix);
    if (sl > nl) return 0;
    const char *tail = name + nl - sl;
    for (int i = 0; suffix[i]; i++) {
        char a = tail[i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static void fm_strcpy(char *dst, const char *src, int max)
{
    int i=0; while(src[i]&&i<max-1){dst[i]=src[i];i++;} dst[i]='\0';
}

static file_type_t classify(const char *name)
{
    if (fm_streq_suffix(name, ".txt") || fm_streq_suffix(name, ".TXT") ||
        fm_streq_suffix(name, ".c")   || fm_streq_suffix(name, ".h")   ||
        fm_streq_suffix(name, ".md"))
        return FT_TEXT;
    if (fm_streq_suffix(name, ".bmp") || fm_streq_suffix(name, ".BMP"))
        return FT_BMP;
    return FT_OTHER;
}

static int icon_type_for(file_type_t ft)
{
    switch (ft) {
    case FT_TEXT:  return 1;
    case FT_BMP:   return 2;
    case FT_DIR:   return 0;
    default:       return 3;
    }
}

/* ---- Populate file list ----------------------------------------------- */

static void fm_refresh(fm_data_t *fm)
{
    char names[FM_MAX_ENTRIES][VFS_NAME_MAX];
    int n = vfs_list("/", names, FM_MAX_ENTRIES);
    if (n < 0) n = 0;

    fm->count = 0;
    for (int i = 0; i < n && fm->count < FM_MAX_ENTRIES; i++) {
        if (!names[i][0]) continue;
        fm_strcpy(fm->entries[fm->count].name, names[i], VFS_NAME_MAX);
        fm->entries[fm->count].type = classify(names[i]);
        fm->count++;
    }

    /* Also list /tmp/. */
    int tmp_n = vfs_list("/tmp/", names, FM_MAX_ENTRIES - fm->count);
    if (tmp_n > 0) {
        for (int i = 0; i < tmp_n && fm->count < FM_MAX_ENTRIES; i++) {
            if (!names[i][0]) continue;
            /* Prepend "tmp/" marker in display name. */
            fm->entries[fm->count].name[0] = '*';  /* * = memfs */
            fm_strcpy(fm->entries[fm->count].name + 1, names[i],
                      VFS_NAME_MAX - 1);
            fm->entries[fm->count].type = classify(names[i]);
            fm->count++;
        }
    }

    fm->selected   = -1;
    fm->scroll_row = 0;
}

/* ---- Drawing ---------------------------------------------------------- */

static void fm_draw(wm_window_t *win)
{
    fm_data_t *fm = (fm_data_t *)win->app_data;
    if (!fm) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    fb_fill_rect(cx, cy, cw, ch, (color_t)THEME_WIN_BG);

    /* Toolbar. */
    fb_fill_rect(cx, cy, cw, FM_TOOLBAR_H, rgba(16, 24, 36, 255));
    const char *hdr = "File Manager - /";
    int hx = cx + 8, hy = cy + (FM_TOOLBAR_H - 8) / 2;
    for (int i = 0; hdr[i]; i++) {
        fb_draw_char(hx, hy, hdr[i], (color_t)THEME_TEXT_ACCENT,
                     rgba(16, 24, 36, 255));
        hx += 8;
    }

    /* Status message (bottom strip). */
    fb_fill_rect(cx, cy + ch - 16, cw, 16, rgba(10, 16, 26, 255));
    if (fm->status_msg[0]) {
        int sx = cx + 8;
        for (int i = 0; fm->status_msg[i] && sx + 8 <= cx + cw - 4; i++) {
            fb_draw_char(sx, cy + ch - 12, fm->status_msg[i],
                         (color_t)THEME_TEXT_DIM, rgba(10, 16, 26, 255));
            sx += 8;
        }
    }

    /* Icon grid. */
    int grid_x = cx + 8;
    int grid_y = cy + FM_TOOLBAR_H + 8;
    int grid_h = ch - FM_TOOLBAR_H - 16 - 8;
    int rows_visible = grid_h / FM_CELL_H;
    int total_rows = (fm->count + FM_COLS - 1) / FM_COLS;

    if (fm->scroll_row < 0) fm->scroll_row = 0;
    if (fm->scroll_row > total_rows - rows_visible)
        fm->scroll_row = total_rows - rows_visible;
    if (fm->scroll_row < 0) fm->scroll_row = 0;

    for (int row = 0; row < rows_visible + 1; row++) {
        int r = row + fm->scroll_row;
        for (int col = 0; col < FM_COLS; col++) {
            int idx = r * FM_COLS + col;
            if (idx >= fm->count) break;

            int ix = grid_x + col * FM_CELL_W;
            int iy = grid_y + row * FM_CELL_H;
            if (iy + FM_CELL_H > cy + ch - 16) break;

            /* Selection highlight. */
            if (idx == fm->selected)
                fb_fill_rect_alpha(ix - 2, iy - 2,
                                   FM_CELL_W, FM_CELL_H,
                                   rgba(79, 195, 247, 30));

            /* Icon. */
            int icon_size = 36;
            int icon_x = ix + (FM_CELL_W - icon_size) / 2;
            ui_draw_icon(icon_x, iy + 4, icon_size,
                         icon_type_for(fm->entries[idx].type));

            /* Filename label (truncated). */
            const char *label = fm->entries[idx].name;
            int label_len = fm_strlen(label);
            int max_chars = FM_CELL_W / 8;
            if (label_len > max_chars) label_len = max_chars;
            int lx = ix + (FM_CELL_W - label_len * 8) / 2;
            int ly = iy + icon_size + 8;
            for (int c = 0; label[c] && c < label_len; c++) {
                fb_draw_char(lx, ly, label[c],
                             idx == fm->selected ? (color_t)THEME_TEXT_ACCENT
                                                 : (color_t)THEME_TEXT_DIM,
                             (color_t)THEME_WIN_BG);
                lx += 8;
            }
        }
    }

    /* Scrollbar. */
    ui_draw_scrollbar(cx + cw - UI_SCROLLBAR_W - 2, cy + FM_TOOLBAR_H,
                      ch - FM_TOOLBAR_H - 16,
                      total_rows, rows_visible, fm->scroll_row);
}

/* ---- Event handling --------------------------------------------------- */

static void fm_open_entry(fm_data_t *fm, int idx)
{
    if (idx < 0 || idx >= fm->count) return;
    fm_entry_t *e = &fm->entries[idx];

    /* Build full path. */
    char path[VFS_NAME_MAX + 2];
    int pi = 0;
    if (e->name[0] == '*') {
        /* memfs file: /tmp/name. */
        path[pi++] = '/';
        path[pi++] = 't'; path[pi++] = 'm'; path[pi++] = 'p'; path[pi++] = '/';
        const char *n = e->name + 1;
        while (*n && pi < VFS_NAME_MAX + 1) path[pi++] = *n++;
    } else {
        path[pi++] = '/';
        const char *n = e->name;
        while (*n && pi < VFS_NAME_MAX + 1) path[pi++] = *n++;
    }
    path[pi] = '\0';

    switch (e->type) {
    case FT_TEXT:
        app_notepad_open_file(path);
        break;
    case FT_BMP:
        desktop_set_wallpaper(path);
        {
            const char *msg = "Wallpaper set!";
            int i = 0; while (msg[i]) { fm->status_msg[i]=msg[i]; i++; }
            fm->status_msg[i] = '\0';
        }
        break;
    default:
        {
            const char *msg = "No handler for this file type.";
            int i = 0; while (msg[i]) { fm->status_msg[i]=msg[i]; i++; }
            fm->status_msg[i] = '\0';
        }
        break;
    }
}

static void fm_event(wm_window_t *win, const void *event)
{
    fm_data_t *fm = (fm_data_t *)win->app_data;
    if (!fm || !event) return;

    const struct input_event *ev = (const struct input_event *)event;

    /* Keyboard: R to refresh. */
    if (ev->type == EVENT_KEY && ev->subtype == KEY_PRESS) {
        char key = (char)(ev->keycode & 0xFF);
        if (key == 'r' || key == 'R') fm_refresh(fm);
        return;
    }

    if (ev->type != EVENT_MOUSE) return;

    int mx = ev->x, my = ev->y;
    int left_down = (ev->keycode & 1) && (ev->subtype == MOUSE_BTN_DOWN);
    if (!left_down) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    int grid_x = cx + 8;
    int grid_y = cy + FM_TOOLBAR_H + 8;

    int rel_x = mx - grid_x;
    int rel_y = my - grid_y;
    if (rel_x >= 0 && rel_y >= 0 && mx < cx + cw && my < cy + ch) {
        int col = rel_x / FM_CELL_W;
        int row = rel_y / FM_CELL_H + fm->scroll_row;
        if (col < FM_COLS) {
            int idx = row * FM_COLS + col;
            if (idx >= 0 && idx < fm->count) {
                if (fm->selected == idx && fm->click_timer > 0) {
                    /* Double-click. */
                    fm_open_entry(fm, idx);
                    fm->click_timer = 0;
                } else {
                    fm->selected   = idx;
                    fm->click_timer = 20; /* ~20 frames to double click */
                }
            }
        }
    }

    /* Decrement click timer. */
    if (fm->click_timer > 0) fm->click_timer--;
}

/* ---- Public API ------------------------------------------------------- */

void app_filemgr_open(void)
{
    fm_data_t *fm = (fm_data_t *)kmalloc(sizeof(fm_data_t));
    if (!fm) return;

    fm->count        = 0;
    fm->selected     = -1;
    fm->scroll_row   = 0;
    fm->last_click   = -1;
    fm->click_timer  = 0;
    fm->status_msg[0] = '\0';

    fm_refresh(fm);

    wm_create_window(40, 40, FM_W, FM_H, "Files",
                     fm_draw, fm_event, fm);
}
