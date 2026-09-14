/*
 * Project Tsukasa — Vanilla Display Server Host SDL2 Backend Implementation
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

#include "sdl_backend.h"
#include "../server/server.h"
#include <sys/input.h>
#include <stdio.h>
#include <string.h>

static uint16_t sdl_scancode_to_evdev(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const uint16_t alpha_map[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
            KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
            KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
        };
        return alpha_map[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return (uint16_t)(KEY_1 + (sc - SDL_SCANCODE_1));
    if (sc == SDL_SCANCODE_0)
        return KEY_0;

    switch (sc) {
    case SDL_SCANCODE_ESCAPE:       return KEY_ESC;
    case SDL_SCANCODE_BACKSPACE:    return KEY_BACKSPACE;
    case SDL_SCANCODE_TAB:          return KEY_TAB;
    case SDL_SCANCODE_RETURN:       return KEY_ENTER;
    case SDL_SCANCODE_SPACE:        return KEY_SPACE;
    case SDL_SCANCODE_MINUS:        return KEY_MINUS;
    case SDL_SCANCODE_EQUALS:       return KEY_EQUAL;
    case SDL_SCANCODE_LEFTBRACKET:  return KEY_LEFTBRACE;
    case SDL_SCANCODE_RIGHTBRACKET: return KEY_RIGHTBRACE;
    case SDL_SCANCODE_BACKSLASH:    return KEY_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON:    return KEY_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE:   return KEY_APOSTROPHE;
    case SDL_SCANCODE_GRAVE:        return KEY_GRAVE;
    case SDL_SCANCODE_COMMA:        return KEY_COMMA;
    case SDL_SCANCODE_PERIOD:       return KEY_DOT;
    case SDL_SCANCODE_SLASH:        return KEY_SLASH;
    case SDL_SCANCODE_CAPSLOCK:     return KEY_CAPSLOCK;
    case SDL_SCANCODE_LCTRL:        return KEY_LEFTCTRL;
    case SDL_SCANCODE_LSHIFT:       return KEY_LEFTSHIFT;
    case SDL_SCANCODE_LALT:         return KEY_LEFTALT;
    case SDL_SCANCODE_RSHIFT:       return KEY_RIGHTSHIFT;
    case SDL_SCANCODE_UP:           return KEY_UP;
    case SDL_SCANCODE_DOWN:         return KEY_DOWN;
    case SDL_SCANCODE_LEFT:         return KEY_LEFT;
    case SDL_SCANCODE_RIGHT:        return KEY_RIGHT;
    default:                        return 0;
    }
}

int sdl_backend_init(vanilla_sdl_backend_t *backend, uint32_t width, uint32_t height, const char *title)
{
    if (!backend || width == 0 || height == 0)
        return -1;

    memset(backend, 0, sizeof(*backend));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        printf("[SDL Backend] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    backend->window = SDL_CreateWindow(title ? title : "Project Vanilla",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       (int)width, (int)height,
                                       SDL_WINDOW_SHOWN);
    if (!backend->window) {
        printf("[SDL Backend] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    backend->renderer = SDL_CreateRenderer(backend->window, -1,
                                           SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!backend->renderer) {
        backend->renderer = SDL_CreateRenderer(backend->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!backend->renderer) {
        printf("[SDL Backend] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(backend->window);
        SDL_Quit();
        return -1;
    }

    backend->texture = SDL_CreateTexture(backend->renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         (int)width, (int)height);
    if (!backend->texture) {
        printf("[SDL Backend] SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(backend->renderer);
        SDL_DestroyWindow(backend->window);
        SDL_Quit();
        return -1;
    }

    SDL_ShowCursor(SDL_DISABLE);

    backend->width = width;
    backend->height = height;
    backend->pitch_px = width;
    backend->running = 1;

    return 0;
}

void sdl_backend_destroy(vanilla_sdl_backend_t *backend)
{
    if (!backend)
        return;

    if (backend->texture) {
        SDL_DestroyTexture(backend->texture);
        backend->texture = NULL;
    }
    if (backend->renderer) {
        SDL_DestroyRenderer(backend->renderer);
        backend->renderer = NULL;
    }
    if (backend->window) {
        SDL_DestroyWindow(backend->window);
        backend->window = NULL;
    }

    SDL_Quit();
    backend->running = 0;
}

void sdl_backend_present(vanilla_sdl_backend_t *backend, const uint32_t *backbuffer,
                         const vanilla_rect_t *dirty_rects, int dirty_count)
{
    if (!backend || !backend->renderer || !backend->texture || !backbuffer)
        return;

    if (dirty_count > 0 && dirty_rects) {
        for (int i = 0; i < dirty_count; i++) {
            const vanilla_rect_t *dr = &dirty_rects[i];
            int32_t rx = dr->x;
            int32_t ry = dr->y;
            int32_t rw = dr->w;
            int32_t rh = dr->h;
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > (int32_t)backend->width) rw = (int32_t)backend->width - rx;
            if (ry + rh > (int32_t)backend->height) rh = (int32_t)backend->height - ry;
            if (rw <= 0 || rh <= 0)
                continue;

            SDL_Rect sr = { (int)rx, (int)ry, (int)rw, (int)rh };
            const uint32_t *src_pixels = backbuffer + (size_t)ry * backend->pitch_px + rx;
            SDL_UpdateTexture(backend->texture, &sr, src_pixels, (int)(backend->pitch_px * sizeof(uint32_t)));
        }
    } else {
        SDL_UpdateTexture(backend->texture, NULL, backbuffer, (int)(backend->pitch_px * sizeof(uint32_t)));
    }

    SDL_RenderClear(backend->renderer);
    SDL_RenderCopy(backend->renderer, backend->texture, NULL, NULL);
    SDL_RenderPresent(backend->renderer);
}

int sdl_backend_poll_events(vanilla_sdl_backend_t *backend, vanilla_server_t *srv)
{
    SDL_Event event;

    if (!backend || !srv)
        return -1;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            backend->running = 0;
            srv->running = 0;
            return 0;

        case SDL_MOUSEMOTION: {
            int32_t old_x = srv->cursor_x;
            int32_t old_y = srv->cursor_y;

            srv->cursor_x = event.motion.x;
            srv->cursor_y = event.motion.y;

            if (srv->cursor_x < 0)
                srv->cursor_x = 0;
            if (srv->cursor_x >= (int32_t)backend->width)
                srv->cursor_x = (int32_t)backend->width - 1;
            if (srv->cursor_y < 0)
                srv->cursor_y = 0;
            if (srv->cursor_y >= (int32_t)backend->height)
                srv->cursor_y = (int32_t)backend->height - 1;

            if (srv->cursor_x != old_x || srv->cursor_y != old_y) {
                vanilla_rect_t old_box = { old_x, old_y, 16, 16 };
                vanilla_rect_t new_box = { srv->cursor_x, srv->cursor_y, 16, 16 };
                compositor_add_damage(&srv->compositor, &old_box);
                compositor_add_damage(&srv->compositor, &new_box);

                if (srv->is_dragging) {
                    vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->drag_window_id);
                    if (w) {
                        wm_invalidate_window(srv, w);
                        w->x = srv->cursor_x - srv->drag_offset_x;
                        w->y = srv->cursor_y - srv->drag_offset_y;
                        wm_invalidate_window(srv, w);
                    }
                }
            }
            break;
        }

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int pressed = (event.type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
            uint16_t code = 0;

            if (event.button.button == SDL_BUTTON_LEFT)
                code = BTN_LEFT;
            else if (event.button.button == SDL_BUTTON_RIGHT)
                code = BTN_RIGHT;
            else if (event.button.button == SDL_BUTTON_MIDDLE)
                code = BTN_MIDDLE;

            if (code != 0) {
                struct input_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EV_KEY;
                ev.code = code;
                ev.value = pressed;
                wm_handle_input_event(srv, &ev);
            }
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            int pressed = (event.type == SDL_KEYDOWN) ? 1 : 0;
            uint16_t code = sdl_scancode_to_evdev(event.key.keysym.scancode);

            if (code != 0) {
                struct input_event ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = EV_KEY;
                ev.code = code;
                ev.value = pressed;
                wm_handle_input_event(srv, &ev);
            }
            break;
        }

        default:
            break;
        }
    }

    return 0;
}
