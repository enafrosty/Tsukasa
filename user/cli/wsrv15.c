/*
 * Project Tsukasa — wsrv15: guide-15 userland display server (compositor)
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

#include "tsukasa_sdk.h"
#include "../../include/socket_defs.h"
#include "../../include/display_proto.h"
#include "../../input/event.h"

#include <stdint.h>
#include <stddef.h>

#define MAX_SURF 8
#define MAX_CONN 4
/* I/O spin budgets are the usock20 gate-proven scale (20000). */
#define SPIN_BUDGET   20000
#define TEST_BACKSTOP 400000
#define SERVE_TICKS   4000000

enum { MODE_M1 = 1, MODE_M2, MODE_M3, MODE_SERVE };

typedef struct {
    int in_use;
    uint32_t id;
    int conn_idx;
    int shm_id;
    uint32_t *px;
    uint32_t w, h;
    int x, y;
} surf_t;

typedef struct {
    int in_use;
    int fd;
} conn_t;

static surf_t g_surf[MAX_SURF];
static conn_t g_conn[MAX_CONN];
static uint32_t g_next_sid = 1;

/* Framebuffer state. */
static int g_fb_fd = -1;
static uint32_t *g_fb = NULL;
static uint32_t g_screen_w, g_screen_h, g_pitch_px;

/* Verdict flags. */
static int g_saw_client, g_created, g_shm_fail, g_proto;
static int g_verify_ok, g_verify_fail, g_shm_leak;

static int g_evfd = -1;   /* /dev/tty0 for serial evidence */

/* small helpers (self-contained, like usock20) */

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void addr_fill(struct tsk_sockaddr_un *ua, const char *path)
{
    int i = 0;
    ua->sun_family = TSK_AF_UNIX;
    while (path[i] && i < TSK_UNIX_PATH_MAX - 1) {
        ua->sun_path[i] = path[i];
        i++;
    }
    ua->sun_path[i] = '\0';
}

static int send_all(int fd, const void *buf, int n)
{
    const char *p = (const char *)buf;
    int sent = 0;
    for (int spin = 0; spin < SPIN_BUDGET && sent < n; spin++) {
        long r = write(fd, p + sent, (size_t)(n - sent));
        if (r > 0) {
            sent += (int)r;
            continue;
        }
        /* Dead peer = 0-byte writes forever: bail on POLLHUP instead of burning the whole budget (the m3 `die`... */
        {
            vfs_pollfd_t pf;
            pf.fd = fd;
            pf.events = (int16_t)(VFS_POLLOUT | VFS_POLLHUP);
            pf.revents = 0;
            poll(&pf, 1, 0);
            if (pf.revents & VFS_POLLHUP)
                return 0;
        }
        sched_yield();
    }
    return sent == n;
}

/* Returns 1 on n bytes read, 0 on HUP/timeout (poll distinguishes a dead peer from a merely-empty ring so we... */
static int recv_all(int fd, void *buf, int n)
{
    char *p = (char *)buf;
    int got = 0;
    for (int spin = 0; spin < SPIN_BUDGET && got < n; spin++) {
        long r = read(fd, p + got, (size_t)(n - got));
        if (r > 0) {
            got += (int)r;
            continue;
        }
        {
            vfs_pollfd_t pf;
            pf.fd = fd;
            pf.events = (int16_t)(VFS_POLLIN | VFS_POLLHUP);
            pf.revents = 0;
            poll(&pf, 1, 0);
            if ((pf.revents & VFS_POLLHUP) && !(pf.revents & VFS_POLLIN))
                return 0;
        }
        sched_yield();
    }
    return got == n;
}

static int send_frame(int fd, uint32_t type, const void *payload, uint32_t size)
{
    wsd_hdr_t h;
    h.magic = WSD_MAGIC;
    h.version = WSD_VERSION;
    h.flags = 0;
    h.msg_type = type;
    h.payload_size = size;
    if (!send_all(fd, &h, (int)sizeof(h)))
        return 0;
    if (size > 0 && payload && !send_all(fd, payload, (int)size))
        return 0;
    return 1;
}

/* Returns 0 ok, -2 no/too-small fb (SKIP), -1 mmap failed (FAIL). */
static int fb_setup(void)
{
    vfs_fb_var_screeninfo_t vinfo;
    vfs_fb_fix_screeninfo_t finfo;
    unsigned long size;

    g_fb_fd = open("/dev/fb0", TSK_O_RDWR);
    if (g_fb_fd < 0)
        return -2;
    if (ioctl(g_fb_fd, VFS_FBIOGET_VSCREENINFO, (long)(uintptr_t)&vinfo) != 0 ||
        ioctl(g_fb_fd, VFS_FBIOGET_FSCREENINFO, (long)(uintptr_t)&finfo) != 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
        return -2;
    }
    g_screen_w = vinfo.xres;
    g_screen_h = vinfo.yres;
    g_pitch_px = finfo.line_length / 4u;
    if (g_screen_w == 0 || g_screen_h == 0 || vinfo.bits_per_pixel != 32) {
        close(g_fb_fd);
        g_fb_fd = -1;
        return -2;
    }

    size = (unsigned long)finfo.line_length * (unsigned long)g_screen_h;
    g_fb = (uint32_t *)mmap(NULL, size, VFS_PROT_READ | VFS_PROT_WRITE,
                            VFS_MAP_SHARED, g_fb_fd, 0);
    if (g_fb == (uint32_t *)-1 || g_fb == NULL) {
        g_fb = NULL;
        close(g_fb_fd);
        g_fb_fd = -1;
        return -1;
    }
    ioctl(g_fb_fd, VFS_KDSETMODE, VFS_KD_GRAPHICS);
    return 0;
}

static void fb_teardown(void)
{
    if (g_fb_fd >= 0)
        ioctl(g_fb_fd, VFS_KDSETMODE, VFS_KD_TEXT);
    if (g_fb_fd >= 0) {
        close(g_fb_fd);
        g_fb_fd = -1;
    }
    g_fb = NULL;
}

static void fb_clear(uint32_t color)
{
    if (!g_fb)
        return;
    for (uint32_t y = 0; y < g_screen_h; y++)
        for (uint32_t x = 0; x < g_screen_w; x++)
            g_fb[y * g_pitch_px + x] = color;
}

/* Draw a filled rect on the framebuffer, clipped to the screen. */
static void fb_fill(int x, int y, int w, int h, uint32_t color)
{
    if (!g_fb)
        return;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)g_screen_h)
            continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)g_screen_w)
                continue;
            g_fb[(uint32_t)py * g_pitch_px + (uint32_t)px] = color;
        }
    }
}

/* Composite one surface's client pixels onto the framebuffer at its screen position, with a minimal... */
#define DECO_H 22
#define DECO_BORDER 2

static void composite(const surf_t *s)
{
    if (!g_fb || !s->px)
        return;

    fb_fill(s->x - DECO_BORDER, s->y - DECO_H,
            (int)s->w + 2 * DECO_BORDER, DECO_H, 0xFF2B2F3A);
    fb_fill(s->x - DECO_BORDER, s->y,
            DECO_BORDER, (int)s->h, 0xFF2B2F3A);
    fb_fill(s->x + (int)s->w, s->y,
            DECO_BORDER, (int)s->h, 0xFF2B2F3A);
    fb_fill(s->x - DECO_BORDER, s->y + (int)s->h,
            (int)s->w + 2 * DECO_BORDER, DECO_BORDER, 0xFF2B2F3A);

    for (uint32_t row = 0; row < s->h; row++) {
        int py = s->y + (int)row;
        if (py < 0 || py >= (int)g_screen_h)
            continue;
        for (uint32_t col = 0; col < s->w; col++) {
            int px = s->x + (int)col;
            if (px < 0 || px >= (int)g_screen_w)
                continue;
            g_fb[(uint32_t)py * g_pitch_px + (uint32_t)px] =
                s->px[row * s->w + col] | 0xFF000000u;
        }
    }
}

static int readback_ok(const surf_t *s)
{
    uint32_t rows = s->h < 16u ? s->h : 16u;
    uint32_t cols = s->w < 64u ? s->w : 64u;
    if (!g_fb || !s->px)
        return 0;
    for (uint32_t row = 0; row < rows; row++) {
        int py = s->y + (int)row;
        if (py < 0 || py >= (int)g_screen_h)
            return 0;
        for (uint32_t col = 0; col < cols; col++) {
            int px = s->x + (int)col;
            uint32_t want = s->px[row * s->w + col] | 0xFF000000u;
            if (px < 0 || px >= (int)g_screen_w)
                return 0;
            if (g_fb[(uint32_t)py * g_pitch_px + (uint32_t)px] != want)
                return 0;
        }
    }
    return 1;
}

static surf_t *surf_find(uint32_t id)
{
    for (int i = 0; i < MAX_SURF; i++)
        if (g_surf[i].in_use && g_surf[i].id == id)
            return &g_surf[i];
    return NULL;
}

static void surf_release(surf_t *s)
{
    if (!s || !s->in_use)
        return;
    if (s->px) {
        shm_detach(s->px);
        s->px = NULL;
    }
    if (s->shm_id > 0) {
        if (shm_destroy(s->shm_id) != 0)
            g_shm_leak = 1;
    }
    s->in_use = 0;
    s->id = 0;
    s->conn_idx = -1;
    s->shm_id = -1;
}

static void conn_teardown(int idx)
{
    for (int i = 0; i < MAX_SURF; i++)
        if (g_surf[i].in_use && g_surf[i].conn_idx == idx)
            surf_release(&g_surf[i]);
    if (g_conn[idx].in_use) {
        close(g_conn[idx].fd);
        g_conn[idx].in_use = 0;
        g_conn[idx].fd = -1;
    }
}

static int active_conns(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CONN; i++)
        if (g_conn[i].in_use)
            n++;
    return n;
}

static void handle_create(int conn_idx, int fd, const wsd_create_req_t *req)
{
    wsd_create_reply_t reply;
    surf_t *s = NULL;
    uint32_t w = req->w, h = req->h;

    g_created = 1;

    if (w == 0 || h == 0 || w > 2048 || h > 2048)
        w = h = 0;
    for (int i = 0; i < MAX_SURF && w; i++) {
        if (!g_surf[i].in_use) { s = &g_surf[i]; break; }
    }

    reply.surface_id = 0;
    reply.shm_id = -1;
    reply.pitch = 0;
    reply.w = 0;
    reply.h = 0;

    if (s) {
        int shm_id = shm_create((unsigned long)w * (unsigned long)h * 4ul);
        uint32_t *px = (shm_id > 0) ? (uint32_t *)shm_attach(shm_id) : NULL;
        if (shm_id > 0 && px) {
            int slot = (int)(s - g_surf);
            s->in_use = 1;
            s->id = g_next_sid++;
            s->conn_idx = conn_idx;
            s->shm_id = shm_id;
            s->px = px;
            s->w = w;
            s->h = h;
            s->x = 100 + slot * 24;
            s->y = 90 + slot * 24;
            reply.surface_id = s->id;
            reply.shm_id = shm_id;
            reply.pitch = w * 4u;
            reply.w = w;
            reply.h = h;
        } else {
            g_shm_fail = 1;
            if (shm_id > 0)
                shm_destroy(shm_id);
        }
    } else {
        g_shm_fail = 1;
    }
    send_frame(fd, WSD_MSG_CREATE_REPLY, &reply, (uint32_t)sizeof(reply));
}

static void handle_damage(int mode, int fd, const wsd_damage_t *dmg)
{
    wsd_evt_damage_ack_t ack;
    surf_t *s = surf_find(dmg->surface_id);
    if (!s)
        return;
    composite(s);
    ack.surface_id = s->id;
    ack.verified = 0;
    if (mode == MODE_M2) {
        if (readback_ok(s)) {
            g_verify_ok = 1;
            ack.verified = 1;
        } else {
            g_verify_fail = 1;
        }
    }
    send_frame(fd, WSD_EVT_DAMAGE_ACK, &ack, (uint32_t)sizeof(ack));
}

static void handle_destroy(const wsd_surface_ref_t *ref)
{
    surf_t *s = surf_find(ref->surface_id);
    if (s)
        surf_release(s);
}

/* Returns 1 = frame handled, 0 = HUP/closed, -1 = protocol error. */
static int conn_handle_frame(int mode, int conn_idx)
{
    int fd = g_conn[conn_idx].fd;
    wsd_hdr_t h;
    uint8_t payload[128];

    if (!recv_all(fd, &h, (int)sizeof(h)))
        return 0;
    if (h.magic != WSD_MAGIC || h.version != WSD_VERSION ||
        h.payload_size > sizeof(payload))
        return -1;
    if (h.payload_size > 0 && !recv_all(fd, payload, (int)h.payload_size))
        return 0;

    switch (h.msg_type) {
    case WSD_MSG_CREATE_SURFACE:
        if (h.payload_size == sizeof(wsd_create_req_t))
            handle_create(conn_idx, fd, (const wsd_create_req_t *)payload);
        break;
    case WSD_MSG_DAMAGE:
        if (h.payload_size == sizeof(wsd_damage_t))
            handle_damage(mode, fd, (const wsd_damage_t *)payload);
        break;
    case WSD_MSG_SET_TITLE:
        break;
    case WSD_MSG_DESTROY_SURFACE:
        if (h.payload_size == sizeof(wsd_surface_ref_t))
            handle_destroy((const wsd_surface_ref_t *)payload);
        break;
    case WSD_MSG_QUIT:
        return 0;
    default:
        break;
    }
    return 1;
}

static surf_t *focus_surface(void)
{
    surf_t *best = NULL;
    for (int i = 0; i < MAX_SURF; i++)
        if (g_surf[i].in_use && (!best || g_surf[i].id > best->id))
            best = &g_surf[i];
    return best;
}

static surf_t *surface_at(int x, int y)
{
    surf_t *hit = NULL;
    for (int i = 0; i < MAX_SURF; i++) {
        surf_t *s = &g_surf[i];
        if (!s->in_use)
            continue;
        if (x >= s->x && x < s->x + (int)s->w &&
            y >= s->y && y < s->y + (int)s->h) {
            if (!hit || s->id > hit->id)
                hit = s;
        }
    }
    return hit;
}

static void forward_input(int kbd_fd, int mouse_fd)
{
    struct gui_event evs[16];
    long r;

    if (kbd_fd >= 0) {
        r = read(kbd_fd, evs, sizeof(evs));
        for (long i = 0; i + (long)sizeof(struct gui_event) <= r;
             i += (long)sizeof(struct gui_event)) {
            struct gui_event *e = &evs[i / (long)sizeof(struct gui_event)];
            surf_t *s = focus_surface();
            if (!s)
                continue;
            wsd_evt_key_t k;
            k.surface_id = s->id;
            k.keycode = e->keycode;
            k.pressed = (e->event_id == INPUT_EVENT_KEY) ? 1 : 0;
            k.modifiers = e->modifiers;
            send_frame(g_conn[s->conn_idx].fd, WSD_EVT_KEY, &k, (uint32_t)sizeof(k));
        }
    }
    if (mouse_fd >= 0) {
        r = read(mouse_fd, evs, sizeof(evs));
        for (long i = 0; i + (long)sizeof(struct gui_event) <= r;
             i += (long)sizeof(struct gui_event)) {
            struct gui_event *e = &evs[i / (long)sizeof(struct gui_event)];
            surf_t *s = surface_at(e->x, e->y);
            if (!s)
                s = focus_surface();
            if (!s)
                continue;
            wsd_evt_pointer_t p;
            p.surface_id = s->id;
            p.x = e->x;
            p.y = e->y;
            p.buttons = e->keycode;
            send_frame(g_conn[s->conn_idx].fd, WSD_EVT_POINTER, &p, (uint32_t)sizeof(p));
        }
    }
}

/* accept + main loop */

static void try_accept(int lfd)
{
    vfs_pollfd_t pf;
    int afd;
    pf.fd = lfd;
    pf.events = (int16_t)VFS_POLLIN;
    pf.revents = 0;
    if (poll(&pf, 1, 0) != 1 || !(pf.revents & VFS_POLLIN))
        return;
    afd = accept(lfd);
    if (afd < 0)
        return;
    for (int i = 0; i < MAX_CONN; i++) {
        if (!g_conn[i].in_use) {
            g_conn[i].in_use = 1;
            g_conn[i].fd = afd;
            g_saw_client = 1;
            return;
        }
    }
    close(afd);
}

static void serve_loop(int mode, int lfd, int kbd_fd, int mouse_fd)
{
    long budget = (mode == MODE_SERVE) ? SERVE_TICKS : TEST_BACKSTOP;

    for (long tick = 0; tick < budget; tick++) {
        try_accept(lfd);

        for (int i = 0; i < MAX_CONN; i++) {
            vfs_pollfd_t pf;
            if (!g_conn[i].in_use)
                continue;
            pf.fd = g_conn[i].fd;
            pf.events = (int16_t)(VFS_POLLIN | VFS_POLLHUP);
            pf.revents = 0;
            poll(&pf, 1, 0);
            if (pf.revents & VFS_POLLIN) {
                int r = conn_handle_frame(mode, i);
                if (r <= 0) {
                    if (r < 0)
                        g_proto = 1;
                    conn_teardown(i);
                    continue;
                }
            } else if (pf.revents & VFS_POLLHUP) {
                conn_teardown(i);
            }
        }

        if (mode == MODE_SERVE)
            forward_input(kbd_fd, mouse_fd);

        if (mode != MODE_SERVE && g_saw_client && active_conns() == 0)
            return;

        sched_yield();
    }
}

static int verdict(int mode)
{
    if (mode == MODE_M1) {
        if (!g_saw_client)
            return 44;
        if (g_proto)
            return 46;
        if (g_shm_fail)
            return 47;
        if (g_shm_leak)
            return 49;
        return g_created ? 0 : 44;
    }
    if (mode == MODE_M2) {
        if (!g_saw_client)
            return 44;
        if (g_proto)
            return 46;
        if (g_shm_fail)
            return 47;
        if (g_verify_fail || !g_verify_ok)
            return 48;
        if (g_shm_leak)
            return 49;
        return 0;
    }
    if (!g_saw_client)
        return 44;
    if (g_proto)
        return 46;
    if (g_shm_fail)
        return 47;
    if (g_shm_leak)
        return 49;
    return g_created ? 0 : 44;
}

int main(int argc, char **argv)
{
    struct tsk_sockaddr_un ua;
    int mode;
    int lfd;
    int kbd_fd = -1, mouse_fd = -1;
    int rc;

    if (argc < 2)
        return 40;
    if (str_eq(argv[1], "m1"))
        mode = MODE_M1;
    else if (str_eq(argv[1], "m2"))
        mode = MODE_M2;
    else if (str_eq(argv[1], "m3"))
        mode = MODE_M3;
    else if (str_eq(argv[1], "serve"))
        mode = MODE_SERVE;
    else
        return 40;

    for (int i = 0; i < MAX_SURF; i++) {
        g_surf[i].in_use = 0;
        g_surf[i].conn_idx = -1;
        g_surf[i].shm_id = -1;
    }
    for (int i = 0; i < MAX_CONN; i++)
        g_conn[i].in_use = 0;

    g_evfd = open("/dev/tty0", TSK_O_WRONLY);

    lfd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (lfd < 0)
        return 41;
    addr_fill(&ua, WSD_SOCKET_PATH);
    if (bind(lfd, &ua, (long)sizeof(ua)) != 0) {
        close(lfd);
        return 42;
    }
    if (listen(lfd, MAX_CONN) != 0) {
        close(lfd);
        return 43;
    }

    if (mode == MODE_M2 || mode == MODE_SERVE) {
        int fr = fb_setup();
        if (fr == -2) {
            close(lfd);
            if (g_evfd >= 0)
                dprintf(g_evfd, "[guide15][srv] no framebuffer — SKIP\n");
            return 20;
        }
        if (fr == -1) {
            close(lfd);
            return 45;
        }
        fb_clear(0xFF101317u);
        if (g_evfd >= 0)
            dprintf(g_evfd, "[guide15][srv] fb %ux%u pitch_px=%u mode=%s\n",
                    g_screen_w, g_screen_h, g_pitch_px, argv[1]);
        if (mode == MODE_SERVE) {
            kbd_fd = open("/dev/keyboard", TSK_O_RDONLY);
            mouse_fd = open("/dev/mouse", TSK_O_RDONLY);
        }
    }

    serve_loop(mode, lfd, kbd_fd, mouse_fd);

    for (int i = 0; i < MAX_CONN; i++)
        if (g_conn[i].in_use)
            conn_teardown(i);

    rc = verdict(mode);

    if (mode == MODE_M2 || mode == MODE_SERVE)
        fb_teardown();
    if (kbd_fd >= 0)
        close(kbd_fd);
    if (mouse_fd >= 0)
        close(mouse_fd);
    close(lfd);

    if (g_evfd >= 0) {
        dprintf(g_evfd, "[guide15][srv] mode=%s verdict=%d created=%d verify=%d leak=%d\n",
                argv[1], rc, g_created, g_verify_ok, g_shm_leak);
        close(g_evfd);
    }
    return rc;
}
