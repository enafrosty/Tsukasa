/*
 * bootfs.c - Boot module index filesystem.
 *
 * Layout:
 *   /boot/modules            (text index)
 *   /boot/<module-name>      (raw module bytes)
 */

#include "bootfs.h"

#include "../include/boot_info.h"
#include "../include/multiboot.h"
#include "../mm/heap.h"

#include <stddef.h>
#include <stdint.h>

#define BOOTFS_MAX_MODULES 32

typedef struct bootfs_module {
    int used;
    const void *data;
    size_t size;
    const char *path;
    const char *cmdline;
    char name[VFS_NAME_MAX];
} bootfs_module_t;

typedef struct out_buf {
    char *data;
    size_t len;
    size_t cap;
} out_buf_t;

static bootfs_module_t g_modules[BOOTFS_MAX_MODULES];
static int g_module_count;

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

static void build_module_name(char *out, int cap, const char *path, int idx)
{
    int oi = 0;
    const char *src = path;
    if (src) {
        int last = -1;
        int len = kstrlen(src);
        for (int i = 0; i < len; i++) {
            if (src[i] == '/' || src[i] == '\\')
                last = i;
        }
        if (last >= 0)
            src = path + last + 1;
    }
    if (!src || !src[0])
        src = "module";
    while (src[oi] && oi < cap - 1) {
        char c = src[oi];
        out[oi] = (c == ' ') ? '_' : c;
        oi++;
    }
    out[oi] = '\0';
    if (!out[0]) {
        kstrncpy(out, "module", cap);
        oi = 6;
    }
    if (out[0] == '\0') {
        int n = idx;
        char rev[16];
        int ri = 0;
        kstrncpy(out, "module", cap);
        if (n < 0)
            return;
        if (n == 0) {
            int l = kstrlen(out);
            if (l < cap - 1) {
                out[l] = '0';
                out[l + 1] = '\0';
            }
            return;
        }
        while (n > 0 && ri < (int)sizeof(rev)) {
            rev[ri++] = (char)('0' + (n % 10));
            n /= 10;
        }
        while (ri > 0) {
            int l = kstrlen(out);
            if (l >= cap - 1)
                break;
            out[l] = rev[--ri];
            out[l + 1] = '\0';
        }
    }
}

void bootfs_init(const void *boot_info)
{
    int i;
    g_module_count = 0;
    for (i = 0; i < BOOTFS_MAX_MODULES; i++)
        g_modules[i].used = 0;

    if (tsukasa_boot_info_is_valid(boot_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)boot_info;
        uint64_t n = bi->module_count;
        if (n > BOOTFS_MAX_MODULES)
            n = BOOTFS_MAX_MODULES;
        for (i = 0; i < (int)n; i++) {
            g_modules[i].used = 1;
            g_modules[i].data = (const void *)(uintptr_t)bi->modules[i].address;
            g_modules[i].size = (size_t)bi->modules[i].size;
            g_modules[i].path = bi->modules[i].path;
            g_modules[i].cmdline = bi->modules[i].cmdline;
            build_module_name(g_modules[i].name, VFS_NAME_MAX, bi->modules[i].path, i);
            g_module_count++;
        }
        return;
    }

    {
        const struct multiboot_info *mb = (const struct multiboot_info *)boot_info;
        if (!mb || !(mb->flags & 8) || mb->mods_count == 0)
            return;
        {
            const struct multiboot_mod_list *mods =
                (const struct multiboot_mod_list *)(uintptr_t)mb->mods_addr;
            uint32_t n = mb->mods_count;
            if (n > BOOTFS_MAX_MODULES)
                n = BOOTFS_MAX_MODULES;
            for (i = 0; i < (int)n; i++) {
                g_modules[i].used = 1;
                g_modules[i].data = (const void *)(uintptr_t)mods[i].mod_start;
                g_modules[i].size = (size_t)(mods[i].mod_end - mods[i].mod_start);
                g_modules[i].path = NULL;
                g_modules[i].cmdline = (const char *)(uintptr_t)mods[i].cmdline;
                build_module_name(g_modules[i].name, VFS_NAME_MAX, NULL, i);
                g_module_count++;
            }
        }
    }
}

int bootfs_module_count(void)
{
    return g_module_count;
}

static bootfs_module_t *find_module_by_name(const char *name)
{
    for (int i = 0; i < g_module_count; i++) {
        if (g_modules[i].used && kstreq(g_modules[i].name, name))
            return &g_modules[i];
    }
    return NULL;
}

int bootfs_stat(const char *path, vfs_stat_t *out)
{
    if (!path || !out)
        return -1;
    out->mode = VFS_MODE_READ;
    out->size = 0;
    out->blocks = 0;

    if (kstreq(path, "/")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (kstreq(path, "/modules")) {
        out->type = VFS_TYPE_FILE;
        return 0;
    }
    if (path[0] == '/' && path[1]) {
        bootfs_module_t *m = find_module_by_name(path + 1);
        if (!m)
            return -1;
        out->type = VFS_TYPE_FILE;
        out->size = m->size;
        out->blocks = (m->size + 511) / 512;
        return 0;
    }
    return -1;
}

int bootfs_list(const char *path, char names[][VFS_NAME_MAX], int max)
{
    int count = 0;
    if (!path || !names || max <= 0)
        return -1;
    if (!kstreq(path, "/"))
        return -1;
    kstrncpy(names[count++], "modules", VFS_NAME_MAX);
    for (int i = 0; i < g_module_count && count < max; i++) {
        if (g_modules[i].used)
            kstrncpy(names[count++], g_modules[i].name, VFS_NAME_MAX);
    }
    return count;
}

static int build_modules_index(out_buf_t *ob)
{
    for (int i = 0; i < g_module_count; i++) {
        if (!g_modules[i].used)
            continue;
        if (out_append_str(ob, g_modules[i].name) != 0) return -1;
        if (out_append_str(ob, " size=") != 0) return -1;
        if (out_append_u64(ob, g_modules[i].size) != 0) return -1;
        if (g_modules[i].path && g_modules[i].path[0]) {
            if (out_append_str(ob, " path=") != 0) return -1;
            if (out_append_str(ob, g_modules[i].path) != 0) return -1;
        }
        if (g_modules[i].cmdline && g_modules[i].cmdline[0]) {
            if (out_append_str(ob, " cmdline=") != 0) return -1;
            if (out_append_str(ob, g_modules[i].cmdline) != 0) return -1;
        }
        if (out_append_str(ob, "\n") != 0) return -1;
    }
    return 0;
}

int bootfs_read_file(const char *path,
                     const void **data_out,
                     size_t *size_out,
                     int *owns_buffer_out)
{
    if (!path || !data_out || !size_out || !owns_buffer_out)
        return -1;

    if (kstreq(path, "/modules")) {
        out_buf_t ob;
        ob.data = NULL;
        ob.len = 0;
        ob.cap = 0;
        if (build_modules_index(&ob) != 0) {
            if (ob.data)
                kfree(ob.data);
            return -1;
        }
        if (!ob.data) {
            ob.data = (char *)kmalloc(1);
            if (!ob.data)
                return -1;
            ob.data[0] = '\0';
        }
        *data_out = ob.data;
        *size_out = ob.len;
        *owns_buffer_out = 1;
        return 0;
    }

    if (path[0] == '/' && path[1]) {
        bootfs_module_t *m = find_module_by_name(path + 1);
        if (!m)
            return -1;
        *data_out = m->data;
        *size_out = m->size;
        *owns_buffer_out = 0;
        return 0;
    }

    return -1;
}
