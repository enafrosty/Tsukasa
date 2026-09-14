/*
 * Project Tsukasa — free Memory Usage Telemetry Utility
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

enum unit_mode {
    UNIT_KILO = 0,
    UNIT_MEGA,
    UNIT_GIGA,
    UNIT_BYTES,
    UNIT_HUMAN
};

static unsigned long read_key_value(const char *filename, const char *key)
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

static void format_human(unsigned long bytes, char *out, size_t max)
{
    if (bytes >= (1024UL * 1024UL * 1024UL))
        snprintf(out, max, "%lu.%luG", bytes / (1024UL * 1024UL * 1024UL), (bytes % (1024UL * 1024UL * 1024UL)) / (100UL * 1024UL * 1024UL));
    else if (bytes >= (1024UL * 1024UL))
        snprintf(out, max, "%lu.%luM", bytes / (1024UL * 1024UL), (bytes % (1024UL * 1024UL)) / (100UL * 1024UL));
    else if (bytes >= 1024UL)
        snprintf(out, max, "%luK", bytes / 1024UL);
    else
        snprintf(out, max, "%luB", bytes);
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-k] [-m] [-g] [-b] [-h]\n", prog);
}

int main(int argc, char **argv)
{
    enum unit_mode mode = UNIT_KILO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0) {
            mode = UNIT_KILO;
        } else if (strcmp(argv[i], "-m") == 0) {
            mode = UNIT_MEGA;
        } else if (strcmp(argv[i], "-g") == 0) {
            mode = UNIT_GIGA;
        } else if (strcmp(argv[i], "-b") == 0) {
            mode = UNIT_BYTES;
        } else if (strcmp(argv[i], "-h") == 0) {
            mode = UNIT_HUMAN;
        } else {
            fprintf(stderr, "free: invalid option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    unsigned long total_pages = read_key_value("/sys/memory", "pmm_total_pages");
    unsigned long used_pages = read_key_value("/sys/memory", "pmm_used_pages");
    unsigned long free_pages = read_key_value("/sys/memory", "pmm_free_pages");

    if (total_pages == 0) {
        fprintf(stderr, "free: cannot read memory statistics from /sys/memory\n");
        return 1;
    }

    unsigned long total_b = total_pages * 4096;
    unsigned long used_b = used_pages * 4096;
    unsigned long free_b = free_pages * 4096;

    printf("%14s %12s %12s\n", "total", "used", "free");

    if (mode == UNIT_HUMAN) {
        char s_total[32], s_used[32], s_free[32];
        format_human(total_b, s_total, sizeof(s_total));
        format_human(used_b, s_used, sizeof(s_used));
        format_human(free_b, s_free, sizeof(s_free));
        printf("Mem: %9s %12s %12s\n", s_total, s_used, s_free);
    } else {
        unsigned long div = 1024;
        if (mode == UNIT_MEGA)
            div = 1024 * 1024;
        else if (mode == UNIT_GIGA)
            div = 1024 * 1024 * 1024;
        else if (mode == UNIT_BYTES)
            div = 1;

        printf("Mem: %9lu %12lu %12lu\n", total_b / div, used_b / div, free_b / div);
    }

    return 0;
}
