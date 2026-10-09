/*
 * Project Tsukasa — Display Server Theme Engine & INI Parser
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

#include "../include/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static vanilla_theme_t g_theme_mutable = {
    .bg_base = 0xFF2E3440u,
    .bg_elevated = 0xFF3B4252u,
    .bg_overlay = 0xCC1C2030u,
    .fg_primary = 0xFFECEFF4u,
    .fg_muted = 0xFFD8DEE9u,
    .fg_dim = 0xFF98A2B3u,
    .border = 0xFF4C566Au,
    .border_focus = 0xFF88C0D0u,
    .accent = 0xFF88C0D0u,
    .accent_hover = 0xFF81A1C1u,
    .accent_pressed = 0xFF5E81ACu,
    .selection = 0x4488C0D0u,
    .success = 0xFFA3BE8Cu,
    .warning = 0xFFEBCB8Bu,
    .danger = 0xFFBF616Au,
    .titlebar_active = 0xFF3B4252u,
    .titlebar_inactive = 0xFF2E3440u,
    .titlebar_text_active = 0xFFECEFF4u,
    .titlebar_text_inactive = 0xFFD8DEE9u,
    .titlebar_btn_bg = 0xFF4C566Au,
    .titlebar_btn_bg_hover = 0xFF5E6A80u,
    .titlebar_btn_icon = 0xFFECEFF4u,
    .taskbar_bg = 0xFF2E3440u,
    .taskbar_item_active = 0xFF88C0D0u,
    .taskbar_item_open = 0xFF4C566Au,
    .taskbar_text = 0xFFECEFF4u,
    .titlebar_height = 24,
    .border_width = 1,
    .btn_size = 12,
    .title_font_size = 13.0f,
    .radius_sm = 4,
    .radius_md = 8,
    .radius_lg = 12,
    .shadow_radius = 16,
    .shadow_alpha = 140,
    .space_1 = 4,
    .space_2 = 8,
    .space_4 = 16,
    .space_8 = 32,
    .scale_factor = 1.0f,
    .dur_fast = 50,
    .dur_base = 150,
    .dur_slow = 300,
    .ease_standard = { 0.4f, 0.0f, 0.2f, 1.0f },
    .ease_decelerate = { 0.0f, 0.0f, 0.2f, 1.0f },
    .ease_accelerate = { 0.4f, 0.0f, 1.0f, 1.0f },
    .reduce_motion = 0,
};

const vanilla_theme_t *g_theme = &g_theme_mutable;

static char s_last_path[256] = "/etc/vanilla/themes/nord-dark.ini";

static void theme_apply_defaults(vanilla_theme_t *t)
{
    if (!t)
        return;

    t->bg_base = 0xFF2E3440u;
    t->bg_elevated = 0xFF3B4252u;
    t->bg_overlay = 0xCC1C2030u;
    t->fg_primary = 0xFFECEFF4u;
    t->fg_muted = 0xFFD8DEE9u;
    t->fg_dim = 0xFF98A2B3u;
    t->border = 0xFF4C566Au;
    t->border_focus = 0xFF88C0D0u;
    t->accent = 0xFF88C0D0u;
    t->accent_hover = 0xFF81A1C1u;
    t->accent_pressed = 0xFF5E81ACu;
    t->selection = 0x4488C0D0u;
    t->success = 0xFFA3BE8Cu;
    t->warning = 0xFFEBCB8Bu;
    t->danger = 0xFFBF616Au;
    t->titlebar_active = 0xFF3B4252u;
    t->titlebar_inactive = 0xFF2E3440u;
    t->titlebar_text_active = 0xFFECEFF4u;
    t->titlebar_text_inactive = 0xFFD8DEE9u;
    t->titlebar_btn_bg = 0xFF4C566Au;
    t->titlebar_btn_bg_hover = 0xFF5E6A80u;
    t->titlebar_btn_icon = 0xFFECEFF4u;
    t->taskbar_bg = 0xFF2E3440u;
    t->taskbar_item_active = 0xFF88C0D0u;
    t->taskbar_item_open = 0xFF4C566Au;
    t->taskbar_text = 0xFFECEFF4u;

    t->titlebar_height = 24;
    t->border_width = 1;
    t->btn_size = 12;
    t->title_font_size = 13.0f;
    t->radius_sm = 4;
    t->radius_md = 8;
    t->radius_lg = 12;
    t->shadow_radius = 16;
    t->shadow_alpha = 140;
    t->space_1 = 4;
    t->space_2 = 8;
    t->space_4 = 16;
    t->space_8 = 32;
    t->scale_factor = 1.0f;

    t->dur_fast = 50;
    t->dur_base = 150;
    t->dur_slow = 300;
    t->ease_standard[0] = 0.4f;
    t->ease_standard[1] = 0.0f;
    t->ease_standard[2] = 0.2f;
    t->ease_standard[3] = 1.0f;
    t->ease_decelerate[0] = 0.0f;
    t->ease_decelerate[1] = 0.0f;
    t->ease_decelerate[2] = 0.2f;
    t->ease_decelerate[3] = 1.0f;
    t->ease_accelerate[0] = 0.4f;
    t->ease_accelerate[1] = 0.0f;
    t->ease_accelerate[2] = 1.0f;
    t->ease_accelerate[3] = 1.0f;
    t->reduce_motion = 0;
}

const vanilla_theme_t *theme_defaults_nord_dark(void)
{
    theme_apply_defaults(&g_theme_mutable);
    return g_theme;
}

static void strip_comments(char *s)
{
    for (char *p = s; *p; p++) {
        if (*p == ';' || *p == '#') {
            *p = '\0';
            break;
        }
    }
}

static char *trim_whitespace(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void parse_bezier(const char *val, float out[4])
{
    const char *p = val;
    for (int i = 0; i < 4; i++) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ','))
            p++;
        if (!*p)
            break;
        char *next = NULL;
        out[i] = strtof(p, &next);
        if (!next || next == p)
            break;
        p = next;
    }
}

static void split_kv(char *line, char **out_key, char **out_val)
{
    char *eq = strchr(line, '=');
    if (!eq) {
        *out_key = NULL;
        *out_val = NULL;
        return;
    }

    *eq = '\0';
    *out_key = trim_whitespace(line);
    *out_val = trim_whitespace(eq + 1);
}

static void theme_apply_kv(vanilla_theme_t *t, const char *section, const char *key, const char *val)
{
    if (!t || !section || !key || !val || !*key || !*val)
        return;

    if (strcmp(section, "colours") == 0 || strcmp(section, "colors") == 0) {
        uint32_t col = (uint32_t)strtoul(val, NULL, 0);

        if (strcmp(key, "bg_base") == 0) t->bg_base = col;
        else if (strcmp(key, "bg_elevated") == 0) t->bg_elevated = col;
        else if (strcmp(key, "bg_overlay") == 0) t->bg_overlay = col;
        else if (strcmp(key, "fg_primary") == 0) t->fg_primary = col;
        else if (strcmp(key, "fg_muted") == 0) t->fg_muted = col;
        else if (strcmp(key, "fg_dim") == 0) t->fg_dim = col;
        else if (strcmp(key, "border") == 0) t->border = col;
        else if (strcmp(key, "border_focus") == 0) t->border_focus = col;
        else if (strcmp(key, "accent") == 0) t->accent = col;
        else if (strcmp(key, "accent_hover") == 0) t->accent_hover = col;
        else if (strcmp(key, "accent_pressed") == 0) t->accent_pressed = col;
        else if (strcmp(key, "selection") == 0) t->selection = col;
        else if (strcmp(key, "success") == 0) t->success = col;
        else if (strcmp(key, "warning") == 0) t->warning = col;
        else if (strcmp(key, "danger") == 0) t->danger = col;
        else if (strcmp(key, "titlebar_active") == 0) t->titlebar_active = col;
        else if (strcmp(key, "titlebar_inactive") == 0) t->titlebar_inactive = col;
        else if (strcmp(key, "titlebar_text_active") == 0) t->titlebar_text_active = col;
        else if (strcmp(key, "titlebar_text_inactive") == 0) t->titlebar_text_inactive = col;
        else if (strcmp(key, "titlebar_btn_bg") == 0) t->titlebar_btn_bg = col;
        else if (strcmp(key, "titlebar_btn_bg_hover") == 0) t->titlebar_btn_bg_hover = col;
        else if (strcmp(key, "titlebar_btn_icon") == 0) t->titlebar_btn_icon = col;
        else if (strcmp(key, "taskbar_bg") == 0) t->taskbar_bg = col;
        else if (strcmp(key, "taskbar_item_active") == 0) t->taskbar_item_active = col;
        else if (strcmp(key, "taskbar_item_open") == 0) t->taskbar_item_open = col;
        else if (strcmp(key, "taskbar_text") == 0) t->taskbar_text = col;
    } else if (strcmp(section, "metrics") == 0) {
        if (strcmp(key, "titlebar_height") == 0) t->titlebar_height = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "border_width") == 0) t->border_width = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "btn_size") == 0) t->btn_size = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "title_font_size") == 0) t->title_font_size = strtof(val, NULL);
        else if (strcmp(key, "radius_sm") == 0) t->radius_sm = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "radius_md") == 0) t->radius_md = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "radius_lg") == 0) t->radius_lg = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "shadow_radius") == 0) t->shadow_radius = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "shadow_alpha") == 0) t->shadow_alpha = (uint8_t)strtoul(val, NULL, 0);
        else if (strcmp(key, "space_1") == 0) t->space_1 = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "space_2") == 0) t->space_2 = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "space_4") == 0) t->space_4 = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "space_8") == 0) t->space_8 = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "scale_factor") == 0) t->scale_factor = strtof(val, NULL);
    } else if (strcmp(section, "motion") == 0) {
        if (strcmp(key, "dur_fast") == 0) t->dur_fast = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "dur_base") == 0) t->dur_base = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "dur_slow") == 0) t->dur_slow = (int32_t)strtol(val, NULL, 0);
        else if (strcmp(key, "ease_standard") == 0) parse_bezier(val, t->ease_standard);
        else if (strcmp(key, "ease_decelerate") == 0) parse_bezier(val, t->ease_decelerate);
        else if (strcmp(key, "ease_accelerate") == 0) parse_bezier(val, t->ease_accelerate);
        else if (strcmp(key, "reduce_motion") == 0) t->reduce_motion = (int32_t)strtol(val, NULL, 0);
    }
}

static FILE *try_open_theme_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f)
        return f;

    const char *slash = strrchr(path, '/');
    const char *fname = slash ? (slash + 1) : path;

    char alt_path[512];
    snprintf(alt_path, sizeof(alt_path), "assets/themes/%s", fname);
    f = fopen(alt_path, "r");
    if (f)
        return f;

    snprintf(alt_path, sizeof(alt_path), "../assets/themes/%s", fname);
    f = fopen(alt_path, "r");
    if (f)
        return f;

    snprintf(alt_path, sizeof(alt_path), "../../assets/themes/%s", fname);
    f = fopen(alt_path, "r");
    if (f)
        return f;

    return NULL;
}

int theme_load(const char *ini_path)
{
    const char *target_path = (ini_path && ini_path[0]) ? ini_path : s_last_path;
    FILE *f = try_open_theme_file(target_path);
    if (!f) {
        theme_apply_defaults(&g_theme_mutable);
        g_theme = &g_theme_mutable;
        return -1;
    }

    theme_apply_defaults(&g_theme_mutable);

    char line[256];
    char current_section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        strip_comments(line);
        char *trimmed = trim_whitespace(line);
        if (!*trimmed)
            continue;

        if (trimmed[0] == '[') {
            char *closing = strchr(trimmed, ']');
            if (closing) {
                *closing = '\0';
                char *sec = trim_whitespace(trimmed + 1);
                strncpy(current_section, sec, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
        } else if (strchr(trimmed, '=')) {
            char *key = NULL;
            char *val = NULL;
            split_kv(trimmed, &key, &val);
            if (key && val)
                theme_apply_kv(&g_theme_mutable, current_section, key, val);
        }
    }

    fclose(f);

    if (ini_path && ini_path[0]) {
        strncpy(s_last_path, ini_path, sizeof(s_last_path) - 1);
        s_last_path[sizeof(s_last_path) - 1] = '\0';
    }

    g_theme = &g_theme_mutable;
    return 0;
}

int theme_reload(void)
{
    return theme_load(s_last_path);
}

int theme_set(const vanilla_theme_t *theme)
{
    if (!theme)
        return -1;
    memcpy(&g_theme_mutable, theme, sizeof(vanilla_theme_t));
    g_theme = &g_theme_mutable;
    return 0;
}
