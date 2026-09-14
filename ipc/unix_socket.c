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

#include "unix_socket.h"
#include "../include/errno.h"
#include "../include/spinlock.h"
#ifdef __x86_64__
/* The accept wait queue is a wakeup hook for future blocking accept (SPEC-K01 poll integration). */
#include "../sys/wait_queue.h"
#endif

struct unix_listener {
    int used;
    char path[TSK_UNIX_PATH_MAX];
    int owner_pid;
    int owner_fd;
    int listening;
    spinlock_t lock;
#ifdef __x86_64__
    wait_queue_head_t accept_waitq;
#endif
    unix_pending_conn_t pending[UNIX_BACKLOG];
    int pend_head;
    int pend_count;
};

static struct unix_listener g_listeners[UNIX_MAX_LISTENERS];
static spinlock_t g_registry_lock = SPINLOCK_INIT;

static int upath_len(const char *s)
{
    int n = 0;
    while (s && s[n] && n < TSK_UNIX_PATH_MAX)
        n++;
    return n;
}

static int upath_eq(const char *a, const char *b)
{
    int i = 0;
    while (i < TSK_UNIX_PATH_MAX && a[i] && b[i] && a[i] == b[i])
        i++;
    if (i >= TSK_UNIX_PATH_MAX)
        return 1;
    return a[i] == '\0' && b[i] == '\0';
}

/* Registry-lock-only path search (caller holds g_registry_lock). */
static struct unix_listener *find_locked(const char *path)
{
    for (int i = 0; i < UNIX_MAX_LISTENERS; i++) {
        if (g_listeners[i].used && upath_eq(g_listeners[i].path, path))
            return &g_listeners[i];
    }
    return (struct unix_listener *)0;
}

int unix_register_listener(const char *path, int owner_pid, int owner_fd)
{
    unsigned long flags;
    struct unix_listener *l = (struct unix_listener *)0;
    int len = upath_len(path);

    if (!path || len == 0 || len >= TSK_UNIX_PATH_MAX)
        return -EINVAL;

    flags = spin_lock_irqsave(&g_registry_lock);
    if (find_locked(path)) {
        spin_unlock_irqrestore(&g_registry_lock, flags);
        return -EADDRINUSE;
    }
    for (int i = 0; i < UNIX_MAX_LISTENERS; i++) {
        if (!g_listeners[i].used) {
            l = &g_listeners[i];
            break;
        }
    }
    if (!l) {
        spin_unlock_irqrestore(&g_registry_lock, flags);
        return -ENOSPC;
    }
    l->used = 1;
    for (int i = 0; i < TSK_UNIX_PATH_MAX; i++)
        l->path[i] = (i < len) ? path[i] : '\0';
    l->owner_pid = owner_pid;
    l->owner_fd = owner_fd;
    l->listening = 0;
    l->lock = SPINLOCK_INIT;
#ifdef __x86_64__
    wait_queue_init(&l->accept_waitq);
#endif
    l->pend_head = 0;
    l->pend_count = 0;
    spin_unlock_irqrestore(&g_registry_lock, flags);
    return 0;
}

int unix_unregister_listener(const char *path)
{
    unsigned long flags;
    struct unix_listener *l;

    if (!path)
        return -EINVAL;
    flags = spin_lock_irqsave(&g_registry_lock);
    l = find_locked(path);
    if (!l) {
        spin_unlock_irqrestore(&g_registry_lock, flags);
        return -ENOENT;
    }
    /* The owner dequeues pendings before unregistering (fs/vfs.c drops the pipe claims); anything left is... */
    l->used = 0;
    l->path[0] = '\0';
    l->listening = 0;
    l->pend_head = 0;
    l->pend_count = 0;
    spin_unlock_irqrestore(&g_registry_lock, flags);
    return 0;
}

unix_listener_t *unix_find_listener(const char *path)
{
    unsigned long flags;
    struct unix_listener *l;

    if (!path)
        return (unix_listener_t *)0;
    flags = spin_lock_irqsave(&g_registry_lock);
    l = find_locked(path);
    spin_unlock_irqrestore(&g_registry_lock, flags);
    return l;
}

void unix_listener_set_listening(unix_listener_t *lst, int listening)
{
    if (lst)
        lst->listening = listening ? 1 : 0;
}

int unix_listener_is_listening(unix_listener_t *lst)
{
    return lst ? lst->listening : 0;
}

int unix_enqueue_pending(unix_listener_t *lst, const unix_pending_conn_t *pc)
{
    unsigned long flags;
    int slot;

    if (!lst || !pc)
        return -1;
    flags = spin_lock_irqsave(&lst->lock);
    if (!lst->used || lst->pend_count >= UNIX_BACKLOG) {
        spin_unlock_irqrestore(&lst->lock, flags);
        return -1;
    }
    slot = (lst->pend_head + lst->pend_count) % UNIX_BACKLOG;
    lst->pending[slot] = *pc;
    lst->pend_count++;
    spin_unlock_irqrestore(&lst->lock, flags);
#ifdef __x86_64__
    wait_queue_wake_all(&lst->accept_waitq);
#endif
    return 0;
}

int unix_dequeue_pending(unix_listener_t *lst, unix_pending_conn_t *out)
{
    unsigned long flags;

    if (!lst || !out)
        return -1;
    flags = spin_lock_irqsave(&lst->lock);
    if (lst->pend_count == 0) {
        spin_unlock_irqrestore(&lst->lock, flags);
        return -1;
    }
    *out = lst->pending[lst->pend_head];
    lst->pend_head = (lst->pend_head + 1) % UNIX_BACKLOG;
    lst->pend_count--;
    spin_unlock_irqrestore(&lst->lock, flags);
    return 0;
}

int unix_listener_has_pending(unix_listener_t *lst)
{
    unsigned long flags;
    int has;

    if (!lst)
        return 0;
    flags = spin_lock_irqsave(&lst->lock);
    has = (lst->pend_count > 0);
    spin_unlock_irqrestore(&lst->lock, flags);
    return has;
}

#ifdef __x86_64__
wait_queue_head_t *unix_listener_get_accept_waitq(unix_listener_t *lst)
{
    return lst ? &lst->accept_waitq : NULL;
}
#endif

void unix_socket_run_selftests(void)
{
    const char *test_path = "/tmp/test.sock";
    unix_listener_t *lst;
    unix_pending_conn_t in_conn;
    unix_pending_conn_t out_conn;
    int rc;

    /* Clean slate */
    unix_unregister_listener(test_path);

    rc = unix_register_listener(test_path, 1, 3);
    if (rc != 0) {
        return;
    }

    /* Duplicate registration must fail */
    if (unix_register_listener(test_path, 2, 4) != -EADDRINUSE) {
        unix_unregister_listener(test_path);
        return;
    }

    lst = unix_find_listener(test_path);
    if (!lst) {
        unix_unregister_listener(test_path);
        return;
    }

    unix_listener_set_listening(lst, 1);
    if (!unix_listener_is_listening(lst)) {
        unix_unregister_listener(test_path);
        return;
    }

    if (unix_listener_has_pending(lst)) {
        unix_unregister_listener(test_path);
        return;
    }

    in_conn.pipe1 = (void *)0x1000;
    in_conn.pipe2 = (void *)0x2000;
    in_conn.client_pid = 42;
    in_conn.client_fd = 5;

    if (unix_enqueue_pending(lst, &in_conn) != 0) {
        unix_unregister_listener(test_path);
        return;
    }

    if (!unix_listener_has_pending(lst)) {
        unix_unregister_listener(test_path);
        return;
    }

    out_conn.pipe1 = NULL;
    out_conn.pipe2 = NULL;
    out_conn.client_pid = 0;
    out_conn.client_fd = 0;

    if (unix_dequeue_pending(lst, &out_conn) != 0) {
        unix_unregister_listener(test_path);
        return;
    }

    if (out_conn.pipe1 != in_conn.pipe1 ||
        out_conn.pipe2 != in_conn.pipe2 ||
        out_conn.client_pid != in_conn.client_pid ||
        out_conn.client_fd != in_conn.client_fd) {
        unix_unregister_listener(test_path);
        return;
    }

    if (unix_listener_has_pending(lst)) {
        unix_unregister_listener(test_path);
        return;
    }

    unix_unregister_listener(test_path);
}
