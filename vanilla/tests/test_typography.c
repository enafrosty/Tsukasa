/*
 * Project Tsukasa — Vector Typography & Image Pipeline Test Suite
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

#include "../server/font.h"
#include "../server/image.h"
#include "../server/blitter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            return -1; \
        } \
    } while (0)

static int test_font_init_and_glyph(void)
{
    vanilla_font_t font;
    int rc = font_init(&font, NULL, 0);
    TEST_ASSERT(rc == 0, "font_init failed with embedded TTF");
    TEST_ASSERT(font.info != NULL, "font.info is NULL");
    TEST_ASSERT(font.ascent > 0, "font ascent <= 0");
    TEST_ASSERT(font.descent < 0, "font descent >= 0");

    /* Rasterize glyph 'A' at 16px */
    const vanilla_glyph_t *g = font_get_glyph(&font, 'A', 16.0f);
    TEST_ASSERT(g != NULL, "font_get_glyph('A') returned NULL");
    TEST_ASSERT(g->bitmap != NULL, "glyph bitmap is NULL");
    TEST_ASSERT(g->width > 0 && g->height > 0, "glyph dimensions are non-positive");
    TEST_ASSERT(g->xadvance > 0, "glyph advance is non-positive");
    TEST_ASSERT(g->pixel_height == 16.0f, "glyph pixel_height mismatch");

    font_destroy(&font);
    TEST_ASSERT(font.info == NULL, "font.info not NULL after destroy");
    TEST_ASSERT(font.glyph_count == 0, "glyph_count != 0 after destroy");
    return 0;
}

static int test_lru_glyph_cache(void)
{
    vanilla_font_t font;
    int rc = font_init(&font, NULL, 0);
    TEST_ASSERT(rc == 0, "font_init failed");

    /* Request 260 distinct glyphs to exceed MAX_CACHED_GLYPHS (256) */
    for (uint32_t cp = 32; cp < 32 + 260; cp++) {
        const vanilla_glyph_t *g = font_get_glyph(&font, cp, 14.0f);
        TEST_ASSERT(g != NULL, "failed to rasterize glyph during cache fill");
    }

    TEST_ASSERT(font.glyph_count == MAX_CACHED_GLYPHS, "cache count exceeded capacity");

    /* Re-request codepoint 32: should trigger re-rasterization or hit */
    uint32_t tick_before = font.current_tick;
    const vanilla_glyph_t *g = font_get_glyph(&font, 32, 14.0f);
    TEST_ASSERT(g != NULL, "failed to re-fetch evicted glyph");
    TEST_ASSERT(g->last_used_tick > tick_before, "LRU tick was not updated");

    font_destroy(&font);
    return 0;
}

static int test_font_measure_text(void)
{
    vanilla_font_t font;
    int rc = font_init(&font, NULL, 0);
    TEST_ASSERT(rc == 0, "font_init failed");

    int32_t w16 = 0, h16 = 0;
    font_measure_text(&font, "Project Tsukasa", 16.0f, &w16, &h16);
    TEST_ASSERT(w16 > 0 && h16 > 0, "16px measurement returned zero");

    int32_t w24 = 0, h24 = 0;
    font_measure_text(&font, "Project Tsukasa", 24.0f, &w24, &h24);
    TEST_ASSERT(w24 > w16, "24px width must exceed 16px width");
    TEST_ASSERT(h24 > h16, "24px height must exceed 16px height");

    int32_t w_empty = 999, h_empty = 999;
    font_measure_text(&font, "", 16.0f, &w_empty, &h_empty);
    TEST_ASSERT(w_empty == 0, "empty text width != 0");

    font_destroy(&font);
    return 0;
}

static int test_font_alpha_blending(void)
{
    vanilla_font_t font;
    int rc = font_init(&font, NULL, 0);
    TEST_ASSERT(rc == 0, "font_init failed");

    const int32_t buf_w = 160;
    const int32_t buf_h = 40;
    size_t sz = (size_t)buf_w * buf_h * sizeof(uint32_t);
    uint32_t *fb = (uint32_t *)malloc(sz);
    TEST_ASSERT(fb != NULL, "malloc fb failed");

    memset(fb, 0, sz);

    vanilla_rect_t clip = { 0, 0, buf_w, buf_h };
    font_draw_text(fb, buf_w, &clip, &font, "Tsukasa", 10, 10, 18.0f, 0xFFFFFFFF);

    int non_zero = 0;
    int anti_aliased = 0;

    for (int32_t i = 0; i < buf_w * buf_h; i++) {
        uint32_t a = (fb[i] >> 24) & 0xFF;
        if (a > 0)
            non_zero++;
        if (a > 0 && a < 255)
            anti_aliased++;
    }

    TEST_ASSERT(non_zero > 0, "no pixels drawn for text");
    TEST_ASSERT(anti_aliased > 0, "no anti-aliased sub-pixel falloff detected");

    free(fb);
    font_destroy(&font);
    return 0;
}

static int test_image_decode_and_blt(void)
{
    /* Construct minimal 2x2 top-down 32-bit BMP */
    #pragma pack(push, 1)
    struct {
        uint16_t bfType;
        uint32_t bfSize;
        uint16_t bfRes1;
        uint16_t bfRes2;
        uint32_t bfOffBits;
        uint32_t biSize;
        int32_t  biWidth;
        int32_t  biHeight;
        uint16_t biPlanes;
        uint16_t biBitCount;
        uint32_t biCompression;
        uint32_t biSizeImage;
        int32_t  biXPelsPerMeter;
        int32_t  biYPelsPerMeter;
        uint32_t biClrUsed;
        uint32_t biClrImportant;
        uint8_t  pixels[16];
    } bmp;
    #pragma pack(pop)

    memset(&bmp, 0, sizeof(bmp));
    bmp.bfType = 0x4D42; /* 'BM' */
    bmp.bfSize = sizeof(bmp);
    bmp.bfOffBits = 54;
    bmp.biSize = 40;
    bmp.biWidth = 2;
    bmp.biHeight = -2; /* top-down */
    bmp.biPlanes = 1;
    bmp.biBitCount = 32;
    bmp.biSizeImage = 16;

    /* Pixel (0,0): Red (BGRA: 0, 0, 255, 255) */
    bmp.pixels[0] = 0;   bmp.pixels[1] = 0;   bmp.pixels[2] = 255; bmp.pixels[3] = 255;
    /* Pixel (1,0): Green (BGRA: 0, 255, 0, 255) */
    bmp.pixels[4] = 0;   bmp.pixels[5] = 255; bmp.pixels[6] = 0;   bmp.pixels[7] = 255;
    /* Pixel (0,1): Blue (BGRA: 255, 0, 0, 255) */
    bmp.pixels[8] = 255; bmp.pixels[9] = 0;   bmp.pixels[10] = 0;  bmp.pixels[11] = 255;
    /* Pixel (1,1): White (BGRA: 255, 255, 255, 255) */
    bmp.pixels[12] = 255; bmp.pixels[13] = 255; bmp.pixels[14] = 255; bmp.pixels[15] = 255;

    vanilla_image_t img;
    int rc = image_load_memory(&img, &bmp, sizeof(bmp));
    TEST_ASSERT(rc == 0, "image_load_memory failed");
    TEST_ASSERT(img.width == 2 && img.height == 2, "image dimensions mismatch");
    TEST_ASSERT(img.pixels != NULL, "img.pixels is NULL");

    /* Verify ARGB32 decoded values */
    TEST_ASSERT(img.pixels[0] == 0xFFFF0000, "pixel (0,0) red mismatch");
    TEST_ASSERT(img.pixels[1] == 0xFF00FF00, "pixel (1,0) green mismatch");
    TEST_ASSERT(img.pixels[2] == 0xFF0000FF, "pixel (0,1) blue mismatch");
    TEST_ASSERT(img.pixels[3] == 0xFFFFFFFF, "pixel (1,1) white mismatch");

    /* Test scaled image drawing into an 8x8 buffer */
    uint32_t dst[64];
    memset(dst, 0, sizeof(dst));

    vanilla_rect_t dst_r = { 0, 0, 4, 4 };
    image_draw_scaled(dst, 8, NULL, &img, &dst_r);

    /* (0,0) should be Red, (2,0) should be Green, (0,2) Blue, (2,2) White */
    TEST_ASSERT(dst[0] == 0xFFFF0000, "scaled pixel (0,0) red mismatch");
    TEST_ASSERT(dst[2] == 0xFF00FF00, "scaled pixel (2,0) green mismatch");
    TEST_ASSERT(dst[2 * 8] == 0xFF0000FF, "scaled pixel (0,2) blue mismatch");
    TEST_ASSERT(dst[2 * 8 + 2] == 0xFFFFFFFF, "scaled pixel (2,2) white mismatch");

    image_destroy(&img);
    TEST_ASSERT(img.pixels == NULL, "img.pixels not NULL after destroy");
    return 0;
}

int main(void)
{
    printf("========================================================\n");
    printf(" Project Vanilla - Phase 4, Step 4.3 Typography & Images\n");
    printf("========================================================\n");

    if (test_font_init_and_glyph() != 0)
        return 1;
    printf("[PASS] 1. Font initialization and vector glyph rasterization\n");

    if (test_lru_glyph_cache() != 0)
        return 1;
    printf("[PASS] 2. LRU glyph cache eviction and retrieval\n");

    if (test_font_measure_text() != 0)
        return 1;
    printf("[PASS] 3. Font text measurement and vertical metrics\n");

    if (test_font_alpha_blending() != 0)
        return 1;
    printf("[PASS] 4. Anti-aliased font alpha blending onto ARGB buffer\n");

    if (test_image_decode_and_blt() != 0)
        return 1;
    printf("[PASS] 5. Image memory decoding and scaled blitter\n");

    printf("========================================================\n");
    printf("[OK] Step 4.3 Typography & Image Pipeline test PASSED.\n");
    printf("========================================================\n");
    return 0;
}
