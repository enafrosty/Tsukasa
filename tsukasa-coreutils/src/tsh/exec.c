/*
 * Project Tsukasa — tsh Execution Engine Implementation
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

#include "exec.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>

int g_last_exit_status = 0;
int g_exit_requested = 0;

char *exec_resolve_path(const char *cmd)
{
    if (!cmd || cmd[0] == '\0')
        return NULL;

    if (strchr(cmd, '/')) {
        struct stat st;
        if (stat(cmd, &st) == 0 && S_ISREG(st.st_mode))
            return strdup(cmd);
        return strdup(cmd);
    }

    const char *path_env = env_get("PATH");
    if (!path_env || path_env[0] == '\0')
        path_env = "/bin:/usr/bin:/sbin:.";

    char *path_copy = strdup(path_env);
    if (!path_copy)
        return NULL;

    char *token = path_copy;
    char candidate[1024];

    while (token && *token) {
        char *colon = strchr(token, ':');
        if (colon)
            *colon = '\0';

        size_t dir_len = strlen(token);
        size_t cmd_len = strlen(cmd);

        if (dir_len + 1 + cmd_len < sizeof(candidate)) {
            memcpy(candidate, token, dir_len);
            candidate[dir_len] = '/';
            memcpy(candidate + dir_len + 1, cmd, cmd_len);
            candidate[dir_len + 1 + cmd_len] = '\0';

            struct stat st;
            if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode)) {
                free(path_copy);
                return strdup(candidate);
            }
        }

        if (colon)
            token = colon + 1;
        else
            break;
    }

    free(path_copy);
    return NULL;
}

static int exec_single_builtin(tsh_cmd_t *cmd)
{
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    int redir_err = 0;
    int status = 0;

    if (cmd->redir.infile) {
        int in_fd = open(cmd->redir.infile, O_RDONLY);
        if (in_fd < 0) {
            dprintf(STDERR_FILENO, "tsh: %s: %s\n", cmd->redir.infile, strerror(errno));
            redir_err = 1;
        } else {
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }
    }

    if (!redir_err && cmd->redir.outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->redir.append ? O_APPEND : O_TRUNC);
        int out_fd = open(cmd->redir.outfile, flags, 0644);
        if (out_fd < 0) {
            dprintf(STDERR_FILENO, "tsh: %s: %s\n", cmd->redir.outfile, strerror(errno));
            redir_err = 1;
        } else {
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }
    }

    if (!redir_err) {
        status = builtin_execute(cmd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, &g_exit_requested);
    } else {
        status = 1;
    }

    dup2(saved_stdin, STDIN_FILENO);
    close(saved_stdin);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    g_last_exit_status = status;
    return status;
}

int exec_pipeline(tsh_pipeline_t *pipeline)
{
    if (!pipeline || pipeline->count <= 0)
        return 0;

    /* Optimize single built-in command without subshell */
    if (pipeline->count == 1 && builtin_lookup(pipeline->cmds[0].argv[0])) {
        return exec_single_builtin(&pipeline->cmds[0]);
    }

    /* Fast path: spawn() execution for single external commands without redirection */
    if (pipeline->count == 1 &&
        !pipeline->cmds[0].redir.infile &&
        !pipeline->cmds[0].redir.outfile) {
        char *resolved = exec_resolve_path(pipeline->cmds[0].argv[0]);
        if (!resolved) {
            dprintf(STDERR_FILENO, "tsh: %s: command not found\n", pipeline->cmds[0].argv[0]);
            g_last_exit_status = 127;
            return 127;
        }

        pid_t sp_pid = spawn(resolved, pipeline->cmds[0].argv, env_get_all());
        free(resolved);

        if (sp_pid > 0) {
            int wstatus = 0;
            waitpid(sp_pid, &wstatus, 0);
            if (WIFEXITED(wstatus))
                g_last_exit_status = WEXITSTATUS(wstatus);
            else if (WIFSIGNALED(wstatus))
                g_last_exit_status = 128 + WTERMSIG(wstatus);
            else
                g_last_exit_status = wstatus;
            return g_last_exit_status;
        }
    }

    /* General pipeline execution: multi-stage or commands requiring I/O redirection */
    int count = pipeline->count;
    int (*pipefds)[2] = NULL;
    if (count > 1) {
        pipefds = (int (*)[2])calloc((size_t)(count - 1), sizeof(int[2]));
        if (!pipefds) {
            dprintf(STDERR_FILENO, "tsh: memory allocation failed\n");
            return 1;
        }

        for (int i = 0; i < count - 1; i++) {
            if (pipe(pipefds[i]) != 0) {
                dprintf(STDERR_FILENO, "tsh: pipe error: %s\n", strerror(errno));
                for (int k = 0; k < i; k++) {
                    close(pipefds[k][0]);
                    close(pipefds[k][1]);
                }
                free(pipefds);
                return 1;
            }
        }
    }

    pid_t *pids = (pid_t *)calloc((size_t)count, sizeof(pid_t));
    if (!pids) {
        if (pipefds) {
            for (int k = 0; k < count - 1; k++) {
                close(pipefds[k][0]);
                close(pipefds[k][1]);
            }
            free(pipefds);
        }
        return 1;
    }

    for (int i = 0; i < count; i++) {
        pids[i] = fork();

        if (pids[i] == 0) {
            /* Child process: connect pipeline stages */
            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            if (i < count - 1) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            if (pipefds) {
                for (int k = 0; k < count - 1; k++) {
                    close(pipefds[k][0]);
                    close(pipefds[k][1]);
                }
            }

            /* Apply file redirections */
            if (pipeline->cmds[i].redir.infile) {
                int in_fd = open(pipeline->cmds[i].redir.infile, O_RDONLY);
                if (in_fd < 0) {
                    dprintf(STDERR_FILENO, "tsh: %s: %s\n",
                            pipeline->cmds[i].redir.infile, strerror(errno));
                    _exit(1);
                }
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }

            if (pipeline->cmds[i].redir.outfile) {
                int flags = O_WRONLY | O_CREAT |
                            (pipeline->cmds[i].redir.append ? O_APPEND : O_TRUNC);
                int out_fd = open(pipeline->cmds[i].redir.outfile, flags, 0644);
                if (out_fd < 0) {
                    dprintf(STDERR_FILENO, "tsh: %s: %s\n",
                            pipeline->cmds[i].redir.outfile, strerror(errno));
                    _exit(1);
                }
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }

            /* Subshell execution: builtin or binary */
            if (builtin_lookup(pipeline->cmds[i].argv[0])) {
                int exit_dummy = 0;
                int ret = builtin_execute(&pipeline->cmds[i],
                                          STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                                          &exit_dummy);
                _exit(ret);
            }

            char *resolved = exec_resolve_path(pipeline->cmds[i].argv[0]);
            if (!resolved) {
                dprintf(STDERR_FILENO, "tsh: %s: command not found\n",
                        pipeline->cmds[i].argv[0]);
                _exit(127);
            }

            execve(resolved, pipeline->cmds[i].argv, env_get_all());
            dprintf(STDERR_FILENO, "tsh: execve %s: %s\n", resolved, strerror(errno));
            _exit(126);
        } else if (pids[i] < 0) {
            dprintf(STDERR_FILENO, "tsh: fork failed: %s\n", strerror(errno));
            break;
        }
    }

    /* Parent process: close pipeline pipes */
    if (pipefds) {
        for (int k = 0; k < count - 1; k++) {
            close(pipefds[k][0]);
            close(pipefds[k][1]);
        }
        free(pipefds);
    }

    /* Wait for child processes and record exit code of last stage */
    for (int i = 0; i < count; i++) {
        if (pids[i] > 0) {
            int wstatus = 0;
            waitpid(pids[i], &wstatus, 0);
            if (i == count - 1) {
                if (WIFEXITED(wstatus))
                    g_last_exit_status = WEXITSTATUS(wstatus);
                else if (WIFSIGNALED(wstatus))
                    g_last_exit_status = 128 + WTERMSIG(wstatus);
                else
                    g_last_exit_status = wstatus;
            }
        }
    }

    free(pids);
    return g_last_exit_status;
}

int exec_string(const char *line)
{
    if (!line)
        return 0;

    tsh_pipeline_t pipeline;
    int res = parser_parse_line(line, &pipeline);
    if (res < 0) {
        dprintf(STDERR_FILENO, "tsh: syntax error near unexpected token\n");
        g_last_exit_status = 2;
        return 2;
    }
    if (res == 0)
        return 0;

    int status = exec_pipeline(&pipeline);
    parser_free_pipeline(&pipeline);
    return status;
}

int exec_script(const char *path)
{
    if (!path)
        return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        dprintf(STDERR_FILENO, "tsh: %s: %s\n", path, strerror(errno));
        g_last_exit_status = 127;
        return 127;
    }

    char line_buf[1024];
    size_t line_len = 0;
    char ch;

    while (read(fd, &ch, 1) > 0) {
        if (ch == '\r')
            continue;

        if (ch == '\n') {
            line_buf[line_len] = '\0';
            if (line_len > 0) {
                exec_string(line_buf);
                if (g_exit_requested)
                    break;
            }
            line_len = 0;
            continue;
        }

        if (line_len + 1 < sizeof(line_buf))
            line_buf[line_len++] = ch;
    }

    if (line_len > 0 && !g_exit_requested) {
        line_buf[line_len] = '\0';
        exec_string(line_buf);
    }

    close(fd);
    return g_last_exit_status;
}
