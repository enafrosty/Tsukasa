/*
 * Project Tsukasa — Minimal true-color BMP parser and wallpaper renderer
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

#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stddef.h>

/* Load a BMP from the VFS, scale it to the framebuffer dimensions (nearest-neighbor), and blit it directly... */
int bmp_draw_wallpaper(const char *vfs_path);

/* Load a BMP from the VFS into a heap-allocated pixel buffer. */
int bmp_load_to_buf(const char *vfs_path,
                    uint32_t **out_pixels,
                    int *out_w, int *out_h);

#endif /* BMP_H */
