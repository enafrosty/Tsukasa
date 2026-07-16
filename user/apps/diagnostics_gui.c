#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#define DG_W 700
#define DG_H 420
#define DG_BUF 8192

typedef struct diagnostics_state {
    ui_window_t win;
    int running;
    char content[DG_BUF];
} diagnostics_state_t;

static void read_file_into(const char *path, char *dst, size_t cap)
{
    int fd = open(path, O_RDONLY, 0);
    ssize_t n;
    if (!dst || cap == 0)
        return;
    dst[0] = '\0';
    if (fd < 0)
        return;
    n = read(fd, dst, cap - 1);
    close(fd);
    if (n < 0)
        return;
    dst[n] = '\0';
}

static void dg_refresh(diagnostics_state_t *st)
{
    char proc[4096];
    char mem[2048];
    char net[1024];
    if (!st)
        return;
    read_file_into("/proc/processes", proc, sizeof(proc));
    read_file_into("/sys/memory", mem, sizeof(mem));
    read_file_into("/sys/net/status", net, sizeof(net));
    snprintf(st->content, sizeof(st->content),
             "[/proc/processes]\n%s\n[/sys/memory]\n%s\n[/sys/net/status]\n%s\n",
             proc, mem, net);
}

static void dg_draw(diagnostics_state_t *st)
{
    int x = 8;
    int y = 24;
    ui_draw_rect(st->win, 0, 0, DG_W, DG_H, 0xFF111A22u);
    ui_draw_rect(st->win, 0, 0, DG_W, 18, 0xFF26384Au);
    ui_draw_string(st->win, 8, 5, "Diagnostics (R refresh)", 0xFFFFFFFFu);
    for (size_t i = 0; st->content[i] && y + 8 < DG_H - 8; i++) {
        char c = st->content[i];
        if (c == '\n') {
            x = 8;
            y += 10;
            continue;
        }
        if (x + 8 > DG_W - 8) {
            x = 8;
            y += 10;
        }
        {
            char tmp[2] = { c, '\0' };
            ui_draw_string(st->win, x, y, tmp, 0xFFE3EDF7u);
            x += 8;
        }
    }
    ui_mark_dirty(st->win, 0, 0, DG_W, DG_H);
}

static int diagnostics_main(int argc, char **argv)
{
    diagnostics_state_t st;
    ui_event_t ev;
    (void)argc;
    (void)argv;
    memset(&st, 0, sizeof(st));
    st.win = ui_window_create("Diagnostics", 120, 70, DG_W, DG_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;
    dg_refresh(&st);
    dg_draw(&st);

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
            dg_draw(&st);
            continue;
        }
        if (ev.type == UI_EVENT_KEY) {
            char c = (char)(ev.keycode & 0xFF);
            if (c == 'r' || c == 'R')
                dg_refresh(&st);
            dg_draw(&st);
        }
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_diagnostics_gui_entry(void)
{
    _exit(app_run_main(diagnostics_main));
}
