/*
 * Project Tsukasa — Comprehensive LibC Core Verification Suite
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

static int cmp_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    printf("[test] Starting tsukasa-libc Step 2.3 verification...\n");

    /* 1. String Operations */
    const char *orig = "Hello, Tsukasa OS!";
    if (strlen(orig) != 18) {
        printf("[FAIL] strlen mismatch\n");
        return 1;
    }

    char buf[64];
    strcpy(buf, "Tsukasa");
    strcat(buf, " Libc");
    if (strcmp(buf, "Tsukasa Libc") != 0) {
        printf("[FAIL] strcmp/strcpy/strcat mismatch\n");
        return 2;
    }

    if (strncmp(buf, "Tsukasa XYZ", 7) != 0) {
        printf("[FAIL] strncmp mismatch\n");
        return 3;
    }

    const char *sub = strstr(buf, "Libc");
    if (!sub || strcmp(sub, "Libc") != 0) {
        printf("[FAIL] strstr mismatch\n");
        return 4;
    }

    char *dup = strdup(buf);
    if (!dup || strcmp(dup, buf) != 0) {
        printf("[FAIL] strdup mismatch\n");
        return 5;
    }
    free(dup);

    const char *err_msg = strerror(2); /* ENOENT */
    if (!err_msg || strcmp(err_msg, "No such file or directory") != 0) {
        printf("[FAIL] strerror mismatch\n");
        return 6;
    }
    printf("[PASS] String operations (strlen, strcpy, strcat, strcmp, strstr, strdup, strerror)\n");

    /* 2. Heap Allocator (16-byte alignment & realloc) */
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        size_t sz = (size_t)(16 * (i + 1));
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) {
            printf("[FAIL] malloc returned NULL for size %zu\n", sz);
            return 7;
        }
        if (((uintptr_t)ptrs[i] & 0xFULL) != 0) {
            printf("[FAIL] malloc pointer not 16-byte aligned: %p\n", ptrs[i]);
            return 8;
        }
        memset(ptrs[i], 0xAA + i, sz);
    }

    for (int i = 0; i < 10; i++) {
        size_t sz = (size_t)(16 * (i + 1));
        const unsigned char *b = (const unsigned char *)ptrs[i];
        for (size_t j = 0; j < sz; j++) {
            if (b[j] != (unsigned char)(0xAA + i)) {
                printf("[FAIL] heap memory data corrupted\n");
                return 9;
            }
        }
    }

    /* Test realloc expansion */
    char *realloc_ptr = (char *)malloc(32);
    strcpy(realloc_ptr, "1234567890");
    realloc_ptr = (char *)realloc(realloc_ptr, 1024);
    if (!realloc_ptr || strcmp(realloc_ptr, "1234567890") != 0) {
        printf("[FAIL] realloc expansion failed\n");
        return 10;
    }
    free(realloc_ptr);

    /* Test calloc zeroing */
    int *calloc_arr = (int *)calloc(64, sizeof(int));
    if (!calloc_arr) {
        printf("[FAIL] calloc returned NULL\n");
        return 11;
    }
    for (int i = 0; i < 64; i++) {
        if (calloc_arr[i] != 0) {
            printf("[FAIL] calloc memory not zeroed\n");
            return 12;
        }
    }
    free(calloc_arr);

    for (int i = 0; i < 10; i++)
        free(ptrs[i]);

    printf("[PASS] Dynamic heap allocator (malloc 16-byte aligned, realloc, calloc, free)\n");

    /* 3. Standard I/O & Formatting */
    char fmt_buf[128];
    snprintf(fmt_buf, sizeof(fmt_buf), "Dec: %d, Hex: 0x%x, UpperHex: %08X, Str: %-8s!",
             -42, 0xcafe, 0x1234abcd, "Tsukasa");
    const char *expected = "Dec: -42, Hex: 0xcafe, UpperHex: 1234ABCD, Str: Tsukasa !";
    if (strcmp(fmt_buf, expected) != 0) {
        printf("[FAIL] snprintf output mismatch:\nExpected: [%s]\nGot:      [%s]\n",
               expected, fmt_buf);
        return 13;
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Binary: %b", 5);
    if (strcmp(fmt_buf, "Binary: 101") != 0) {
        printf("[FAIL] snprintf binary specifier failed: %s\n", fmt_buf);
        return 14;
    }
    printf("[PASS] Formatted I/O (snprintf integer, hex, padding, alignment, binary)\n");

    /* 4. Conversions & Algorithms */
    if (atoi(" -12345 ") != -12345) {
        printf("[FAIL] atoi failed\n");
        return 15;
    }
    if (strtol("0xDeadBeef", NULL, 0) != 0xdeadbeefL) {
        printf("[FAIL] strtol hex prefix failed\n");
        return 16;
    }
    if (abs(-999) != 999) {
        printf("[FAIL] abs failed\n");
        return 17;
    }

    int arr[] = { 42, 13, 99, -5, 0, 7 };
    qsort(arr, 6, sizeof(int), cmp_int);
    for (int i = 0; i < 5; i++) {
        if (arr[i] > arr[i + 1]) {
            printf("[FAIL] qsort did not sort array correctly\n");
            return 18;
        }
    }

    int key = 42;
    int *found = (int *)bsearch(&key, arr, 6, sizeof(int), cmp_int);
    if (!found || *found != 42) {
        printf("[FAIL] bsearch failed to find 42\n");
        return 19;
    }
    printf("[PASS] General utilities (atoi, strtol, abs, qsort, bsearch)\n");

    printf("[ALL PASS] tsukasa-libc Step 2.3 core standard library verified successfully\n");
    return 0;
}
