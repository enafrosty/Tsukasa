/*
 * Project Tsukasa — Project Vanilla System Settings & About
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

#include "app_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SETTINGS_WIDTH   520
#define SETTINGS_HEIGHT  360
#define TAB_HEIGHT       36

typedef struct {
    int  current_tab;
    char mem_info[64];
    int  dirty;
} settings_state_t;

static unsigned long read_key_val(const char *filename, const char *key)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[256];
    size_t klen = strlen(key);
    unsigned long val = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            val = strtoul(p, NULL, 10);
            break;
        }
    }
    fclose(fp);
    return val;
}

static void settings_render(vanilla_surface_t *surf, settings_state_t *st)
{
    app_fill_rect(surf, 0, 0, SETTINGS_WIDTH, SETTINGS_HEIGHT, APP_COLOR_BG);

    /* Top Tabs Header */
    app_fill_rect(surf, 0, 0, SETTINGS_WIDTH, TAB_HEIGHT, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, TAB_HEIGHT - 1, SETTINGS_WIDTH, 1, APP_COLOR_BORDER);

    /* Tab buttons */
    app_draw_button(surf, 12, 6, 110, 24, "System Info", st->current_tab == 0);
    app_draw_button(surf, 130, 6, 110, 24, "Appearance", st->current_tab == 1);

    int content_y = TAB_HEIGHT + 16;

    if (st->current_tab == 0) {
        /* Section card 1: OS Information */
        app_fill_rect(surf, 16, content_y, SETTINGS_WIDTH - 32, 120, APP_COLOR_CARD);
        app_draw_rect(surf, 16, content_y, SETTINGS_WIDTH - 32, 120, APP_COLOR_BORDER);

        app_draw_text(surf, 28, content_y + 12, "Operating System", APP_COLOR_PRIMARY);
        app_draw_text(surf, 28, content_y + 34, "Distribution:  Project Tsukasa OS (x86_64 Long Mode)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, content_y + 54, "Architecture:  x86_64 (SMP, SSE2, Ring 3 Isolated)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, content_y + 74, "Display Server: Project Vanilla 1.0 (Zero-Copy SHM)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, content_y + 94, "Kernel ABI:    SPEC-C01 Linux-Compatible Fast Syscalls", APP_COLOR_TEXT);

        /* Section card 2: Hardware & Memory */
        int card2_y = content_y + 136;
        app_fill_rect(surf, 16, card2_y, SETTINGS_WIDTH - 32, 120, APP_COLOR_CARD);
        app_draw_rect(surf, 16, card2_y, SETTINGS_WIDTH - 32, 120, APP_COLOR_BORDER);

        app_draw_text(surf, 28, card2_y + 12, "Hardware & Resources", APP_COLOR_PRIMARY);

        unsigned long total_pages = read_key_val("/sys/memory", "pmm_total_pages");
        unsigned long used_pages = read_key_val("/sys/memory", "pmm_used_pages");
        unsigned long free_pages = read_key_val("/sys/memory", "pmm_free_pages");

        char mem_line[80];
        if (total_pages > 0) {
            snprintf(mem_line, sizeof(mem_line), "Physical Memory: %lu MB Total, %lu MB Used, %lu MB Free",
                     (total_pages * 4096UL) / (1024 * 1024),
                     (used_pages * 4096UL) / (1024 * 1024),
                     (free_pages * 4096UL) / (1024 * 1024));
        } else {
            strcpy(mem_line, "Physical Memory: 256 MB Total (PMM Monitored)");
        }

        app_draw_text(surf, 28, card2_y + 34, mem_line, APP_COLOR_TEXT);
        app_draw_text(surf, 28, card2_y + 54, "Graphics Output: /dev/fb0 (32 bpp ARGB32 VESA/GOP)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, card2_y + 74, "Input Device:    /dev/input/events (Unified Evdev)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, card2_y + 94, "Network NIC:     Intel 8254x / VirtIO-Net Gigabit", APP_COLOR_TEXT);
    } else {
        /* Appearance Settings */
        app_fill_rect(surf, 16, content_y, SETTINGS_WIDTH - 32, 240, APP_COLOR_CARD);
        app_draw_rect(surf, 16, content_y, SETTINGS_WIDTH - 32, 240, APP_COLOR_BORDER);

        app_draw_text(surf, 28, content_y + 16, "Desktop Theme", APP_COLOR_PRIMARY);
        app_draw_text(surf, 28, content_y + 40, "Current Theme: Nord Polar Night (Dark Mode)", APP_COLOR_TEXT);
        app_draw_text(surf, 28, content_y + 60, "Wallpaper:     /assets/wallpaper.bmp", APP_COLOR_TEXT);
        app_draw_text(surf, 28, content_y + 80, "Font Family:   Roboto Medium / Bitmap 8x8", APP_COLOR_TEXT);

        app_draw_text(surf, 28, content_y + 120, "Accent Color Swatches:", APP_COLOR_MUTED);

        /* Color Swatches */
        app_fill_rect(surf, 28, content_y + 146, 44, 28, APP_COLOR_PRIMARY);
        app_draw_rect(surf, 28, content_y + 146, 44, 28, APP_COLOR_WHITE);

        app_fill_rect(surf, 84, content_y + 146, 44, 28, APP_COLOR_ACCENT);
        app_draw_rect(surf, 84, content_y + 146, 44, 28, APP_COLOR_BORDER);

        app_fill_rect(surf, 140, content_y + 146, 44, 28, APP_COLOR_SUCCESS);
        app_draw_rect(surf, 140, content_y + 146, 44, 28, APP_COLOR_BORDER);

        app_fill_rect(surf, 196, content_y + 146, 44, 28, APP_COLOR_WARNING);
        app_draw_rect(surf, 196, content_y + 146, 44, 28, APP_COLOR_BORDER);

        app_draw_text(surf, 28, content_y + 196, "Press Tab or Left/Right arrows to toggle sections.", APP_COLOR_DIM);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    settings_state_t state;
    memset(&state, 0, sizeof(state));
    state.current_tab = 0;
    state.dirty = 1;

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) {
        fprintf(stderr, "settings: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Settings", 180, 110,
                                                  SETTINGS_WIDTH, SETTINGS_HEIGHT,
                                                  WINDOW_FLAG_NONE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(win);

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY && iev->value == 1) {
                    if (iev->code == BTN_LEFT) {
                        int cx = (int)iev->pad1;
                        int cy = (int)iev->pad2;
                        if (cy >= 6 && cy < 30) {
                            if (cx >= 12 && cx < 122) {
                                state.current_tab = 0;
                                state.dirty = 1;
                            } else if (cx >= 130 && cx < 240) {
                                state.current_tab = 1;
                                state.dirty = 1;
                            }
                        }
                    } else if (iev->code == KEY_TAB || iev->code == KEY_LEFT || iev->code == KEY_RIGHT) {
                        state.current_tab = (state.current_tab == 0) ? 1 : 0;
                        state.dirty = 1;
                    }
                }
            }
        }

        if (state.dirty) {
            settings_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(20000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
