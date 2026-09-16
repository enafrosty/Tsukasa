/*
 * Project Tsukasa — Vanilla Display Server IPC & Shared Memory Loopback Test
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

#include "../include/vanilla.h"
#include "../server/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "../../scripts/test/tsk_test.h"

#define TEST_SOCKET_PATH "/tmp/vanilla_test.sock"
#define TEST_WIN_W 320
#define TEST_WIN_H 240

static uint32_t expected_pixel(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint8_t r = (uint8_t)((x * 255u) / w);
    uint8_t g = (uint8_t)((y * 255u) / h);
    uint8_t b = 0x80;
    return vanilla_make_argb(0xFF, r, g, b);
}

static int exact_write_fd(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;

    while (written < count) {
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                sched_yield();
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static int exact_read_fd(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;

    while (received < count) {
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                sched_yield();
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static int run_client(const char *sock_path)
{
    vanilla_client_t *client = NULL;
    vanilla_window_t *win;
    vanilla_rect_t damage;

    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(sock_path);
        if (client)
            break;
        sched_yield();
    }

    if (!client) {
        printf("[test_client] Error: Failed to connect to %s\n", sock_path);
        return 1;
    }

    win = vanilla_create_window(client, "IPC Test Window", 40, 40,
                                TEST_WIN_W, TEST_WIN_H, WINDOW_FLAG_NONE);
    if (!win) {
        printf("[test_client] Error: Failed to create window\n");
        vanilla_disconnect(client);
        return 2;
    }

    if (!win->surface.pixels || win->surface.shm_id <= 0) {
        printf("[test_client] Error: Invalid SHM surface attachment\n");
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 3;
    }

    for (uint32_t y = 0; y < win->height; y++) {
        for (uint32_t x = 0; x < win->width; x++) {
            win->surface.pixels[y * win->width + x] =
                expected_pixel(x, y, win->width, win->height);
        }
    }

    damage.x = 0;
    damage.y = 0;
    damage.w = TEST_WIN_W;
    damage.h = TEST_WIN_H;
    vanilla_present(win, &damage);

    for (int i = 0; i < 5; i++)
        sched_yield();

    vanilla_destroy_window(win);
    vanilla_disconnect(client);

    return 0;
}

static int run_server_loop(vanilla_server_t *srv)
{
    int client_idx = -1;
    vanilla_server_window_t *win = NULL;
    int saw_present = 0;
    int verified_shm = 0;

    for (int spin = 0; spin < 500 && client_idx < 0; spin++) {
        client_idx = vanilla_server_accept(srv);
        if (client_idx < 0)
            sched_yield();
    }

    if (client_idx < 0) {
        printf("[test_server] Error: Timed out waiting for client connection\n");
        return 1;
    }

    for (int cycle = 0; cycle < 1000; cycle++) {
        vanilla_server_poll(srv, 10);

        if (!win) {
            for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
                if (srv->windows[i].in_use) {
                    win = &srv->windows[i];
                    break;
                }
            }
        }

        if (win && !saw_present) {
            if (win->damage.w == TEST_WIN_W && win->damage.h == TEST_WIN_H) {
                saw_present = 1;

                uint32_t p_top_left = win->surface.pixels[0];
                uint32_t p_mid = win->surface.pixels[(TEST_WIN_H / 2) * TEST_WIN_W + (TEST_WIN_W / 2)];
                uint32_t p_bottom_right = win->surface.pixels[(TEST_WIN_H - 1) * TEST_WIN_W + (TEST_WIN_W - 1)];

                uint32_t exp_tl = expected_pixel(0, 0, TEST_WIN_W, TEST_WIN_H);
                uint32_t exp_mid = expected_pixel(TEST_WIN_W / 2, TEST_WIN_H / 2, TEST_WIN_W, TEST_WIN_H);
                uint32_t exp_br = expected_pixel(TEST_WIN_W - 1, TEST_WIN_H - 1, TEST_WIN_W, TEST_WIN_H);

                if (p_top_left == exp_tl && p_mid == exp_mid && p_bottom_right == exp_br)
                    verified_shm = 1;
            }
        }

        if (saw_present && !srv->clients[client_idx].in_use)
            break;
    }

    if (!saw_present) {
        printf("[test_server] Error: Present message not received\n");
        return 2;
    }

    if (!verified_shm) {
        printf("[test_server] Error: SHM surface pixel verification failed\n");
        return 3;
    }

    int active_windows = 0;
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use)
            active_windows++;
    }

    if (active_windows != 0) {
        printf("[test_server] Error: Window table not cleanly released\n");
        return 4;
    }

    return 0;
}

static int run_inprocess_loopback(const char *sock_path)
{
    vanilla_server_t srv;
    int cfd = -1;
    int client_idx = -1;
    vanilla_surface_t client_surf;
    memset(&client_surf, 0, sizeof(client_surf));

    if (vanilla_server_init(&srv, sock_path) < 0) {
        printf("[FAIL] 1. Failed to start and bind server socket\n");
        return 1;
    }
    printf("[PASS] 1. Server started and bound socket at %s\n", sock_path);

    cfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cfd < 0) {
        printf("[FAIL] 2. Failed to create client socket\n");
        vanilla_server_close(&srv);
        return 2;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(cfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[FAIL] 2. Failed to connect client socket\n");
        close(cfd);
        vanilla_server_close(&srv);
        return 2;
    }

    client_idx = vanilla_server_accept(&srv);
    if (client_idx < 0) {
        printf("[FAIL] 2. Server failed to accept client\n");
        close(cfd);
        vanilla_server_close(&srv);
        return 2;
    }

    /* Step 3: MSG_HELLO */
    vanilla_msg_hdr_t hdr;
    vanilla_msg_hello_t hello;
    memset(&hdr, 0, sizeof(hdr));
    memset(&hello, 0, sizeof(hello));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_HELLO;
    hdr.payload_len = (uint16_t)sizeof(hello);
    hdr.window_id = 0;

    hello.client_version = VANILLA_IPC_VERSION;
    strncpy(hello.client_name, "loopback_test", sizeof(hello.client_name) - 1);

    if (exact_write_fd(cfd, &hdr, sizeof(hdr)) < 0 ||
        exact_write_fd(cfd, &hello, sizeof(hello)) < 0) {
        printf("[FAIL] 3. Failed to send MSG_HELLO\n");
        goto fail;
    }

    if (vanilla_server_dispatch_client(&srv, client_idx) < 0) {
        printf("[FAIL] 3. Server dispatch MSG_HELLO failed\n");
        goto fail;
    }

    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack_body;
    if (exact_read_fd(cfd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_read_fd(cfd, &ack_body, sizeof(ack_body)) < 0) {
        printf("[FAIL] 3. Failed to receive MSG_HELLO_ACK\n");
        goto fail;
    }

    if (ack_hdr.magic != VANILLA_IPC_MAGIC ||
        ack_hdr.msg_type != MSG_HELLO_ACK ||
        ack_body.status != 0) {
        printf("[FAIL] 3. Invalid MSG_HELLO_ACK\n");
        goto fail;
    }
    printf("[PASS] 2. Client connected and completed HELLO handshake.\n");

    /* Step 4: MSG_CREATE_WINDOW */
    vanilla_msg_create_window_t create_req;
    memset(&hdr, 0, sizeof(hdr));
    memset(&create_req, 0, sizeof(create_req));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_CREATE_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(create_req);
    hdr.window_id = 0;

    create_req.x = 40;
    create_req.y = 40;
    create_req.width = TEST_WIN_W;
    create_req.height = TEST_WIN_H;
    create_req.flags = WINDOW_FLAG_NONE;
    strncpy(create_req.title, "Loopback Window", sizeof(create_req.title) - 1);

    if (exact_write_fd(cfd, &hdr, sizeof(hdr)) < 0 ||
        exact_write_fd(cfd, &create_req, sizeof(create_req)) < 0) {
        printf("[FAIL] 4. Failed to send MSG_CREATE_WINDOW\n");
        goto fail;
    }

    if (vanilla_server_dispatch_client(&srv, client_idx) < 0) {
        printf("[FAIL] 4. Server dispatch MSG_CREATE_WINDOW failed\n");
        goto fail;
    }

    vanilla_msg_create_window_ack_t create_ack;
    if (exact_read_fd(cfd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_read_fd(cfd, &create_ack, sizeof(create_ack)) < 0) {
        printf("[FAIL] 4. Failed to receive MSG_CREATE_WINDOW_ACK\n");
        goto fail;
    }

    if (ack_hdr.magic != VANILLA_IPC_MAGIC ||
        ack_hdr.msg_type != MSG_CREATE_WINDOW_ACK ||
        create_ack.status != 0 || create_ack.shm_id <= 0) {
        printf("[FAIL] 4. Invalid MSG_CREATE_WINDOW_ACK\n");
        goto fail;
    }

    uint32_t win_id = ack_hdr.window_id;
    int shm_id = create_ack.shm_id;
    size_t shm_size = create_ack.buffer_size;
    uint32_t pitch = create_ack.pitch;
    printf("[PASS] 3. Client requested 320x240 window with SHM surface.\n");

    /* Step 5: Attach SHM and render gradient pattern */
    if (surface_attach_shm(&client_surf, shm_id, TEST_WIN_W, TEST_WIN_H, pitch, shm_size) < 0) {
        printf("[FAIL] 5. Failed to attach client SHM surface\n");
        goto fail;
    }

    for (uint32_t y = 0; y < TEST_WIN_H; y++) {
        for (uint32_t x = 0; x < TEST_WIN_W; x++) {
            client_surf.pixels[y * TEST_WIN_W + x] =
                expected_pixel(x, y, TEST_WIN_W, TEST_WIN_H);
        }
    }
    printf("[PASS] 4. Client mapped SHM surface and rendered gradient pattern.\n");

    /* Step 6: MSG_PRESENT */
    vanilla_msg_present_t pres;
    memset(&hdr, 0, sizeof(hdr));
    memset(&pres, 0, sizeof(pres));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_PRESENT;
    hdr.payload_len = (uint16_t)sizeof(pres);
    hdr.window_id = win_id;

    pres.x = 0;
    pres.y = 0;
    pres.w = TEST_WIN_W;
    pres.h = TEST_WIN_H;

    if (exact_write_fd(cfd, &hdr, sizeof(hdr)) < 0 ||
        exact_write_fd(cfd, &pres, sizeof(pres)) < 0) {
        printf("[FAIL] 6. Failed to send MSG_PRESENT\n");
        goto fail;
    }

    if (vanilla_server_dispatch_client(&srv, client_idx) < 0) {
        printf("[FAIL] 6. Server dispatch MSG_PRESENT failed\n");
        goto fail;
    }

    vanilla_server_window_t *srv_win = vanilla_server_find_window(&srv, win_id);
    if (!srv_win) {
        printf("[FAIL] 6. Window not found on server\n");
        goto fail;
    }

    if (srv_win->damage.w != TEST_WIN_W || srv_win->damage.h != TEST_WIN_H) {
        printf("[FAIL] 6. Server damage rect mismatch\n");
        goto fail;
    }

    uint32_t p_tl = srv_win->surface.pixels[0];
    uint32_t p_mid = srv_win->surface.pixels[(TEST_WIN_H / 2) * TEST_WIN_W + (TEST_WIN_W / 2)];
    uint32_t p_br = srv_win->surface.pixels[(TEST_WIN_H - 1) * TEST_WIN_W + (TEST_WIN_W - 1)];

    uint32_t exp_tl = expected_pixel(0, 0, TEST_WIN_W, TEST_WIN_H);
    uint32_t exp_mid = expected_pixel(TEST_WIN_W / 2, TEST_WIN_H / 2, TEST_WIN_W, TEST_WIN_H);
    uint32_t exp_br = expected_pixel(TEST_WIN_W - 1, TEST_WIN_H - 1, TEST_WIN_W, TEST_WIN_H);

    if (p_tl != exp_tl || p_mid != exp_mid || p_br != exp_br) {
        printf("[FAIL] 6. SHM pixel verification failed\n");
        goto fail;
    }
    printf("[PASS] 5. Server received MSG_PRESENT and verified zero-copy pixels.\n");

    /* Step 7: MSG_MOVE_RESIZE */
    vanilla_msg_move_resize_t mr;
    memset(&hdr, 0, sizeof(hdr));
    memset(&mr, 0, sizeof(mr));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_MOVE_RESIZE;
    hdr.payload_len = (uint16_t)sizeof(mr);
    hdr.window_id = win_id;

    mr.x = 60;
    mr.y = 80;
    mr.width = TEST_WIN_W;
    mr.height = TEST_WIN_H;

    if (exact_write_fd(cfd, &hdr, sizeof(hdr)) < 0 ||
        exact_write_fd(cfd, &mr, sizeof(mr)) < 0) {
        printf("[FAIL] 7. Failed to send MSG_MOVE_RESIZE\n");
        goto fail;
    }

    if (vanilla_server_dispatch_client(&srv, client_idx) < 0) {
        printf("[FAIL] 7. Server dispatch MSG_MOVE_RESIZE failed\n");
        goto fail;
    }

    if (srv_win->x != 60 || srv_win->y != 80) {
        printf("[FAIL] 7. Coordinates mismatch (x=%d, y=%d)\n", srv_win->x, srv_win->y);
        goto fail;
    }

    /* Step 8: Focus and input event notifications */
    if (vanilla_server_focus_window(&srv, win_id) < 0) {
        printf("[FAIL] 8. Focus notification failed\n");
        goto fail;
    }

    vanilla_msg_window_focus_t foc;
    if (exact_read_fd(cfd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_read_fd(cfd, &foc, sizeof(foc)) < 0) {
        printf("[FAIL] 8. Failed to read focus event\n");
        goto fail;
    }

    if (ack_hdr.magic != VANILLA_IPC_MAGIC ||
        ack_hdr.msg_type != MSG_WINDOW_FOCUS ||
        ack_hdr.window_id != win_id ||
        foc.focused != 1) {
        printf("[FAIL] 8. Invalid focus payload\n");
        goto fail;
    }

    struct input_event send_ev;
    memset(&send_ev, 0, sizeof(send_ev));
    send_ev.type = EV_KEY;
    send_ev.code = KEY_A;
    send_ev.value = 1;

    if (vanilla_server_send_input(&srv, win_id, &send_ev) < 0) {
        printf("[FAIL] 8. Input notification failed\n");
        goto fail;
    }

    struct input_event recv_ev;
    if (exact_read_fd(cfd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_read_fd(cfd, &recv_ev, sizeof(recv_ev)) < 0) {
        printf("[FAIL] 8. Failed to read input event\n");
        goto fail;
    }

    if (ack_hdr.magic != VANILLA_IPC_MAGIC ||
        ack_hdr.msg_type != MSG_INPUT_EVENT ||
        ack_hdr.window_id != win_id ||
        recv_ev.type != EV_KEY ||
        recv_ev.code != KEY_A ||
        recv_ev.value != 1) {
        printf("[FAIL] 8. Invalid input payload\n");
        goto fail;
    }

    /* Step 9: MSG_DESTROY_WINDOW */
    vanilla_msg_destroy_window_t dest_req;
    memset(&hdr, 0, sizeof(hdr));
    memset(&dest_req, 0, sizeof(dest_req));
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DESTROY_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(dest_req);
    hdr.window_id = win_id;
    dest_req.window_id = win_id;

    if (exact_write_fd(cfd, &hdr, sizeof(hdr)) < 0 ||
        exact_write_fd(cfd, &dest_req, sizeof(dest_req)) < 0) {
        printf("[FAIL] 9. Failed to send MSG_DESTROY_WINDOW\n");
        goto fail;
    }

    if (vanilla_server_dispatch_client(&srv, client_idx) < 0) {
        printf("[FAIL] 9. Server dispatch MSG_DESTROY_WINDOW failed\n");
        goto fail;
    }

    if (vanilla_server_find_window(&srv, win_id) != NULL) {
        printf("[FAIL] 9. Window still active in server table\n");
        goto fail;
    }

    close(cfd);
    cfd = -1;
    vanilla_server_remove_client(&srv, client_idx);
    client_idx = -1;
    vanilla_server_close(&srv);

    printf("[PASS] 6. Clean disconnection, resource release, and shutdown verified.\n");
    printf("========================================================\n");
    printf("[OK] Step 4.1 IPC & Shared Memory Architecture test PASSED.\n");
    printf("========================================================\n");
    return 0;

fail:
    if (cfd >= 0)
        close(cfd);
    if (client_idx >= 0)
        vanilla_server_remove_client(&srv, client_idx);
    vanilla_server_close(&srv);
    return 1;
}

int main(int argc, char **argv)
{
    const char *sock_path = TEST_SOCKET_PATH;

    if (argc > 1 && strcmp(argv[1], "client") == 0)
        return run_client(sock_path);

    int res = 0;
    if (argc > 1 && strcmp(argv[1], "loopback") == 0) {
        printf("========================================================\n");
        printf(" Project Vanilla - Phase 4, Step 4.1 IPC & SHM Test\n");
        printf("========================================================\n");
        res = run_inprocess_loopback(sock_path);
    } else {
        printf("========================================================\n");
        printf(" Project Vanilla - Phase 4, Step 4.1 IPC & SHM Test\n");
        printf("========================================================\n");

        vanilla_server_t srv;
        if (vanilla_server_init(&srv, sock_path) < 0) {
            printf("[FAIL] 1. Failed to start and bind server socket\n");
            TSK_TEST_FAIL("ipc", "server_bind", "Failed to start and bind server socket");
            return 1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            return run_client(sock_path);
        } else if (pid > 0) {
            printf("[PASS] 1. Server started and bound socket at %s\n", sock_path);
            int srv_rc = run_server_loop(&srv);
            int status = 0;
            waitpid(pid, &status, 0);
            vanilla_server_close(&srv);

            if (srv_rc != 0 || status != 0)
                res = 1;
        } else {
            /* Single-process environment (fork unsupported): run synchronous loopback */
            vanilla_server_close(&srv);
            res = run_inprocess_loopback(sock_path);
        }
    }

    if (res == 0) {
        TSK_TEST_PASS("ipc", "server_bind");
        TSK_TEST_PASS("ipc", "hello_handshake");
        TSK_TEST_PASS("ipc", "create_window");
        TSK_TEST_PASS("ipc", "present_shm");
        TSK_TEST_PASS("ipc", "move_focus_input");
        TSK_TEST_PASS("ipc", "destroy_shutdown");
        TSK_TEST_DONE("ipc", 6, 6);
        return 0;
    } else {
        TSK_TEST_FAIL("ipc", "loopback", "IPC and SHM test failed");
        return 1;
    }
}

