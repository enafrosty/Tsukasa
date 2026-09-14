/*
 * Project Tsukasa — tsh Built-in Commands Implementation
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

#include "builtins.h"
#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static char **g_envp = NULL;
static int g_env_count = 0;
static int g_env_cap = 0;

void env_init(char **initial_envp)
{
    g_env_cap = 32;
    g_env_count = 0;
    g_envp = (char **)calloc((size_t)g_env_cap, sizeof(char *));
    if (!g_envp)
        return;

    if (initial_envp) {
        for (int i = 0; initial_envp[i]; i++) {
            char *eq = strchr(initial_envp[i], '=');
            if (eq) {
                size_t nlen = (size_t)(eq - initial_envp[i]);
                char name[256];
                if (nlen < sizeof(name)) {
                    memcpy(name, initial_envp[i], nlen);
                    name[nlen] = '\0';
                    env_set(name, eq + 1);
                }
            }
        }
    }

    if (!env_get("PATH"))
        env_set("PATH", "/bin:/usr/bin:/sbin:.");
    if (!env_get("HOME"))
        env_set("HOME", "/");
    if (!env_get("SHELL"))
        env_set("SHELL", "/bin/tsh");

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        env_set("PWD", cwd);
}

const char *env_get(const char *name)
{
    if (!name || !g_envp)
        return NULL;

    size_t nlen = strlen(name);
    for (int i = 0; i < g_env_count; i++) {
        if (strncmp(g_envp[i], name, nlen) == 0 && g_envp[i][nlen] == '=')
            return &g_envp[i][nlen + 1];
    }
    return NULL;
}

int env_set(const char *name, const char *value)
{
    if (!name || !value)
        return -1;

    size_t nlen = strlen(name);
    size_t vlen = strlen(value);
    size_t entry_len = nlen + 1 + vlen;

    char *entry = (char *)malloc(entry_len + 1);
    if (!entry)
        return -1;

    memcpy(entry, name, nlen);
    entry[nlen] = '=';
    memcpy(entry + nlen + 1, value, vlen);
    entry[entry_len] = '\0';

    for (int i = 0; i < g_env_count; i++) {
        if (strncmp(g_envp[i], name, nlen) == 0 && g_envp[i][nlen] == '=') {
            free(g_envp[i]);
            g_envp[i] = entry;
            return 0;
        }
    }

    if (g_env_count + 2 >= g_env_cap) {
        int new_cap = g_env_cap * 2;
        char **new_env = (char **)realloc(g_envp, (size_t)new_cap * sizeof(char *));
        if (!new_env) {
            free(entry);
            return -1;
        }
        g_envp = new_env;
        g_env_cap = new_cap;
    }

    g_envp[g_env_count++] = entry;
    g_envp[g_env_count] = NULL;
    return 0;
}

char **env_get_all(void)
{
    return g_envp;
}

void env_cleanup(void)
{
    if (!g_envp)
        return;

    for (int i = 0; i < g_env_count; i++)
        free(g_envp[i]);
    free(g_envp);
    g_envp = NULL;
    g_env_count = 0;
    g_env_cap = 0;
}

int builtin_lookup(const char *name)
{
    if (!name)
        return 0;

    return (strcmp(name, "cd") == 0 ||
            strcmp(name, "pwd") == 0 ||
            strcmp(name, "echo") == 0 ||
            strcmp(name, "clear") == 0 ||
            strcmp(name, "exit") == 0 ||
            strcmp(name, "export") == 0 ||
            strcmp(name, "help") == 0 ||
            strcmp(name, "history") == 0);
}

static int builtin_cd(int argc, char **argv, int err_fd)
{
    const char *target = NULL;
    if (argc <= 1) {
        target = env_get("HOME");
        if (!target || target[0] == '\0')
            target = "/";
    } else if (strcmp(argv[1], "-") == 0) {
        target = env_get("OLDPWD");
        if (!target || target[0] == '\0')
            target = "/";
    } else {
        target = argv[1];
    }

    char oldcwd[1024];
    char newcwd[1024];
    if (!getcwd(oldcwd, sizeof(oldcwd)))
        oldcwd[0] = '\0';

    if (chdir(target) != 0) {
        dprintf(err_fd, "tsh: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    if (oldcwd[0])
        env_set("OLDPWD", oldcwd);
    if (getcwd(newcwd, sizeof(newcwd)))
        env_set("PWD", newcwd);

    return 0;
}

static int builtin_pwd(int out_fd, int err_fd)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        dprintf(out_fd, "%s\n", cwd);
        return 0;
    }
    dprintf(err_fd, "tsh: pwd: %s\n", strerror(errno));
    return 1;
}

static int builtin_echo(int argc, char **argv, int out_fd)
{
    int newline = 1;
    int start = 1;

    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        if (i > start)
            write(out_fd, " ", 1);
        write(out_fd, argv[i], strlen(argv[i]));
    }

    if (newline)
        write(out_fd, "\n", 1);

    return 0;
}

static int builtin_clear(int out_fd)
{
    write(out_fd, "\x1b[2J\x1b[H", 7);
    return 0;
}

static int builtin_exit(int argc, char **argv, int *exit_shell)
{
    if (exit_shell)
        *exit_shell = 1;

    if (argc > 1)
        return atoi(argv[1]);
    return 0;
}

static int builtin_export(int argc, char **argv, int out_fd)
{
    if (argc <= 1) {
        char **all = env_get_all();
        if (all) {
            for (int i = 0; all[i]; i++)
                dprintf(out_fd, "export %s\n", all[i]);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            size_t nlen = (size_t)(eq - argv[i]);
            char name[256];
            if (nlen < sizeof(name)) {
                memcpy(name, argv[i], nlen);
                name[nlen] = '\0';
                env_set(name, eq + 1);
            }
        } else {
            if (!env_get(argv[i]))
                env_set(argv[i], "");
        }
    }
    return 0;
}

static int builtin_help(int out_fd)
{
    dprintf(out_fd, "Tsukasa Shell (tsh) - Interactive User Shell\n");
    dprintf(out_fd, "Built-in commands:\n");
    dprintf(out_fd, "  cd [dir]       Change current directory\n");
    dprintf(out_fd, "  pwd            Print current directory\n");
    dprintf(out_fd, "  echo [args]    Print arguments to standard output\n");
    dprintf(out_fd, "  clear          Clear console display\n");
    dprintf(out_fd, "  exit [code]    Exit shell session\n");
    dprintf(out_fd, "  export [v=val] Set or display environment variables\n");
    dprintf(out_fd, "  help           Display shell built-ins overview\n");
    dprintf(out_fd, "  history        Display command history buffer\n");
    return 0;
}

static int builtin_history(int out_fd)
{
    history_print(out_fd);
    return 0;
}

int builtin_execute(tsh_cmd_t *cmd, int in_fd, int out_fd, int err_fd, int *exit_shell)
{
    (void)in_fd;
    if (!cmd || cmd->argc == 0)
        return 0;

    const char *name = cmd->argv[0];
    if (strcmp(name, "cd") == 0)
        return builtin_cd(cmd->argc, cmd->argv, err_fd);
    if (strcmp(name, "pwd") == 0)
        return builtin_pwd(out_fd, err_fd);
    if (strcmp(name, "echo") == 0)
        return builtin_echo(cmd->argc, cmd->argv, out_fd);
    if (strcmp(name, "clear") == 0)
        return builtin_clear(out_fd);
    if (strcmp(name, "exit") == 0)
        return builtin_exit(cmd->argc, cmd->argv, exit_shell);
    if (strcmp(name, "export") == 0)
        return builtin_export(cmd->argc, cmd->argv, out_fd);
    if (strcmp(name, "help") == 0)
        return builtin_help(out_fd);
    if (strcmp(name, "history") == 0)
        return builtin_history(out_fd);

    return -1;
}
