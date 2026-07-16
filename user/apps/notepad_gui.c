#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#define NP_W 640
#define NP_H 420
#define NP_CAP 8192

typedef struct notepad_state {
    ui_window_t win;
    char text[NP_CAP];
    int len;
    int cursor;
    char path[128];
    int dirty;
    int running;
} notepad_state_t;

static void np_load(notepad_state_t *st, const char *path)
{
    int fd;
    ssize_t n;
    if (!st || !path || !path[0])
        return;
    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return;
    n = read(fd, st->text, NP_CAP - 1);
    close(fd);
    if (n < 0)
        return;
    st->len = (int)n;
    st->text[st->len] = '\0';
    st->cursor = st->len;
    strncpy(st->path, path, sizeof(st->path) - 1);
    st->path[sizeof(st->path) - 1] = '\0';
}

static void np_save(notepad_state_t *st)
{
    int fd;
    if (!st)
        return;
    if (!st->path[0])
        strcpy(st->path, "/tmp/notepad.txt");
    fd = open(st->path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
        return;
    (void)write(fd, st->text, (size_t)st->len);
    close(fd);
    st->dirty = 0;
}

static void np_draw(notepad_state_t *st)
{
    int x = 8;
    int y = 24;
    ui_draw_rect(st->win, 0, 0, NP_W, NP_H, 0xFFF4F1E8u);
    ui_draw_rect(st->win, 0, 0, NP_W, 18, st->dirty ? 0xFFAA6B4Cu : 0xFF4C7A5Fu);
    ui_draw_string(st->win, 8, 5, st->path[0] ? st->path : "(untitled)", 0xFFFFFFFFu);
    ui_draw_string(st->win, NP_W - 150, 5, st->dirty ? "Modified [Ctrl+S]" : "Saved", 0xFFFFFFFFu);

    for (int i = 0; i < st->len; i++) {
        char c = st->text[i];
        if (c == '\n') {
            x = 8;
            y += 10;
            continue;
        }
        if (x + 8 > NP_W - 8) {
            x = 8;
            y += 10;
        }
        if (y + 8 > NP_H - 8)
            break;
        {
            char tmp[2] = { c, '\0' };
            ui_draw_string(st->win, x, y, tmp, 0xFF202020u);
            x += 8;
        }
    }
    ui_mark_dirty(st->win, 0, 0, NP_W, NP_H);
}

static void np_insert_char(notepad_state_t *st, char c)
{
    if (!st || st->len + 1 >= NP_CAP)
        return;
    for (int i = st->len; i >= st->cursor; i--)
        st->text[i + 1] = st->text[i];
    st->text[st->cursor++] = c;
    st->len++;
    st->dirty = 1;
}

static int notepad_main(int argc, char **argv)
{
    notepad_state_t st;
    ui_event_t ev;
    memset(&st, 0, sizeof(st));
    if (argc > 1)
        np_load(&st, argv[1]);

    st.win = ui_window_create("Notepad", 110, 80, NP_W, NP_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;

    np_draw(&st);
    while (st.running) {
        if (!ui_get_event(st.win, &ev)) {
            yield();
            continue;
        }
        if (ev.type == UI_EVENT_CLOSE) {
            st.running = 0;
            break;
        }
        if (ev.type == UI_EVENT_PAINT || ev.type == UI_EVENT_RESIZE) {
            np_draw(&st);
            continue;
        }
        if (ev.type != UI_EVENT_KEY)
            continue;

        {
            char c = (char)(ev.keycode & 0xFF);
            if (c == 0x13) {
                np_save(&st);
            } else if (c == '\b') {
                if (st.cursor > 0) {
                    for (int i = st.cursor - 1; i < st.len; i++)
                        st.text[i] = st.text[i + 1];
                    st.cursor--;
                    st.len--;
                    st.dirty = 1;
                }
            } else if (c == '\n' || c == '\r') {
                np_insert_char(&st, '\n');
            } else if (c >= ' ' && c <= '~') {
                np_insert_char(&st, c);
            }
        }
        np_draw(&st);
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_notepad_gui_entry(void)
{
    _exit(app_run_main(notepad_main));
}
