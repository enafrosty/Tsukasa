/*
 * Project Tsukasa — Notification Daemon Implementation
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

#include "notifyd.h"
#include "../libvanilla/service.h"
#include "../apps/app_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32) && defined(VANILLA_HOST)
#include <winsock2.h>
#include <afunix.h>
#include <ws2tcpip.h>
#define close_socket(s) closesocket(s)
#define unlink_file(p) remove(p)
#define sleep_ms(ms) Sleep(ms)
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <time.h>
#include <signal.h>
#define close_socket(s) close(s)
#define unlink_file(p) unlink(p)
#define sleep_ms(ms) do { struct timespec ts = { 0, (ms) * 1000000 }; nanosleep(&ts, NULL); } while (0)
#endif

#ifndef FBIOGET_VSCREENINFO
#define FBIOGET_VSCREENINFO 0x4600
#endif

struct fb_var_screeninfo_stub {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
};

typedef struct {
    uint32_t         notif_id;
    int              active;
    uint32_t         window_id;
    vanilla_window_t *window;
    int              source_fd;
    uint32_t         timeout_ms;
    uint64_t         start_ms;
    uint32_t         seq;
    char             title[VNOTIF_TITLE_MAX];
    char             body[VNOTIF_BODY_MAX];
    char             icon_name[VNOTIF_ICON_MAX];
    vnotif_action_t  actions[VNOTIF_ACTION_MAX];
    int              action_count;
    int              height;
    int              current_y;
    int              target_y;
} notif_entry_t;

static notif_entry_t g_notifs[NOTIF_MAX_ACTIVE];
static uint32_t      g_next_notif_id = 1;
static uint32_t      g_seq_counter = 1;

static uint64_t get_monotonic_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
    return 0;
#endif
}

static int vnotif_write_exact(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    int retries = 0;

    while (written < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = send(fd, (const char *)(p + written), (int)(count - written), 0);
        if (ret > 0) {
            written += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#else
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#endif
    }
    return 0;
}

static int vnotif_read_exact(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;

    while (received < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = recv(fd, (char *)(p + received), (int)(count - received), 0);
        if (ret > 0) {
            received += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#else
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#endif
    }
    return 0;
}

static void render_toast(notif_entry_t *n)
{
    if (!n || !n->window)
        return;

    vanilla_surface_t *surf = &n->window->surface;
    uint32_t bg_col = g_theme ? g_theme->bg_elevated : 0xFF23272E;
    uint32_t border_col = g_theme ? g_theme->border : 0xFF3E4451;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFF888888;

    app_fill_rect(surf, 0, 0, TOAST_WIDTH, n->height, bg_col);
    app_draw_rect(surf, 0, 0, TOAST_WIDTH, n->height, border_col);

    app_draw_text(surf, TOAST_WIDTH - 18, 8, "x", fg_muted);

    uint32_t icon_bg = g_theme ? g_theme->accent : 0xFF3B82F6;
    const char *symbol = "i";

    if (strcmp(n->icon_name, "warning") == 0) {
        icon_bg = g_theme ? g_theme->warning : 0xFFF59E0B;
        symbol = "!";
    } else if (strcmp(n->icon_name, "error") == 0) {
        icon_bg = g_theme ? g_theme->danger : 0xFFEF4444;
        symbol = "X";
    } else if (strcmp(n->icon_name, "success") == 0) {
        icon_bg = g_theme ? g_theme->success : 0xFF10B981;
        symbol = "v";
    }

    app_fill_rect(surf, 12, 14, 24, 24, icon_bg);
    app_draw_text(surf, 20, 22, symbol, 0xFFFFFFFF);

    app_draw_text(surf, 44, 14, n->title, fg_primary);

    if (n->body[0] != '\0') {
        size_t blen = strlen(n->body);
        if (blen <= 32) {
            app_draw_text(surf, 44, 34, n->body, fg_muted);
        } else {
            char line1[33];
            size_t split = 32;
            while (split > 20 && n->body[split] != ' ')
                split--;
            if (split <= 20)
                split = 32;
            memcpy(line1, n->body, split);
            line1[split] = '\0';
            app_draw_text(surf, 44, 32, line1, fg_muted);

            const char *rem = n->body + split;
            while (*rem == ' ')
                rem++;
            if (*rem != '\0') {
                char line2[33];
                strncpy(line2, rem, 32);
                line2[32] = '\0';
                app_draw_text(surf, 44, 46, line2, fg_muted);
            }
        }
    }

    if (n->action_count > 0) {
        int btn_gap = 8;
        int total_btn_w = (TOAST_WIDTH - 24) - (n->action_count - 1) * btn_gap;
        int btn_w = total_btn_w / n->action_count;
        for (int a = 0; a < n->action_count; a++) {
            int bx = 12 + a * (btn_w + btn_gap);
            int by = 70;
            app_draw_button(surf, bx, by, btn_w, 24, n->actions[a].label, 0);
        }
    }
}

static void notif_dismiss(notif_entry_t *n, uint32_t action_id)
{
    if (!n || !n->active)
        return;

    if (n->source_fd >= 0) {
        vnotif_event_t evt;
        memset(&evt, 0, sizeof(evt));
        evt.magic = VNOTIF_MAGIC;
        evt.op = VNOTIF_OP_EVENT;
        evt.notif_id = n->notif_id;
        evt.action_id = action_id;
        (void)vnotif_write_exact(n->source_fd, &evt, sizeof(evt));
        close_socket(n->source_fd);
        n->source_fd = -1;
    }

    if (n->window) {
        vanilla_destroy_window(n->window);
        n->window = NULL;
        n->window_id = 0;
    }

    n->active = 0;
}

static void restack_toasts(vanilla_client_t *client, int screen_w)
{
    int toast_x = screen_w - TOAST_WIDTH - TOAST_MARGIN_X;
    int next_y = TOAST_MARGIN_Y;
    int vis_count = 0;

    /* Collect visible toasts sorted by sequence order */
    int vis_indices[NOTIF_MAX_ACTIVE];
    int vis_total = 0;

    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active && (g_notifs[i].window != NULL || (client == NULL && g_notifs[i].start_ms != 0))) {
            vis_indices[vis_total++] = i;
        }
    }

    for (int i = 0; i < vis_total; i++) {
        for (int j = i + 1; j < vis_total; j++) {
            if (g_notifs[vis_indices[j]].seq < g_notifs[vis_indices[i]].seq) {
                int tmp = vis_indices[i];
                vis_indices[i] = vis_indices[j];
                vis_indices[j] = tmp;
            }
        }
    }

    for (int idx = 0; idx < vis_total; idx++) {
        notif_entry_t *n = &g_notifs[vis_indices[idx]];
        if (n->current_y != next_y) {
            n->current_y = next_y;
            n->target_y = next_y;
            if (n->window) {
                vanilla_move_window(n->window, toast_x, next_y);
            }
        }
        next_y += n->height + TOAST_SPACING;
        vis_count++;
    }

    /* Promote queued notifications in sequence order */
    while (vis_count < NOTIF_MAX_VISIBLE) {
        int best = -1;
        for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
            if (g_notifs[i].active && g_notifs[i].window == NULL && g_notifs[i].start_ms == 0) {
                if (best < 0 || g_notifs[i].seq < g_notifs[best].seq)
                    best = i;
            }
        }
        if (best < 0)
            break;

        notif_entry_t *n = &g_notifs[best];
        n->current_y = next_y;
        n->target_y = next_y;
        n->start_ms = get_monotonic_ms();

        if (client) {
            n->window = vanilla_create_window(client, "Notification",
                                              toast_x, next_y,
                                              TOAST_WIDTH, n->height,
                                              WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_TRANSPARENT);
            if (!n->window) {
                notif_dismiss(n, 0);
                continue;
            }
            n->window_id = n->window->window_id;
            render_toast(n);
            vanilla_map_window(n->window);
            vanilla_present(n->window, NULL);
        }

        next_y += n->height + TOAST_SPACING;
        vis_count++;
    }
}

static void check_timeouts(vanilla_client_t *client, int screen_w)
{
    uint64_t now_ms = get_monotonic_ms();
    int any_dismissed = 0;

    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active && g_notifs[i].timeout_ms > 0 && g_notifs[i].start_ms > 0) {
            if (now_ms >= g_notifs[i].start_ms + (uint64_t)g_notifs[i].timeout_ms) {
                notif_dismiss(&g_notifs[i], 0);
                any_dismissed = 1;
            }
        }
    }

    if (any_dismissed)
        restack_toasts(client, screen_w);
}

static void notif_handle_request(int cfd, vanilla_client_t *client, int screen_w)
{
    uint8_t hdr_buf[5];
    if (vnotif_read_exact(cfd, hdr_buf, sizeof(hdr_buf)) < 0) {
        close_socket(cfd);
        return;
    }

    uint32_t magic;
    memcpy(&magic, hdr_buf, 4);
    uint8_t op = hdr_buf[4];

    if (magic != VNOTIF_MAGIC) {
        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic = VNOTIF_MAGIC;
        resp.status = -EINVAL;
        (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
        close_socket(cfd);
        return;
    }

    if (op == VNOTIF_OP_SEND) {
        vnotif_send_t send_req;
        memset(&send_req, 0, sizeof(send_req));
        memcpy(&send_req, hdr_buf, sizeof(hdr_buf));

        size_t rem = sizeof(vnotif_send_t) - sizeof(hdr_buf);
        if (vnotif_read_exact(cfd, ((uint8_t *)&send_req) + sizeof(hdr_buf), rem) < 0) {
            close_socket(cfd);
            return;
        }

        if (send_req.action_count > VNOTIF_ACTION_MAX) {
            vnotif_resp_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic = VNOTIF_MAGIC;
            resp.status = -EINVAL;
            (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
            close_socket(cfd);
            return;
        }

        int slot = -1;
        for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
            if (!g_notifs[i].active) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            vnotif_resp_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic = VNOTIF_MAGIC;
            resp.status = -ENOSPC;
            (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
            close_socket(cfd);
            return;
        }

        notif_entry_t *n = &g_notifs[slot];
        memset(n, 0, sizeof(*n));
        n->active = 1;
        n->notif_id = g_next_notif_id++;
        if (g_next_notif_id == 0)
            g_next_notif_id = 1;
        n->seq = g_seq_counter++;

        send_req.title[sizeof(send_req.title) - 1] = '\0';
        send_req.body[sizeof(send_req.body) - 1] = '\0';
        send_req.icon_name[sizeof(send_req.icon_name) - 1] = '\0';
        for (int a = 0; a < VNOTIF_ACTION_MAX; a++)
            send_req.actions[a].label[sizeof(send_req.actions[a].label) - 1] = '\0';

        strncpy(n->title, send_req.title, sizeof(n->title) - 1);
        n->title[sizeof(n->title) - 1] = '\0';
        strncpy(n->body, send_req.body, sizeof(n->body) - 1);
        n->body[sizeof(n->body) - 1] = '\0';
        strncpy(n->icon_name, send_req.icon_name, sizeof(n->icon_name) - 1);
        n->icon_name[sizeof(n->icon_name) - 1] = '\0';
        n->timeout_ms = send_req.timeout_ms;
        n->action_count = send_req.action_count;
        if (n->action_count > 0) {
            memcpy(n->actions, send_req.actions, sizeof(vnotif_action_t) * (size_t)n->action_count);
            n->source_fd = cfd;
        } else {
            n->source_fd = -1;
        }

        n->height = (n->action_count > 0) ? (TOAST_BASE_HEIGHT + TOAST_ACTION_HEIGHT) : TOAST_BASE_HEIGHT;

        restack_toasts(client, screen_w);

        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic = VNOTIF_MAGIC;
        resp.status = 0;
        resp.notif_id = n->notif_id;

        (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
        if (n->action_count == 0) {
            close_socket(cfd);
        }

        return;
    }

    if (op == VNOTIF_OP_CLOSE) {
        vnotif_close_t close_req;
        memset(&close_req, 0, sizeof(close_req));
        memcpy(&close_req, hdr_buf, sizeof(hdr_buf));

        size_t rem = sizeof(vnotif_close_t) - sizeof(hdr_buf);
        if (vnotif_read_exact(cfd, ((uint8_t *)&close_req) + sizeof(hdr_buf), rem) < 0) {
            close_socket(cfd);
            return;
        }

        int found = 0;
        for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
            if (g_notifs[i].active && g_notifs[i].notif_id == close_req.notif_id) {
                notif_dismiss(&g_notifs[i], 0);
                found = 1;
                break;
            }
        }

        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic = VNOTIF_MAGIC;
        resp.status = found ? 0 : -ENOENT;
        resp.notif_id = found ? close_req.notif_id : 0;

        (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
        close_socket(cfd);

        if (found)
            restack_toasts(client, screen_w);
        return;
    }

    vnotif_resp_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = VNOTIF_MAGIC;
    resp.status = -EINVAL;
    (void)vnotif_write_exact(cfd, &resp, sizeof(resp));
    close_socket(cfd);
}

int notifyd_run(const char *sock_path, volatile int *stop_flag)
{
    volatile int default_stop = 0;
    if (!stop_flag)
        stop_flag = &default_stop;

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    unlink_file(sock_path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("[notifyd] Failed to create socket: %d\n", errno);
        return 1;
    }

#if defined(_WIN32) && defined(VANILLA_HOST)
    u_long nonblock_mode = 1;
    ioctlsocket(listen_fd, FIONBIO, &nonblock_mode);
#else
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[notifyd] Failed to bind to %s: %d\n", sock_path, errno);
        close_socket(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        printf("[notifyd] Failed to listen on socket: %d\n", errno);
        close_socket(listen_fd);
        return 1;
    }

    printf("[notifyd] Notification daemon listening on %s\n", sock_path);
    fflush(stdout);

    /* Connect to Vanilla display server */
    vanilla_client_t *client = NULL;
    int screen_w = 1024;

    if (!getenv("VNOTIF_HEADLESS")) {
        for (int retry = 0; retry < 50; retry++) {
            if (*stop_flag)
                break;
            client = vanilla_connect(NULL);
            if (client)
                break;
            sleep_ms(100);
        }

        if (client) {
#if !defined(_WIN32)
            int fb_fd = open("/dev/fb0", O_RDONLY);
            if (fb_fd >= 0) {
                struct fb_var_screeninfo_stub vinfo;
                if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && vinfo.xres > 0) {
                    screen_w = (int)vinfo.xres;
                }
                close(fb_fd);
            }
#endif
        } else {
            printf("[notifyd] Warning: display server not connected, running headless\n");
            fflush(stdout);
        }
    }

    int registered = 0;
    if (!getenv("VNOTIF_NO_REGISTER")) {
        for (int retry = 0; retry < 30; retry++) {
            if (*stop_flag)
                break;
            if (service_register("notification", sock_path) == 0) {
                registered = 1;
                break;
            }
            sleep_ms(100);
        }
        if (!registered && !*stop_flag) {
            printf("[notifyd] WARN: failed to register with registryd\n");
            fflush(stdout);
        }
    }

    while (!*stop_flag) {
        /* 1. Accept client requests */
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            notif_handle_request(client_fd, client, screen_w);
        }

        /* 2. Process input events from toast windows */
        if (client) {
            vanilla_event_t ev;
            while (vanilla_poll_event(client, &ev) > 0) {
                if (ev.type == VANILLA_EVENT_INPUT ||
                    ev.type == VANILLA_EVENT_DOUBLE_CLICK ||
                    ev.type == VANILLA_EVENT_TRIPLE_CLICK) {
                    struct input_event *iev = &ev.input;
                    if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value >= 1) {
                        int lx = (int)iev->pad1;
                        int ly = (int)iev->pad2;

                        notif_entry_t *target = NULL;
                        for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
                            if (g_notifs[i].active && g_notifs[i].window_id == ev.window_id) {
                                target = &g_notifs[i];
                                break;
                            }
                        }

                        if (target) {
                            if (lx >= TOAST_WIDTH - 26 && lx <= TOAST_WIDTH - 2 && ly >= 2 && ly <= 26) {
                                notif_dismiss(target, 0);
                                restack_toasts(client, screen_w);
                            } else if (target->action_count > 0 && ly >= 66 && ly <= 98) {
                                int btn_gap = 8;
                                int total_btn_w = (TOAST_WIDTH - 24) - (target->action_count - 1) * btn_gap;
                                int btn_w = total_btn_w / target->action_count;
                                for (int a = 0; a < target->action_count; a++) {
                                    int bx = 12 + a * (btn_w + btn_gap);
                                    if (lx >= bx && lx < bx + btn_w) {
                                        uint32_t act_id = target->actions[a].action_id;
                                        notif_dismiss(target, act_id);
                                        restack_toasts(client, screen_w);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* 3. Check for dropped client sockets on action notifications */
        for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
            if (g_notifs[i].active && g_notifs[i].source_fd >= 0) {
#if !defined(_WIN32)
                struct pollfd pfd;
                pfd.fd = g_notifs[i].source_fd;
                pfd.events = POLLIN | POLLHUP | POLLERR;
                pfd.revents = 0;
                if (poll(&pfd, 1, 0) > 0) {
                    if (pfd.revents & (POLLHUP | POLLERR)) {
                        close_socket(g_notifs[i].source_fd);
                        g_notifs[i].source_fd = -1;
                    } else if (pfd.revents & POLLIN) {
                        char dummy;
                        if (read(g_notifs[i].source_fd, &dummy, 1) <= 0) {
                            close_socket(g_notifs[i].source_fd);
                            g_notifs[i].source_fd = -1;
                        }
                    }
                }
#endif
            }
        }

        /* 4. Check auto-dismiss timeouts */
        check_timeouts(client, screen_w);

        /* 5. Frame pacing (~60Hz) */
        sleep_ms(16);
    }

    if (registered)
        service_unregister("notification");

    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active) {
            notif_dismiss(&g_notifs[i], 0);
        }
    }

    if (client)
        vanilla_disconnect(client);

    close_socket(listen_fd);
    unlink_file(sock_path);

    return 0;
}

#ifdef VNOTIF_TEST_NO_MAIN
int notifyd_test_get_counts(int *out_active, int *out_visible)
{
    int active = 0;
    int visible = 0;
    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active) {
            active++;
            if (g_notifs[i].window != NULL || g_notifs[i].start_ms != 0)
                visible++;
        }
    }
    if (out_active) *out_active = active;
    if (out_visible) *out_visible = visible;
    return 0;
}

int notifyd_test_trigger_action(uint32_t notif_id, uint32_t action_id)
{
    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active && g_notifs[i].notif_id == notif_id) {
            notif_dismiss(&g_notifs[i], action_id);
            restack_toasts(NULL, 1024);
            return 0;
        }
    }
    return -ENOENT;
}

uint32_t notifyd_test_get_window_id(uint32_t notif_id)
{
    for (int i = 0; i < NOTIF_MAX_ACTIVE; i++) {
        if (g_notifs[i].active && g_notifs[i].notif_id == notif_id)
            return g_notifs[i].window_id;
    }
    return 0;
}
#endif

#ifndef VNOTIF_TEST_NO_MAIN
int main(int argc, char **argv)
{
    const char *sock_path = VNOTIF_SOCKET_PATH;
    if (argc > 1 && argv[1] && argv[1][0] != '\0')
        sock_path = argv[1];

    volatile int stop_flag = 0;
    return notifyd_run(sock_path, &stop_flag);
}
#endif
