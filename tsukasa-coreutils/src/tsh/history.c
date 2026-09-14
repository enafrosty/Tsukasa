/*
 * Project Tsukasa — tsh Command History Buffer Implementation
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

#include "history.h"
#include <stdio.h>
#include <string.h>

static char g_history_entries[TSH_HISTORY_MAX][TSH_HISTORY_LINE_MAX];
static int g_history_count = 0;
static int g_history_head = 0;

void history_init(void)
{
    g_history_count = 0;
    g_history_head = 0;
    for (int i = 0; i < TSH_HISTORY_MAX; i++)
        g_history_entries[i][0] = '\0';
}

void history_add(const char *line)
{
    if (!line)
        return;

    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
        line++;
    if (*line == '\0')
        return;

    if (g_history_count > 0) {
        const char *prev = history_get(g_history_count - 1);
        if (prev && strcmp(prev, line) == 0)
            return;
    }

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
        len--;

    if (len >= TSH_HISTORY_LINE_MAX)
        len = TSH_HISTORY_LINE_MAX - 1;

    memcpy(g_history_entries[g_history_head], line, len);
    g_history_entries[g_history_head][len] = '\0';

    g_history_head = (g_history_head + 1) % TSH_HISTORY_MAX;
    if (g_history_count < TSH_HISTORY_MAX)
        g_history_count++;
}

int history_count(void)
{
    return g_history_count;
}

const char *history_get(int index)
{
    if (index < 0 || index >= g_history_count)
        return NULL;

    int slot;
    if (g_history_count < TSH_HISTORY_MAX)
        slot = index;
    else
        slot = (g_history_head + index) % TSH_HISTORY_MAX;

    return g_history_entries[slot];
}

void history_print(int out_fd)
{
    for (int i = 0; i < g_history_count; i++) {
        const char *cmd = history_get(i);
        if (cmd)
            dprintf(out_fd, "%5d  %s\n", i + 1, cmd);
    }
}
