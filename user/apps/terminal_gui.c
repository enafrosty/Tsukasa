#include "../include/libui.h"
#include "../include/shell.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#define TERM_W 640
#define TERM_H 420
#define TERM_OUT_CAP 4096
#define TERM_CMD_CAP 256
#define TERM_HISTORY_MAX 64
#define TERM_LINE_H 10

typedef struct terminal_state {
    ui_window_t win;
    int running;
    char out[TERM_OUT_CAP];
    int out_len;
    char cmd[TERM_CMD_CAP];
    int cmd_len;
    char history[TERM_HISTORY_MAX][TERM_CMD_CAP];
    int history_count;
    int history_pos;
} terminal_state_t;

static void term_append_raw(terminal_state_t *st, const char *s)
{
    if (!st || !s)
        return;
    while (*s) {
        if (st->out_len + 1 >= TERM_OUT_CAP) {
            memmove(st->out, st->out + TERM_OUT_CAP / 3, TERM_OUT_CAP - TERM_OUT_CAP / 3);
            st->out_len = TERM_OUT_CAP - TERM_OUT_CAP / 3 - 1;
            if (st->out_len < 0)
                st->out_len = 0;
            st->out[st->out_len] = '\0';
        }
        st->out[st->out_len++] = *s++;
    }
    st->out[st->out_len] = '\0';
}

static void term_append_line(terminal_state_t *st, const char *s)
{
    term_append_raw(st, s);
    term_append_raw(st, "\n");
}

static void term_draw(terminal_state_t *st)
{
    int x = 8;
    int y = 8;
    int max_y = TERM_H - 16;
    int line_count = 1;
    int skip = 0;
    int seen = 0;

    ui_draw_rect(st->win, 0, 0, TERM_W, TERM_H, 0xFF000000u);

    for (int i = 0; i < st->out_len; i++) {
        if (st->out[i] == '\n')
            line_count++;
    }
    skip = line_count - ((max_y - y) / TERM_LINE_H);
    if (skip < 0)
        skip = 0;

    x = 8;
    y = 8;
    for (int i = 0; i < st->out_len; i++) {
        char c = st->out[i];
        if (seen < skip) {
            if (c == '\n')
                seen++;
            continue;
        }
        if (c == '\n') {
            x = 8;
            y += TERM_LINE_H;
            if (y + 8 > max_y)
                break;
            continue;
        }
        char tmp[2];
        tmp[0] = c;
        tmp[1] = '\0';
        ui_draw_string(st->win, x, y, tmp, 0xFF33FF88u);
        x += 8;
        if (x + 8 > TERM_W - 8) {
            x = 8;
            y += TERM_LINE_H;
            if (y + 8 > max_y)
                break;
        }
    }

    ui_draw_string(st->win, 8, TERM_H - 14, "> ", 0xFF33FF88u);
    ui_draw_string(st->win, 24, TERM_H - 14, st->cmd, 0xFFFFFFFFu);
    ui_mark_dirty(st->win, 0, 0, TERM_W, TERM_H);
}

static void term_capture_shell(terminal_state_t *st, const char *line)
{
    int pipefd[2];
    char buf[256];
    ssize_t n;
    if (pipe(pipefd) != 0) {
        term_append_line(st, "shell: failed to allocate output pipe");
        return;
    }
    shell_exec_line(line, pipefd[1], pipefd[1]);
    close(pipefd[1]);

    while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        term_append_raw(st, buf);
    }
    close(pipefd[0]);
}

static void term_push_history(terminal_state_t *st, const char *line)
{
    if (!line || !line[0])
        return;
    if (st->history_count < TERM_HISTORY_MAX) {
        strncpy(st->history[st->history_count++], line, TERM_CMD_CAP - 1);
        st->history[st->history_count - 1][TERM_CMD_CAP - 1] = '\0';
    } else {
        for (int i = 1; i < TERM_HISTORY_MAX; i++)
            strcpy(st->history[i - 1], st->history[i]);
        strncpy(st->history[TERM_HISTORY_MAX - 1], line, TERM_CMD_CAP - 1);
        st->history[TERM_HISTORY_MAX - 1][TERM_CMD_CAP - 1] = '\0';
    }
    st->history_pos = st->history_count;
}

static void term_history_up(terminal_state_t *st)
{
    if (st->history_count <= 0)
        return;
    if (st->history_pos > 0)
        st->history_pos--;
    strncpy(st->cmd, st->history[st->history_pos], TERM_CMD_CAP - 1);
    st->cmd[TERM_CMD_CAP - 1] = '\0';
    st->cmd_len = (int)strlen(st->cmd);
}

static void term_history_down(terminal_state_t *st)
{
    if (st->history_count <= 0)
        return;
    if (st->history_pos + 1 < st->history_count) {
        st->history_pos++;
        strncpy(st->cmd, st->history[st->history_pos], TERM_CMD_CAP - 1);
        st->cmd[TERM_CMD_CAP - 1] = '\0';
        st->cmd_len = (int)strlen(st->cmd);
        return;
    }
    st->history_pos = st->history_count;
    st->cmd_len = 0;
    st->cmd[0] = '\0';
}

static void term_execute(terminal_state_t *st)
{
    if (st->cmd_len <= 0) {
        term_append_line(st, "");
        st->cmd[0] = '\0';
        st->cmd_len = 0;
        return;
    }

    term_append_raw(st, "> ");
    term_append_line(st, st->cmd);
    term_push_history(st, st->cmd);

    if (strcmp(st->cmd, "exit") == 0) {
        st->running = 0;
        return;
    }

    term_capture_shell(st, st->cmd);
    st->cmd[0] = '\0';
    st->cmd_len = 0;
}

void app_terminal_gui_entry(void)
{
    terminal_state_t st;
    ui_event_t ev;
    memset(&st, 0, sizeof(st));

    st.win = ui_window_create("Terminal", 90, 70, TERM_W, TERM_H);
    if ((int64_t)st.win <= 0)
        _exit(1);
    st.running = 1;
    st.history_pos = 0;

    term_append_line(&st, "Tsukasa Terminal (userspace)");
    term_append_line(&st, "Supports history, pipes, redirection, and rc scripts.");
    term_capture_shell(&st, "help");
    term_capture_shell(&st, "cat /etc/tsukasa.rc");

    term_draw(&st);
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
            term_draw(&st);
            continue;
        }
        if (ev.type != UI_EVENT_KEY)
            continue;

        if ((uint32_t)ev.keycode == 0x48u || (uint32_t)ev.keycode == 0xE048u) {
            term_history_up(&st);
            term_draw(&st);
            continue;
        }
        if ((uint32_t)ev.keycode == 0x50u || (uint32_t)ev.keycode == 0xE050u) {
            term_history_down(&st);
            term_draw(&st);
            continue;
        }

        {
            char c = (char)(ev.keycode & 0xFF);
            if (c == '\b') {
                if (st.cmd_len > 0)
                    st.cmd[--st.cmd_len] = '\0';
            } else if (c == '\r' || c == '\n') {
                term_execute(&st);
            } else if (c >= ' ' && c <= '~') {
                if (st.cmd_len + 1 < TERM_CMD_CAP) {
                    st.cmd[st.cmd_len++] = c;
                    st.cmd[st.cmd_len] = '\0';
                }
            }
        }
        term_draw(&st);
    }

    ui_window_destroy(st.win);
    _exit(0);
}
