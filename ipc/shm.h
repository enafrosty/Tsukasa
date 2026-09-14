/*
 * Project Tsukasa — Shared memory IPC primitives
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

#ifndef SHM_H
#define SHM_H

#include <stddef.h>
#include <stdint.h>

struct process;

struct shm_stats {
    size_t region_count;
    size_t attachment_count;
    size_t reserved_pages;
};

/* Create a shared memory region. */
int shm_create(size_t size);

/* Attach a shared memory region to the current process. */
void *shm_attach(int shm_id);

/* Map a shared memory region (alias of shm_attach). */
void *shm_map(int shm_id);

/* Detach a shared memory region. @param addr Address returned by shm_attach. @return 0 on success, -1 on error. */
int shm_detach(void *addr);

/* Unmap a shared memory region (alias of shm_detach). */
int shm_unmap(void *addr);

/* Destroy a shared memory region. @param shm_id Region ID. @return 0 on success, -1 on error. */
int shm_destroy(int shm_id);

/* Cleanup SHM attachments owned by a process during exit/reap. @param proc Process to cleanup. */
void shm_process_cleanup(struct process *proc);

/* Populate snapshot statistics for diagnostics. */
void shm_get_stats(struct shm_stats *out);

/* Emit SHM state to serial log. */
void shm_dump_state(void);

#endif /* SHM_H */
