/*
 * Project Tsukasa - AF_UNIX listener registry and dynamic stream ring buffers
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

#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

#include <stddef.h>
#include "../include/socket_defs.h"

#define UNIX_MAX_LISTENERS 64
#define UNIX_BACKLOG        16
#define UNIX_RING_CAPACITY 4096

typedef struct unix_listener unix_listener_t;
typedef struct unix_ring unix_ring_t;

typedef struct unix_pending_conn {
    unix_ring_t *pipe1;
    unix_ring_t *pipe2;
    int          client_pid;
    int          client_fd;
} unix_pending_conn_t;

/* Dynamically allocated ring buffer for AF_UNIX stream endpoints. */
unix_ring_t *unix_ring_create(void);
void unix_ring_drop_claim(unix_ring_t *r, int drop_reader, int drop_writer);
size_t unix_ring_read(unix_ring_t *r, void *buf, size_t count, int nonblock, int *err_out);
size_t unix_ring_write(unix_ring_t *r, const void *buf, size_t count, int nonblock, int *err_out);
size_t unix_ring_avail(unix_ring_t *r);
int unix_ring_poll_mask(unix_ring_t *rx, unix_ring_t *tx);

/* Register a listener for a pathname. */
int unix_register_listener(const char *path, int owner_pid, int owner_fd);

/* Unregister by pathname. */
int unix_unregister_listener(const char *path);

/* Find a listener by path; NULL if not registered. */
unix_listener_t *unix_find_listener(const char *path);

void unix_listener_set_listening(unix_listener_t *lst, int listening);
int  unix_listener_is_listening(unix_listener_t *lst);

/* Enqueue a pending connection (copied by value). */
int unix_enqueue_pending(unix_listener_t *lst, const unix_pending_conn_t *pc);

/* Dequeue the OLDEST pending connection (FIFO) into *out. 0 on success, -1 when empty. */
int unix_dequeue_pending(unix_listener_t *lst, unix_pending_conn_t *out);

int unix_listener_has_pending(unix_listener_t *lst);

#ifdef __x86_64__
#include "../sys/wait_queue.h"
wait_queue_head_t *unix_listener_get_accept_waitq(unix_listener_t *lst);
wait_queue_head_t *unix_ring_get_waitq(unix_ring_t *r);
#endif

void unix_socket_run_selftests(void);

#endif /* UNIX_SOCKET_H */
