/*
 * Project Tsukasa — Clipboard Service Integration Test Suite
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

#include "clipboard.h"
#include "clipboardd.h"
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

#define TEST_VCLIP_SOCK "/tmp/vclip_test.sock"

/* External daemon runner from clipboardd.c */
int clipboardd_run(const char *sock_path, volatile int *stop_flag);

typedef struct {
    const char *sock_path;
    volatile int stop_flag;
} server_thread_arg_t;

static void *server_thread_func(void *arg)
{
    server_thread_arg_t *s = (server_thread_arg_t *)arg;
    clipboardd_run(s->sock_path, &s->stop_flag);
    return NULL;
}

typedef struct {
    int thread_id;
    int success;
} worker_arg_t;

static void *worker_thread_func(void *arg)
{
    worker_arg_t *w = (worker_arg_t *)arg;
    w->success = 1;

    char msg[64];
    char read_buf[128];
    snprintf(msg, sizeof(msg), "worker_payload_id_%d", w->thread_id);
    size_t msg_len = strlen(msg);

    for (int i = 0; i < 20; i++) {
        if (clipboard_set(msg, msg_len) != 0) {
            w->success = 0;
            break;
        }
        int n = clipboard_get(read_buf, sizeof(read_buf));
        if (n < 0 || (size_t)n > VCLIP_TEXT_MAX) {
            w->success = 0;
            break;
        }
        read_buf[n] = '\0';
    }
    return NULL;
}

int main(void)
{
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("[TEST] vclip.init FAIL: WSAStartup failed\n");
        return 1;
    }
    _putenv("VCLIP_SOCKET_PATH=" TEST_VCLIP_SOCK);
    _putenv("VCLIP_NO_REGISTER=1");
#else
    setenv("VCLIP_SOCKET_PATH", TEST_VCLIP_SOCK, 1);
    setenv("VCLIP_NO_REGISTER", "1", 1);
#endif

    unlink_file(TEST_VCLIP_SOCK);

    int total_tests = 0;
    int passed_tests = 0;

    printf("=== Running Clipboard Tests ===\n");

    /* 1. Connection refused when daemon is down */
    total_tests++;
    char test_buf[256];
    int rc = clipboard_get(test_buf, sizeof(test_buf));
    if (rc == -ECONNREFUSED) {
        TSK_TEST_PASS("vclip", "connect_refused_when_down");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vclip", "connect_refused_when_down", "expected -ECONNREFUSED");
    }

    /* Start daemon thread */
    server_thread_arg_t sarg;
    sarg.sock_path = TEST_VCLIP_SOCK;
    sarg.stop_flag = 0;

    pthread_t th;
    if (pthread_create(&th, NULL, server_thread_func, &sarg) != 0) {
        printf("[TEST] vclip.start FAIL: could not create daemon thread\n");
        return 1;
    }

    /* Wait for daemon socket to become ready */
    for (int retry = 0; retry < 50; retry++) {
        sleep_ms(20);
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, TEST_VCLIP_SOCK, sizeof(addr.sun_path) - 1);
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                close_socket(fd);
                break;
            }
            close_socket(fd);
        }
    }

    /* 2. Initially empty clipboard */
    total_tests++;
    test_buf[0] = 'X';
    rc = clipboard_get(test_buf, sizeof(test_buf));
    if (rc == 0 && test_buf[0] == '\0') {
        TSK_TEST_PASS("vclip", "initially_empty");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vclip", "initially_empty", "expected 0 with empty string");
    }

    /* 3. Parameter validation */
    total_tests++;
    int pval_ok = 1;
    if (clipboard_set(NULL, 10) != -EINVAL)
        pval_ok = 0;
    if (clipboard_get(NULL, 10) != -EINVAL)
        pval_ok = 0;
    if (clipboard_get(test_buf, 0) != -EINVAL)
        pval_ok = 0;
    if (clipboard_set(test_buf, VCLIP_TEXT_MAX + 1) != -EMSGSIZE)
        pval_ok = 0;

    if (pval_ok) {
        TSK_TEST_PASS("vclip", "param_validation");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vclip", "param_validation", "invalid parameter not rejected");
    }

    /* 4. Basic Set and Get */
    total_tests++;
    const char *sample = "Hello, Project Tsukasa!";
    size_t sample_len = strlen(sample);
    rc = clipboard_set(sample, sample_len);
    if (rc == 0) {
        memset(test_buf, 0, sizeof(test_buf));
        int n = clipboard_get(test_buf, sizeof(test_buf));
        if (n == (int)sample_len && strcmp(test_buf, sample) == 0) {
            TSK_TEST_PASS("vclip", "set_and_get_basic");
            passed_tests++;
        } else {
            TSK_TEST_FAIL("vclip", "set_and_get_basic", "content or length mismatch");
        }
    } else {
        TSK_TEST_FAIL("vclip", "set_and_get_basic", "clipboard_set returned error");
    }

    /* 5. Overwrite existing clipboard */
    total_tests++;
    const char *sample2 = "Overwritten clipboard content";
    size_t sample2_len = strlen(sample2);
    rc = clipboard_set(sample2, sample2_len);
    if (rc == 0) {
        memset(test_buf, 0, sizeof(test_buf));
        int n = clipboard_get(test_buf, sizeof(test_buf));
        if (n == (int)sample2_len && strcmp(test_buf, sample2) == 0) {
            TSK_TEST_PASS("vclip", "overwrite");
            passed_tests++;
        } else {
            TSK_TEST_FAIL("vclip", "overwrite", "overwritten content mismatch");
        }
    } else {
        TSK_TEST_FAIL("vclip", "overwrite", "clipboard_set failed");
    }

    /* 6. Truncation when destination buffer is smaller */
    total_tests++;
    const char *long_sample = "0123456789ABCDEFGHIJ";
    clipboard_set(long_sample, strlen(long_sample));
    char small_buf[10];
    memset(small_buf, 0xFF, sizeof(small_buf));
    rc = clipboard_get(small_buf, sizeof(small_buf));
    if (rc == 9 && small_buf[9] == '\0' && strncmp(small_buf, "012345678", 9) == 0) {
        /* Verify full content is preserved in daemon */
        memset(test_buf, 0, sizeof(test_buf));
        int full_n = clipboard_get(test_buf, sizeof(test_buf));
        if (full_n == 20 && strcmp(test_buf, long_sample) == 0) {
            TSK_TEST_PASS("vclip", "truncation");
            passed_tests++;
        } else {
            TSK_TEST_FAIL("vclip", "truncation", "daemon content altered after small read");
        }
    } else {
        TSK_TEST_FAIL("vclip", "truncation", "small buffer not truncated or NUL terminated properly");
    }

    /* 7. Clear clipboard */
    total_tests++;
    rc = clipboard_clear();
    if (rc == 0) {
        memset(test_buf, 'Z', sizeof(test_buf));
        int n = clipboard_get(test_buf, sizeof(test_buf));
        if (n == 0 && test_buf[0] == '\0') {
            TSK_TEST_PASS("vclip", "clear");
            passed_tests++;
        } else {
            TSK_TEST_FAIL("vclip", "clear", "clipboard not empty after clear");
        }
    } else {
        TSK_TEST_FAIL("vclip", "clear", "clipboard_clear failed");
    }

    /* 8. Large payload at maximum capacity (64 KiB) */
    total_tests++;
    char *large_buf = (char *)malloc(VCLIP_TEXT_MAX);
    char *verify_buf = (char *)malloc(VCLIP_TEXT_MAX + 1);
    if (large_buf && verify_buf) {
        for (size_t i = 0; i < VCLIP_TEXT_MAX; i++) {
            large_buf[i] = (char)('a' + (i % 26));
        }
        rc = clipboard_set(large_buf, VCLIP_TEXT_MAX);
        if (rc == 0) {
            int n = clipboard_get(verify_buf, VCLIP_TEXT_MAX + 1);
            if (n == VCLIP_TEXT_MAX && memcmp(large_buf, verify_buf, VCLIP_TEXT_MAX) == 0) {
                TSK_TEST_PASS("vclip", "max_capacity_64k");
                passed_tests++;
            } else {
                TSK_TEST_FAIL("vclip", "max_capacity_64k", "payload mismatch at max capacity");
            }
        } else {
            TSK_TEST_FAIL("vclip", "max_capacity_64k", "clipboard_set failed at max capacity");
        }
    } else {
        TSK_TEST_FAIL("vclip", "max_capacity_64k", "host malloc failed");
    }
    free(large_buf);
    free(verify_buf);

    /* 9. Phase B stubs */
    total_tests++;
    if (clipboard_offer(NULL, 0) == -ENOSYS &&
        clipboard_fetch("text/plain", test_buf, sizeof(test_buf)) == -ENOSYS) {
        TSK_TEST_PASS("vclip", "phase_b_stubs");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vclip", "phase_b_stubs", "expected -ENOSYS for Phase B APIs");
    }

    /* 10. Concurrent stress clients */
    total_tests++;
    const int num_workers = 4;
    pthread_t workers[4];
    worker_arg_t wargs[4];
    for (int i = 0; i < num_workers; i++) {
        wargs[i].thread_id = i;
        wargs[i].success = 0;
        pthread_create(&workers[i], NULL, worker_thread_func, &wargs[i]);
    }
    int all_workers_ok = 1;
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
        if (!wargs[i].success)
            all_workers_ok = 0;
    }
    if (all_workers_ok) {
        TSK_TEST_PASS("vclip", "concurrent_stress_clients");
        passed_tests++;
    } else {
        TSK_TEST_FAIL("vclip", "concurrent_stress_clients", "concurrent worker failure");
    }

    /* Stop daemon */
    sarg.stop_flag = 1;
    /* Trigger accept loop wakeup by connecting one more time */
    int wake_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (wake_fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, TEST_VCLIP_SOCK, sizeof(addr.sun_path) - 1);
        (void)connect(wake_fd, (struct sockaddr *)&addr, sizeof(addr));
        close_socket(wake_fd);
    }
    pthread_join(th, NULL);

    unlink_file(TEST_VCLIP_SOCK);

    TSK_TEST_DONE("vclip", passed_tests, total_tests);

    return (passed_tests == total_tests) ? 0 : 1;
}
