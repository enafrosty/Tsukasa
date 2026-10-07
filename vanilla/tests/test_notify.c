/*
 * Project Tsukasa — Notification Service Integration Test Suite
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

#include "vanilla.h"
#include "notify.h"
#include "notifyd.h"
#include "tsk_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <afunix.h>
#define close_socket(s) closesocket(s)
#define unlink_file(p) remove(p)
#define sleep_ms(ms) Sleep(ms)
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#define close_socket(s) close(s)
#define unlink_file(p) unlink(p)
#define sleep_ms(ms) do { struct timespec ts = { 0, (ms) * 1000000 }; nanosleep(&ts, NULL); } while (0)
#endif

#define TEST_VNOTIF_SOCK "/tmp/vnotif_test.sock"

/* Headless Vanilla display server stubs for host test */
vanilla_client_t *vanilla_connect(const char *path) { (void)path; return NULL; }
void vanilla_disconnect(vanilla_client_t *client) { (void)client; }
vanilla_window_t *vanilla_create_window(vanilla_client_t *c, const char *t, int x, int y, int w, int h, uint32_t f)
{
    (void)c; (void)t; (void)x; (void)y; (void)w; (void)h; (void)f;
    return NULL;
}
void vanilla_destroy_window(vanilla_window_t *w) { (void)w; }
int vanilla_map_window(vanilla_window_t *w) { (void)w; return 0; }
int vanilla_unmap_window(vanilla_window_t *w) { (void)w; return 0; }
int vanilla_move_window(vanilla_window_t *w, int32_t x, int32_t y) { (void)w; (void)x; (void)y; return 0; }
void vanilla_present(vanilla_window_t *w, const vanilla_rect_t *d) { (void)w; (void)d; }
int vanilla_poll_event(vanilla_client_t *c, vanilla_event_t *ev) { (void)c; (void)ev; return 0; }

typedef struct {
    const char *sock_path;
    volatile int stop_flag;
} server_thread_arg_t;

static void *server_thread_func(void *arg)
{
    server_thread_arg_t *s = (server_thread_arg_t *)arg;
    notifyd_run(s->sock_path, &s->stop_flag);
    return NULL;
}

static int raw_client_rpc(const void *req, size_t req_len, vnotif_resp_t *resp)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TEST_VNOTIF_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close_socket(fd);
        return -1;
    }

#if defined(_WIN32)
    if (send(fd, (const char *)req, (int)req_len, 0) != (int)req_len) {
        close_socket(fd);
        return -1;
    }
    int r = recv(fd, (char *)resp, (int)sizeof(*resp), 0);
#else
    if (write(fd, req, req_len) != (ssize_t)req_len) {
        close_socket(fd);
        return -1;
    }
    ssize_t r = read(fd, resp, sizeof(*resp));
#endif
    close_socket(fd);

    if (r != (int)sizeof(*resp))
        return -1;
    return 0;
}

int main(void)
{
    printf("=== Running Notification Daemon Tests ===\n");
    int passed = 0;
    int total = 12;

#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    _putenv("VNOTIF_SOCKET_PATH=" TEST_VNOTIF_SOCK);
    _putenv("VNOTIF_NO_REGISTER=1");
    _putenv("VNOTIF_HEADLESS=1");
#else
    setenv("VNOTIF_SOCKET_PATH", TEST_VNOTIF_SOCK, 1);
    setenv("VNOTIF_NO_REGISTER", "1", 1);
    setenv("VNOTIF_HEADLESS", "1", 1);
#endif

    unlink_file(TEST_VNOTIF_SOCK);

    /* Test 1: Connect refused before daemon starts */
    {
        int32_t rc = notify_send("Test", "Body", "info", 1000, NULL, 0);
        if (rc < 0) {
            TSK_TEST_PASS("vnotif", "connect_refused_when_down");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "connect_refused_when_down", "expected error");
        }
    }

    /* Start daemon background thread */
    server_thread_arg_t srv_arg;
    srv_arg.sock_path = TEST_VNOTIF_SOCK;
    srv_arg.stop_flag = 0;

    pthread_t th;
    pthread_create(&th, NULL, server_thread_func, &srv_arg);

    for (int i = 0; i < 50; i++) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, TEST_VNOTIF_SOCK, sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                close_socket(fd);
                break;
            }
            close_socket(fd);
        }
        sleep_ms(20);
    }

    /* Test 2: Parameter validation */
    {
        uint32_t act = 0;
        vnotif_action_t a[5];
        memset(a, 0, sizeof(a));

        int ok = 1;
        if (notify_send(NULL, "Body", "info", 1000, NULL, 0) != -EINVAL) ok = 0;
        if (notify_send("Title", "Body", "info", 1000, NULL, 2) != -EINVAL) ok = 0;
        if (notify_send("Title", "Body", "info", 1000, a, 5) != -EINVAL) ok = 0;
        if (notify_close(0) != -EINVAL) ok = 0;
        if (notify_wait_action(0, &act) != -EINVAL) ok = 0;
        if (notify_wait_action(1, NULL) != -EINVAL) ok = 0;
        if (notify_wait_action(999999, &act) != -ENOENT) ok = 0;

        if (ok) {
            TSK_TEST_PASS("vnotif", "param_validation");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "param_validation", "validation check failed");
        }
    }

    /* Test 3: Basic send and explicit close */
    {
        int32_t id = notify_send("Greeting", "Hello world", "info", 0, NULL, 0);
        if (id > 0) {
            int rc = notify_close((uint32_t)id);
            int rc2 = notify_close((uint32_t)id);
            if (rc == 0 && rc2 == -ENOENT) {
                TSK_TEST_PASS("vnotif", "send_and_close_basic");
                passed++;
            } else {
                TSK_TEST_FAIL("vnotif", "send_and_close_basic", "close return code error");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "send_and_close_basic", "notify_send failed");
        }
    }

    /* Test 4: Auto-dismiss timeout */
    {
        int32_t id = notify_send("Ephemeral", "Gone soon", "info", 150, NULL, 0);
        if (id > 0) {
            sleep_ms(300);
            int rc = notify_close((uint32_t)id);
            if (rc == -ENOENT) {
                TSK_TEST_PASS("vnotif", "auto_dismiss_timeout");
                passed++;
            } else {
                TSK_TEST_FAIL("vnotif", "auto_dismiss_timeout", "notification did not auto-dismiss");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "auto_dismiss_timeout", "notify_send failed");
        }
    }

    /* Test 5: Queueing 4th notification behind top 3 */
    {
        int32_t id1 = notify_send("N1", "First", "info", 10000, NULL, 0);
        int32_t id2 = notify_send("N2", "Second", "info", 10000, NULL, 0);
        int32_t id3 = notify_send("N3", "Third", "info", 10000, NULL, 0);
        int32_t id4 = notify_send("N4", "Fourth", "info", 10000, NULL, 0);

        int act = 0, vis = 0;
        notifyd_test_get_counts(&act, &vis);

        if (id1 > 0 && id2 > 0 && id3 > 0 && id4 > 0 && act == 4 && vis == 3) {
            /* Dismiss id1; id4 should be promoted to visible */
            notify_close((uint32_t)id1);
            act = 0; vis = 0;
            notifyd_test_get_counts(&act, &vis);

            if (act == 3 && vis == 3) {
                TSK_TEST_PASS("vnotif", "queueing_fourth_toast");
                passed++;
            } else {
                TSK_TEST_FAIL("vnotif", "queueing_fourth_toast", "promotion count mismatch");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "queueing_fourth_toast", "initial queue counts mismatch");
        }

        notify_close((uint32_t)id2);
        notify_close((uint32_t)id3);
        notify_close((uint32_t)id4);
    }

    /* Test 6: Action event delivery */
    {
        vnotif_action_t actions[2];
        memset(actions, 0, sizeof(actions));
        strncpy(actions[0].label, "Accept", sizeof(actions[0].label) - 1);
        actions[0].action_id = 42;
        strncpy(actions[1].label, "Decline", sizeof(actions[1].label) - 1);
        actions[1].action_id = 99;

        int32_t id = notify_send("Dialog", "Confirm action?", "warning", 0, actions, 2);
        if (id > 0) {
            notifyd_test_trigger_action((uint32_t)id, 42);
            uint32_t chosen = 0;
            int rc = notify_wait_action((uint32_t)id, &chosen);
            if (rc == 0 && chosen == 42) {
                TSK_TEST_PASS("vnotif", "action_event_delivery");
                passed++;
            } else {
                TSK_TEST_FAIL("vnotif", "action_event_delivery", "action id mismatch");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "action_event_delivery", "notify_send failed");
        }
    }

    /* Test 7: Action notification dismiss event (action_id == 0 on timeout/dismiss) */
    {
        vnotif_action_t actions[1];
        memset(actions, 0, sizeof(actions));
        strncpy(actions[0].label, "OK", sizeof(actions[0].label) - 1);
        actions[0].action_id = 1;

        int32_t id = notify_send("Dismissible", "Dismiss test", "info", 150, actions, 1);
        if (id > 0) {
            uint32_t chosen = 999;
            int rc = notify_wait_action((uint32_t)id, &chosen);
            if (rc == 0 && chosen == 0) {
                TSK_TEST_PASS("vnotif", "action_dismiss_delivery");
                passed++;
            } else {
                TSK_TEST_FAIL("vnotif", "action_dismiss_delivery", "dismiss action id mismatch");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "action_dismiss_delivery", "notify_send failed");
        }
    }

    /* Test 8: Queue capacity limit (8 max) */
    {
        uint32_t ids[8];
        int ok = 1;
        for (int i = 0; i < 8; i++) {
            ids[i] = (uint32_t)notify_send("Stress", "Fill queue", "info", 10000, NULL, 0);
            if (ids[i] == 0) ok = 0;
        }

        int32_t overflow = notify_send("Overflow", "Should fail", "info", 10000, NULL, 0);
        if (ok && overflow == -ENOSPC) {
            notify_close(ids[0]);
            int32_t retry = notify_send("Retry", "Should fit", "info", 10000, NULL, 0);
            if (retry > 0) {
                TSK_TEST_PASS("vnotif", "capacity_limit");
                passed++;
                notify_close((uint32_t)retry);
            } else {
                TSK_TEST_FAIL("vnotif", "capacity_limit", "retry after free failed");
            }
        } else {
            TSK_TEST_FAIL("vnotif", "capacity_limit", "did not return ENOSPC on overflow");
        }

        for (int i = 1; i < 8; i++)
            notify_close(ids[i]);
    }

    /* Test 9: Icon types support */
    {
        int32_t i1 = notify_send("Info", "Text", "info", 0, NULL, 0);
        int32_t i2 = notify_send("Warn", "Text", "warning", 0, NULL, 0);
        int32_t i3 = notify_send("Err", "Text", "error", 0, NULL, 0);
        int32_t i4 = notify_send("Succ", "Text", "success", 0, NULL, 0);

        if (i1 > 0 && i2 > 0 && i3 > 0 && i4 > 0) {
            TSK_TEST_PASS("vnotif", "icon_types_support");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "icon_types_support", "failed to post typed notifications");
        }

        notify_close((uint32_t)i1);
        notify_close((uint32_t)i2);
        notify_close((uint32_t)i3);
        notify_close((uint32_t)i4);
    }

    /* Test 10: Wire invalid magic */
    {
        vnotif_send_t bad_req;
        memset(&bad_req, 0, sizeof(bad_req));
        bad_req.magic = 0xDEADBEEF;
        bad_req.op = VNOTIF_OP_SEND;
        strncpy(bad_req.title, "Bad Magic", sizeof(bad_req.title) - 1);

        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        int r = raw_client_rpc(&bad_req, sizeof(bad_req), &resp);
        if (r == 0 && resp.status == -EINVAL) {
            TSK_TEST_PASS("vnotif", "wire_invalid_magic");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "wire_invalid_magic", "expected EINVAL");
        }
    }

    /* Test 11: Wire oversized actions */
    {
        vnotif_send_t bad_req;
        memset(&bad_req, 0, sizeof(bad_req));
        bad_req.magic = VNOTIF_MAGIC;
        bad_req.op = VNOTIF_OP_SEND;
        bad_req.action_count = 10;
        strncpy(bad_req.title, "Oversized Actions", sizeof(bad_req.title) - 1);

        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        int r = raw_client_rpc(&bad_req, sizeof(bad_req), &resp);
        if (r == 0 && resp.status == -EINVAL) {
            TSK_TEST_PASS("vnotif", "wire_oversized_actions");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "wire_oversized_actions", "expected EINVAL");
        }
    }

    /* Test 12: Wire invalid opcode */
    {
        vnotif_send_t bad_req;
        memset(&bad_req, 0, sizeof(bad_req));
        bad_req.magic = VNOTIF_MAGIC;
        bad_req.op = 99;

        vnotif_resp_t resp;
        memset(&resp, 0, sizeof(resp));
        int r = raw_client_rpc(&bad_req, sizeof(bad_req), &resp);
        if (r == 0 && resp.status == -EINVAL) {
            TSK_TEST_PASS("vnotif", "wire_invalid_opcode");
            passed++;
        } else {
            TSK_TEST_FAIL("vnotif", "wire_invalid_opcode", "expected EINVAL");
        }
    }

    /* Stop daemon thread */
    srv_arg.stop_flag = 1;
    pthread_join(th, NULL);
    unlink_file(TEST_VNOTIF_SOCK);

    TSK_TEST_DONE("vnotif", passed, total);
    return (passed == total) ? 0 : 1;
}
