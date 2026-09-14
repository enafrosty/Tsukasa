/*
 * Project Tsukasa — tsh Interactive Shell Main Entry Point
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

#include "line_editor.h"
#include "parser.h"
#include "builtins.h"
#include "exec.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void format_prompt(char *prompt_buf, size_t buf_size)
{
    char cwd[512];
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "/");

    const char *home = env_get("HOME");
    if (home && home[0] != '\0' && strcmp(home, "/") != 0) {
        size_t hlen = strlen(home);
        if (strncmp(cwd, home, hlen) == 0 && (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
            snprintf(prompt_buf, buf_size, "tsukasa:~%s$ ", cwd + hlen);
            return;
        }
    }

    snprintf(prompt_buf, buf_size, "tsukasa:%s$ ", cwd);
}

int main(int argc, char *argv[], char *envp[])
{
    env_init(envp);
    history_init();
    line_editor_init();

    /* Non-interactive command string execution */
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        int status = exec_string(argv[2]);
        env_cleanup();
        return status;
    }

    /* Script file execution */
    if (argc >= 2) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            dprintf(STDOUT_FILENO, "Usage: tsh [-c command] [script.sh]\n");
            env_cleanup();
            return 0;
        }
        int status = exec_script(argv[1]);
        env_cleanup();
        return status;
    }

    /* Interactive REPL loop */
    char prompt[576];
    char line_buf[TSH_HISTORY_LINE_MAX];

    while (!g_exit_requested) {
        format_prompt(prompt, sizeof(prompt));

        int len = line_editor_readline(prompt, line_buf, sizeof(line_buf));
        if (len < 0) {
            /* Received EOF */
            write(STDOUT_FILENO, "\nexit\n", 6);
            break;
        }

        if (len == 0 || line_buf[0] == '\0')
            continue;

        history_add(line_buf);
        exec_string(line_buf);
    }

    int exit_status = g_last_exit_status;
    env_cleanup();
    return exit_status;
}
