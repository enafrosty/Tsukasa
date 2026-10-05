/*
 * Project Tsukasa — Service Supervisor Host Unit Tests
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

#include "../supervisor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

extern int supervisor_parse_line(char *line, svc_entry_t *out);
extern uint32_t supervisor_compute_next_backoff(uint32_t current_backoff_ms, uint64_t runtime_ms);

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

static void test_wire_protocol_layout(void)
{
    printf("[test] wire protocol structure sizes and alignments\n");

    TEST_ASSERT(sizeof(vsup_req_t) == 8, "vsup_req_t size is 8 bytes");
    TEST_ASSERT(sizeof(vsup_resp_hdr_t) == 8, "vsup_resp_hdr_t size is 8 bytes");
    TEST_ASSERT(sizeof(vsup_svc_entry_t) == 44, "vsup_svc_entry_t size is 44 bytes");

    vsup_req_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VSUP_MAGIC;
    req.op = VSUP_OP_LIST;
    TEST_ASSERT(req.magic == 0x56535550u, "magic matches 0x56535550");
    TEST_ASSERT(req.op == 1, "op matches VSUP_OP_LIST (1)");

    vsup_resp_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = VSUP_MAGIC;
    hdr.service_count = 4;
    TEST_ASSERT(hdr.magic == 0x56535550u, "hdr magic matches");
    TEST_ASSERT(hdr.service_count == 4, "hdr service count matches");

    vsup_svc_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, "registryd", sizeof(entry.name) - 1);
    entry.pid = 42;
    entry.restart_count = 3;
    entry.state = (uint8_t)SVC_STATE_RUNNING;
    TEST_ASSERT(strcmp(entry.name, "registryd") == 0, "entry name preserved");
    TEST_ASSERT(entry.pid == 42, "entry pid preserved");
    TEST_ASSERT(entry.restart_count == 3, "entry restart_count preserved");
    TEST_ASSERT(entry.state == SVC_STATE_RUNNING, "entry state preserved");
}

static void test_config_parsing(void)
{
    printf("[test] config line parser\n");

    svc_entry_t entry;

    /* Empty line */
    char line1[] = "   \t  \r\n";
    TEST_ASSERT(supervisor_parse_line(line1, &entry) == 0, "blank line returns 0");

    /* Comment line */
    char line2[] = "# this is a comment";
    TEST_ASSERT(supervisor_parse_line(line2, &entry) == 0, "comment line returns 0");

    /* Comment line with leading spaces */
    char line3[] = "   # indented comment";
    TEST_ASSERT(supervisor_parse_line(line3, &entry) == 0, "indented comment returns 0");

    /* Invalid line: no equal sign */
    char line4[] = "invalid_line_without_equals";
    TEST_ASSERT(supervisor_parse_line(line4, &entry) == -1, "no '=' returns -1");

    /* Empty name */
    char line5[] = " = /bin/app.elf";
    TEST_ASSERT(supervisor_parse_line(line5, &entry) == -1, "empty name returns -1");

    /* Empty value */
    char line6[] = "app =   ";
    TEST_ASSERT(supervisor_parse_line(line6, &entry) == -1, "empty val returns -1");

    /* Valid line without arguments */
    char line7[] = "registryd = /bin/registryd.elf";
    TEST_ASSERT(supervisor_parse_line(line7, &entry) == 1, "valid line returns 1");
    TEST_ASSERT(strcmp(entry.name, "registryd") == 0, "name matches registryd");
    TEST_ASSERT(strcmp(entry.path, "/bin/registryd.elf") == 0, "path matches /bin/registryd.elf");
    TEST_ASSERT(entry.args[0] == '\0', "args empty");
    TEST_ASSERT(entry.backoff_ms == SUPERVISOR_BACKOFF_INIT_MS, "init backoff is 1000ms");
    TEST_ASSERT(entry.state == SVC_STATE_BACKOFF, "initial state is backoff");
    TEST_ASSERT(entry.pid == -1, "initial pid is -1");

    /* Valid line with whitespace and arguments */
    char line8[] = "  shell  =  /bin/shell.elf  --flag  arg2  ";
    TEST_ASSERT(supervisor_parse_line(line8, &entry) == 1, "valid line with args returns 1");
    TEST_ASSERT(strcmp(entry.name, "shell") == 0, "trimmed name matches shell");
    TEST_ASSERT(strcmp(entry.path, "/bin/shell.elf") == 0, "trimmed path matches /bin/shell.elf");
    TEST_ASSERT(strcmp(entry.args, "--flag  arg2") == 0, "args parsed correctly");
}

static void test_backoff_logic(void)
{
    printf("[test] backoff doubling, capping, and reset\n");

    /* Consecutive failure with short runtime (< 30s) */
    uint32_t b = SUPERVISOR_BACKOFF_INIT_MS; /* 1000ms */
    b = supervisor_compute_next_backoff(b, 500);
    TEST_ASSERT(b == 2000, "1000ms doubles to 2000ms");

    b = supervisor_compute_next_backoff(b, 1000);
    TEST_ASSERT(b == 4000, "2000ms doubles to 4000ms");

    b = supervisor_compute_next_backoff(b, 2000);
    TEST_ASSERT(b == 8000, "4000ms doubles to 8000ms");

    b = supervisor_compute_next_backoff(b, 15000);
    TEST_ASSERT(b == 16000, "8000ms doubles to 16000ms");

    b = supervisor_compute_next_backoff(b, 29999);
    TEST_ASSERT(b == 32000, "16000ms doubles to 32000ms");

    b = supervisor_compute_next_backoff(b, 100);
    TEST_ASSERT(b == 60000, "32000ms capped at 60000ms");

    b = supervisor_compute_next_backoff(b, 500);
    TEST_ASSERT(b == 60000, "60000ms remains capped at 60000ms");

    /* Successful run >= 30s resets backoff to 1000ms */
    b = supervisor_compute_next_backoff(60000, 30000);
    TEST_ASSERT(b == SUPERVISOR_BACKOFF_INIT_MS, "runtime >= 30000ms resets backoff to 1000ms");

    b = supervisor_compute_next_backoff(32000, 45000);
    TEST_ASSERT(b == SUPERVISOR_BACKOFF_INIT_MS, "runtime 45000ms resets backoff to 1000ms");
}

int main(void)
{
    printf("=== Running Supervisor Host Unit Tests ===\n");

    test_wire_protocol_layout();
    test_config_parsing();
    test_backoff_logic();

    printf("=========================================\n");
    printf("Result: %d/%d assertions passed\n", g_tests_passed, g_tests_run);

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
