/*
 * procfs.c - Read-only process pseudo filesystem.
 *
 * Layout:
 *   /proc/processes           (file)
 *   /proc/self/               (dir)
 *   /proc/self/status         (file)
 *   /proc/<pid>/              (dir)
 *   /proc/<pid>/status        (file)
 */

#include "procfs.h"

#include "../mm/heap.h"
#include "../proc/process.h"

#include <stddef.h>
#include <stdint.h>

#ifndef __x86_64__

void procfs_init(void)
{
}

int procfs_stat(const char *path, vfs_stat_t *out)
{
    (void)path;
    (void)out;
    return -1;
}

int procfs_list(const char *path, char names[][VFS_NAME_MAX], int max)
{
    (void)path;
    (void)names;
    (void)max;
    return -1;
}

int procfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out)
{
    (void)path;
    (void)buf_out;
    (void)size_out;
    return -1;
}

#else

typedef struct out_buf {
    char *data;
    size_t len;
    size_t cap;
} out_buf_t;

static int kstrlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int kstrcmp(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int kstreq(const char *a, const char *b)
{
    return kstrcmp(a, b) == 0;
}

static int kstrncpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return -1;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] ? -1 : 0;
}

static int parse_u32(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    int i = 0;
    if (!s || !s[0])
        return -1;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10u + (uint64_t)(s[i] - '0');
        if (v > 0xFFFFFFFFu)
            return -1;
        i++;
    }
    if (out)
        *out = (uint32_t)v;
    return 0;
}

static const char *state_name(process_state_t st)
{
    switch (st) {
    case PROCESS_CREATED: return "created";
    case PROCESS_READY: return "ready";
    case PROCESS_RUNNING: return "running";
    case PROCESS_BLOCKED: return "blocked";
    case PROCESS_SLEEPING: return "sleeping";
    case PROCESS_STOPPED: return "stopped";
    case PROCESS_ZOMBIE: return "zombie";
    case PROCESS_DEAD: return "dead";
    default: return "unknown";
    }
}

static int out_reserve(out_buf_t *ob, size_t add)
{
    char *nb;
    size_t ncap;
    if (!ob)
        return -1;
    if (ob->len + add + 1 <= ob->cap)
        return 0;
    ncap = ob->cap ? ob->cap : 128;
    while (ncap < ob->len + add + 1)
        ncap *= 2;
    nb = (char *)kmalloc(ncap);
    if (!nb)
        return -1;
    for (size_t i = 0; i < ob->len; i++)
        nb[i] = ob->data[i];
    if (ob->data)
        kfree(ob->data);
    ob->data = nb;
    ob->cap = ncap;
    if (ob->len == 0)
        ob->data[0] = '\0';
    return 0;
}

static int out_append_str(out_buf_t *ob, const char *s)
{
    int slen = kstrlen(s);
    if (out_reserve(ob, (size_t)slen) != 0)
        return -1;
    for (int i = 0; i < slen; i++)
        ob->data[ob->len++] = s[i];
    ob->data[ob->len] = '\0';
    return 0;
}

static int out_append_u64(out_buf_t *ob, uint64_t v)
{
    char tmp[24];
    int i = 0;
    if (v == 0)
        return out_append_str(ob, "0");
    while (v > 0 && i < (int)(sizeof(tmp) - 1)) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (out_reserve(ob, (size_t)i) != 0)
        return -1;
    while (i > 0)
        ob->data[ob->len++] = tmp[--i];
    ob->data[ob->len] = '\0';
    return 0;
}

static int fill_status(uint32_t pid, out_buf_t *ob)
{
    process_snapshot_t ps;
    if (process_get_info((int)pid, &ps) != 0)
        return -1;
    if (out_append_str(ob, "pid: ") != 0) return -1;
    if (out_append_u64(ob, ps.pid) != 0) return -1;
    if (out_append_str(ob, "\nppid: ") != 0) return -1;
    if (out_append_u64(ob, ps.ppid) != 0) return -1;
    if (out_append_str(ob, "\npgid: ") != 0) return -1;
    if (out_append_u64(ob, ps.pgid) != 0) return -1;
    if (out_append_str(ob, "\nname: ") != 0) return -1;
    if (out_append_str(ob, ps.name) != 0) return -1;
    if (out_append_str(ob, "\nstate: ") != 0) return -1;
    if (out_append_str(ob, state_name(ps.state)) != 0) return -1;
    if (out_append_str(ob, "\ncwd: ") != 0) return -1;
    if (out_append_str(ob, ps.cwd) != 0) return -1;
    if (out_append_str(ob, "\nticks: ") != 0) return -1;
    if (out_append_u64(ob, ps.ticks) != 0) return -1;
    if (out_append_str(ob, "\nshm_attachments: ") != 0) return -1;
    if (out_append_u64(ob, ps.shm_attachments) != 0) return -1;
    if (out_append_str(ob, "\ntty: ") != 0) return -1;
    if (out_append_u64(ob, (uint64_t)(uint32_t)ps.tty_id) != 0) return -1;
    if (out_append_str(ob, "\ncmdline: ") != 0) return -1;
    if (out_append_str(ob, ps.cmdline) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;
    return 0;
}

static int is_pid_dir(const char *path, uint32_t *pid_out)
{
    if (!path || path[0] != '/')
        return 0;
    return parse_u32(path + 1, pid_out) == 0;
}

static int is_pid_status(const char *path, uint32_t *pid_out)
{
    int i = 1;
    uint32_t pid;
    char num[16];
    int ni = 0;
    if (!path || path[0] != '/')
        return 0;
    while (path[i] && path[i] != '/' && ni < (int)sizeof(num) - 1)
        num[ni++] = path[i++];
    num[ni] = '\0';
    if (path[i] != '/' || kstrcmp(path + i, "/status") != 0)
        return 0;
    if (parse_u32(num, &pid) != 0)
        return 0;
    if (pid_out)
        *pid_out = pid;
    return 1;
}

void procfs_init(void)
{
}

int procfs_stat(const char *path, vfs_stat_t *out)
{
    uint32_t pid = 0;
    if (!path || !out)
        return -1;
    out->mode = VFS_MODE_READ;
    out->blocks = 0;
    out->size = 0;

    if (kstreq(path, "/")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (kstreq(path, "/processes")) {
        out->type = VFS_TYPE_FILE;
        return 0;
    }
    if (kstreq(path, "/self")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (kstreq(path, "/self/status")) {
        out->type = VFS_TYPE_FILE;
        return 0;
    }
    if (is_pid_dir(path, &pid) && process_get_info((int)pid, NULL) == 0) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (is_pid_status(path, &pid) && process_get_info((int)pid, NULL) == 0) {
        out->type = VFS_TYPE_FILE;
        return 0;
    }
    return -1;
}

int procfs_list(const char *path, char names[][VFS_NAME_MAX], int max)
{
    process_snapshot_t *procs = NULL;
    int count = 0;
    int n = 0;

    if (!path || !names || max <= 0)
        return -1;

    if (kstreq(path, "/")) {
        kstrncpy(names[count++], "processes", VFS_NAME_MAX);
        if (count < max)
            kstrncpy(names[count++], "self", VFS_NAME_MAX);
        procs = (process_snapshot_t *)kmalloc(sizeof(process_snapshot_t) * PROCESS_MAX_COUNT);
        if (!procs)
            return count;
        n = process_snapshot(procs, PROCESS_MAX_COUNT);
        if (n < 0)
            goto out;
        for (int i = 0; i < n && count < max; i++) {
            char pid_buf[VFS_NAME_MAX];
            int bi = 0;
            uint32_t pid = procs[i].pid;
            char rev[16];
            int ri = 0;
            if (pid == 0)
                continue;
            while (pid > 0 && ri < (int)sizeof(rev)) {
                rev[ri++] = (char)('0' + (pid % 10u));
                pid /= 10u;
            }
            while (ri > 0 && bi < VFS_NAME_MAX - 1)
                pid_buf[bi++] = rev[--ri];
            pid_buf[bi] = '\0';
            kstrncpy(names[count++], pid_buf, VFS_NAME_MAX);
        }
        goto out;
    }

    if (kstreq(path, "/self")) {
        kstrncpy(names[0], "status", VFS_NAME_MAX);
        return 1;
    }

    {
        uint32_t pid = 0;
        if (is_pid_dir(path, &pid) && process_get_info((int)pid, NULL) == 0) {
            kstrncpy(names[0], "status", VFS_NAME_MAX);
            count = 1;
            goto out;
        }
    }
    count = -1;

out:
    if (procs)
        kfree(procs);
    return count;
}

int procfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out)
{
    out_buf_t ob;
    process_snapshot_t *procs = NULL;
    uint32_t pid = 0;
    int n = 0;

    if (!path || !buf_out || !size_out)
        return -1;

    ob.data = NULL;
    ob.len = 0;
    ob.cap = 0;

    if (kstreq(path, "/processes")) {
        procs = (process_snapshot_t *)kmalloc(sizeof(process_snapshot_t) * PROCESS_MAX_COUNT);
        if (!procs)
            goto fail;
        if (out_append_str(&ob, "pid ppid state name cwd\n") != 0)
            goto fail;
        n = process_snapshot(procs, PROCESS_MAX_COUNT);
        if (n < 0)
            goto fail;
        for (int i = 0; i < n; i++) {
            if (out_append_u64(&ob, procs[i].pid) != 0) goto fail;
            if (out_append_str(&ob, " ") != 0) goto fail;
            if (out_append_u64(&ob, procs[i].ppid) != 0) goto fail;
            if (out_append_str(&ob, " ") != 0) goto fail;
            if (out_append_str(&ob, state_name(procs[i].state)) != 0) goto fail;
            if (out_append_str(&ob, " ") != 0) goto fail;
            if (out_append_str(&ob, procs[i].name) != 0) goto fail;
            if (out_append_str(&ob, " ") != 0) goto fail;
            if (out_append_str(&ob, procs[i].cwd) != 0) goto fail;
            if (out_append_str(&ob, "\n") != 0) goto fail;
        }
    } else if (kstreq(path, "/self/status")) {
        int cur = process_current_pid();
        if (cur <= 0)
            goto fail;
        if (fill_status((uint32_t)cur, &ob) != 0)
            goto fail;
    } else if (is_pid_status(path, &pid)) {
        if (fill_status(pid, &ob) != 0)
            goto fail;
    } else {
        goto fail;
    }

    if (!ob.data) {
        ob.data = (char *)kmalloc(1);
        if (!ob.data)
            goto fail;
        ob.data[0] = '\0';
    }

    if (procs) {
        kfree(procs);
        procs = NULL;
    }
    *buf_out = (uint8_t *)ob.data;
    *size_out = ob.len;
    return 0;

fail:
    if (procs)
        kfree(procs);
    if (ob.data)
        kfree(ob.data);
    return -1;
}

#endif /* __x86_64__ */
