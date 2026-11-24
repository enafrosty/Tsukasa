/*
 * Project Tsukasa — Test Buffered Standard I/O Subsystem
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>
#include <sys/wait.h>
#include "../../scripts/test/tsk_test.h"

static FILE *g_exit_fp = NULL;
static FILE *g_atexit_order_fp = NULL;
static int g_atexit_cnt = 0;

static void child_atexit_c(void)
{
    if (g_exit_fp)
        fputs("C", g_exit_fp);
}

static void child_atexit_b(void)
{
    if (g_exit_fp)
        fputs("B", g_exit_fp);
    (void)atexit(child_atexit_c);
}

static void child_atexit_a(void)
{
    if (g_exit_fp)
        fputs("A", g_exit_fp);
}

static void child_atexit_count_inc(void)
{
    g_atexit_cnt++;
    if (g_atexit_order_fp)
        fprintf(g_atexit_order_fp, "%d,", g_atexit_cnt);
}

static int run_child_exit_test(void)
{
    if (atexit(NULL) != -1)
        return 10;
    g_exit_fp = fopen("/tmp/test_stdio_exit.tmp", "w");
    if (!g_exit_fp)
        return 11;
    fputs("main:", g_exit_fp);
    if (atexit(child_atexit_a) != 0 || atexit(child_atexit_b) != 0)
        return 12;
    /* Intentionally return without calling fflush or fclose. */
    return 42;
}

static int run_child_atexit_full(void)
{
    g_atexit_order_fp = fopen("/tmp/test_atexit_full.tmp", "w");
    if (!g_atexit_order_fp)
        return 20;
    for (int i = 0; i < 32; i++) {
        if (atexit(child_atexit_count_inc) != 0)
            return 21;
    }
    if (atexit(child_atexit_count_inc) != -1)
        return 22;
    return 0;
}

static pid_t spawn_self_child(const char *flag)
{
    char *child_argv[3];
    child_argv[0] = (char *)"/bin/TSTDIO.ELF";
    child_argv[1] = (char *)flag;
    child_argv[2] = NULL;
    pid_t pid = -1;
    for (int retry = 0; retry < 50 && pid < 0; retry++) {
        pid = spawn("/bin/TSTDIO.ELF", child_argv, NULL);
        if (pid < 0)
            sched_yield();
    }
    return pid;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--child-exit-test") == 0)
        return run_child_exit_test();
    if (argc >= 2 && strcmp(argv[1], "--child-atexit-full") == 0)
        return run_child_atexit_full();

    int passed = 0;
    int total = 7;

    const char *test_path = "/tmp/test_stdio.tmp";

    /* Test 1: File creation and formatted/buffered writing */
    FILE *fp = fopen(test_path, "w");
    if (!fp) {
        TSK_TEST_FAIL("stdio", "file_write_buffered", "fopen('w') returned NULL");
        return 1;
    }

    int written = fprintf(fp, "Line %d: The quick brown fox jumps over the lazy dog.\n", 1);
    if (written <= 0 || fputs("Line 2: Standard I/O buffering in Project Tsukasa.\n", fp) < 0 ||
        fputc('A', fp) != 'A' || fputc('\n', fp) != '\n') {
        TSK_TEST_FAIL("stdio", "file_write_buffered", "buffered write/formatting failed");
        fclose(fp);
        return 1;
    }

    /* Write 2048 bytes of sequential pattern to verify multi-block buffering */
    char pattern[2048];
    for (int i = 0; i < 2048; i++)
        pattern[i] = (char)('a' + (i % 26));

    size_t nw = fwrite(pattern, 1, sizeof(pattern), fp);
    if (nw != sizeof(pattern) || fflush(fp) != 0 || fclose(fp) != 0) {
        TSK_TEST_FAIL("stdio", "file_write_buffered", "fwrite, fflush or fclose failed");
        return 1;
    }
    TSK_TEST_PASS("stdio", "file_write_buffered");
    passed++;

    /* Test 2: File reading and ftell read-buffer offset accuracy */
    fp = fopen(test_path, "r");
    if (!fp) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fopen('r') returned NULL");
        return 2;
    }

    long pos0 = ftell(fp);
    if (pos0 != 0L) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "initial ftell is not 0");
        fclose(fp);
        return 2;
    }

    char line_buf[128];
    char *s = fgets(line_buf, sizeof(line_buf), fp);
    if (!s) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fgets line 1 returned NULL");
        fclose(fp);
        return 2;
    }
    size_t line1_len = strlen(line_buf);
    if (ftell(fp) != (long)line1_len) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "ftell mismatch after line 1");
        fclose(fp);
        return 2;
    }

    /* Read another line */
    s = fgets(line_buf, sizeof(line_buf), fp);
    if (!s) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fgets line 2 returned NULL");
        fclose(fp);
        return 2;
    }
    size_t line2_len = strlen(line_buf);
    if (ftell(fp) != (long)(line1_len + line2_len)) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "ftell mismatch after line 2");
        fclose(fp);
        return 2;
    }

    /* Read the 'A\n' line */
    s = fgets(line_buf, sizeof(line_buf), fp);
    if (!s || line_buf[0] != 'A') {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fgets line 3 returned unexpected content");
        fclose(fp);
        return 2;
    }
    size_t line3_len = strlen(line_buf);
    long header_total = (long)(line1_len + line2_len + line3_len);
    if (ftell(fp) != header_total) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "ftell mismatch at header total");
        fclose(fp);
        return 2;
    }

    char read_buf[100];
    size_t nr = fread(read_buf, 1, 100, fp);
    if (nr != 100 || memcmp(read_buf, pattern, 100) != 0 ||
        ftell(fp) != header_total + 100L) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fread chunk 1 pattern/ftell mismatch");
        fclose(fp);
        return 2;
    }

    char read_buf2[150];
    nr = fread(read_buf2, 1, 150, fp);
    if (nr != 150 || memcmp(read_buf2, pattern + 100, 150) != 0 ||
        ftell(fp) != header_total + 250L) {
        TSK_TEST_FAIL("stdio", "file_read_offset", "fread chunk 2 pattern/ftell mismatch");
        fclose(fp);
        return 2;
    }
    TSK_TEST_PASS("stdio", "file_read_offset");
    passed++;

    /* Test 3: fseek with SEEK_SET, SEEK_CUR, SEEK_END */
    if (fseek(fp, header_total, SEEK_SET) != 0 || ftell(fp) != header_total ||
        fgetc(fp) != (int)pattern[0] || ftell(fp) != header_total + 1L ||
        fseek(fp, 99, SEEK_CUR) != 0 || ftell(fp) != header_total + 100L ||
        fseek(fp, 0, SEEK_END) != 0 || ftell(fp) != header_total + 2048L) {
        TSK_TEST_FAIL("stdio", "fseek_eof", "fseek positioning mismatch");
        fclose(fp);
        return 3;
    }

    /* EOF check */
    int eof_char = fgetc(fp);
    if (eof_char != EOF || feof(fp) == 0) {
        TSK_TEST_FAIL("stdio", "fseek_eof", "fgetc at EOF did not set feof");
        fclose(fp);
        return 3;
    }

    clearerr(fp);
    if (feof(fp) != 0 || fclose(fp) != 0) {
        TSK_TEST_FAIL("stdio", "fseek_eof", "clearerr or fclose failed");
        return 3;
    }
    TSK_TEST_PASS("stdio", "fseek_eof");
    passed++;

    /* Test 4: Global flush */
    if (fflush(NULL) != 0) {
        TSK_TEST_FAIL("stdio", "global_flush", "fflush(NULL) failed");
        return 4;
    }
    TSK_TEST_PASS("stdio", "global_flush");
    passed++;

    /* Test 5: Integer edge cases (octal, zero precision, LLONG_MIN, negative * width) */
    {
        char b[128];
        int ok = 1;
        snprintf(b, sizeof(b), "%o %#o %#o %#.0o %.0o", 0, 0, 8, 0, 0);
        if (strcmp(b, "0 0 010 0 ") != 0) ok = 0;
        snprintf(b, sizeof(b), "|%.0d|%.0u|%.0x|%05.1d|", 0, 0U, 0U, 3);
        if (strcmp(b, "||||    3|") != 0) ok = 0;
        snprintf(b, sizeof(b), "%lld", LLONG_MIN);
        if (strcmp(b, "-9223372036854775808") != 0) ok = 0;
        snprintf(b, sizeof(b), "%*d", -5, 42);
        if (strcmp(b, "42   ") != 0) ok = 0;

        if (ok) {
            TSK_TEST_PASS("stdio", "int_octal_llong_min");
            passed++;
        } else {
            TSK_TEST_FAIL("stdio", "int_octal_llong_min", "integer/octal formatting mismatch");
            return 5;
        }
    }

    /* Test 6: Floating-point formatting (%f/%F/%e/%E/%g/%G) */
    {
        char b[128];
        int ok = 1;
        double neg_zero = -0.0;
        double pos_inf = (double)INFINITY;
        double neg_inf = -(double)INFINITY;
        double qnan = (double)NAN;

        snprintf(b, sizeof(b), "%.14g", 3.14);
        if (strcmp(b, "3.14") != 0) ok = 0;
        snprintf(b, sizeof(b), "%.2f %+08.2f %-6.1f %#.0f", 3.5, 3.5, 2.5, 4.0);
        if (strcmp(b, "3.50 +0003.50 2.5    4.") != 0) ok = 0;
        snprintf(b, sizeof(b), "%.1f %.0f %.0f", 9.99, 0.5, 1.5);
        if (strcmp(b, "10.0 0 2") != 0) ok = 0;
        snprintf(b, sizeof(b), "%.2f %g %e", neg_zero, neg_zero, neg_zero);
        if (strcmp(b, "-0.00 -0 -0.000000e+00") != 0) ok = 0;
        snprintf(b, sizeof(b), "%f %F %e %E %06f", pos_inf, pos_inf, neg_inf, neg_inf, pos_inf);
        if (strcmp(b, "inf INF -inf -INF    inf") != 0) ok = 0;
        snprintf(b, sizeof(b), "%f %F %g %G", qnan, qnan, qnan, qnan);
        if (strcmp(b, "nan NAN nan NAN") != 0) ok = 0;
        snprintf(b, sizeof(b), "%g %g %G %#g", 0.0001, 0.00001, 1000000.0, 100.0);
        if (strcmp(b, "0.0001 1e-05 1E+06 100.000") != 0) ok = 0;
        snprintf(b, sizeof(b), "%.*f", 3, 1.25);
        if (strcmp(b, "1.250") != 0) ok = 0;

        if (ok) {
            TSK_TEST_PASS("stdio", "float_formatting");
            passed++;
        } else {
            TSK_TEST_FAIL("stdio", "float_formatting", "floating-point printf mismatch");
            return 6;
        }
    }

    /* Test 7: atexit LIFO + dynamic registration + exit fflush(NULL) via child */
    {
        unlink("/tmp/test_stdio_exit.tmp");
        unlink("/tmp/test_atexit_full.tmp");

        pid_t pid1 = spawn_self_child("--child-exit-test");
        int st1 = 0;
        if (pid1 <= 0 || waitpid(pid1, &st1, 0) != pid1 ||
            !WIFEXITED(st1) || WEXITSTATUS(st1) != 42) {
            TSK_TEST_FAIL("stdio", "atexit_exit_flush", "child-exit-test wait/exit mismatch");
            return 7;
        }

        FILE *cfp = fopen("/tmp/test_stdio_exit.tmp", "r");
        char out1[64];
        size_t nr1 = 0;
        if (cfp) {
            nr1 = fread(out1, 1, sizeof(out1) - 1, cfp);
            out1[nr1] = '\0';
            fclose(cfp);
        }
        unlink("/tmp/test_stdio_exit.tmp");

        pid_t pid2 = spawn_self_child("--child-atexit-full");
        int st2 = 0;
        if (pid2 <= 0 || waitpid(pid2, &st2, 0) != pid2 ||
            !WIFEXITED(st2) || WEXITSTATUS(st2) != 0) {
            TSK_TEST_FAIL("stdio", "atexit_exit_flush", "child-atexit-full wait/exit mismatch");
            return 7;
        }

        FILE *afp = fopen("/tmp/test_atexit_full.tmp", "r");
        char out2[256];
        size_t nr2 = 0;
        if (afp) {
            nr2 = fread(out2, 1, sizeof(out2) - 1, afp);
            out2[nr2] = '\0';
            fclose(afp);
        }
        unlink("/tmp/test_atexit_full.tmp");

        int commas = 0;
        for (size_t k = 0; k < nr2; k++) {
            if (out2[k] == ',')
                commas++;
        }

        if (nr1 == 8 && strcmp(out1, "main:BCA") == 0 && commas == 32) {
            TSK_TEST_PASS("stdio", "atexit_exit_flush");
            passed++;
        } else {
            TSK_TEST_FAIL("stdio", "atexit_exit_flush", "atexit order or fflush-on-exit mismatch");
            return 7;
        }
    }

    TSK_TEST_DONE("stdio", passed, total);
    return 0;
}
