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

static int wait_status_to_exit_code(int wstatus)
{
    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);
    if (WIFSIGNALED(wstatus))
        return 128 + WTERMSIG(wstatus);
    return wstatus;
}

static void restore_stdio(int saved_stdin, int saved_stdout)
{
    if (saved_stdin >= 0)
        dup2(saved_stdin, STDIN_FILENO);
    if (saved_stdout >= 0)
        dup2(saved_stdout, STDOUT_FILENO);
}

static int apply_redirections(const tsh_redir_t *redir)
{
    if (!redir)
        return 0;

    if (redir->infile) {
        int in_fd = open(redir->infile, O_RDONLY);
        if (in_fd < 0) {
            dprintf(STDERR_FILENO, "tsh: %s: %s\n", redir->infile, strerror(errno));
            return -1;
        }
        if (dup2(in_fd, STDIN_FILENO) < 0) {
            dprintf(STDERR_FILENO, "tsh: dup2: %s\n", strerror(errno));
            close(in_fd);
            return -1;
        }
        if (in_fd != STDIN_FILENO)
            close(in_fd);
    }

    if (redir->outfile) {
        int flags = O_WRONLY | O_CREAT | (redir->append ? O_APPEND : O_TRUNC);
        int out_fd = open(redir->outfile, flags, 0644);
        if (out_fd < 0) {
            dprintf(STDERR_FILENO, "tsh: %s: %s\n", redir->outfile, strerror(errno));
            return -1;
        }
        if (dup2(out_fd, STDOUT_FILENO) < 0) {
            dprintf(STDERR_FILENO, "tsh: dup2: %s\n", strerror(errno));
            close(out_fd);
            return -1;
        }
        if (out_fd != STDOUT_FILENO)
            close(out_fd);
    }

    return 0;
}

static int exec_single_builtin(tsh_cmd_t *cmd)
{
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    int status = 1;

    if (apply_redirections(&cmd->redir) == 0) {
        status = builtin_execute(cmd, STDIN_FILENO, STDOUT_FILENO,
                                 STDERR_FILENO, &g_exit_requested);
    }

    restore_stdio(saved_stdin, saved_stdout);
    if (saved_stdin >= 0)
        close(saved_stdin);
    if (saved_stdout >= 0)
        close(saved_stdout);

    g_last_exit_status = status;
    return status;
}

int exec_pipeline(tsh_pipeline_t *pipeline)
{
    if (!pipeline || pipeline->count <= 0)
        return 0;

    /* Single built-in command runs in-process without spawning */
    if (pipeline->count == 1 && builtin_lookup(pipeline->cmds[0].argv[0])) {
        return exec_single_builtin(&pipeline->cmds[0]);
    }

    /* Fast path: single external command without redirection */
    if (pipeline->count == 1 &&
        !pipeline->cmds[0].redir.infile &&
        !pipeline->cmds[0].redir.outfile) {
        char *resolved = exec_resolve_path(pipeline->cmds[0].argv[0]);
        if (!resolved) {
            dprintf(STDERR_FILENO, "tsh: %s: command not found\n",
                    pipeline->cmds[0].argv[0]);
            g_last_exit_status = 127;
            return 127;
        }

        pid_t sp_pid = spawn(resolved, pipeline->cmds[0].argv, env_get_all());
        if (sp_pid <= 0) {
            dprintf(STDERR_FILENO, "tsh: spawn %s: %s\n",
                    resolved, strerror(errno));
            free(resolved);
            g_last_exit_status = 126;
            return 126;
        }
        free(resolved);

        int wstatus = 0;
        if (waitpid(sp_pid, &wstatus, 0) == sp_pid)
            g_last_exit_status = wait_status_to_exit_code(wstatus);
        else
            g_last_exit_status = 1;
        return g_last_exit_status;
    }

    /*
     * General pipeline or redirected external command via spawn():
     * vfs_process_inherit copies all parent FDs without FD_CLOEXEC, so we
     * create each pipe just before its writer stage and restore parent stdio
     * immediately after each spawn() so no stage inherits earlier pipe ends.
     */
    int count = pipeline->count;
    int saved_stdin = dup(STDIN_FILENO);
    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdin < 0 || saved_stdout < 0) {
        dprintf(STDERR_FILENO, "tsh: dup failed: %s\n", strerror(errno));
        if (saved_stdin >= 0)
            close(saved_stdin);
        if (saved_stdout >= 0)
            close(saved_stdout);
        g_last_exit_status = 1;
        return 1;
    }

    pid_t *pids = (pid_t *)calloc((size_t)count, sizeof(pid_t));
    if (!pids) {
        dprintf(STDERR_FILENO, "tsh: memory allocation failed\n");
        close(saved_stdin);
        close(saved_stdout);
        g_last_exit_status = 1;
        return 1;
    }

    int prev_read_fd = -1;
    int last_status = 0;
    int pipe_failed = 0;

    for (int i = 0; i < count; i++) {
        int next_read_fd = -1;
        int stage_err = 0;

        pids[i] = -1;

        if (prev_read_fd >= 0) {
            if (dup2(prev_read_fd, STDIN_FILENO) < 0) {
                dprintf(STDERR_FILENO, "tsh: dup2: %s\n", strerror(errno));
                stage_err = 1;
            }
            if (prev_read_fd != STDIN_FILENO)
                close(prev_read_fd);
            prev_read_fd = -1;
        } else {
            dup2(saved_stdin, STDIN_FILENO);
        }

        if (i < count - 1) {
            int pfd[2];
            if (pipe(pfd) != 0) {
                dprintf(STDERR_FILENO, "tsh: pipe error: %s\n", strerror(errno));
                restore_stdio(saved_stdin, saved_stdout);
                pipe_failed = 1;
                last_status = 1;
                break;
            }
            if (dup2(pfd[1], STDOUT_FILENO) < 0) {
                dprintf(STDERR_FILENO, "tsh: dup2: %s\n", strerror(errno));
                close(pfd[0]);
                close(pfd[1]);
                restore_stdio(saved_stdin, saved_stdout);
                pipe_failed = 1;
                last_status = 1;
                break;
            }
            if (pfd[1] != STDOUT_FILENO)
                close(pfd[1]);
            next_read_fd = pfd[0];
        } else {
            dup2(saved_stdout, STDOUT_FILENO);
        }

        if (!stage_err && apply_redirections(&pipeline->cmds[i].redir) != 0) {
            stage_err = 1;
            if (i == count - 1)
                last_status = 1;
        }

        if (!stage_err) {
            tsh_cmd_t *cmd = &pipeline->cmds[i];
            if (!cmd->argv || !cmd->argv[0]) {
                if (i == count - 1)
                    last_status = 1;
            } else if (builtin_lookup(cmd->argv[0])) {
                int exit_dummy = 0;
                int ret = builtin_execute(cmd, STDIN_FILENO, STDOUT_FILENO,
                                          STDERR_FILENO, &exit_dummy);
                if (i == count - 1)
                    last_status = ret;
            } else {
                char *resolved = exec_resolve_path(cmd->argv[0]);
                if (!resolved) {
                    dprintf(STDERR_FILENO, "tsh: %s: command not found\n",
                            cmd->argv[0]);
                    if (i == count - 1)
                        last_status = 127;
                } else {
                    pid_t pid = spawn(resolved, cmd->argv, env_get_all());
                    if (pid <= 0) {
                        dprintf(STDERR_FILENO, "tsh: spawn %s: %s\n",
                                resolved, strerror(errno));
                        if (i == count - 1)
                            last_status = 126;
                    } else {
                        pids[i] = pid;
                    }
                    free(resolved);
                }
            }
        }

        /* Restore parent stdio immediately to close this stage's pipe write end */
        restore_stdio(saved_stdin, saved_stdout);
        prev_read_fd = next_read_fd;
    }

    if (prev_read_fd >= 0)
        close(prev_read_fd);

    restore_stdio(saved_stdin, saved_stdout);
    close(saved_stdin);
    close(saved_stdout);

    for (int i = 0; i < count; i++) {
        if (pids[i] > 0) {
            int wstatus = 0;
            if (waitpid(pids[i], &wstatus, 0) == pids[i]) {
                if (i == count - 1 && !pipe_failed)
                    last_status = wait_status_to_exit_code(wstatus);
            } else if (i == count - 1 && !pipe_failed) {
                last_status = 1;
            }
        }
    }

    free(pids);
    g_last_exit_status = last_status;
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
