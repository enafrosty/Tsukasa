/*
 * exec.c - Builtin executable registry used by spawn/exec.
 */

#include "exec.h"

#include <stdint.h>

#include "../include/spinlock.h"

#define EXEC_BUILTIN_MAX 32
#define EXEC_PATH_MAX    64

struct exec_builtin {
    int used;
    char path[EXEC_PATH_MAX];
    exec_entry_t entry;
};

static struct exec_builtin g_exec_builtins[EXEC_BUILTIN_MAX];
static spinlock_t g_exec_lock = SPINLOCK_INIT;

static int path_equal(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return (a[i] == '\0' && b[i] == '\0') ? 1 : 0;
}

static void path_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int exec_register_builtin(const char *path, exec_entry_t entry)
{
    int free_slot = -1;
    if (!path || !entry)
        return -1;

    spin_lock(&g_exec_lock);
    for (int i = 0; i < EXEC_BUILTIN_MAX; i++) {
        if (g_exec_builtins[i].used && path_equal(g_exec_builtins[i].path, path)) {
            g_exec_builtins[i].entry = entry;
            spin_unlock(&g_exec_lock);
            return 0;
        }
        if (!g_exec_builtins[i].used && free_slot < 0)
            free_slot = i;
    }

    if (free_slot < 0) {
        spin_unlock(&g_exec_lock);
        return -1;
    }

    g_exec_builtins[free_slot].used = 1;
    g_exec_builtins[free_slot].entry = entry;
    path_copy(g_exec_builtins[free_slot].path, path, EXEC_PATH_MAX);
    spin_unlock(&g_exec_lock);
    return 0;
}

int exec_resolve_builtin(const char *path, exec_entry_t *entry_out)
{
    if (!path || !entry_out)
        return -1;

    spin_lock(&g_exec_lock);
    for (int i = 0; i < EXEC_BUILTIN_MAX; i++) {
        if (g_exec_builtins[i].used && path_equal(g_exec_builtins[i].path, path)) {
            *entry_out = g_exec_builtins[i].entry;
            spin_unlock(&g_exec_lock);
            return 0;
        }
    }
    spin_unlock(&g_exec_lock);
    return -1;
}

