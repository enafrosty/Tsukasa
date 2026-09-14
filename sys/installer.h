/*
 * Project Tsukasa — disk installer core (guide 13)
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

#ifndef TSUKASA_INSTALLER_H
#define TSUKASA_INSTALLER_H

#include "../drv/blockdev.h"

/* 64 MiB floor: bootblob region + one FAT32 partition >= 32 MiB. */
#define INSTALLER_MIN_SECTORS 131072u

typedef void (*installer_progress_fn)(const char *stage, int percent);

/* Validate `bd` as an install target and the live medium as an install source... */
int installer_preflight(block_dev_t *bd, char *err, int errlen);

/* DESTRUCTIVE. */
int installer_run(block_dev_t *bd, installer_progress_fn cb);

#endif /* TSUKASA_INSTALLER_H */
