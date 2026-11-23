/*
 * Project Tsukasa - Host Unit Tests for LZ4 Frame Packer & Decompressor
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

#include "../../lib/lz4.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_packer_bin = NULL;

#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("[TEST] lz4.assert FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static uint8_t *run_packer(const uint8_t *in_data, size_t in_len, size_t *out_len)
{
    const char *in_path = "/tmp/tsukasa-lz4-test.in";
    const char *out_path = "/tmp/tsukasa-lz4-test.lz4";
    char cmd[512];

    FILE *f = fopen(in_path, "wb");
    TEST_ASSERT(f != NULL);
    if (in_len > 0)
        TEST_ASSERT(fwrite(in_data, 1, in_len, f) == in_len);
    fclose(f);

    snprintf(cmd, sizeof(cmd), "%s %s %s", g_packer_bin, in_path, out_path);
    TEST_ASSERT(system(cmd) == 0);

    FILE *fo = fopen(out_path, "rb");
    TEST_ASSERT(fo != NULL);
    TEST_ASSERT(fseek(fo, 0, SEEK_END) == 0);
    long sz = ftell(fo);
    TEST_ASSERT(sz >= 0);
    rewind(fo);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    TEST_ASSERT(buf != NULL);
    TEST_ASSERT(fread(buf, 1, (size_t)sz, fo) == (size_t)sz);
    fclose(fo);

    remove(in_path);
    remove(out_path);
    *out_len = (size_t)sz;
    return buf;
}

static void verify_roundtrip(const char *name, const uint8_t *orig, size_t len)
{
    size_t comp_len = 0;
    uint8_t *comp = run_packer(orig, len, &comp_len);

    uint64_t content_sz = 0;
    int has_sz = 0;
    size_t max_blk = 0;
    TEST_ASSERT(lz4_frame_info(comp, comp_len, &content_sz, &has_sz, &max_blk) == LZ4_OK);
    TEST_ASSERT(has_sz == 1);
    TEST_ASSERT(content_sz == (uint64_t)len);

    uint8_t *out = len ? (uint8_t *)malloc(len) : NULL;
    if (len)
        TEST_ASSERT(out != NULL);

    size_t out_len = 999999;
    int rc = lz4_decompress(comp, comp_len, out, len, &out_len);
    TEST_ASSERT(rc == LZ4_OK);
    TEST_ASSERT(out_len == len);
    if (len > 0)
        TEST_ASSERT(memcmp(orig, out, len) == 0);

    free(out);
    free(comp);
    printf("[TEST] lz4.%s PASS\n", name);
}

static void test_corruption_cases(void)
{
    static const char sample[] =
        "Tsukasa OS LZ4 corruption test payload! "
        "Tsukasa OS LZ4 corruption test payload! "
        "Tsukasa OS LZ4 corruption test payload!";
    size_t len = strlen(sample);
    size_t comp_len = 0;
    uint8_t *comp = run_packer((const uint8_t *)sample, len, &comp_len);
    uint8_t out[256];
    size_t out_len = 0;

    /* 1. Corrupted magic */
    uint8_t *bad = (uint8_t *)malloc(comp_len);
    TEST_ASSERT(bad != NULL);
    memcpy(bad, comp, comp_len);
    bad[0] ^= 0xFF;
    TEST_ASSERT(lz4_decompress(bad, comp_len, out, sizeof(out), &out_len) == LZ4_ERR_MAGIC);
    printf("[TEST] lz4.reject_bad_magic PASS\n");

    /* 2. Corrupted header checksum */
    memcpy(bad, comp, comp_len);
    bad[14] ^= 0x55;
    TEST_ASSERT(lz4_decompress(bad, comp_len, out, sizeof(out), &out_len) == LZ4_ERR_HEADER_CRC);
    printf("[TEST] lz4.reject_header_crc PASS\n");

    /* 3. Corrupted block checksum (4 bytes before EndMark + ContentCRC = comp_len - 12) */
    memcpy(bad, comp, comp_len);
    bad[comp_len - 10] ^= 0xAA;
    TEST_ASSERT(lz4_decompress(bad, comp_len, out, sizeof(out), &out_len) == LZ4_ERR_BLOCK_CRC);
    printf("[TEST] lz4.reject_block_crc PASS\n");

    /* 4. Corrupted content checksum (last 4 bytes) */
    memcpy(bad, comp, comp_len);
    bad[comp_len - 1] ^= 0x01;
    TEST_ASSERT(lz4_decompress(bad, comp_len, out, sizeof(out), &out_len) == LZ4_ERR_CONTENT_CRC);
    printf("[TEST] lz4.reject_content_crc PASS\n");

    /* 5. Truncated stream */
    TEST_ASSERT(lz4_decompress(comp, comp_len - 5, out, sizeof(out), &out_len) == LZ4_ERR_TRUNCATED);
    printf("[TEST] lz4.reject_truncated PASS\n");

    /* 6. Output buffer too small */
    TEST_ASSERT(lz4_decompress(comp, comp_len, out, len - 1, &out_len) == LZ4_ERR_OVERFLOW);
    printf("[TEST] lz4.reject_overflow PASS\n");

    free(bad);
    free(comp);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-lz4pack>\n", argv[0]);
        return 1;
    }
    g_packer_bin = argv[1];

    /* 1. Empty payload */
    verify_roundtrip("empty", (const uint8_t *)"", 0);

    /* 2. Highly repetitive payload (compressible) */
    size_t rep_len = 65536;
    uint8_t *rep = (uint8_t *)malloc(rep_len);
    TEST_ASSERT(rep != NULL);
    for (size_t i = 0; i < rep_len; i++)
        rep[i] = (uint8_t)("TSUKASA-INITRD-PATTERN-0123456789abcdef"[i % 39]);
    verify_roundtrip("repetitive", rep, rep_len);
    free(rep);

    /* 3. Incompressible pseudo-random payload (uncompressed block path) */
    size_t rnd_len = 32768;
    uint8_t *rnd = (uint8_t *)malloc(rnd_len);
    TEST_ASSERT(rnd != NULL);
    uint32_t rng = 0x12345678U;
    for (size_t i = 0; i < rnd_len; i++) {
        rng = rng * 1664525U + 1013904223U;
        rnd[i] = (uint8_t)(rng >> 24);
    }
    verify_roundtrip("incompressible", rnd, rnd_len);
    free(rnd);

    /* 4. Multi-block >4 MiB payload (5 MiB mixed compressible + random) */
    size_t big_len = 5U * 1024U * 1024U + 12345U;
    uint8_t *big = (uint8_t *)malloc(big_len);
    TEST_ASSERT(big != NULL);
    for (size_t i = 0; i < big_len; i++) {
        if ((i / 1024U) % 2U == 0U) {
            big[i] = (uint8_t)(i & 0x3FU);
        } else {
            rng = rng * 1664525U + 1013904223U;
            big[i] = (uint8_t)(rng >> 24);
        }
    }
    verify_roundtrip("multiblock_5mb", big, big_len);
    free(big);

    /* 5. Corruption & bounds rejection */
    test_corruption_cases();

    return 0;
}
