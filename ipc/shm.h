/*
 * shm.h - Shared memory IPC primitives.
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

/**
 * Create a shared memory region.
 *
 * @param size Size in bytes (will be rounded up to pages).
 * @return shm_id (>= 0) on success, -1 on error.
 */
int shm_create(size_t size);

/**
 * Attach a shared memory region to the current process.
 *
 * @param shm_id Region ID from shm_create.
 * @return Virtual address of mapping, or NULL on error.
 */
void *shm_attach(int shm_id);

/**
 * Detach a shared memory region.
 *
 * @param addr Address returned by shm_attach.
 * @return 0 on success, -1 on error.
 */
int shm_detach(void *addr);

/**
 * Destroy a shared memory region.
 *
 * @param shm_id Region ID.
 * @return 0 on success, -1 on error.
 */
int shm_destroy(int shm_id);

/**
 * Cleanup SHM attachments owned by a process during exit/reap.
 *
 * @param proc Process to cleanup.
 */
void shm_process_cleanup(struct process *proc);

/**
 * Populate snapshot statistics for diagnostics.
 */
void shm_get_stats(struct shm_stats *out);

/**
 * Emit SHM state to serial log.
 */
void shm_dump_state(void);

#endif /* SHM_H */
