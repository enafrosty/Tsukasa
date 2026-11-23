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

#ifndef TSUKASA_LZ4_H
#define TSUKASA_LZ4_H

#include <stddef.h>
#include <stdint.h>

#define LZ4_FRAME_MAGIC       0x184D2204U
#define LZ4_OK                0
#define LZ4_ERR_ARGS          (-1)
#define LZ4_ERR_MAGIC         (-2)
#define LZ4_ERR_UNSUPPORTED   (-3)
#define LZ4_ERR_HEADER_CRC    (-4)
#define LZ4_ERR_TRUNCATED     (-5)
#define LZ4_ERR_CORRUPT       (-6)
#define LZ4_ERR_BLOCK_CRC     (-7)
#define LZ4_ERR_CONTENT_CRC   (-8)
#define LZ4_ERR_SIZE          (-9)
#define LZ4_ERR_OVERFLOW      (-10)

/* Compute xxHash32 over `data` of length `len` with `seed`. */
uint32_t lz4_xxh32(const void *data, size_t len, uint32_t seed);

/* Validate an LZ4 frame header and extract metadata. */
int lz4_frame_info(const void *src, size_t src_len,
                   uint64_t *content_size_out, int *has_content_size_out,
                   size_t *max_block_size_out);

/* Decompress a standard LZ4 frame from `src` into `dst` (capacity `dst_cap`). */
int lz4_decompress(const void *src, size_t src_len,
                   void *dst, size_t dst_cap, size_t *out_len);

/* Return a short diagnostic string for an LZ4_ERR_* code. */
const char *lz4_strerror(int err);

#endif /* TSUKASA_LZ4_H */
