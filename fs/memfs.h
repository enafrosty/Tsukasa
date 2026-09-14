/*
 * Project Tsukasa — In-memory writable filesystem (temporary storage)
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

#ifndef MEMFS_H
#define MEMFS_H

#include <stdint.h>
#include <stddef.h>

#define MEMFS_MAX_FILES    16
#define MEMFS_MAX_NAME     64
#define MEMFS_MAX_FD        8   /* concurrent open file descriptors */
#define MEMFS_INIT_CAP   4096   /* initial buffer size per file      */

typedef struct {
    char     name[MEMFS_MAX_NAME];
    uint8_t *data;
    size_t   size;
    size_t   capacity;
    int      used;
} memfs_inode_t;

void memfs_init(void);

/* Create a new file; returns inode index or -1 on failure. */
int memfs_create(const char *name);

/* Open an existing file; returns inode index or -1. */
int memfs_open(const char *name);

/* Read data from an inode at given position. */
size_t memfs_read(int inode, size_t pos, void *buf, size_t count);

/* Write data into an inode at given position (grows as needed). */
size_t memfs_write(int inode, size_t pos, const void *buf, size_t count);

/* Get size of an inode's data. */
size_t memfs_size(int inode);

/* Truncate inode data to zero bytes. */
int memfs_truncate(int inode);

/* Stat by file name. */
int memfs_stat(const char *name, size_t *size_out);

/* List all valid file names. */
int memfs_list(char names[][MEMFS_MAX_NAME], int max);

/* Delete file by name. */
int memfs_unlink(const char *name);

/* Rename file. */
int memfs_rename(const char *old_name, const char *new_name);

#endif /* MEMFS_H */

