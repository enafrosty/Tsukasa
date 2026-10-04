/*
 * Project Tsukasa — Service Registry Integration Test Suite
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

#include "service.h"
#include "registryd.h"
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

#define TEST_VREG_SOCK "/tmp/vreg_test.sock"

/* External daemon runner from registryd.c */
int registryd_run(const char *sock_path, volatile int *stop_flag);

typedef struct {
    const char *sock_path;
    volatile int stop_flag;
} server_thread_arg_t;

static void *server_thread_func(void *arg)
{
    server_thread_arg_t *s = (server_thread_arg_t *)arg;
    registryd_run(s->sock_path, &s->stop_flag);
    return NULL;
}

int main(void)
{
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[TEST] vreg.init FAIL: WSAStartup failed\n");
        return 1;
    }
    _putenv("VREG_SOCKET_PATH=" TEST_VREG_SOCK);
#else
    setenv("VREG_SOCKET_PATH", TEST_VREG_SOCK, 1);
#endif

    unlink_file(TEST_VREG_SOCK);

    int total_tests = 0;
    int passed_tests = 0;

    printf("=== Running Service Registry Tests ===\n");

    /* 1. Connection refused when daemon is down */
    total_tests++;
    char buf[VREG_PATH_MAX];
    int rc = service_connect("clipboard", buf, sizeof(buf));
    if (rc == -ECONNREFUSED) {
        TSK_TEST_PASS("vreg", "connect_refused_when_down");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "connect_refused_when_down", "expected -ECONNREFUSED");
    }

    /* 2. Start registry daemon in background thread */
    server_thread_arg_t sarg;
    sarg.sock_path = TEST_VREG_SOCK;
    sarg.stop_flag = 0;

    pthread_t th;
    if (pthread_create(&th, NULL, server_thread_func, &sarg) != 0) {
        printf("[TEST] vreg.server_thread FAIL: failed to create server thread\n");
        return 1;
    }

    /* Wait briefly for server socket to become ready */
    for (int retry = 0; retry < 50; retry++) {
        rc = service_connect("ping", buf, sizeof(buf));
        if (rc == -ENOENT)
            break;
        sleep_ms(10);
    }

    /* 3. Parameter validation */
    total_tests++;
    int pval_ok = 1;
    if (service_register(NULL, "/tmp/test.sock") != -EINVAL) pval_ok = 0;
    if (service_register("", "/tmp/test.sock") != -EINVAL) pval_ok = 0;
    if (service_register("name", NULL) != -EINVAL) pval_ok = 0;
    if (service_register("name", "") != -EINVAL) pval_ok = 0;
    if (service_connect(NULL, buf, sizeof(buf)) != -EINVAL) pval_ok = 0;
    if (service_connect("", buf, sizeof(buf)) != -EINVAL) pval_ok = 0;
    if (service_connect("name", NULL, sizeof(buf)) != -EINVAL) pval_ok = 0;
    if (service_connect("name", buf, 0) != -EINVAL) pval_ok = 0;
    if (service_unregister(NULL) != -EINVAL) pval_ok = 0;
    if (service_unregister("") != -EINVAL) pval_ok = 0;

    char long_name[VREG_NAME_MAX + 10];
    memset(long_name, 'a', sizeof(long_name));
    long_name[sizeof(long_name) - 1] = '\0';
    if (service_register(long_name, "/tmp/test.sock") != -EINVAL) pval_ok = 0;
    if (service_connect(long_name, buf, sizeof(buf)) != -EINVAL) pval_ok = 0;
    if (service_unregister(long_name) != -EINVAL) pval_ok = 0;

    if (pval_ok) {
        TSK_TEST_PASS("vreg", "param_validation");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "param_validation", "one or more checks failed");
    }

    /* 4. Lookup non-existent service returns -ENOENT */
    total_tests++;
    rc = service_connect("nonexistent", buf, sizeof(buf));
    if (rc == -ENOENT) {
        TSK_TEST_PASS("vreg", "lookup_nonexistent");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "lookup_nonexistent", "expected -ENOENT");
    }

    /* 5. Register single service */
    total_tests++;
    rc = service_register("clipboard", "/tmp/clipboard.sock");
    if (rc == 0) {
        TSK_TEST_PASS("vreg", "register_single");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "register_single", "service_register failed");
    }

    /* 6. Lookup registered service */
    total_tests++;
    memset(buf, 0, sizeof(buf));
    rc = service_connect("clipboard", buf, sizeof(buf));
    if (rc == 0 && strcmp(buf, "/tmp/clipboard.sock") == 0) {
        TSK_TEST_PASS("vreg", "lookup_registered");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "lookup_registered", "path mismatch or connect error");
    }

    /* 7. Register multiple services */
    total_tests++;
    int m_ok = 1;
    if (service_register("notify", "/tmp/notify.sock") != 0) m_ok = 0;
    if (service_register("shell", "/tmp/shell.sock") != 0) m_ok = 0;

    if (service_connect("clipboard", buf, sizeof(buf)) != 0 || strcmp(buf, "/tmp/clipboard.sock") != 0) m_ok = 0;
    if (service_connect("notify", buf, sizeof(buf)) != 0 || strcmp(buf, "/tmp/notify.sock") != 0) m_ok = 0;
    if (service_connect("shell", buf, sizeof(buf)) != 0 || strcmp(buf, "/tmp/shell.sock") != 0) m_ok = 0;

    if (m_ok) {
        TSK_TEST_PASS("vreg", "register_multiple");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "register_multiple", "multiple registration check failed");
    }

    /* 8. Re-register overwrite */
    total_tests++;
    rc = service_register("clipboard", "/tmp/clipboard_v2.sock");
    if (rc == 0) {
        memset(buf, 0, sizeof(buf));
        rc = service_connect("clipboard", buf, sizeof(buf));
        if (rc == 0 && strcmp(buf, "/tmp/clipboard_v2.sock") == 0) {
            TSK_TEST_PASS("vreg", "re_register_overwrite");
            passed_tests++;
        } else {
            TSK_TEST_FAIL("vreg", "re_register_overwrite", "lookup after overwrite mismatch");
        }
    } else {
        TSK_TEST_FAIL("vreg", "re_register_overwrite", "re-register failed");
    }

    /* 9. Path buffer too small boundary check */
    total_tests++;
    char small_buf[5];
    rc = service_connect("clipboard", small_buf, sizeof(small_buf));
    if (rc == -ENAMETOOLONG) {
        TSK_TEST_PASS("vreg", "path_buffer_too_small");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "path_buffer_too_small", "expected -ENAMETOOLONG");
    }

    /* 10. Unregister existing and already-unregistered service */
    total_tests++;
    int u_ok = 1;
    if (service_unregister("clipboard") != 0) u_ok = 0;
    if (service_connect("clipboard", buf, sizeof(buf)) != -ENOENT) u_ok = 0;
    if (service_unregister("clipboard") != -ENOENT) u_ok = 0;
    if (service_connect("notify", buf, sizeof(buf)) != 0 || strcmp(buf, "/tmp/notify.sock") != 0) u_ok = 0;

    if (u_ok) {
        TSK_TEST_PASS("vreg", "unregister_service");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "unregister_service", "unregister check failed");
    }

    /* 11. Capacity limit check (VREG_MAX_SERVICES = 32) */
    total_tests++;
    int cap_ok = 1;
    char name_tmp[32];
    char path_tmp[64];
    /* We currently have 2 entries in use: notify and shell.
     * Fill 30 more to reach 32 entries. */
    for (int i = 0; i < 30; i++) {
        snprintf(name_tmp, sizeof(name_tmp), "svc_%d", i);
        snprintf(path_tmp, sizeof(path_tmp), "/tmp/svc_%d.sock", i);
        if (service_register(name_tmp, path_tmp) != 0) {
            cap_ok = 0;
            break;
        }
    }
    /* 33rd registration must fail with -ENOSPC */
    if (service_register("overflow_svc", "/tmp/overflow.sock") != -ENOSPC)
        cap_ok = 0;

    if (cap_ok) {
        TSK_TEST_PASS("vreg", "capacity_overflow_enospc");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vreg", "capacity_overflow_enospc", "expected -ENOSPC on table overflow");
    }

    /* Clean shutdown: signal server and wake up accept */
    sarg.stop_flag = 1;
    int dummy_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (dummy_fd >= 0) {
        struct sockaddr_un daddr;
        memset(&daddr, 0, sizeof(daddr));
        daddr.sun_family = AF_UNIX;
        strncpy(daddr.sun_path, TEST_VREG_SOCK, sizeof(daddr.sun_path) - 1);
        (void)connect(dummy_fd, (struct sockaddr *)&daddr, sizeof(daddr));
        close_socket(dummy_fd);
    }

    pthread_join(th, NULL);
    unlink_file(TEST_VREG_SOCK);

#if defined(_WIN32)
    WSACleanup();
#endif

    TSK_TEST_DONE("vreg", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
