/*
 * Project Tsukasa — Vanilla Display Server Host SDL2 Backend Header
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

#ifndef _VANILLA_SDL_BACKEND_H
#define _VANILLA_SDL_BACKEND_H

#include <SDL2/SDL.h>
#include <stdint.h>
#include "../include/surface.h"

struct vanilla_server;

typedef struct vanilla_sdl_backend {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    uint32_t      width;
    uint32_t      height;
    uint32_t      pitch_px;
    int           running;
} vanilla_sdl_backend_t;

int  sdl_backend_init(vanilla_sdl_backend_t *backend, uint32_t width, uint32_t height, const char *title);
void sdl_backend_destroy(vanilla_sdl_backend_t *backend);
void sdl_backend_present(vanilla_sdl_backend_t *backend, const uint32_t *backbuffer,
                         const vanilla_rect_t *dirty_rects, int dirty_count);
int  sdl_backend_poll_events(vanilla_sdl_backend_t *backend, struct vanilla_server *srv);

#endif /* _VANILLA_SDL_BACKEND_H */
