/*
 * Project Tsukasa — Builtin manual pages definitions
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

#ifndef TSUKASA_MAN_PAGES_H
#define TSUKASA_MAN_PAGES_H

typedef struct man_page {
    const char *name;
    const char *text;
} man_page_t;

static const man_page_t g_man_pages[] = {
    {
        "help",
        "HELP(1)\n"
        "  help - list available commands\n"
        "USAGE\n"
        "  help\n"
    },
    {
        "echo",
        "ECHO(1)\n"
        "  echo - print arguments\n"
        "USAGE\n"
        "  echo <text>\n"
    },
    {
        "pwd",
        "PWD(1)\n"
        "  pwd - print current working directory\n"
        "USAGE\n"
        "  pwd\n"
    },
    {
        "date",
        "DATE(1)\n"
        "  date - print RTC date/time (UTC)\n"
        "USAGE\n"
        "  date\n"
    },
    {
        "sort",
        "SORT(1)\n"
        "  sort - sort lines from stdin\n"
        "USAGE\n"
        "  sort\n"
    },
    {
        "sysfetch",
        "SYSFETCH(1)\n"
        "  sysfetch - print system summary from /proc and /sys\n"
        "USAGE\n"
        "  sysfetch\n"
    },
    {
        "man",
        "MAN(1)\n"
        "  man - show manual pages\n"
        "USAGE\n"
        "  man <command>\n"
    },
    {
        "grep",
        "GREP(1)\n"
        "  grep - filter lines containing a substring\n"
        "USAGE\n"
        "  grep <pattern>\n"
    },
    {
        "cat",
        "CAT(1)\n"
        "  cat - print file contents or stdin\n"
        "USAGE\n"
        "  cat [file...]\n"
    },
    {
        "net",
        "NET(1)\n"
        "  net - network status summary\n"
        "USAGE\n"
        "  net\n"
    },
    {
        "ping",
        "PING(1)\n"
        "  ping - ICMP echo to default gateway target\n"
        "USAGE\n"
        "  ping\n"
    },
    {
        "telnet",
        "TELNET(1)\n"
        "  telnet - minimal TCP probe to example.com:80\n"
        "USAGE\n"
        "  telnet\n"
    },
};

static inline int man_page_count(void)
{
    return (int)(sizeof(g_man_pages) / sizeof(g_man_pages[0]));
}

#endif /* TSUKASA_MAN_PAGES_H */
