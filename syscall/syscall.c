/*
 * syscall.c - Multiplexed syscall dispatcher for Phase 2.
 */

#include "syscall.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#include "../fs/vfs.h"
#include "../drv/rtc.h"
#include "../include/kprintf.h"
#include "../ipc/shm.h"
#include "../loader/exec.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../net/network.h"
#include "../proc/process.h"
#include "../proc/signal.h"
#include "../tty/tty.h"
#include "../gfx/desktop.h"
#include "../gfx/theme.h"
#include "../gfx/gui_srv.h"

struct tsukasa_sigaction {
    uintptr_t sa_handler;
    uint64_t sa_mask;
    int sa_flags;
};

static struct tsukasa_theme_state g_theme_state;
static int g_theme_loaded;

#define THEME_CFG_VERSION 1u
#define THEME_CFG_PRIMARY  "/disk/tsukasa/theme.cfg"
#define THEME_CFG_FALLBACK "/tmp/theme.cfg"
#define THEME_CFG_MAX      1024u

static void copy_small_string(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static size_t small_strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int small_streq(const char *a, const char *b)
{
    size_t i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int is_space_char(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static char *trim_ws(char *s)
{
    char *end;
    if (!s)
        return s;
    while (*s && is_space_char(*s))
        s++;
    end = s;
    while (*end)
        end++;
    while (end > s && is_space_char(end[-1]))
        end--;
    *end = '\0';
    return s;
}

static int parse_hex_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int digits = 0;
    const char *p = s;
    if (!s || !out)
        return -1;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        p += 2;
    while (*p) {
        uint32_t d;
        if (*p >= '0' && *p <= '9')
            d = (uint32_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            d = 10u + (uint32_t)(*p - 'a');
        else if (*p >= 'A' && *p <= 'F')
            d = 10u + (uint32_t)(*p - 'A');
        else
            return -1;
        v = (v << 4) | d;
        digits++;
        if (digits > 8)
            return -1;
        p++;
    }
    if (digits <= 0)
        return -1;
    *out = v;
    return 0;
}

static int parse_dec_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int digits = 0;
    const char *p = s;
    if (!s || !out)
        return -1;
    while (*p) {
        uint32_t d;
        if (*p < '0' || *p > '9')
            return -1;
        d = (uint32_t)(*p - '0');
        if (v > 429496729u || (v == 429496729u && d > 5u))
            return -1;
        v = v * 10u + d;
        digits++;
        p++;
    }
    if (digits <= 0)
        return -1;
    *out = v;
    return 0;
}

static int validate_theme_color(uint32_t argb)
{
    return ((argb >> 24) & 0xFFu) == 0xFFu;
}

static int validate_bg_mode(uint32_t mode)
{
    return mode == TSUKASA_THEME_BG_GRADIENT ||
           mode == TSUKASA_THEME_BG_SOLID ||
           mode == TSUKASA_THEME_BG_WALLPAPER;
}

static int validate_wallpaper_style(uint32_t mode)
{
    return mode == TSUKASA_THEME_WP_SCALE_FILL ||
           mode == TSUKASA_THEME_WP_CENTER;
}

static int validate_wallpaper_path(const char *path, int allow_empty)
{
    size_t n;
    if (!path)
        return allow_empty ? 0 : -1;
    n = small_strlen(path);
    if (n == 0)
        return allow_empty ? 0 : -1;
    if (n >= sizeof(g_theme_state.wallpaper_path))
        return -1;
    if (path[0] != '/')
        return -1;
    for (size_t i = 0; i < n; i++) {
        char c = path[i];
        if (c < 32 || c > 126)
            return -1;
        if (c == '\\')
            return -1;
    }
    for (size_t i = 0; i + 1 < n; i++) {
        if (path[i] == '.' && path[i + 1] == '.') {
            char prev = (i > 0) ? path[i - 1] : '/';
            char next = (i + 2 < n) ? path[i + 2] : '/';
            if (prev == '/' && next == '/')
                return -1;
        }
    }
    return 0;
}

static const char *bg_mode_to_str(uint32_t mode)
{
    switch (mode) {
    case TSUKASA_THEME_BG_GRADIENT:  return "gradient";
    case TSUKASA_THEME_BG_SOLID:     return "solid";
    case TSUKASA_THEME_BG_WALLPAPER: return "wallpaper";
    default:                         return "gradient";
    }
}

static int parse_bg_mode(const char *s, uint32_t *mode_out)
{
    if (!s || !mode_out)
        return -1;
    if (small_streq(s, "gradient")) {
        *mode_out = TSUKASA_THEME_BG_GRADIENT;
        return 0;
    }
    if (small_streq(s, "solid")) {
        *mode_out = TSUKASA_THEME_BG_SOLID;
        return 0;
    }
    if (small_streq(s, "wallpaper")) {
        *mode_out = TSUKASA_THEME_BG_WALLPAPER;
        return 0;
    }
    return -1;
}

static const char *wallpaper_style_to_str(uint32_t style)
{
    return (style == TSUKASA_THEME_WP_CENTER) ? "center" : "scale-fill";
}

static int parse_wallpaper_style(const char *s, uint32_t *style_out)
{
    if (!s || !style_out)
        return -1;
    if (small_streq(s, "scale-fill")) {
        *style_out = TSUKASA_THEME_WP_SCALE_FILL;
        return 0;
    }
    if (small_streq(s, "center")) {
        *style_out = TSUKASA_THEME_WP_CENTER;
        return 0;
    }
    return -1;
}

static void theme_set_defaults(struct tsukasa_theme_state *st)
{
    if (!st)
        return;
    st->accent_color = THEME_ACCENT_DEFAULT;
    st->background_mode = TSUKASA_THEME_BG_GRADIENT;
    st->solid_color = THEME_BG_BOT;
    st->wallpaper_style = TSUKASA_THEME_WP_SCALE_FILL;
    st->flags = 0;
    st->wallpaper_path[0] = '\0';
}

static int theme_apply_state(const struct tsukasa_theme_state *st)
{
    return desktop_apply_theme(st->accent_color,
                               st->background_mode,
                               st->solid_color,
                               st->wallpaper_style,
                               st->wallpaper_path);
}

static int theme_write_cfg_to(const char *path, const struct tsukasa_theme_state *st)
{
    int fd;
    char cfg[512];
    int len;
    if (!path || !st)
        return -1;
    len = ksprintf(cfg, sizeof(cfg),
                   "version=%u\n"
                   "accent_color=0x%08X\n"
                   "background_mode=%s\n"
                   "solid_color=0x%08X\n"
                   "wallpaper_path=%s\n"
                   "wallpaper_style=%s\n",
                   (uint32_t)THEME_CFG_VERSION,
                   st->accent_color,
                   bg_mode_to_str(st->background_mode),
                   st->solid_color,
                   st->wallpaper_path,
                   wallpaper_style_to_str(st->wallpaper_style));
    if (len <= 0 || (size_t)len >= sizeof(cfg))
        return -1;

    fd = vfs_open_flags(path, VFS_O_WRONLY | VFS_O_TRUNC | VFS_O_CREAT);
    if (fd < 0)
        return -1;

    if (vfs_write(fd, cfg, (size_t)len) != (size_t)len) {
        vfs_close(fd);
        return -1;
    }
    vfs_close(fd);
    return 0;
}

static int theme_persist_state(struct tsukasa_theme_state *st)
{
    if (!st)
        return -1;
    if (theme_write_cfg_to(THEME_CFG_PRIMARY, st) == 0) {
        st->flags &= ~TSUKASA_THEME_FLAG_EPHEMERAL;
        return 0;
    }
    if (theme_write_cfg_to(THEME_CFG_FALLBACK, st) == 0) {
        st->flags |= TSUKASA_THEME_FLAG_EPHEMERAL;
        return 0;
    }
    st->flags |= TSUKASA_THEME_FLAG_EPHEMERAL;
    return -1;
}

static int theme_read_cfg_from(const char *path, struct tsukasa_theme_state *out)
{
    int fd;
    size_t sz;
    size_t got;
    char *buf = NULL;
    char *scan;
    struct tsukasa_theme_state st;
    int has_version = 0;
    int has_accent = 0;
    int has_bg = 0;
    int has_solid = 0;
    int has_wall = 0;
    uint32_t ver = 0;

    if (!path || !out)
        return -1;

    fd = vfs_open(path);
    if (fd < 0)
        return -1;
    sz = vfs_size(fd);
    if (sz == 0 || sz > THEME_CFG_MAX) {
        vfs_close(fd);
        return -1;
    }
    buf = (char *)kmalloc(sz + 1u);
    if (!buf) {
        vfs_close(fd);
        return -1;
    }
    got = vfs_read(fd, buf, sz);
    vfs_close(fd);
    if (got != sz) {
        kfree(buf);
        return -1;
    }
    buf[sz] = '\0';

    theme_set_defaults(&st);
    scan = buf;
    while (*scan) {
        char *line = scan;
        char *eq;
        char *key;
        char *val;

        while (*scan && *scan != '\n' && *scan != '\r')
            scan++;
        if (*scan) {
            *scan = '\0';
            scan++;
            if (*scan == '\n' || *scan == '\r') {
                *scan = '\0';
                scan++;
            }
        }

        line = trim_ws(line);
        if (!line[0] || line[0] == '#')
            continue;
        eq = line;
        while (*eq && *eq != '=')
            eq++;
        if (*eq != '=')
            continue;
        *eq = '\0';
        key = trim_ws(line);
        val = trim_ws(eq + 1);

        if (small_streq(key, "version")) {
            if (parse_dec_u32(val, &ver) != 0)
                goto fail;
            has_version = 1;
        } else if (small_streq(key, "accent_color")) {
            if (parse_hex_u32(val, &st.accent_color) != 0)
                goto fail;
            if (!validate_theme_color(st.accent_color))
                goto fail;
            has_accent = 1;
        } else if (small_streq(key, "background_mode")) {
            if (parse_bg_mode(val, &st.background_mode) != 0)
                goto fail;
            has_bg = 1;
        } else if (small_streq(key, "solid_color")) {
            if (parse_hex_u32(val, &st.solid_color) != 0)
                goto fail;
            if (!validate_theme_color(st.solid_color))
                goto fail;
            has_solid = 1;
        } else if (small_streq(key, "wallpaper_path")) {
            if (validate_wallpaper_path(val, 1) != 0)
                goto fail;
            copy_small_string(st.wallpaper_path, sizeof(st.wallpaper_path), val);
            has_wall = 1;
        } else if (small_streq(key, "wallpaper_style")) {
            if (parse_wallpaper_style(val, &st.wallpaper_style) != 0)
                goto fail;
        }
    }

    kfree(buf);
    if (!has_version || !has_accent || !has_bg || !has_solid || !has_wall)
        return -1;
    if (ver != THEME_CFG_VERSION)
        return -1;
    if (st.background_mode == TSUKASA_THEME_BG_WALLPAPER &&
        st.wallpaper_path[0] == '\0')
        return -1;
    *out = st;
    return 0;

fail:
    kfree(buf);
    return -1;
}

static void theme_ensure_loaded(void)
{
    if (g_theme_loaded)
        return;

    theme_set_defaults(&g_theme_state);
    if (theme_read_cfg_from(THEME_CFG_PRIMARY, &g_theme_state) == 0) {
        g_theme_state.flags &= ~TSUKASA_THEME_FLAG_EPHEMERAL;
    } else if (theme_read_cfg_from(THEME_CFG_FALLBACK, &g_theme_state) == 0) {
        g_theme_state.flags |= TSUKASA_THEME_FLAG_EPHEMERAL;
    } else {
        g_theme_state.flags = 0;
    }

    if (theme_apply_state(&g_theme_state) != 0 &&
        g_theme_state.background_mode == TSUKASA_THEME_BG_WALLPAPER) {
        g_theme_state.background_mode = TSUKASA_THEME_BG_GRADIENT;
        g_theme_state.wallpaper_path[0] = '\0';
        (void)theme_apply_state(&g_theme_state);
        (void)theme_persist_state(&g_theme_state);
    }
    g_theme_loaded = 1;
}

static uintptr_t handle_fs(uintptr_t cmd,
                           uintptr_t arg2,
                           uintptr_t arg3,
                           uintptr_t arg4,
                           uintptr_t arg5)
{
    switch (cmd) {
    case FS_CMD_OPEN:
        return (uintptr_t)vfs_open_flags((const char *)(uintptr_t)arg2, (int)arg3);
    case FS_CMD_READ:
        return (uintptr_t)vfs_read((int)arg2, (void *)(uintptr_t)arg3, (size_t)arg4);
    case FS_CMD_WRITE:
        return (uintptr_t)vfs_write((int)arg2, (const void *)(uintptr_t)arg3, (size_t)arg4);
    case FS_CMD_CLOSE:
        vfs_close((int)arg2);
        return 0;
    case FS_CMD_SEEK:
        return (uintptr_t)vfs_seek((int)arg2, (size_t)arg3, (int)arg4);
    case FS_CMD_SIZE:
        return (uintptr_t)vfs_size((int)arg2);
    case FS_CMD_CREATE:
        return (uintptr_t)vfs_create((const char *)(uintptr_t)arg2);
    case FS_CMD_LIST:
        return (uintptr_t)vfs_list((const char *)(uintptr_t)arg2,
                                   (char (*)[VFS_NAME_MAX])(uintptr_t)arg3,
                                   (int)arg4);
    case FS_CMD_TELL:
        return (uintptr_t)vfs_tell((int)arg2);
    case FS_CMD_STAT:
        return (uintptr_t)vfs_stat((const char *)(uintptr_t)arg2,
                                   (vfs_stat_t *)(uintptr_t)arg3);
    case FS_CMD_FSTAT:
        return (uintptr_t)vfs_fstat((int)arg2, (vfs_stat_t *)(uintptr_t)arg3);
    case FS_CMD_DUP:
        return (uintptr_t)vfs_dup((int)arg2);
    case FS_CMD_DUP2:
        return (uintptr_t)vfs_dup2((int)arg2, (int)arg3);
    case FS_CMD_PIPE:
        return (uintptr_t)vfs_pipe((int *)(uintptr_t)arg2);
    case FS_CMD_FCNTL:
        return (uintptr_t)vfs_fcntl((int)arg2, (int)arg3, (int)arg4);
    case FS_CMD_GETCWD:
        return (uintptr_t)vfs_getcwd((char *)(uintptr_t)arg2, (size_t)arg3);
    case FS_CMD_CHDIR:
        return (uintptr_t)vfs_chdir((const char *)(uintptr_t)arg2);
    case FS_CMD_IOCTL:
        return (uintptr_t)vfs_ioctl((int)arg2,
                                    (unsigned long)arg3,
                                    (void *)(uintptr_t)arg4);
    case FS_CMD_MMAP:
    {
        vfs_mmap_request_t *req = (vfs_mmap_request_t *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)vfs_mmap(req->addr,
                                   req->length,
                                   req->prot,
                                   req->flags,
                                   req->fd,
                                   req->offset);
    }
    case FS_CMD_MUNMAP:
        return (uintptr_t)vfs_munmap((void *)(uintptr_t)arg2, (size_t)arg3);
    case FS_CMD_POLL:
        return (uintptr_t)vfs_poll((vfs_pollfd_t *)(uintptr_t)arg2,
                                   (size_t)arg3,
                                   (int)arg4);
    default:
        return (uintptr_t)-1;
    }
}

#else

#include "../ipc/shm.h"
#include "../proc/task.h"

uintptr_t syscall_handler(uintptr_t num,
                          uintptr_t arg1,
                          uintptr_t arg2,
                          uintptr_t arg3,
                          uintptr_t arg4,
                          uintptr_t arg5)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;

    switch (num) {
    case SYS_YIELD:
        task_yield();
        return 0;
    case SYS_EXIT:
        if (task_current())
            task_current()->state = TASK_DEAD;
        task_yield();
        return 0;
    case SYS_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg1);
    case SYS_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg1);
    case SYS_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg1);
    case SYS_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg1);
    default:
        return (uintptr_t)-1;
    }
}

#endif

#ifdef __x86_64__

static uintptr_t handle_gui(uintptr_t cmd,
                            uintptr_t arg2,
                            uintptr_t arg3,
                            uintptr_t arg4,
                            uintptr_t arg5)
{
    int pid = process_current_pid();
    int32_t x, y, w, h;

    switch (cmd) {
    case GUI_CMD_WINDOW_CREATE:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        w = (int32_t)(uint32_t)(arg4 & 0xFFFFFFFFu);
        h = (int32_t)(uint32_t)((arg4 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_window_create(pid,
                                                (const char *)(uintptr_t)arg2,
                                                x, y, w, h);
    case GUI_CMD_WINDOW_DESTROY:
        return (uintptr_t)gui_srv_window_destroy(pid, (int)arg2);
    case GUI_CMD_WINDOW_SET_TITLE:
        return (uintptr_t)gui_srv_window_set_title(pid, (int)arg2, (const char *)(uintptr_t)arg3);
    case GUI_CMD_WINDOW_SET_RESIZABLE:
        return (uintptr_t)gui_srv_window_set_resizable(pid, (int)arg2, (int)arg3);
    case GUI_CMD_DRAW_RECT:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        w = (int32_t)(uint32_t)(arg4 & 0xFFFFFFFFu);
        h = (int32_t)(uint32_t)((arg4 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_draw_rect(pid, (int)arg2, x, y, w, h, (uint32_t)arg5);
    case GUI_CMD_DRAW_ROUNDED_RECT_FILLED:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        w = (int32_t)(uint32_t)(arg4 & 0xFFFFFFFFu);
        h = (int32_t)(uint32_t)((arg4 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_draw_rounded_rect(pid,
                                                    (int)arg2,
                                                    x, y, w, h,
                                                    (int32_t)(uint32_t)(arg5 & 0xFFFFFFFFu),
                                                    (uint32_t)((arg5 >> 32) & 0xFFFFFFFFu));
    case GUI_CMD_DRAW_STRING:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_draw_text(pid,
                                            (int)arg2,
                                            x, y,
                                            (const char *)(uintptr_t)arg4,
                                            (uint32_t)arg5);
    case GUI_CMD_DRAW_IMAGE:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        w = (int32_t)(uint32_t)(arg4 & 0xFFFFFFFFu);
        h = (int32_t)(uint32_t)((arg4 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_draw_image(pid,
                                             (int)arg2,
                                             x, y, w, h,
                                             (const uint32_t *)(uintptr_t)arg5);
    case GUI_CMD_MARK_DIRTY:
        x = (int32_t)(uint32_t)(arg3 & 0xFFFFFFFFu);
        y = (int32_t)(uint32_t)((arg3 >> 32) & 0xFFFFFFFFu);
        w = (int32_t)(uint32_t)(arg4 & 0xFFFFFFFFu);
        h = (int32_t)(uint32_t)((arg4 >> 32) & 0xFFFFFFFFu);
        return (uintptr_t)gui_srv_mark_dirty(pid, (int)arg2, x, y, w, h);
    case GUI_CMD_GET_EVENT:
        return (uintptr_t)gui_srv_get_event(pid, (int)arg2, (struct tsukasa_gui_event *)(uintptr_t)arg3);
    case GUI_CMD_GET_STRING_WIDTH:
        return (uintptr_t)gui_srv_get_string_width((const char *)(uintptr_t)arg2);
    case GUI_CMD_GET_FONT_HEIGHT:
        return (uintptr_t)gui_srv_get_font_height();
    case GUI_CMD_GET_SCREEN_SIZE:
        return (uintptr_t)gui_srv_get_screen_size((uint64_t *)(uintptr_t)arg2,
                                                  (uint64_t *)(uintptr_t)arg3);
    default:
        return (uintptr_t)GUI_ERR_INVALID;
    }
}

static uintptr_t handle_system(uintptr_t cmd,
                               uintptr_t arg2,
                               uintptr_t arg3,
                               uintptr_t arg4,
                               uintptr_t arg5)
{
    (void)arg5;
    switch (cmd) {
    case SYSTEM_CMD_YIELD:
        process_yield();
        return 0;

    case SYSTEM_CMD_SPAWN:
    {
        const char *path = (const char *)(uintptr_t)arg2;
        exec_entry_t entry = NULL;
        process_t *child;
        if (!path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(path, &entry) != 0 || !entry)
            return (uintptr_t)-1;
        child = process_spawn_kernel(path, entry);
        if (child)
            process_set_cmdline((int)child->pid, path);
        return child ? (uintptr_t)child->pid : (uintptr_t)-1;
    }

    case SYSTEM_CMD_SPAWN_EX:
    {
        const struct tsukasa_spawn_request *req =
            (const struct tsukasa_spawn_request *)(uintptr_t)arg2;
        exec_entry_t entry = NULL;
        process_t *child;
        int parent_pid = process_current_pid();
        if (!req || !req->path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(req->path, &entry) != 0 || !entry)
            return (uintptr_t)-1;
        child = process_spawn_kernel(req->path, entry);
        if (!child)
            return (uintptr_t)-1;

        process_set_cmdline((int)child->pid, req->args ? req->args : req->path);
        if (req->tty_id >= 0)
            process_set_tty((int)child->pid, req->tty_id);

        if (parent_pid > 0) {
            if (req->stdin_fd >= 0)
                process_clone_fd((int)child->pid, 0, parent_pid, req->stdin_fd);
            if (req->stdout_fd >= 0)
                process_clone_fd((int)child->pid, 1, parent_pid, req->stdout_fd);
            if (req->stderr_fd >= 0)
                process_clone_fd((int)child->pid, 2, parent_pid, req->stderr_fd);
        }
        return (uintptr_t)child->pid;
    }

    case SYSTEM_CMD_EXEC:
    {
        int pid = (int)arg2;
        const char *path = (const char *)(uintptr_t)arg3;
        exec_entry_t entry = NULL;
        if (!path)
            return (uintptr_t)-1;
        if (exec_resolve_builtin(path, &entry) != 0 || !entry)
            return (uintptr_t)-1;
        process_set_cmdline(pid, path);
        return (uintptr_t)process_exec(pid, entry, path);
    }

    case SYSTEM_CMD_GET_CMDLINE:
        return (uintptr_t)process_get_cmdline(process_current_pid(),
                                              (char *)(uintptr_t)arg2,
                                              (size_t)arg3);

    case SYSTEM_CMD_WAITPID:
    {
        int caller = process_current_pid();
        int target = (int)arg2;
        int *status = (int *)(uintptr_t)arg3;
        int options = (int)arg4;
        return (uintptr_t)process_waitpid(caller, target, options, status);
    }

    case SYSTEM_CMD_KILL:
        return (uintptr_t)process_kill((int)arg2, (int)arg3);

    case SYSTEM_CMD_SIGACTION:
    {
        int sig = (int)arg2;
        const struct tsukasa_sigaction *act = (const struct tsukasa_sigaction *)(uintptr_t)arg3;
        struct tsukasa_sigaction *old = (struct tsukasa_sigaction *)(uintptr_t)arg4;
        int pid = process_current_pid();
        if (old) {
            uint64_t pending = 0;
            process_signal_pending(pid, &pending);
            old->sa_handler = PROCESS_SIG_DFL;
            old->sa_mask = pending;
            old->sa_flags = 0;
        }
        if (act) {
            if (process_signal_register(pid, sig,
                    (process_signal_handler_t)(uintptr_t)act->sa_handler) != 0)
                return (uintptr_t)-1;
            if (process_signal_mask(pid, SIG_SETMASK, act->sa_mask, NULL) != 0)
                return (uintptr_t)-1;
        }
        return 0;
    }

    case SYSTEM_CMD_SIGPROCMASK:
    {
        uint64_t set = 0;
        if (arg3)
            set = *(const uint64_t *)(uintptr_t)arg3;
        return (uintptr_t)process_signal_mask(process_current_pid(),
                                              (int)arg2,
                                              set,
                                              (uint64_t *)(uintptr_t)arg4);
    }

    case SYSTEM_CMD_SIGPENDING:
        return (uintptr_t)process_signal_pending(process_current_pid(),
                                                 (uint64_t *)(uintptr_t)arg2);

    case SYSTEM_CMD_TTY_CREATE:
        return (uintptr_t)tty_create();
    case SYSTEM_CMD_TTY_SET_FG:
        return (uintptr_t)tty_set_foreground_pgid((int)arg2, (int)arg3);
    case SYSTEM_CMD_TTY_GET_FG:
        return (uintptr_t)tty_get_foreground_pgid((int)arg2);
    case SYSTEM_CMD_TTY_KILL_FG:
        return (uintptr_t)tty_kill_foreground((int)arg2, (int)arg3);

    case SYSTEM_CMD_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg2);
    case SYSTEM_CMD_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg2);
    case SYSTEM_CMD_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg2);
    case SYSTEM_CMD_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg2);
    case SYSTEM_CMD_MEM_STATS:
    {
        struct tsukasa_mem_stats *out = (struct tsukasa_mem_stats *)(uintptr_t)arg2;
        heap_stats_t heap_stats = {0};
        struct shm_stats shm_stats = {0};
        size_t proc_count = 0;
        size_t proc_maps = 0;
        size_t proc_shm_pages = 0;
        size_t proc_shm_attachments = 0;

        if (!out)
            return (uintptr_t)-1;

        heap_get_stats(&heap_stats);
        shm_get_stats(&shm_stats);
        process_get_memory_totals(&proc_count,
                                  &proc_maps,
                                  &proc_shm_pages,
                                  &proc_shm_attachments);

        out->total_pages = pmm_total_page_count();
        out->used_pages = pmm_used_page_count();
        out->free_pages = pmm_free_page_count();
        out->heap_pool_bytes = heap_stats.pool_bytes;
        out->heap_used_bytes = heap_stats.allocated_bytes;
        out->heap_peak_bytes = heap_stats.peak_allocated_bytes;
        out->process_count = proc_count;
        out->process_mapped_pages = proc_maps;
        out->process_shm_pages = proc_shm_pages;
        out->process_shm_attachments = proc_shm_attachments;
        out->shm_regions = shm_stats.region_count;
        out->shm_attachments = shm_stats.attachment_count;
        out->shm_reserved_pages = shm_stats.reserved_pages;
        return 0;
    }
    case SYSTEM_CMD_MEM_DUMP:
    {
        struct tsukasa_mem_stats stats = {0};
        heap_stats_t heap_stats = {0};
        struct shm_stats shm_stats = {0};
        size_t proc_count = 0;
        size_t proc_maps = 0;
        size_t proc_shm_pages = 0;
        size_t proc_shm_attachments = 0;

        heap_get_stats(&heap_stats);
        shm_get_stats(&shm_stats);
        process_get_memory_totals(&proc_count,
                                  &proc_maps,
                                  &proc_shm_pages,
                                  &proc_shm_attachments);

        stats.total_pages = pmm_total_page_count();
        stats.used_pages = pmm_used_page_count();
        stats.free_pages = pmm_free_page_count();
        stats.heap_pool_bytes = heap_stats.pool_bytes;
        stats.heap_used_bytes = heap_stats.allocated_bytes;
        stats.heap_peak_bytes = heap_stats.peak_allocated_bytes;
        stats.process_count = proc_count;
        stats.process_mapped_pages = proc_maps;
        stats.process_shm_pages = proc_shm_pages;
        stats.process_shm_attachments = proc_shm_attachments;
        stats.shm_regions = shm_stats.region_count;
        stats.shm_attachments = shm_stats.attachment_count;
        stats.shm_reserved_pages = shm_stats.reserved_pages;

        kprintf("[mem] pages total=%u used=%u free=%u\n",
                (uint32_t)stats.total_pages,
                (uint32_t)stats.used_pages,
                (uint32_t)stats.free_pages);
        kprintf("[mem] heap pool=%u used=%u peak=%u\n",
                (uint32_t)stats.heap_pool_bytes,
                (uint32_t)stats.heap_used_bytes,
                (uint32_t)stats.heap_peak_bytes);
        kprintf("[mem] proc count=%u maps=%u shm_pages=%u shm_atts=%u\n",
                (uint32_t)stats.process_count,
                (uint32_t)stats.process_mapped_pages,
                (uint32_t)stats.process_shm_pages,
                (uint32_t)stats.process_shm_attachments);
        process_dump_memory_state();
        shm_dump_state();
        return 0;
    }

    case SYSTEM_CMD_NET_INIT:
        return (uintptr_t)network_initialize_stack();
    case SYSTEM_CMD_NET_IS_INIT:
        return (uintptr_t)network_is_initialized();
    case SYSTEM_CMD_NET_HAS_IP:
        return (uintptr_t)network_has_ipv4();
    case SYSTEM_CMD_NET_GET_LINK:
        return (uintptr_t)network_get_link_info((net_link_info_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_GET_MAC:
    {
        net_link_info_t info;
        struct tsukasa_net_mac *out = (struct tsukasa_net_mac *)(uintptr_t)arg2;
        if (!out)
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 6; i++)
            out->bytes[i] = info.mac.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_IP:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out)
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.ip.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_GATEWAY:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out)
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.gateway.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_DNS:
    {
        net_link_info_t info;
        struct tsukasa_net_ipv4 *out = (struct tsukasa_net_ipv4 *)(uintptr_t)arg2;
        if (!out)
            return (uintptr_t)-1;
        if (network_get_link_info(&info) != 0)
            return (uintptr_t)-1;
        for (int i = 0; i < 4; i++)
            out->bytes[i] = info.dns.bytes[i];
        return 0;
    }
    case SYSTEM_CMD_NET_GET_STATS:
        return (uintptr_t)network_get_stats((net_runtime_stats_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_DHCP:
        return (uintptr_t)network_dhcp_acquire();
    case SYSTEM_CMD_NET_DNS_LOOKUP:
    {
        const struct tsukasa_net_dns_req *req = (const struct tsukasa_net_dns_req *)(uintptr_t)arg2;
        if (!req || !req->name || !req->out_ip)
            return (uintptr_t)-1;
        return (uintptr_t)network_dns_lookup(req->name, (net_ipv4_addr_t *)req->out_ip);
    }
    case SYSTEM_CMD_NET_PING:
    {
        const struct tsukasa_net_ping_req *req = (const struct tsukasa_net_ping_req *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)network_ping((const net_ipv4_addr_t *)&req->ip, req->timeout_ms);
    }
    case SYSTEM_CMD_NET_TCP_CONNECT:
        return (uintptr_t)network_tcp_connect((const net_tcp_connect_req_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_TCP_SEND:
        return (uintptr_t)network_tcp_send((const void *)(uintptr_t)arg2, (size_t)arg3);
    case SYSTEM_CMD_NET_TCP_RECV:
    {
        const struct tsukasa_net_tcp_recv_req *req =
            (const struct tsukasa_net_tcp_recv_req *)(uintptr_t)arg2;
        if (!req)
            return (uintptr_t)-1;
        return (uintptr_t)network_tcp_recv(req->buffer, req->max_len, req->wait);
    }
    case SYSTEM_CMD_NET_TCP_CLOSE:
        return (uintptr_t)network_tcp_close();
    case SYSTEM_CMD_NET_UDP_SEND:
        return (uintptr_t)network_udp_send((const net_udp_send_req_t *)(uintptr_t)arg2);
    case SYSTEM_CMD_NET_POLL:
        network_poll();
        return 0;

    case SYSTEM_CMD_THEME_SET_ACCENT:
    {
        uint32_t accent = (uint32_t)arg2;
        theme_ensure_loaded();
        if (!validate_theme_color(accent))
            return (uintptr_t)-1;
        g_theme_state.accent_color = accent;
        if (theme_apply_state(&g_theme_state) != 0)
            return (uintptr_t)-1;
        (void)theme_persist_state(&g_theme_state);
        return 0;
    }
    case SYSTEM_CMD_THEME_SET_BG_MODE:
    {
        uint32_t mode = (uint32_t)arg2;
        uint32_t aux = (uint32_t)arg3;
        int has_aux = (arg4 != 0);
        theme_ensure_loaded();
        if (!validate_bg_mode(mode))
            return (uintptr_t)-1;

        if (mode == TSUKASA_THEME_BG_SOLID && has_aux) {
            if (!validate_theme_color(aux))
                return (uintptr_t)-1;
            g_theme_state.solid_color = aux;
        } else if (mode == TSUKASA_THEME_BG_WALLPAPER && has_aux) {
            if (!validate_wallpaper_style(aux))
                return (uintptr_t)-1;
            g_theme_state.wallpaper_style = aux;
        }

        g_theme_state.background_mode = mode;
        if (mode == TSUKASA_THEME_BG_WALLPAPER &&
            g_theme_state.wallpaper_path[0] == '\0') {
            g_theme_state.background_mode = TSUKASA_THEME_BG_GRADIENT;
            (void)theme_apply_state(&g_theme_state);
            (void)theme_persist_state(&g_theme_state);
            return (uintptr_t)-1;
        }

        if (theme_apply_state(&g_theme_state) != 0) {
            if (mode == TSUKASA_THEME_BG_WALLPAPER) {
                g_theme_state.background_mode = TSUKASA_THEME_BG_GRADIENT;
                g_theme_state.wallpaper_path[0] = '\0';
                (void)theme_apply_state(&g_theme_state);
            }
            (void)theme_persist_state(&g_theme_state);
            return (uintptr_t)-1;
        }

        (void)theme_persist_state(&g_theme_state);
        return 0;
    }
    case SYSTEM_CMD_THEME_SET_WALLPAPER:
    {
        const char *path = (const char *)(uintptr_t)arg2;
        int has_style = (arg4 != 0);
        theme_ensure_loaded();
        if (validate_wallpaper_path(path, 0) != 0)
            return (uintptr_t)-1;

        if (has_style) {
            uint32_t style = (uint32_t)arg3;
            if (!validate_wallpaper_style(style))
                return (uintptr_t)-1;
            g_theme_state.wallpaper_style = style;
        }

        copy_small_string(g_theme_state.wallpaper_path,
                          sizeof(g_theme_state.wallpaper_path),
                          path);
        g_theme_state.background_mode = TSUKASA_THEME_BG_WALLPAPER;
        if (theme_apply_state(&g_theme_state) != 0) {
            g_theme_state.background_mode = TSUKASA_THEME_BG_GRADIENT;
            g_theme_state.wallpaper_path[0] = '\0';
            (void)theme_apply_state(&g_theme_state);
            (void)theme_persist_state(&g_theme_state);
            return (uintptr_t)-1;
        }
        (void)theme_persist_state(&g_theme_state);
        return 0;
    }
    case SYSTEM_CMD_THEME_GET_STATE:
    {
        struct tsukasa_theme_state *out =
            (struct tsukasa_theme_state *)(uintptr_t)arg2;
        theme_ensure_loaded();
        if (!out)
            return (uintptr_t)-1;
        *out = g_theme_state;
        return 0;
    }

    case SYSTEM_CMD_TIME_GET:
    {
        rtc_time_t now;
        struct tsukasa_time *out = (struct tsukasa_time *)(uintptr_t)arg2;
        if (!out)
            return (uintptr_t)-1;
        rtc_read(&now);
        out->sec = now.sec;
        out->min = now.min;
        out->hour = now.hour;
        out->day = now.day;
        out->month = now.month;
        out->year = now.year;
        return 0;
    }

    default:
        return (uintptr_t)-1;
    }
}

uintptr_t syscall_handler(uintptr_t num,
                          uintptr_t arg1,
                          uintptr_t arg2,
                          uintptr_t arg3,
                          uintptr_t arg4,
                          uintptr_t arg5)
{
    switch (num) {
    case SYS_YIELD:
        process_yield();
        return 0;
    case SYS_EXIT:
        process_exit((int)arg1);
        return 0;
    case SYS_SHM_CREATE:
        return (uintptr_t)shm_create((size_t)arg1);
    case SYS_SHM_ATTACH:
        return (uintptr_t)shm_attach((int)arg1);
    case SYS_SHM_DETACH:
        return (uintptr_t)shm_detach((void *)(uintptr_t)arg1);
    case SYS_SHM_DESTROY:
        return (uintptr_t)shm_destroy((int)arg1);
    case SYS_GUI:
        return handle_gui(arg1, arg2, arg3, arg4, arg5);
    case SYS_FS:
        return handle_fs(arg1, arg2, arg3, arg4, arg5);
    case SYS_SYSTEM:
        return handle_system(arg1, arg2, arg3, arg4, arg5);
    default:
        return (uintptr_t)-1;
    }
}

#endif /* __x86_64__ */
