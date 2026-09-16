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
#include "../../scripts/test/tsk_test.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int passed = 0;
    int total = 4;

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

    TSK_TEST_DONE("stdio", passed, total);
    return 0;
}
