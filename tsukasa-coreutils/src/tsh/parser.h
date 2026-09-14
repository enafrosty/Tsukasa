/*
 * Project Tsukasa — tsh Command Line Parser Header
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

#ifndef _TSH_PARSER_H
#define _TSH_PARSER_H

#include <stddef.h>

typedef struct tsh_redir {
    char *infile;
    char *outfile;
    int append;
} tsh_redir_t;

typedef struct tsh_cmd {
    char **argv;
    int argc;
    tsh_redir_t redir;
} tsh_cmd_t;

typedef struct tsh_pipeline {
    tsh_cmd_t *cmds;
    int count;
} tsh_pipeline_t;

int parser_parse_line(const char *line, tsh_pipeline_t *pipeline);
void parser_free_pipeline(tsh_pipeline_t *pipeline);

#endif /* _TSH_PARSER_H */
