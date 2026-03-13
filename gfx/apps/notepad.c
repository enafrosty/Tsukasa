/*
 * notepad.c  -  Enhanced text editor.
 *
 * Features:
 *   - Dynamic kmalloc'd text buffer (grows as needed).
 *   - Vertical scrolling with visible-line tracking.
 *   - Cursor movement: arrows, Home, End, Backspace, Delete.
 *   - File load: vfs_open + vfs_read.
 *   - File save: Ctrl+S  → vfs_create + vfs_write.
 *   - Modern UI chrome via ui.h / theme.h.
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

/* ---- Layout ----------------------------------------------------------- */

#define NP_W           480
#define NP_H           340
#define NP_CHAR_W        8
#define NP_CHAR_H       10   /* 8px glyph + 2px leading */
#define NP_MARGIN_X      8
#define NP_MARGIN_Y      6
#define NP_INIT_CAP   4096

/* Scrollbar width is UI_SCROLLBAR_W (10). */
#define NP_TEXT_AREA_MARGIN_RIGHT (UI_SCROLLBAR_W + 4)

/* Extended key codes (expected in keycode high byte via PS/2 driver). */
#define KEY_UP      0x4800
#define KEY_DOWN    0x5000
#define KEY_LEFT    0x4B00
#define KEY_RIGHT   0x4D00
#define KEY_HOME    0x4700
#define KEY_END     0x4F00
#define KEY_DEL     0x5300
#define KEY_CTRL_S  0x001F   /* Ctrl+S */

/* ---- Data structure --------------------------------------------------- */

typedef struct {
    char   *text;        /* kmalloc'd buffer, null-terminated    */
    size_t  len;         /* bytes of text (not counting \0)      */
    size_t  capacity;    /* allocated bytes (incl. null)         */
    int     cursor_pos;  /* byte index of insertion cursor       */
    int     scroll_line; /* index of first visible line          */
    char    filepath[VFS_NAME_MAX]; /* "" if unsaved             */
    int     dirty;       /* non-zero = unsaved changes           */
} notepad_data_t;

/* ---- String helpers --------------------------------------------------- */

static void np_strcpy(char *dst, const char *src, int max)
{
    int i = 0; while (src[i] && i < max-1) { dst[i]=src[i]; i++; } dst[i]='\0';
}

/* ---- Buffer management ------------------------------------------------ */

static int np_ensure_cap(notepad_data_t *nd, size_t needed)
{
    if (needed <= nd->capacity) return 1;
    size_t new_cap = nd->capacity ? nd->capacity * 2 : NP_INIT_CAP;
    while (new_cap < needed) new_cap *= 2;
    char *nb = (char *)kmalloc(new_cap);
    if (!nb) return 0;
    for (size_t i = 0; i <= nd->len; i++) nb[i] = nd->text[i];
    kfree(nd->text);
    nd->text     = nb;
    nd->capacity = new_cap;
    return 1;
}

/* Insert a character at cursor_pos. */
static void np_insert(notepad_data_t *nd, char c)
{
    if (!np_ensure_cap(nd, nd->len + 2)) return;
    /* Shift right. */
    for (int i = (int)nd->len; i >= nd->cursor_pos; i--)
        nd->text[i + 1] = nd->text[i];
    nd->text[nd->cursor_pos++] = c;
    nd->len++;
    nd->dirty = 1;
}

/* Delete character before cursor (backspace). */
static void np_backspace(notepad_data_t *nd)
{
    if (nd->cursor_pos == 0) return;
    for (int i = nd->cursor_pos - 1; i < (int)nd->len; i++)
        nd->text[i] = nd->text[i + 1];
    nd->cursor_pos--;
    nd->len--;
    nd->dirty = 1;
}

/* Delete character at cursor (delete). */
static void np_delete(notepad_data_t *nd)
{
    if (nd->cursor_pos >= (int)nd->len) return;
    for (int i = nd->cursor_pos; i < (int)nd->len; i++)
        nd->text[i] = nd->text[i + 1];
    nd->len--;
    nd->dirty = 1;
}

/* ---- Line tracking ---------------------------------------------------- */

/* Count line number (0-based) of byte position pos. */
static int np_line_of(const char *text, int pos)
{
    int line = 0;
    for (int i = 0; i < pos; i++)
        if (text[i] == '\n') line++;
    return line;
}

/* Find start byte of line n. */
static int np_line_start(const char *text, int len, int line)
{
    int cur = 0;
    for (int i = 0; i < len && cur < line; i++)
        if (text[i] == '\n') cur++;
    /* Walk back to after the last \n. */
    int start = 0;
    int lcount = 0;
    for (int i = 0; i < len; i++) {
        if (lcount == line) { start = i; break; }
        if (text[i] == '\n') lcount++;
    }
    if (lcount == line) return start; /* line 0 edge case */
    if (line == 0) return 0;
    return (int)len;   /* past end */
}

/* Total number of lines in the buffer. */
static int np_total_lines(const char *text, int len)
{
    int lines = 1;
    for (int i = 0; i < len; i++)
        if (text[i] == '\n') lines++;
    return lines;
}

/* ---- Drawing ---------------------------------------------------------- */

static void notepad_draw(wm_window_t *win)
{
    notepad_data_t *nd = (notepad_data_t *)win->app_data;
    if (!nd) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    /* Background. */
    fb_fill_rect(cx, cy, cw, ch, (color_t)THEME_WIN_BG);

    /* Toolbar strip (save indicator). */
    color_t tb_col = nd->dirty ? rgba(80, 20, 20, 255) : rgba(20, 40, 20, 255);
    fb_fill_rect(cx, cy, cw, 16, tb_col);
    const char *status = nd->dirty ? "* Unsaved" : "Saved";
    if (nd->filepath[0]) status = nd->dirty ? "* Modified" : "Saved";
    const char *fname = nd->filepath[0] ? nd->filepath : "(untitled)";
    int ix = cx + 4;
    for (int i = 0; fname[i] && ix + 8 < cx + cw - 80; i++) {
        fb_draw_char(ix, cy + 4, fname[i], (color_t)THEME_TEXT_DIM, tb_col);
        ix += 8;
    }
    /* Save hint on right. */
    {
        int hx = cx + cw - 88;
        for (int i = 0; status[i] && hx + 8 <= cx + cw - 4; i++) {
            fb_draw_char(hx, cy + 4, status[i],
                         nd->dirty ? rgba(255,100,100,255) : rgba(80,200,80,255),
                         tb_col);
            hx += 8;
        }
        /* Ctrl+S hint. */
        const char *hint = " [Ctrl+S]";
        if (nd->dirty) {
            for (int i = 0; hint[i] && hx + 8 <= cx + cw - 4; i++) {
                fb_draw_char(hx, cy + 4, hint[i], (color_t)THEME_TEXT_DIM, tb_col);
                hx += 8;
            }
        }
    }

    /* Text area. */
    int tx0 = cx + NP_MARGIN_X;
    int ty0 = cy + 16 + NP_MARGIN_Y;
    int tw  = cw - 2 * NP_MARGIN_X - NP_TEXT_AREA_MARGIN_RIGHT;
    int th  = ch - 16 - 2 * NP_MARGIN_Y;
    int cols_visible = tw / NP_CHAR_W;
    int rows_visible = th / NP_CHAR_H;
    if (cols_visible < 1) cols_visible = 1;
    if (rows_visible < 1) rows_visible = 1;

    /* Inset textbox background. */
    ui_draw_textbox_bg(tx0 - 2, ty0 - 2, tw + 4, th + 4);

    /* Draw text lines. */
    int total_lines = np_total_lines(nd->text, (int)nd->len);
    int cursor_line = np_line_of(nd->text, nd->cursor_pos);

    /* Auto-scroll so cursor is visible. */
    if (cursor_line < nd->scroll_line)
        nd->scroll_line = cursor_line;
    if (cursor_line >= nd->scroll_line + rows_visible)
        nd->scroll_line = cursor_line - rows_visible + 1;
    if (nd->scroll_line < 0) nd->scroll_line = 0;

    /* Render visible lines. */
    int line  = 0;
    int col   = 0;
    int py    = ty0;
    int px    = tx0;
    int in_vis = (line >= nd->scroll_line);
    int drawn_lines = 0;

    for (int i = 0; i <= (int)nd->len && drawn_lines <= rows_visible; i++) {
        char c = (i < (int)nd->len) ? nd->text[i] : '\0';

        /* Draw cursor at this position. */
        if (i == nd->cursor_pos && in_vis) {
            int cur_py = ty0 + (line - nd->scroll_line) * NP_CHAR_H;
            if (cur_py >= ty0 && cur_py + NP_CHAR_H <= ty0 + th)
                fb_fill_rect(px, cur_py, 2, NP_CHAR_H, (color_t)g_accent_color);
        }

        if (c == '\0') break;

        if (c == '\n') {
            line++;
            col = 0;
            in_vis = (line >= nd->scroll_line);
            if (in_vis) {
                py = ty0 + (line - nd->scroll_line) * NP_CHAR_H;
                px = tx0;
            }
            drawn_lines = line - nd->scroll_line;
            continue;
        }

        if (!in_vis) continue;

        /* Word-wrap. */
        if (col >= cols_visible) {
            line++;
            col = 0;
            in_vis = (line >= nd->scroll_line);
            py = ty0 + (line - nd->scroll_line) * NP_CHAR_H;
            px = tx0;
            drawn_lines = line - nd->scroll_line;
        }
        in_vis = (line >= nd->scroll_line) && drawn_lines <= rows_visible;
        if (!in_vis) continue;

        py = ty0 + (line - nd->scroll_line) * NP_CHAR_H;
        if (py < ty0 || py + NP_CHAR_H > ty0 + th) { col++; continue; }

        fb_draw_char(px, py, c, (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
        px += NP_CHAR_W;
        col++;
    }

    /* Scrollbar. */
    ui_draw_scrollbar(cx + cw - UI_SCROLLBAR_W - 2,
                      cy + 16 + 2,
                      ch - 16 - 4,
                      total_lines, rows_visible, nd->scroll_line);
}

/* ---- Event handling --------------------------------------------------- */

static void notepad_event(wm_window_t *win, const void *event)
{
    notepad_data_t *nd = (notepad_data_t *)win->app_data;
    if (!nd) return;

    const struct input_event *ev = (const struct input_event *)event;
    if (ev->type != EVENT_KEY || ev->subtype != KEY_PRESS)
        return;

    uint32_t kc = (uint32_t)ev->keycode;

    /* --- Extended keys (high byte non-zero). --- */
    uint32_t ext = kc & 0xFF00;

    if (ext == 0xFF00 || kc == KEY_UP || kc == KEY_DOWN ||
        kc == KEY_LEFT || kc == KEY_RIGHT ||
        kc == KEY_HOME || kc == KEY_END || kc == KEY_DEL) {

        if (kc == KEY_LEFT && nd->cursor_pos > 0) nd->cursor_pos--;
        else if (kc == KEY_RIGHT && nd->cursor_pos < (int)nd->len) nd->cursor_pos++;
        else if (kc == KEY_HOME) {
            /* Go to start of current line. */
            int pos = nd->cursor_pos - 1;
            while (pos > 0 && nd->text[pos-1] != '\n') pos--;
            nd->cursor_pos = pos;
        }
        else if (kc == KEY_END) {
            /* Go to end of current line. */
            while (nd->cursor_pos < (int)nd->len && nd->text[nd->cursor_pos] != '\n')
                nd->cursor_pos++;
        }
        else if (kc == KEY_UP) {
            int cur_line  = np_line_of(nd->text, nd->cursor_pos);
            int cur_col   = nd->cursor_pos - np_line_start(nd->text, (int)nd->len, cur_line);
            if (cur_line > 0) {
                int prev_start = np_line_start(nd->text, (int)nd->len, cur_line - 1);
                nd->cursor_pos = prev_start + cur_col;
                /* Don't go past end of previous line. */
                int prev_end = np_line_start(nd->text, (int)nd->len, cur_line);
                if (nd->cursor_pos > prev_end - 1) nd->cursor_pos = prev_end - 1;
                if (nd->cursor_pos < prev_start) nd->cursor_pos = prev_start;
            }
        }
        else if (kc == KEY_DOWN) {
            int cur_line  = np_line_of(nd->text, nd->cursor_pos);
            int total     = np_total_lines(nd->text, (int)nd->len);
            int cur_col   = nd->cursor_pos - np_line_start(nd->text, (int)nd->len, cur_line);
            if (cur_line < total - 1) {
                int next_start = np_line_start(nd->text, (int)nd->len, cur_line + 1);
                nd->cursor_pos = next_start + cur_col;
                if (nd->cursor_pos > (int)nd->len) nd->cursor_pos = (int)nd->len;
            }
        }
        else if (kc == KEY_DEL) np_delete(nd);
        return;
    }

    char key = (char)(kc & 0xFF);

    /* Ctrl+S: save. */
    if (key == 0x13 /* Ctrl+S */ || key == KEY_CTRL_S) {
        if (nd->filepath[0] == '\0') {
            /* Default: save to /tmp/notepad.txt */
            np_strcpy(nd->filepath, "/tmp/notepad.txt", VFS_NAME_MAX);
        }
        int fd = vfs_create(nd->filepath);
        if (fd >= 0) {
            vfs_write(fd, nd->text, nd->len);
            vfs_close(fd);
            nd->dirty = 0;
        }
        return;
    }

    /* Backspace. */
    if (key == '\b') { np_backspace(nd); return; }

    /* Enter. */
    if (key == '\n' || key == '\r' || key == 0x0A || key == 0x0D) {
        np_insert(nd, '\n');
        return;
    }

    /* Printable. */
    if (key >= ' ' && key <= '~')
        np_insert(nd, key);
}

/* ---- Public API ------------------------------------------------------- */

static notepad_data_t *np_alloc(void)
{
    notepad_data_t *nd = (notepad_data_t *)kmalloc(sizeof(notepad_data_t));
    if (!nd) return NULL;
    nd->text = (char *)kmalloc(NP_INIT_CAP);
    if (!nd->text) { kfree(nd); return NULL; }
    nd->text[0]     = '\0';
    nd->len         = 0;
    nd->capacity    = NP_INIT_CAP;
    nd->cursor_pos  = 0;
    nd->scroll_line = 0;
    nd->filepath[0] = '\0';
    nd->dirty       = 0;
    return nd;
}

void app_notepad_open(void)
{
    notepad_data_t *nd = np_alloc();
    if (!nd) return;
    wm_create_window(80, 60, NP_W, NP_H, "Notepad",
                     notepad_draw, notepad_event, nd);
}

void app_notepad_open_file(const char *path)
{
    if (!path) { app_notepad_open(); return; }

    notepad_data_t *nd = np_alloc();
    if (!nd) return;

    int fd = vfs_open(path);
    if (fd >= 0) {
        size_t sz = vfs_size(fd);
        vfs_seek(fd, 0, VFS_SEEK_SET);
        if (sz > 0) {
            if (!np_ensure_cap(nd, sz + 1)) sz = 0;
            if (sz > 0) {
                nd->len = vfs_read(fd, nd->text, sz);
                nd->text[nd->len] = '\0';
            }
        }
        vfs_close(fd);
        np_strcpy(nd->filepath, path, VFS_NAME_MAX);
    }

    wm_create_window(80, 60, NP_W, NP_H, path, notepad_draw, notepad_event, nd);
}
