/*
 * Project Tsukasa - LZ4 Frame Format Decompressor
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

#include "lz4.h"

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

static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint64_t read_le64(const uint8_t *p)
{
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

uint32_t lz4_xxh32(const void *data, size_t len, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = len ? (p + len) : p;
    uint32_t h32;

    if (len == 0) {
        h32 = seed + XXH32_PRIME5;
        h32 ^= h32 >> 15;
        h32 *= XXH32_PRIME2;
        h32 ^= h32 >> 13;
        h32 *= XXH32_PRIME3;
        h32 ^= h32 >> 16;
        return h32;
    }

    if (len >= 16) {
        const uint8_t *limit = end - 16;
        uint32_t v1 = seed + XXH32_PRIME1 + XXH32_PRIME2;
        uint32_t v2 = seed + XXH32_PRIME2;
        uint32_t v3 = seed + 0U;
        uint32_t v4 = seed - XXH32_PRIME1;

        while (p <= limit) {
            v1 = rotl32(v1 + read_le32(p) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v2 = rotl32(v2 + read_le32(p + 4) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v3 = rotl32(v3 + read_le32(p + 8) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            v4 = rotl32(v4 + read_le32(p + 12) * XXH32_PRIME2, 13) * XXH32_PRIME1;
            p += 16;
        }
        h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    } else {
        h32 = seed + XXH32_PRIME5;
    }

    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        h32 += read_le32(p) * XXH32_PRIME3;
        h32 = rotl32(h32, 17) * XXH32_PRIME4;
        p += 4;
    }

    while (p < end) {
        h32 += (uint32_t)(*p) * XXH32_PRIME5;
        h32 = rotl32(h32, 11) * XXH32_PRIME1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= XXH32_PRIME2;
    h32 ^= h32 >> 13;
    h32 *= XXH32_PRIME3;
    h32 ^= h32 >> 16;

    return h32;
}

typedef struct lz4_frame_hdr {
    int      block_indep;
    int      block_crc;
    int      has_content_size;
    int      content_crc;
    uint64_t content_size;
    size_t   max_block_size;
    size_t   payload_offset;
} lz4_frame_hdr_t;

static int parse_frame_header(const uint8_t *src, size_t src_len,
                              lz4_frame_hdr_t *hdr)
{
    uint32_t magic;
    uint8_t flg, bd, hc, expected_hc;
    size_t pos = 0;
    size_t desc_start;
    uint8_t max_blk_id;

    if (!src || !hdr)
        return LZ4_ERR_ARGS;
    if (src_len < 7)
        return LZ4_ERR_TRUNCATED;

    magic = read_le32(src);
    if (magic != LZ4_FRAME_MAGIC)
        return LZ4_ERR_MAGIC;

    pos = 4;
    desc_start = pos;
    flg = src[pos++];
    bd = src[pos++];

    if (((flg >> 6) & 0x03U) != 1U)
        return LZ4_ERR_UNSUPPORTED;
    if ((flg & 0x02U) != 0U)
        return LZ4_ERR_CORRUPT;
    if ((flg & 0x01U) != 0U)
        return LZ4_ERR_UNSUPPORTED; /* Dictionary ID not supported */
    if ((bd & 0x8FU) != 0U)
        return LZ4_ERR_CORRUPT;

    max_blk_id = (bd >> 4) & 0x07U;
    switch (max_blk_id) {
    case 4: hdr->max_block_size = 64U * 1024U; break;
    case 5: hdr->max_block_size = 256U * 1024U; break;
    case 6: hdr->max_block_size = 1024U * 1024U; break;
    case 7: hdr->max_block_size = 4U * 1024U * 1024U; break;
    default: return LZ4_ERR_UNSUPPORTED;
    }

    hdr->block_indep = (flg & 0x20U) ? 1 : 0;
    hdr->block_crc = (flg & 0x10U) ? 1 : 0;
    hdr->has_content_size = (flg & 0x08U) ? 1 : 0;
    hdr->content_crc = (flg & 0x04U) ? 1 : 0;
    hdr->content_size = 0;

    if (hdr->has_content_size) {
        if (pos + 8 + 1 > src_len)
            return LZ4_ERR_TRUNCATED;
        hdr->content_size = read_le64(src + pos);
        pos += 8;
    } else {
        if (pos + 1 > src_len)
            return LZ4_ERR_TRUNCATED;
    }

    expected_hc = (uint8_t)((lz4_xxh32(src + desc_start, pos - desc_start, 0U) >> 8) & 0xFFU);
    hc = src[pos++];
    if (hc != expected_hc)
        return LZ4_ERR_HEADER_CRC;

    hdr->payload_offset = pos;
    return LZ4_OK;
}

int lz4_frame_info(const void *src, size_t src_len,
                   uint64_t *content_size_out, int *has_content_size_out,
                   size_t *max_block_size_out)
{
    lz4_frame_hdr_t hdr;
    int rc = parse_frame_header((const uint8_t *)src, src_len, &hdr);
    if (rc != LZ4_OK)
        return rc;
    if (content_size_out)
        *content_size_out = hdr.content_size;
    if (has_content_size_out)
        *has_content_size_out = hdr.has_content_size;
    if (max_block_size_out)
        *max_block_size_out = hdr.max_block_size;
    return LZ4_OK;
}

static int decompress_block(const uint8_t *src, size_t src_len,
                            uint8_t *dst_base, size_t dict_offset,
                            size_t out_start, size_t dst_cap,
                            size_t *out_end)
{
    size_t ip = 0;
    size_t op = out_start;

    while (ip < src_len) {
        uint8_t token = src[ip++];
        size_t lit_len = (size_t)(token >> 4);
        size_t match_len;
        size_t offset;
        size_t match_src;

        if (lit_len == 15U) {
            uint8_t s;
            do {
                if (ip >= src_len)
                    return LZ4_ERR_CORRUPT;
                s = src[ip++];
                if (lit_len > SIZE_MAX - s)
                    return LZ4_ERR_CORRUPT;
                lit_len += s;
            } while (s == 255U);
        }

        if (lit_len > src_len - ip)
            return LZ4_ERR_CORRUPT;
        if (lit_len > dst_cap - op)
            return LZ4_ERR_OVERFLOW;

        for (size_t i = 0; i < lit_len; i++)
            dst_base[op++] = src[ip++];

        if (ip == src_len)
            break;

        if (ip + 2 > src_len)
            return LZ4_ERR_CORRUPT;
        offset = (size_t)read_le16(src + ip);
        ip += 2;

        if (offset == 0 || offset > op - dict_offset)
            return LZ4_ERR_CORRUPT;

        match_len = (size_t)(token & 0x0FU) + 4U;
        if ((token & 0x0FU) == 15U) {
            uint8_t s;
            do {
                if (ip >= src_len)
                    return LZ4_ERR_CORRUPT;
                s = src[ip++];
                if (match_len > SIZE_MAX - s)
                    return LZ4_ERR_CORRUPT;
                match_len += s;
            } while (s == 255U);
        }

        if (match_len > dst_cap - op)
            return LZ4_ERR_OVERFLOW;

        match_src = op - offset;
        for (size_t i = 0; i < match_len; i++)
            dst_base[op++] = dst_base[match_src + i];
    }

    *out_end = op;
    return LZ4_OK;
}

int lz4_decompress(const void *src, size_t src_len,
                   void *dst, size_t dst_cap, size_t *out_len)
{
    const uint8_t *in = (const uint8_t *)src;
    uint8_t *out = (uint8_t *)dst;
    lz4_frame_hdr_t hdr;
    size_t ip;
    size_t op = 0;
    int rc;

    if (!in || (!out && dst_cap > 0))
        return LZ4_ERR_ARGS;

    rc = parse_frame_header(in, src_len, &hdr);
    if (rc != LZ4_OK)
        return rc;

    if (hdr.has_content_size && hdr.content_size > (uint64_t)dst_cap)
        return LZ4_ERR_OVERFLOW;

    ip = hdr.payload_offset;

    for (;;) {
        uint32_t blk_hdr;
        uint32_t blk_size;
        int is_uncompressed;

        if (ip + 4 > src_len)
            return LZ4_ERR_TRUNCATED;

        blk_hdr = read_le32(in + ip);
        ip += 4;

        if (blk_hdr == 0U)
            break;

        is_uncompressed = (blk_hdr & 0x80000000U) ? 1 : 0;
        blk_size = blk_hdr & 0x7FFFFFFFU;

        if (blk_size > hdr.max_block_size)
            return LZ4_ERR_CORRUPT;
        if (blk_size > src_len - ip)
            return LZ4_ERR_TRUNCATED;

        if (hdr.block_crc) {
            if (ip + blk_size + 4 > src_len)
                return LZ4_ERR_TRUNCATED;
            uint32_t expected = read_le32(in + ip + blk_size);
            uint32_t actual = lz4_xxh32(in + ip, blk_size, 0U);
            if (actual != expected)
                return LZ4_ERR_BLOCK_CRC;
        }

        if (is_uncompressed) {
            if (blk_size > dst_cap - op)
                return LZ4_ERR_OVERFLOW;
            for (uint32_t i = 0; i < blk_size; i++)
                out[op++] = in[ip + i];
        } else {
            size_t dict_off = hdr.block_indep ? op : 0U;
            size_t new_op = op;
            rc = decompress_block(in + ip, blk_size, out, dict_off, op, dst_cap, &new_op);
            if (rc != LZ4_OK)
                return rc;
            op = new_op;
        }

        ip += blk_size;
        if (hdr.block_crc)
            ip += 4;
    }

    if (hdr.content_crc) {
        if (ip + 4 > src_len)
            return LZ4_ERR_TRUNCATED;
        uint32_t expected = read_le32(in + ip);
        uint32_t actual = lz4_xxh32(out, op, 0U);
        if (actual != expected)
            return LZ4_ERR_CONTENT_CRC;
        ip += 4;
    }

    if (hdr.has_content_size && (uint64_t)op != hdr.content_size)
        return LZ4_ERR_SIZE;

    if (out_len)
        *out_len = op;
    return LZ4_OK;
}

const char *lz4_strerror(int err)
{
    switch (err) {
    case LZ4_OK:              return "ok";
    case LZ4_ERR_ARGS:        return "invalid arguments";
    case LZ4_ERR_MAGIC:       return "invalid LZ4 frame magic";
    case LZ4_ERR_UNSUPPORTED: return "unsupported LZ4 frame flags";
    case LZ4_ERR_HEADER_CRC:  return "LZ4 header checksum mismatch";
    case LZ4_ERR_TRUNCATED:   return "truncated LZ4 stream";
    case LZ4_ERR_CORRUPT:     return "corrupt LZ4 block data";
    case LZ4_ERR_BLOCK_CRC:   return "LZ4 block checksum mismatch";
    case LZ4_ERR_CONTENT_CRC: return "LZ4 content checksum mismatch";
    case LZ4_ERR_SIZE:        return "LZ4 content size mismatch";
    case LZ4_ERR_OVERFLOW:    return "LZ4 output buffer too small";
    default:                  return "unknown LZ4 error";
    }
}
