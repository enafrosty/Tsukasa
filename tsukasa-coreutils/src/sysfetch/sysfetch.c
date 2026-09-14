/*
 * Project Tsukasa — sysfetch System Information Telemetry Utility
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
#include <unistd.h>
#include <sys/syscall.h>

#define COLOR_CYAN    "\x1b[1;36m"
#define COLOR_BLUE    "\x1b[1;34m"
#define COLOR_MAGENTA "\x1b[1;35m"
#define COLOR_GREEN   "\x1b[1;32m"
#define COLOR_YELLOW  "\x1b[1;33m"
#define COLOR_RESET   "\x1b[0m"
#define COLOR_BOLD    "\x1b[1m"

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

static int count_processes(void)
{
    FILE *fp = fopen("/proc/processes", "r");
    if (!fp)
        return 1;

    char line[256];
    int count = 0;
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        if (line[0] >= '0' && line[0] <= '9')
            count++;
    }

    fclose(fp);
    return (count > 0) ? count : 1;
}

int main(void)
{
    /* Compute uptime from 100 Hz timer ticks (SYS_ticks 320) */
    int64_t ticks = __syscall0(SYS_ticks);
    unsigned long uptime_sec = (ticks >= 0) ? (unsigned long)(ticks / 100) : 0;
    unsigned int hrs = (unsigned int)(uptime_sec / 3600);
    unsigned int mins = (unsigned int)((uptime_sec % 3600) / 60);
    unsigned int secs = (unsigned int)(uptime_sec % 60);

    /* Read memory telemetry */
    unsigned long total_pages = read_key_value("/sys/memory", "pmm_total_pages");
    unsigned long used_pages = read_key_value("/sys/memory", "pmm_used_pages");
    unsigned long total_mb = (total_pages * 4096) / (1024 * 1024);
    unsigned long used_mb = (used_pages * 4096) / (1024 * 1024);

    /* Read display info */
    unsigned long fb_w = read_key_value("/sys/devices/summary", "framebuffer.width");
    unsigned long fb_h = read_key_value("/sys/devices/summary", "framebuffer.height");
    unsigned long fb_bpp = read_key_value("/sys/devices/summary", "framebuffer.bpp");
    if (fb_w == 0) fb_w = 1024;
    if (fb_h == 0) fb_h = 768;
    if (fb_bpp == 0) fb_bpp = 32;

    int procs = count_processes();

    /* Format side-by-side output */
    printf("\n");
    printf(COLOR_CYAN "       /\\         " COLOR_BOLD "frosty" COLOR_RESET "@" COLOR_CYAN "tsukasa\n" COLOR_RESET);
    printf(COLOR_CYAN "      /  \\        " COLOR_RESET "--------------\n");
    printf(COLOR_CYAN "     /\\   /\\      " COLOR_YELLOW "OS" COLOR_RESET ": Project Tsukasa (x86_64)\n");
    printf(COLOR_CYAN "    /  \\ /  \\     " COLOR_YELLOW "Kernel" COLOR_RESET ": Tsukasa 1.0-alpha\n");
    printf(COLOR_CYAN "   /    V    \\    " COLOR_YELLOW "Uptime" COLOR_RESET ": %uh %um %us\n", hrs, mins, secs);
    printf(COLOR_CYAN "  /  /\\   /\\  \\   " COLOR_YELLOW "Memory" COLOR_RESET ": %lu MB / %lu MB\n", used_mb, total_mb);
    printf(COLOR_CYAN " /__/  \\_/  \\__\\  " COLOR_YELLOW "Display" COLOR_RESET ": %lux%lux%lu\n", fb_w, fb_h, fb_bpp);
    printf("                  " COLOR_YELLOW "Processes" COLOR_RESET ": %d\n", procs);
    printf("                  " COLOR_YELLOW "Shell" COLOR_RESET ": tsh\n\n");

    /* Color blocks */
    printf("                  "
           "\x1b[40m   \x1b[41m   \x1b[42m   \x1b[43m   "
           "\x1b[44m   \x1b[45m   \x1b[46m   \x1b[47m   \x1b[0m\n\n");

    return 0;
}
