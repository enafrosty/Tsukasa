/*
 * Project Tsukasa - Host LZ4 Frame Packer (standard LZ4 frame format)
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LZ4_MAGIC       0x184D2204U
#define LZ4_BLOCK_MAX   (4U * 1024U * 1024U)
#define HASH_BITS       16
#define HASH_SIZE       (1U << HASH_BITS)
#define MIN_MATCH       4
#define LAST_LITERALS   5
#define MF_LIMIT        12

#define XXH32_PRIME1 0x9E3779B1U
#define XXH32_PRIME2 0x85EBCA77U
#define XXH32_PRIME3 0xC2B2AE3DU
#define XXH32_PRIME4 0x27D4EB2FU
#define XXH32_PRIME5 0x165667B1U

static inline uint32_t rotl32(uint32_t v, int shift)
{
    return (v << shift) | (v >> (32 - shift));
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static inline void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static inline void write_le64(uint8_t *p, uint64_t v)
{
    write_le32(p, (uint32_t)(v & 0xFFFFFFFFULL));
    write_le32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t xxh32(const uint8_t *p, size_t len, uint32_t seed)
{
    uint32_t h32;
    size_t i = 0;

    if (len >= 16) {
        uint32_t v1 = seed + XXH32_PRIME1 + XXH32_PRIME2;
        uint32_t v2 = seed + XXH32_PRIME2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - XXH32_PRIME1;

        while (i + 16 <= len) {
            v1 = rotl32(v1 + read_le32(p + i) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v2 = rotl32(v2 + read_le32(p + i + 4) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v3 = rotl32(v3 + read_le32(p + i + 8) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v4 = rotl32(v4 + read_le32(p + i + 12) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            i += 16;
        }
        h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    } else {
        h32 = seed + XXH32_PRIME5;
    }

    h32 += (uint32_t)len;

    while (i + 4 <= len) {
        h32 += read_le32(p + i) * XXH32_PRIME3;
        h32 = rotl32(h32, 17) * XXH32_PRIME4;
        i += 4;
    }

    while (i < len) {
        h32 += (uint32_t)p[i] * XXH32_PRIME5;
        h32 = rotl32(h32, 11) * XXH32_PRIME1;
        i++;
    }

    h32 ^= h32 >> 15;
    h32 *= XXH32_PRIME2;
    h32 ^= h32 >> 13;
    h32 *= XXH32_PRIME3;
    h32 ^= h32 >> 16;

    return h32;
}

static inline uint32_t hash4(const uint8_t *p)
{
    uint32_t v = read_le32(p);
    return (v * 2654435761U) >> (32 - HASH_BITS);
}

static size_t write_len_ext(uint8_t *dst, size_t op, size_t len)
{
    while (len >= 255U) {
        dst[op++] = 255U;
        len -= 255U;
    }
    dst[op++] = (uint8_t)len;
    return op;
}

/* Compress one block; returns compressed size, or 0 if incompressible. */
static size_t lz4_compress_block(const uint8_t *src, size_t src_len,
                                 uint8_t *dst, size_t dst_cap,
                                 uint32_t *htab)
{
    size_t ip = 0;
    size_t anchor = 0;
    size_t op = 0;
    size_t limit;

    if (src_len == 0)
        return 0;

    for (size_t i = 0; i < HASH_SIZE; i++)
        htab[i] = UINT32_MAX;

    if (src_len < MF_LIMIT)
        goto emit_last_literals;

    limit = src_len - MF_LIMIT;
    htab[hash4(src)] = 0;
    ip = 1;

    while (ip <= limit) {
        uint32_t h = hash4(src + ip);
        uint32_t ref_idx = htab[h];
        htab[h] = (uint32_t)ip;

        if (ref_idx == UINT32_MAX || (ip - ref_idx) > 65535U ||
            read_le32(src + ref_idx) != read_le32(src + ip)) {
            ip++;
            continue;
        }

        /* Extend match backwards within literals if possible */
        while (ip > anchor && ref_idx > 0 && src[ip - 1] == src[ref_idx - 1]) {
            ip--;
            ref_idx--;
        }

        size_t lit_len = ip - anchor;
        size_t match_start = ip;
        size_t m_ip = ip + MIN_MATCH;
        size_t m_ref = ref_idx + MIN_MATCH;
        size_t m_limit = src_len - LAST_LITERALS;

        while (m_ip < m_limit && src[m_ip] == src[m_ref]) {
            m_ip++;
            m_ref++;
        }

        size_t match_len = m_ip - match_start;
        size_t ml_code = match_len - MIN_MATCH;

        /* Worst-case check for output space */
        if (op + 1 + (lit_len / 255U) + 1 + lit_len + 2 + (ml_code / 255U) + 1 + 8 > dst_cap)
            return 0;

        uint8_t *tok = &dst[op++];
        *tok = (uint8_t)(((lit_len < 15U) ? lit_len : 15U) << 4) |
               (uint8_t)((ml_code < 15U) ? ml_code : 15U);

        if (lit_len >= 15U)
            op = write_len_ext(dst, op, lit_len - 15U);

        memcpy(dst + op, src + anchor, lit_len);
        op += lit_len;

        write_le16(dst + op, (uint16_t)(match_start - ref_idx));
        op += 2;

        if (ml_code >= 15U)
            op = write_len_ext(dst, op, ml_code - 15U);

        ip = m_ip;
        anchor = ip;
        if (ip <= limit)
            htab[hash4(src + ip - 2)] = (uint32_t)(ip - 2);
    }

emit_last_literals:
    {
        size_t last_lit = src_len - anchor;
        if (op + 1 + (last_lit / 255U) + 1 + last_lit > dst_cap)
            return 0;
        dst[op++] = (uint8_t)(((last_lit < 15U) ? last_lit : 15U) << 4);
        if (last_lit >= 15U)
            op = write_len_ext(dst, op, last_lit - 15U);
        memcpy(dst + op, src + anchor, last_lit);
        op += last_lit;
    }

    return (op < src_len) ? op : 0;
}

static int write_all(FILE *f, const void *buf, size_t len)
{
    return fwrite(buf, 1, len, f) == len ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *in_path = NULL;
    const char *out_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-')
            continue;
        if (!in_path)
            in_path = argv[i];
        else if (!out_path)
            out_path = argv[i];
    }

    if (!in_path || !out_path) {
        fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        perror("fopen input");
        return 1;
    }

    if (fseek(fin, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fin);
        return 1;
    }
    long fsz = ftell(fin);
    if (fsz < 0) {
        perror("ftell");
        fclose(fin);
        return 1;
    }
    rewind(fin);

    size_t in_size = (size_t)fsz;
    uint8_t *in_buf = in_size ? (uint8_t *)malloc(in_size) : NULL;
    if (in_size && !in_buf) {
        fprintf(stderr, "lz4pack: out of memory (%zu bytes)\n", in_size);
        fclose(fin);
        return 1;
    }
    if (in_size && fread(in_buf, 1, in_size, fin) != in_size) {
        fprintf(stderr, "lz4pack: failed to read %s\n", in_path);
        free(in_buf);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        perror("fopen output");
        free(in_buf);
        return 1;
    }

    /* Frame header: Magic(4) + FLG(1) + BD(1) + ContentSize(8) + HC(1) = 15 bytes */
    uint8_t hdr[15];
    write_le32(hdr, LZ4_MAGIC);
    hdr[4] = 0x7CU; /* Ver=01, B.Indep=1, B.Checksum=1, C.Size=1, C.Checksum=1 */
    hdr[5] = 0x70U; /* Max block size = 4 MiB */
    write_le64(hdr + 6, (uint64_t)in_size);
    hdr[14] = (uint8_t)((xxh32(hdr + 4, 10, 0U) >> 8) & 0xFFU);

    if (write_all(fout, hdr, sizeof(hdr)) != 0) {
        fprintf(stderr, "lz4pack: write header failed\n");
        free(in_buf);
        fclose(fout);
        return 1;
    }

    uint8_t *comp_buf = (uint8_t *)malloc(LZ4_BLOCK_MAX);
    uint32_t *htab = (uint32_t *)malloc(HASH_SIZE * sizeof(uint32_t));
    if (!comp_buf || !htab) {
        fprintf(stderr, "lz4pack: out of memory\n");
        free(comp_buf);
        free(htab);
        free(in_buf);
        fclose(fout);
        return 1;
    }

    size_t pos = 0;
    while (pos < in_size) {
        size_t chunk = in_size - pos;
        if (chunk > LZ4_BLOCK_MAX)
            chunk = LZ4_BLOCK_MAX;

        size_t csz = lz4_compress_block(in_buf + pos, chunk, comp_buf, chunk, htab);
        uint8_t bmeta[4];
        const uint8_t *bdata;
        uint32_t blen;

        if (csz > 0 && csz < chunk) {
            blen = (uint32_t)csz;
            write_le32(bmeta, blen);
            bdata = comp_buf;
        } else {
            blen = (uint32_t)chunk;
            write_le32(bmeta, blen | 0x80000000U);
            bdata = in_buf + pos;
        }

        uint32_t bcrc = xxh32(bdata, blen, 0U);
        uint8_t crc_bytes[4];
        write_le32(crc_bytes, bcrc);

        if (write_all(fout, bmeta, 4) != 0 ||
            write_all(fout, bdata, blen) != 0 ||
            write_all(fout, crc_bytes, 4) != 0) {
            fprintf(stderr, "lz4pack: write block failed\n");
            free(comp_buf);
            free(htab);
            free(in_buf);
            fclose(fout);
            return 1;
        }
        pos += chunk;
    }

    uint8_t end_and_crc[8];
    write_le32(end_and_crc, 0U);
    write_le32(end_and_crc + 4, xxh32(in_buf, in_size, 0U));
    if (write_all(fout, end_and_crc, sizeof(end_and_crc)) != 0) {
        fprintf(stderr, "lz4pack: write footer failed\n");
        free(comp_buf);
        free(htab);
        free(in_buf);
        fclose(fout);
        return 1;
    }

    free(comp_buf);
    free(htab);
    free(in_buf);
    if (fclose(fout) != 0) {
        perror("fclose");
        return 1;
    }
    return 0;
}
