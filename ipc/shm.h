/*
 * shm.h - Shared memory IPC primitives.
 */

#ifndef SHM_H
#define SHM_H

#include <stddef.h>

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
 */
void shm_detach(void *addr);

/**
 * Destroy a shared memory region.
 *
 * @param shm_id Region ID.
 * @return 0 on success, -1 on error.
 */
int shm_destroy(int shm_id);

#endif /* SHM_H */
