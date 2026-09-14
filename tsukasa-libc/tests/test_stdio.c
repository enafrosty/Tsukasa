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

static void assert_test(int cond, const char *msg)
{
    if (!cond) {
        printf("[FAIL] stdio assertion: %s\n", msg);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[TEST] Starting tsukasa-libc buffered stdio test suite...\n");

    const char *test_path = "/tmp/test_stdio.tmp";

    /* Test 1: File creation and formatted/buffered writing */
    FILE *fp = fopen(test_path, "w");
    assert_test(fp != NULL, "fopen(..., 'w') returned non-null");

    int written = fprintf(fp, "Line %d: The quick brown fox jumps over the lazy dog.\n", 1);
    assert_test(written > 0, "fprintf returned positive written count");

    assert_test(fputs("Line 2: Standard I/O buffering in Project Tsukasa.\n", fp) >= 0, "fputs succeeded");
    assert_test(fputc('A', fp) == 'A', "fputc wrote 'A'");
    assert_test(fputc('\n', fp) == '\n', "fputc wrote newline");

    /* Write 2048 bytes of sequential pattern to verify multi-block buffering */
    char pattern[2048];
    for (int i = 0; i < 2048; i++)
        pattern[i] = (char)('a' + (i % 26));

    size_t nw = fwrite(pattern, 1, sizeof(pattern), fp);
    assert_test(nw == sizeof(pattern), "fwrite wrote 2048 pattern bytes");

    assert_test(fflush(fp) == 0, "fflush succeeded");
    assert_test(fclose(fp) == 0, "fclose succeeded");

    /* Test 2: File reading and ftell read-buffer offset accuracy */
    fp = fopen(test_path, "r");
    assert_test(fp != NULL, "fopen(..., 'r') returned non-null");

    long pos0 = ftell(fp);
    assert_test(pos0 == 0L, "initial ftell is 0");

    char line_buf[128];
    char *s = fgets(line_buf, sizeof(line_buf), fp);
    assert_test(s != NULL, "fgets read line 1");
    size_t line1_len = strlen(line_buf);

    long pos1 = ftell(fp);
    assert_test(pos1 == (long)line1_len, "ftell accurately accounts for read buffer after fgets");

    /* Read another line */
    s = fgets(line_buf, sizeof(line_buf), fp);
    assert_test(s != NULL, "fgets read line 2");
    size_t line2_len = strlen(line_buf);

    long pos2 = ftell(fp);
    assert_test(pos2 == (long)(line1_len + line2_len), "ftell accurate after line 2");

    /* Read the 'A\n' line */
    s = fgets(line_buf, sizeof(line_buf), fp);
    assert_test(s != NULL && line_buf[0] == 'A', "fgets read 'A\\n'");
    size_t line3_len = strlen(line_buf);

    long header_total = (long)(line1_len + line2_len + line3_len);
    assert_test(ftell(fp) == header_total, "ftell at header total");

    /*
     * Crucial test: fread 100 bytes from the pattern.
     * The underlying read() will buffer BUFSIZ (1024) bytes.
     * ftell() must report header_total + 100, NOT header_total + 1024!
     */
    char read_buf[100];
    size_t nr = fread(read_buf, 1, 100, fp);
    assert_test(nr == 100, "fread read 100 bytes");
    assert_test(memcmp(read_buf, pattern, 100) == 0, "read pattern bytes match written pattern");

    long pos_100 = ftell(fp);
    assert_test(pos_100 == header_total + 100L, "ftell accurately subtracts (rend - rpos)");

    /* Read another 150 bytes */
    char read_buf2[150];
    nr = fread(read_buf2, 1, 150, fp);
    assert_test(nr == 150, "fread read 150 bytes");
    assert_test(memcmp(read_buf2, pattern + 100, 150) == 0, "second chunk matches pattern");

    long pos_250 = ftell(fp);
    assert_test(pos_250 == header_total + 250L, "ftell accurately advances by 150 bytes");

    /* Test 3: fseek with SEEK_SET, SEEK_CUR, SEEK_END */
    assert_test(fseek(fp, header_total, SEEK_SET) == 0, "fseek to header_total with SEEK_SET");
    assert_test(ftell(fp) == header_total, "ftell after fseek matches header_total");

    int c = fgetc(fp);
    assert_test(c == (int)pattern[0], "fgetc reads pattern[0] after seek");
    assert_test(ftell(fp) == header_total + 1L, "ftell is header_total + 1");

    assert_test(fseek(fp, 99, SEEK_CUR) == 0, "fseek +99 from CUR");
    assert_test(ftell(fp) == header_total + 100L, "ftell is header_total + 100");

    assert_test(fseek(fp, 0, SEEK_END) == 0, "fseek to SEEK_END");
    long file_size = ftell(fp);
    assert_test(file_size == header_total + 2048L, "file_size matches expected total bytes");

    /* EOF check */
    int eof_char = fgetc(fp);
    assert_test(eof_char == EOF, "fgetc at end returns EOF");
    assert_test(feof(fp) != 0, "feof(fp) is true");

    clearerr(fp);
    assert_test(feof(fp) == 0, "clearerr cleared eof");

    assert_test(fclose(fp) == 0, "fclose succeeded");

    /* Test 4: Global flush */
    assert_test(fflush(NULL) == 0, "fflush(NULL) succeeds");

    printf("[PASS] All buffered stdio assertions passed successfully.\n");
    return 0;
}
