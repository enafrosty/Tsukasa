/*
 * Project Tsukasa — AF_UNIX listener registry (guide 20)
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

#include "../include/socket_defs.h"

#define UNIX_MAX_LISTENERS 16
#define UNIX_BACKLOG        8

typedef struct unix_listener unix_listener_t;   /* opaque */

typedef struct unix_pending_conn {
    void *pipe1;
    void *pipe2;
    int   client_pid;
    int   client_fd;
} unix_pending_conn_t;

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

/* Dequeue the OLDEST pending connection (FIFO) into *out. 0 on success, -1 when the queue is empty. */
int unix_dequeue_pending(unix_listener_t *lst, unix_pending_conn_t *out);

int unix_listener_has_pending(unix_listener_t *lst);

#ifdef __x86_64__
#include "../sys/wait_queue.h"
wait_queue_head_t *unix_listener_get_accept_waitq(unix_listener_t *lst);
#endif

void unix_socket_run_selftests(void);

#endif /* UNIX_SOCKET_H */
