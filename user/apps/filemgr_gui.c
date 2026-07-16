#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/sys/stat.h"
#include "../include/unistd.h"

#include "../lib/syscall.h"

#define FM_W 560
#define FM_H 380
#define FM_MAX 96

typedef struct filemgr_state {
    ui_window_t win;
    char names[FM_MAX][64];
    int count;
    int selected;
    int scroll;
    int running;
} filemgr_state_t;

static void fm_refresh(filemgr_state_t *st)
{
    if (!st)
        return;
    st->count = fs_list("/", st->names, FM_MAX);
    if (st->count < 0)
        st->count = 0;
    if (st->selected >= st->count)
        st->selected = st->count - 1;
    if (st->selected < 0)
        st->selected = 0;
}

static void fm_draw(filemgr_state_t *st)
{
    int y = 28;
    int visible = (FM_H - 40) / 12;
    ui_draw_rect(st->win, 0, 0, FM_W, FM_H, 0xFF16212Au);
    ui_draw_rect(st->win, 0, 0, FM_W, 20, 0xFF274055u);
    ui_draw_string(st->win, 8, 6, "Files - userspace (Enter=open, R=refresh)", 0xFFFFFFFFu);

    if (st->scroll > st->selected)
        st->scroll = st->selected;
    if (st->selected >= st->scroll + visible)
        st->scroll = st->selected - visible + 1;
    if (st->scroll < 0)
        st->scroll = 0;

    for (int i = 0; i < visible; i++) {
        int idx = st->scroll + i;
        if (idx >= st->count)
            break;
        if (idx == st->selected)
            ui_draw_rect(st->win, 6, y - 1, FM_W - 12, 11, 0xFF335066u);
        ui_draw_string(st->win, 10, y, st->names[idx], 0xFFE8F2FFu);
        y += 12;
    }
    ui_mark_dirty(st->win, 0, 0, FM_W, FM_H);
}

static int is_text_file(const char *name)
{
    size_t n = strlen(name);
    if (n < 4)
        return 0;
    return (strcmp(name + n - 4, ".txt") == 0 ||
            strcmp(name + n - 3, ".md") == 0 ||
            strcmp(name + n - 2, ".c") == 0 ||
            strcmp(name + n - 2, ".h") == 0);
}

static void fm_open_selected(filemgr_state_t *st)
{
    char full[128];
    char args[196];
    struct tsukasa_spawn_request req;
    if (!st || st->selected < 0 || st->selected >= st->count)
        return;

    full[0] = '/';
    strncpy(full + 1, st->names[st->selected], sizeof(full) - 2);
    full[sizeof(full) - 1] = '\0';

    if (!is_text_file(st->names[st->selected]))
        return;

    strcpy(args, "/apps/notepad ");
    strncat(args, full, sizeof(args) - strlen(args) - 1);
    req.path = "/apps/notepad";
    req.args = args;
    req.stdin_fd = 0;
    req.stdout_fd = 1;
    req.stderr_fd = 2;
    req.tty_id = -1;
    (void)spawn_ex(&req);
}

static int filemgr_main(int argc, char **argv)
{
    filemgr_state_t st;
    ui_event_t ev;
    (void)argc;
    (void)argv;
    memset(&st, 0, sizeof(st));
    st.win = ui_window_create("Files", 70, 60, FM_W, FM_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;
    fm_refresh(&st);
    fm_draw(&st);

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
            fm_draw(&st);
            continue;
        }
        if (ev.type == UI_EVENT_KEY) {
            uint32_t k = (uint32_t)ev.keycode;
            if (k == 'r' || k == 'R')
                fm_refresh(&st);
            else if (k == '\r' || k == '\n')
                fm_open_selected(&st);
            else if (k == 0x48u || k == 0xE048u) {
                if (st.selected > 0)
                    st.selected--;
            } else if (k == 0x50u || k == 0xE050u) {
                if (st.selected + 1 < st.count)
                    st.selected++;
            }
            fm_draw(&st);
        }
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_filemgr_gui_entry(void)
{
    _exit(app_run_main(filemgr_main));
}
