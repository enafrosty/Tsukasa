/*
 * Project Tsukasa — Minimal ELF64 user-process loader (SPEC-C01 option A foundation)
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

#ifndef TSUKASA_ELF64_H
#define TSUKASA_ELF64_H

/* Spawn a ring-3 process from an ET_EXEC ELF64 at the given VFS path. Returns pid >= 0 on success, negative... */
int elf64_spawn(const char *path, const char *name);

/* Same, with a spawn cmdline that becomes the process's argv[] (tokenized with app_tokenize()-compatible... */
int elf64_spawn_cmdline(const char *path, const char *args, const char *name);

#endif
