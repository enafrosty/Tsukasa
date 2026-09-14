/*
 * Project Tsukasa — tsh Command Line Parser Implementation
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

#include "parser.h"
#include <stdlib.h>
#include <string.h>

#define MAX_STAGES 16
#define MAX_TOKENS 128
#define MAX_TOKEN_LEN 1024

static char *strip_comments(char *line)
{
    int in_single = 0;
    int in_double = 0;

    for (size_t i = 0; line[i]; i++) {
        if (line[i] == '\'' && !in_double) {
            in_single = !in_single;
        } else if (line[i] == '"' && !in_single) {
            in_double = !in_double;
        } else if (line[i] == '\\' && !in_single && line[i + 1]) {
            i++;
        } else if (line[i] == '#' && !in_single && !in_double) {
            line[i] = '\0';
            break;
        }
    }
    return line;
}

static int split_pipeline_stages(const char *line, char **stages, int max_stages)
{
    int count = 0;
    int in_single = 0;
    int in_double = 0;
    size_t start = 0;
    size_t i = 0;

    while (line[i]) {
        if (line[i] == '\'' && !in_double) {
            in_single = !in_single;
        } else if (line[i] == '"' && !in_single) {
            in_double = !in_double;
        } else if (line[i] == '\\' && !in_single && line[i + 1]) {
            i++;
        } else if (line[i] == '|' && !in_single && !in_double) {
            if (count >= max_stages)
                return -1;

            size_t len = i - start;
            char *stage = (char *)malloc(len + 1);
            if (!stage)
                return -1;
            memcpy(stage, &line[start], len);
            stage[len] = '\0';
            stages[count++] = stage;
            start = i + 1;
        }
        i++;
    }

    if (count >= max_stages)
        return -1;

    size_t len = i - start;
    char *stage = (char *)malloc(len + 1);
    if (!stage)
        return -1;
    memcpy(stage, &line[start], len);
    stage[len] = '\0';
    stages[count++] = stage;

    return count;
}

static int tokenize_stage(const char *stage, char **tokens, int max_tokens)
{
    int num_tokens = 0;
    const char *p = stage;
    char buf[MAX_TOKEN_LEN];

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (*p == '\0')
            break;

        if (num_tokens >= max_tokens)
            return -1;

        if (*p == '<') {
            tokens[num_tokens++] = strdup("<");
            p++;
            continue;
        }

        if (*p == '>') {
            if (*(p + 1) == '>') {
                tokens[num_tokens++] = strdup(">>");
                p += 2;
            } else {
                tokens[num_tokens++] = strdup(">");
                p++;
            }
            continue;
        }

        size_t bi = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
               *p != '<' && *p != '>') {
            if (*p == '\'') {
                p++;
                while (*p && *p != '\'') {
                    if (bi + 1 < MAX_TOKEN_LEN)
                        buf[bi++] = *p;
                    p++;
                }
                if (*p == '\'')
                    p++;
            } else if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && *(p + 1)) {
                        p++;
                        if (bi + 1 < MAX_TOKEN_LEN)
                            buf[bi++] = *p;
                        p++;
                    } else {
                        if (bi + 1 < MAX_TOKEN_LEN)
                            buf[bi++] = *p;
                        p++;
                    }
                }
                if (*p == '"')
                    p++;
            } else if (*p == '\\' && *(p + 1)) {
                p++;
                if (bi + 1 < MAX_TOKEN_LEN)
                    buf[bi++] = *p;
                p++;
            } else {
                if (bi + 1 < MAX_TOKEN_LEN)
                    buf[bi++] = *p;
                p++;
            }
        }

        buf[bi] = '\0';
        tokens[num_tokens++] = strdup(buf);
    }

    return num_tokens;
}

int parser_parse_line(const char *line, tsh_pipeline_t *pipeline)
{
    if (!line || !pipeline)
        return -1;

    pipeline->cmds = NULL;
    pipeline->count = 0;

    char *dup_line = strdup(line);
    if (!dup_line)
        return -1;

    strip_comments(dup_line);

    /* Check if line contains only whitespace */
    const char *check = dup_line;
    while (*check == ' ' || *check == '\t' || *check == '\r' || *check == '\n')
        check++;
    if (*check == '\0') {
        free(dup_line);
        return 0;
    }

    char *stages[MAX_STAGES];
    int stage_count = split_pipeline_stages(dup_line, stages, MAX_STAGES);
    free(dup_line);

    if (stage_count <= 0)
        return -1;

    tsh_cmd_t *cmds = (tsh_cmd_t *)calloc((size_t)stage_count, sizeof(tsh_cmd_t));
    if (!cmds) {
        for (int i = 0; i < stage_count; i++)
            free(stages[i]);
        return -1;
    }

    for (int s = 0; s < stage_count; s++) {
        char *tokens[MAX_TOKENS];
        int num_tokens = tokenize_stage(stages[s], tokens, MAX_TOKENS);
        free(stages[s]);

        if (num_tokens < 0) {
            for (int i = 0; i < s; i++) {
                for (int a = 0; a < cmds[i].argc; a++)
                    free(cmds[i].argv[a]);
                free(cmds[i].argv);
                if (cmds[i].redir.infile) free(cmds[i].redir.infile);
                if (cmds[i].redir.outfile) free(cmds[i].redir.outfile);
            }
            free(cmds);
            return -1;
        }

        cmds[s].argv = (char **)calloc((size_t)(num_tokens + 1), sizeof(char *));
        cmds[s].argc = 0;
        cmds[s].redir.infile = NULL;
        cmds[s].redir.outfile = NULL;
        cmds[s].redir.append = 0;

        for (int t = 0; t < num_tokens; t++) {
            if (strcmp(tokens[t], "<") == 0) {
                if (t + 1 >= num_tokens) {
                    /* Missing redirection target */
                    for (int k = 0; k < num_tokens; k++)
                        free(tokens[k]);
                    parser_free_pipeline(pipeline);
                    return -1;
                }
                if (cmds[s].redir.infile)
                    free(cmds[s].redir.infile);
                cmds[s].redir.infile = strdup(tokens[t + 1]);
                t++;
            } else if (strcmp(tokens[t], ">") == 0) {
                if (t + 1 >= num_tokens) {
                    for (int k = 0; k < num_tokens; k++)
                        free(tokens[k]);
                    parser_free_pipeline(pipeline);
                    return -1;
                }
                if (cmds[s].redir.outfile)
                    free(cmds[s].redir.outfile);
                cmds[s].redir.outfile = strdup(tokens[t + 1]);
                cmds[s].redir.append = 0;
                t++;
            } else if (strcmp(tokens[t], ">>") == 0) {
                if (t + 1 >= num_tokens) {
                    for (int k = 0; k < num_tokens; k++)
                        free(tokens[k]);
                    parser_free_pipeline(pipeline);
                    return -1;
                }
                if (cmds[s].redir.outfile)
                    free(cmds[s].redir.outfile);
                cmds[s].redir.outfile = strdup(tokens[t + 1]);
                cmds[s].redir.append = 1;
                t++;
            } else {
                cmds[s].argv[cmds[s].argc++] = strdup(tokens[t]);
            }
        }

        for (int t = 0; t < num_tokens; t++)
            free(tokens[t]);

        if (cmds[s].argc == 0) {
            /* Empty command in stage */
            for (int i = 0; i <= s; i++) {
                for (int a = 0; a < cmds[i].argc; a++)
                    free(cmds[i].argv[a]);
                free(cmds[i].argv);
                if (cmds[i].redir.infile) free(cmds[i].redir.infile);
                if (cmds[i].redir.outfile) free(cmds[i].redir.outfile);
            }
            free(cmds);
            return -1;
        }

        cmds[s].argv[cmds[s].argc] = NULL;
    }

    pipeline->cmds = cmds;
    pipeline->count = stage_count;
    return stage_count;
}

void parser_free_pipeline(tsh_pipeline_t *pipeline)
{
    if (!pipeline || !pipeline->cmds)
        return;

    for (int i = 0; i < pipeline->count; i++) {
        if (pipeline->cmds[i].argv) {
            for (int a = 0; a < pipeline->cmds[i].argc; a++)
                free(pipeline->cmds[i].argv[a]);
            free(pipeline->cmds[i].argv);
        }
        if (pipeline->cmds[i].redir.infile)
            free(pipeline->cmds[i].redir.infile);
        if (pipeline->cmds[i].redir.outfile)
            free(pipeline->cmds[i].redir.outfile);
    }

    free(pipeline->cmds);
    pipeline->cmds = NULL;
    pipeline->count = 0;
}
