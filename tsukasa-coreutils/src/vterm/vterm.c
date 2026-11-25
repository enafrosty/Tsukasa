/*
 * Project Tsukasa — Userspace Multi-VT Virtual Terminal (/bin/vterm)
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define VTERM_MAX_ROWS 64
#define VTERM_MAX_COLS 160
#define CELL_W         8
#define CELL_H         12

typedef struct vterm_state {
    int rows;
    int cols;
    int cur_r;
    int cur_c;
    uint32_t fg;
    uint32_t bg;
    int esc_state;
    char esc_buf[32];
    size_t esc_len;
    char cells[VTERM_MAX_ROWS][VTERM_MAX_COLS];
    uint32_t cell_fg[VTERM_MAX_ROWS][VTERM_MAX_COLS];
    uint32_t cell_bg[VTERM_MAX_ROWS][VTERM_MAX_COLS];
    int dirty;
} vterm_state_t;

static void vterm_clear_row(vterm_state_t *st, int r)
{
    for (int c = 0; c < st->cols; c++) {
        st->cells[r][c] = ' ';
        st->cell_fg[r][c] = st->fg;
        st->cell_bg[r][c] = st->bg;
    }
}

static void vterm_scroll_up(vterm_state_t *st)
{
    for (int r = 1; r < st->rows; r++) {
        memcpy(st->cells[r - 1], st->cells[r], (size_t)st->cols);
        memcpy(st->cell_fg[r - 1], st->cell_fg[r], (size_t)st->cols * sizeof(uint32_t));
        memcpy(st->cell_bg[r - 1], st->cell_bg[r], (size_t)st->cols * sizeof(uint32_t));
    }
    vterm_clear_row(st, st->rows - 1);
}

static void vterm_init(vterm_state_t *st, int rows, int cols)
{
    memset(st, 0, sizeof(*st));
    st->rows = (rows > 0 && rows <= VTERM_MAX_ROWS) ? rows : 25;
    st->cols = (cols > 0 && cols <= VTERM_MAX_COLS) ? cols : 80;
    st->fg = 0xFFD8DEE9U;
    st->bg = 0xFF141820U;
    for (int r = 0; r < st->rows; r++)
        vterm_clear_row(st, r);
    st->dirty = 1;
}

static void vterm_apply_csi(vterm_state_t *st, char final_ch)
{
    int p1 = 0;
    int p2 = 0;
    int have_p1 = 0;
    int have_p2 = 0;
    int cur_val = 0;
    int field = 0;

    for (size_t i = 0; i < st->esc_len; i++) {
        char c = st->esc_buf[i];
        if (c >= '0' && c <= '9') {
            cur_val = cur_val * 10 + (c - '0');
            if (field == 0)
                have_p1 = 1;
            else
                have_p2 = 1;
        } else if (c == ';') {
            if (field == 0)
                p1 = cur_val;
            cur_val = 0;
            field++;
        }
    }
    if (field == 0)
        p1 = cur_val;
    else if (field == 1)
        p2 = cur_val;

    if (final_ch == 'H' || final_ch == 'f') {
        int r = (have_p1 && p1 > 0) ? (p1 - 1) : 0;
        int c = (have_p2 && p2 > 0) ? (p2 - 1) : 0;
        if (r >= st->rows) r = st->rows - 1;
        if (c >= st->cols) c = st->cols - 1;
        st->cur_r = r;
        st->cur_c = c;
    } else if (final_ch == 'J') {
        if (p1 == 2) {
            for (int r = 0; r < st->rows; r++)
                vterm_clear_row(st, r);
            st->cur_r = 0;
            st->cur_c = 0;
        }
    } else if (final_ch == 'K') {
        for (int c = st->cur_c; c < st->cols; c++) {
            st->cells[st->cur_r][c] = ' ';
            st->cell_fg[st->cur_r][c] = st->fg;
            st->cell_bg[st->cur_r][c] = st->bg;
        }
    } else if (final_ch == 'm') {
        if (!have_p1 || p1 == 0) {
            st->fg = 0xFFD8DEE9U;
            st->bg = 0xFF141820U;
        } else if (p1 >= 30 && p1 <= 37) {
            static const uint32_t pal[8] = {
                0xFF2E3440U, 0xFFBF616AU, 0xFFA3BE8CU, 0xFFEBCB8BU,
                0xFF81A1C1U, 0xFFB48EADU, 0xFF88C0D0U, 0xFFE5E9F0U
            };
            st->fg = pal[p1 - 30];
        }
    }
    st->dirty = 1;
}

static void vterm_feed(vterm_state_t *st, const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        unsigned char uc = (unsigned char)buf[i];
        char ch = (char)uc;

        if (st->esc_state == 1) {
            if (ch == '[') {
                st->esc_state = 2;
                st->esc_len = 0;
            } else {
                st->esc_state = 0;
            }
            continue;
        }
        if (st->esc_state == 2) {
            if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') {
                if (st->esc_len + 1 < sizeof(st->esc_buf))
                    st->esc_buf[st->esc_len++] = ch;
            } else {
                st->esc_buf[st->esc_len] = '\0';
                vterm_apply_csi(st, ch);
                st->esc_state = 0;
            }
            continue;
        }

        if (ch == 0x1B) {
            st->esc_state = 1;
            continue;
        }
        if (ch == '\r') {
            st->cur_c = 0;
            st->dirty = 1;
            continue;
        }
        if (ch == '\n') {
            st->cur_r++;
            if (st->cur_r >= st->rows) {
                vterm_scroll_up(st);
                st->cur_r = st->rows - 1;
            }
            st->dirty = 1;
            continue;
        }
        if (ch == '\b') {
            if (st->cur_c > 0)
                st->cur_c--;
            st->dirty = 1;
            continue;
        }
        if (ch == '\t') {
            int next = (st->cur_c + 4) & ~3;
            st->cur_c = (next < st->cols) ? next : (st->cols - 1);
            st->dirty = 1;
            continue;
        }
        if (uc >= 32 && uc < 127) {
            if (st->cur_c >= st->cols) {
                st->cur_c = 0;
                st->cur_r++;
                if (st->cur_r >= st->rows) {
                    vterm_scroll_up(st);
                    st->cur_r = st->rows - 1;
                }
            }
            st->cells[st->cur_r][st->cur_c] = ch;
            st->cell_fg[st->cur_r][st->cur_c] = st->fg;
            st->cell_bg[st->cur_r][st->cur_c] = st->bg;
            st->cur_c++;
            st->dirty = 1;
        }
    }
}

static void vterm_render_fb(vterm_state_t *st, uint32_t *fb_mem,
                            uint32_t fb_w, uint32_t fb_h, uint32_t pitch_px)
{
    if (!fb_mem || fb_w == 0 || fb_h == 0)
        return;

    for (int r = 0; r < st->rows; r++) {
        uint32_t y0 = (uint32_t)(r * CELL_H);
        if (y0 + CELL_H > fb_h)
            break;
        for (int c = 0; c < st->cols; c++) {
            uint32_t x0 = (uint32_t)(c * CELL_W);
            if (x0 + CELL_W > fb_w)
                break;
            uint32_t bg = st->cell_bg[r][c];
            uint32_t fg = st->cell_fg[r][c];
            char ch = st->cells[r][c];
            int is_cur = (r == st->cur_r && c == st->cur_c);

            for (uint32_t py = 0; py < CELL_H; py++) {
                uint32_t *row_ptr = fb_mem + (y0 + py) * pitch_px + x0;
                for (uint32_t px = 0; px < CELL_W; px++) {
                    uint32_t px_col = bg;
                    if (is_cur && py >= CELL_H - 2)
                        px_col = 0xFF88C0D0U;
                    else if (ch > ' ' && py >= 2 && py <= 9 && px >= 1 && px <= 6)
                        px_col = fg;
                    row_ptr[px] = px_col;
                }
            }
        }
    }
}

static int parse_vt_arg(const char *s)
{
    if (!s || !s[0])
        return -1;
    if (strncmp(s, "/dev/tty", 8) == 0)
        s += 8;
    int v = atoi(s);
    return (v >= 0 && v < 8) ? v : -1;
}

int main(int argc, char **argv)
{
    int target_vt = 1;
    int activate_vt = 0;
    const char *cmd_str = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vt") == 0 && i + 1 < argc) {
            int vt = parse_vt_arg(argv[++i]);
            if (vt >= 0) {
                target_vt = vt;
                activate_vt = 1;
            }
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cmd_str = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: vterm [--vt N] [-c command]\n");
            return 0;
        }
    }

    char tty_path[32];
    snprintf(tty_path, sizeof(tty_path), "/dev/tty%d", target_vt);
    int tty_fd = open(tty_path, O_RDWR, 0);
    if (tty_fd >= 0 && activate_vt)
        (void)ioctl(tty_fd, VT_ACTIVATE, (void *)(intptr_t)target_vt);

    int fb_fd = open("/dev/fb0", O_RDWR, 0);
    uint32_t *fb_mem = NULL;
    uint32_t fb_w = 0, fb_h = 0, pitch_px = 0;
    size_t fb_size = 0;
    if (fb_fd >= 0) {
        struct fb_var_screeninfo vinfo;
        struct fb_fix_screeninfo finfo;
        if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 &&
            ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0 &&
            vinfo.bits_per_pixel == 32 && finfo.smem_len > 0) {
            void *m = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
            if (m != MAP_FAILED) {
                fb_mem = (uint32_t *)m;
                fb_w = vinfo.xres;
                fb_h = vinfo.yres;
                pitch_px = finfo.line_length / 4U;
                fb_size = finfo.smem_len;
            }
        }
    }

    int ptm_fd = open("/dev/ptmx", O_RDWR | O_NONBLOCK, 0);
    if (ptm_fd < 0) {
        fprintf(stderr, "vterm: failed to open /dev/ptmx (%d)\n", errno);
        if (fb_mem) munmap(fb_mem, fb_size);
        if (fb_fd >= 0) close(fb_fd);
        if (tty_fd >= 0) close(tty_fd);
        return 1;
    }

    unsigned int ptn = 0;
    int unlock = 0;
    if (ioctl(ptm_fd, TIOCGPTN, &ptn) < 0 || ioctl(ptm_fd, TIOCSPTLCK, &unlock) < 0) {
        fprintf(stderr, "vterm: PTY setup failed (%d)\n", errno);
        close(ptm_fd);
        if (fb_mem) munmap(fb_mem, fb_size);
        if (fb_fd >= 0) close(fb_fd);
        if (tty_fd >= 0) close(tty_fd);
        return 1;
    }

    char pts_path[32];
    snprintf(pts_path, sizeof(pts_path), "/dev/pts/%u", ptn);
    int pts_fd = open(pts_path, O_RDWR, 0);
    if (pts_fd < 0) {
        fprintf(stderr, "vterm: failed to open %s (%d)\n", pts_path, errno);
        close(ptm_fd);
        if (fb_mem) munmap(fb_mem, fb_size);
        if (fb_fd >= 0) close(fb_fd);
        if (tty_fd >= 0) close(tty_fd);
        return 1;
    }

    int cols = (fb_w >= 640) ? (int)(fb_w / CELL_W) : 80;
    int rows = (fb_h >= 400) ? (int)(fb_h / CELL_H) : 25;
    if (cols > VTERM_MAX_COLS) cols = VTERM_MAX_COLS;
    if (rows > VTERM_MAX_ROWS) rows = VTERM_MAX_ROWS;

    struct winsize ws;
    ws.ws_row = (uint16_t)rows;
    ws.ws_col = (uint16_t)cols;
    ws.ws_xpixel = (uint16_t)fb_w;
    ws.ws_ypixel = (uint16_t)fb_h;
    (void)ioctl(ptm_fd, TIOCSWINSZ, &ws);

    vterm_state_t vt;
    vterm_init(&vt, rows, cols);

    char cmd_args[512];
    if (cmd_str)
        snprintf(cmd_args, sizeof(cmd_args), "tsh -c \"%s\"", cmd_str);
    else
        snprintf(cmd_args, sizeof(cmd_args), "/bin/tsh");

    struct tsukasa_spawn_request req;
    memset(&req, 0, sizeof(req));
    req.path = "/bin/tsh";
    req.args = cmd_args;
    req.stdin_fd = pts_fd;
    req.stdout_fd = pts_fd;
    req.stderr_fd = pts_fd;
    req.tty_id = -1;

    int child_pid = spawn_ex(&req);
    close(pts_fd);

    if (child_pid <= 0) {
        fprintf(stderr, "vterm: failed to spawn /bin/tsh\n");
        close(ptm_fd);
        if (fb_mem) munmap(fb_mem, fb_size);
        if (fb_fd >= 0) close(fb_fd);
        if (tty_fd >= 0) close(tty_fd);
        return 1;
    }

    struct termios orig_tty;
    int have_raw_tty = 0;
    if (!cmd_str && tty_fd >= 0 && tcgetattr(tty_fd, &orig_tty) == 0) {
        struct termios raw = orig_tty;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(tty_fd, TCSANOW, &raw) == 0)
            have_raw_tty = 1;
    }

    int child_status = 0;
    int child_exited = 0;
    char io_buf[512];

    for (;;) {
        for (;;) {
            ssize_t r = read(ptm_fd, io_buf, sizeof(io_buf));
            if (r > 0) {
                vterm_feed(&vt, io_buf, (size_t)r);
                if (cmd_str)
                    (void)write(STDOUT_FILENO, io_buf, (size_t)r);
                continue;
            }
            if (r == 0)
                child_exited = 1;
            break;
        }

        if (!child_exited) {
            int st = 0;
            if (waitpid(child_pid, &st, WNOHANG) == child_pid) {
                child_status = st;
                child_exited = 1;
            }
        }

        if (vt.dirty && fb_mem && tty_fd >= 0 && fb_fd >= 0) {
            int kd_mode = KD_GRAPHICS;
            struct vt_stat vst;
            if (ioctl(fb_fd, KDGETMODE, &kd_mode) == 0 &&
                kd_mode == KD_TEXT &&
                ioctl(tty_fd, VT_GETSTATE, &vst) == 0 &&
                (int)vst.v_active == target_vt) {
                vterm_render_fb(&vt, fb_mem, fb_w, fb_h, pitch_px);
            }
            vt.dirty = 0;
        }

        if (child_exited) {
            ssize_t r;
            while ((r = read(ptm_fd, io_buf, sizeof(io_buf))) > 0) {
                vterm_feed(&vt, io_buf, (size_t)r);
                if (cmd_str)
                    (void)write(STDOUT_FILENO, io_buf, (size_t)r);
            }
            break;
        }

        struct pollfd pfds[2];
        nfds_t nfds = 0;
        pfds[nfds].fd = ptm_fd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        nfds++;

        if (!cmd_str && tty_fd >= 0) {
            pfds[nfds].fd = tty_fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        int pr = poll(pfds, nfds, 50);
        if (pr > 0 && !cmd_str && tty_fd >= 0 && (pfds[1].revents & POLLIN)) {
            ssize_t kr = read(tty_fd, io_buf, sizeof(io_buf));
            if (kr > 0)
                (void)write(ptm_fd, io_buf, (size_t)kr);
        }
        if (pr > 0 && (pfds[0].revents & POLLHUP) && !(pfds[0].revents & POLLIN)) {
            if (!child_exited) {
                int st = 0;
                if (waitpid(child_pid, &st, WNOHANG) == child_pid) {
                    child_status = st;
                    child_exited = 1;
                }
            }
            if (child_exited)
                break;
        }
    }

    if (have_raw_tty)
        (void)tcsetattr(tty_fd, TCSANOW, &orig_tty);

    close(ptm_fd);
    if (fb_mem) munmap(fb_mem, fb_size);
    if (fb_fd >= 0) close(fb_fd);
    if (tty_fd >= 0) close(tty_fd);

    if (WIFEXITED(child_status))
        return WEXITSTATUS(child_status);
    return 0;
}
