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
#include "../../scripts/test/tsk_test.h"

static int cmp_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

struct sort_item_12 {
    int key;
    unsigned char pad[8];
};

static int cmp_item_12(const void *a, const void *b)
{
    const struct sort_item_12 *ia = (const struct sort_item_12 *)a;
    const struct sort_item_12 *ib = (const struct sort_item_12 *)b;
    return (ia->key > ib->key) - (ia->key < ib->key);
}

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;
    int passed = 0;
    int total = 5;

    /* 1. String Operations */
    const char *orig = "Hello, Tsukasa OS!";
    if (strlen(orig) != 18) {
        TSK_TEST_FAIL("libc", "strings", "strlen mismatch");
        return 1;
    }

    char buf[64];
    strcpy(buf, "Tsukasa");
    strcat(buf, " Libc");
    if (strcmp(buf, "Tsukasa Libc") != 0) {
        TSK_TEST_FAIL("libc", "strings", "strcmp/strcpy/strcat mismatch");
        return 2;
    }

    if (strncmp(buf, "Tsukasa XYZ", 7) != 0) {
        TSK_TEST_FAIL("libc", "strings", "strncmp mismatch");
        return 3;
    }

    const char *sub = strstr(buf, "Libc");
    if (!sub || strcmp(sub, "Libc") != 0) {
        TSK_TEST_FAIL("libc", "strings", "strstr mismatch");
        return 4;
    }

    char *dup = strdup(buf);
    if (!dup || strcmp(dup, buf) != 0) {
        TSK_TEST_FAIL("libc", "strings", "strdup mismatch");
        return 5;
    }
    free(dup);

    const char *err_msg = strerror(2); /* ENOENT */
    if (!err_msg || strcmp(err_msg, "No such file or directory") != 0) {
        TSK_TEST_FAIL("libc", "strings", "strerror mismatch");
        return 6;
    }
    TSK_TEST_PASS("libc", "strings");
    passed++;

    /* 2. Heap Allocator (16-byte alignment & realloc) */
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        size_t sz = (size_t)(16 * (i + 1));
        ptrs[i] = malloc(sz);
        if (!ptrs[i]) {
            TSK_TEST_FAIL("libc", "heap", "malloc returned NULL");
            return 7;
        }
        if (((uintptr_t)ptrs[i] & 0xFULL) != 0) {
            TSK_TEST_FAIL("libc", "heap", "malloc pointer not 16-byte aligned");
            return 8;
        }
        memset(ptrs[i], 0xAA + i, sz);
    }

    for (int i = 0; i < 10; i++) {
        size_t sz = (size_t)(16 * (i + 1));
        const unsigned char *b = (const unsigned char *)ptrs[i];
        for (size_t j = 0; j < sz; j++) {
            if (b[j] != (unsigned char)(0xAA + i)) {
                TSK_TEST_FAIL("libc", "heap", "heap memory data corrupted");
                return 9;
            }
        }
    }

    /* Test realloc expansion */
    char *realloc_ptr = (char *)malloc(32);
    strcpy(realloc_ptr, "1234567890");
    realloc_ptr = (char *)realloc(realloc_ptr, 1024);
    if (!realloc_ptr || strcmp(realloc_ptr, "1234567890") != 0) {
        TSK_TEST_FAIL("libc", "heap", "realloc expansion failed");
        return 10;
    }
    free(realloc_ptr);

    /* Test calloc zeroing */
    int *calloc_arr = (int *)calloc(64, sizeof(int));
    if (!calloc_arr) {
        TSK_TEST_FAIL("libc", "heap", "calloc returned NULL");
        return 11;
    }
    for (int i = 0; i < 64; i++) {
        if (calloc_arr[i] != 0) {
            TSK_TEST_FAIL("libc", "heap", "calloc memory not zeroed");
            return 12;
        }
    }
    free(calloc_arr);

    for (int i = 0; i < 10; i++)
        free(ptrs[i]);

    TSK_TEST_PASS("libc", "heap");
    passed++;

    /* 3. Standard I/O & Formatting */
    char fmt_buf[128];
    snprintf(fmt_buf, sizeof(fmt_buf), "Dec: %d, Hex: 0x%x, UpperHex: %08X, Str: %-8s!",
             -42, 0xcafe, 0x1234abcd, "Tsukasa");
    const char *expected = "Dec: -42, Hex: 0xcafe, UpperHex: 1234ABCD, Str: Tsukasa !";
    if (strcmp(fmt_buf, expected) != 0) {
        TSK_TEST_FAIL("libc", "stdio_formatting", "snprintf output mismatch");
        return 13;
    }

    snprintf(fmt_buf, sizeof(fmt_buf), "Binary: %b", 5);
    if (strcmp(fmt_buf, "Binary: 101") != 0) {
        TSK_TEST_FAIL("libc", "stdio_formatting", "snprintf binary specifier failed");
        return 14;
    }
    TSK_TEST_PASS("libc", "stdio_formatting");
    passed++;

    /* 4. Conversions & Algorithms */
    if (atoi(" -12345 ") != -12345) {
        TSK_TEST_FAIL("libc", "utilities", "atoi failed");
        return 15;
    }
    if (strtol("0xDeadBeef", NULL, 0) != 0xdeadbeefL) {
        TSK_TEST_FAIL("libc", "utilities", "strtol hex prefix failed");
        return 16;
    }
    if (abs(-999) != 999) {
        TSK_TEST_FAIL("libc", "utilities", "abs failed");
        return 17;
    }

    int arr[] = { 42, 13, 99, -5, 0, 7 };
    qsort(arr, 6, sizeof(int), cmp_int);
    for (int i = 0; i < 5; i++) {
        if (arr[i] > arr[i + 1]) {
            TSK_TEST_FAIL("libc", "utilities", "qsort did not sort array correctly");
            return 18;
        }
    }

    int key = 42;
    int *found = (int *)bsearch(&key, arr, 6, sizeof(int), cmp_int);
    if (!found || *found != 42) {
        TSK_TEST_FAIL("libc", "utilities", "bsearch failed to find 42");
        return 19;
    }
    TSK_TEST_PASS("libc", "utilities");
    passed++;

    /* 5. qsort stress (N=2048 reverse, random, duplicates, non-power-of-two structs) */
    {
        enum { N_QSORT = 2048 };
        int *qarr = (int *)malloc((size_t)N_QSORT * sizeof(int));
        struct sort_item_12 *sarr = (struct sort_item_12 *)malloc(
            (size_t)N_QSORT * sizeof(struct sort_item_12));
        if (!qarr || !sarr) {
            free(qarr);
            free(sarr);
            TSK_TEST_FAIL("libc", "qsort_stress", "malloc failed");
            return 20;
        }

        /* Reverse-sorted */
        for (int i = 0; i < N_QSORT; i++)
            qarr[i] = N_QSORT - 1 - i;
        qsort(qarr, N_QSORT, sizeof(int), cmp_int);
        for (int i = 0; i < N_QSORT; i++) {
            if (qarr[i] != i) {
                free(qarr);
                free(sarr);
                TSK_TEST_FAIL("libc", "qsort_stress", "reverse array sort mismatch");
                return 21;
            }
        }

        /* Deterministic pseudo-random with heavy duplicates */
        unsigned int seed = 0x12345678u;
        for (int i = 0; i < N_QSORT; i++) {
            seed = seed * 1664525u + 1013904223u;
            qarr[i] = (int)((seed >> 16) % 128u) - 64;
        }
        qsort(qarr, N_QSORT, sizeof(int), cmp_int);
        for (int i = 0; i + 1 < N_QSORT; i++) {
            if (qarr[i] > qarr[i + 1]) {
                free(qarr);
                free(sarr);
                TSK_TEST_FAIL("libc", "qsort_stress", "random/duplicate array not sorted");
                return 22;
            }
        }

        /* Non-power-of-two 12-byte structs */
        for (int i = 0; i < N_QSORT; i++) {
            seed = seed * 1664525u + 1013904223u;
            sarr[i].key = (int)(seed >> 8);
            sarr[i].pad[0] = (unsigned char)(i & 0xFF);
        }
        qsort(sarr, N_QSORT, sizeof(struct sort_item_12), cmp_item_12);
        for (int i = 0; i + 1 < N_QSORT; i++) {
            if (sarr[i].key > sarr[i + 1].key) {
                free(qarr);
                free(sarr);
                TSK_TEST_FAIL("libc", "qsort_stress", "12-byte struct sort mismatch");
                return 23;
            }
        }

        free(qarr);
        free(sarr);
        TSK_TEST_PASS("libc", "qsort_stress");
        passed++;
    }

    TSK_TEST_DONE("libc", passed, total);
    return 0;
}
