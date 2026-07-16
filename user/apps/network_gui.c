#include "../include/app_runtime.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#include "../lib/syscall.h"

#define NW_W 440
#define NW_H 240

typedef struct network_state {
    ui_window_t win;
    int running;
    char status[256];
} network_state_t;

static void nw_refresh(network_state_t *st)
{
    struct tsukasa_net_link_info info;
    if (!st)
        return;
    if (net_init() != 0 || net_get_link(&info) != 0) {
        strcpy(st->status, "network unavailable");
        return;
    }
    snprintf(st->status, sizeof(st->status),
             "nic=%s link=%u\nip=%u.%u.%u.%u\ngateway=%u.%u.%u.%u\ndns=%u.%u.%u.%u",
             info.nic_name,
             (unsigned)info.link_up,
             (unsigned)info.ip.bytes[0], (unsigned)info.ip.bytes[1],
             (unsigned)info.ip.bytes[2], (unsigned)info.ip.bytes[3],
             (unsigned)info.gateway.bytes[0], (unsigned)info.gateway.bytes[1],
             (unsigned)info.gateway.bytes[2], (unsigned)info.gateway.bytes[3],
             (unsigned)info.dns.bytes[0], (unsigned)info.dns.bytes[1],
             (unsigned)info.dns.bytes[2], (unsigned)info.dns.bytes[3]);
}

static void nw_draw(network_state_t *st)
{
    int x = 8;
    int y = 30;
    ui_draw_rect(st->win, 0, 0, NW_W, NW_H, 0xFF0E1C2Au);
    ui_draw_rect(st->win, 0, 0, NW_W, 18, 0xFF2A4A68u);
    ui_draw_string(st->win, 8, 5, "Network (R refresh)", 0xFFFFFFFFu);
    for (size_t i = 0; st->status[i] && y + 8 < NW_H - 8; i++) {
        char c = st->status[i];
        if (c == '\n') {
            x = 8;
            y += 10;
            continue;
        }
        {
            char tmp[2] = { c, '\0' };
            ui_draw_string(st->win, x, y, tmp, 0xFFE9F3FFu);
            x += 8;
        }
    }
    ui_mark_dirty(st->win, 0, 0, NW_W, NW_H);
}

static int network_main(int argc, char **argv)
{
    network_state_t st;
    ui_event_t ev;
    (void)argc;
    (void)argv;
    memset(&st, 0, sizeof(st));
    st.win = ui_window_create("Network", 180, 120, NW_W, NW_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;
    nw_refresh(&st);
    nw_draw(&st);

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
            nw_draw(&st);
            continue;
        }
        if (ev.type == UI_EVENT_KEY) {
            char c = (char)(ev.keycode & 0xFF);
            if (c == 'r' || c == 'R')
                nw_refresh(&st);
            nw_draw(&st);
        }
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_network_gui_entry(void)
{
    _exit(app_run_main(network_main));
}
