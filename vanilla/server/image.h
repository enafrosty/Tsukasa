/*
 * Project Tsukasa — Multi-Format Image Loading Pipeline Header
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

#ifndef _VANILLA_IMAGE_H
#define _VANILLA_IMAGE_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"

typedef struct {
    uint32_t *pixels;
    int32_t   width;
    int32_t   height;
    int32_t   channels;
} vanilla_image_t;

/* Image lifecycle */
int  image_load_memory(vanilla_image_t *img, const void *data, size_t len);
int  image_load_file(vanilla_image_t *img, const char *path);
void image_destroy(vanilla_image_t *img);

/* Image blitting and scaling */
void image_draw(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                const vanilla_image_t *img, int32_t x, int32_t y);

void image_draw_scaled(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                       const vanilla_image_t *img, const vanilla_rect_t *dst_rect);

#endif /* _VANILLA_IMAGE_H */
