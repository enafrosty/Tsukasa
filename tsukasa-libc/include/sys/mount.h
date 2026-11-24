/*
 * Project Tsukasa - Mount Management Header
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

#ifndef _TSUKASA_SYS_MOUNT_H
#define _TSUKASA_SYS_MOUNT_H

#define MS_RDONLY       1
#define MNT_FORCE       1
#define MNT_DETACH      2
#define MNT_EXPIRE      4
#define UMOUNT_NOFOLLOW 8

int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data);
int umount(const char *target);
int umount2(const char *target, int flags);

#endif /* _TSUKASA_SYS_MOUNT_H */
