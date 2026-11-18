/*
 * Project Tsukasa — Project Vanilla System Settings & About
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

#include "app_common.h"
#include "../include/ui.h"
#include "../include/ui_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>

#define SETTINGS_WIDTH   560
#define SETTINGS_HEIGHT  440

enum {
    PRESET_NORD_DARK = 0,
    PRESET_NORD_LIGHT = 1,
    PRESET_HIGH_CONTRAST = 2,
    PRESET_COUNT = 3
};

enum {
    DLG_NONE = 0,
    DLG_APPLY = 1,
    DLG_RESET = 2,
    DLG_ABOUT = 3
};

typedef struct {
    int             current_tab;
    int             preset_idx;
    int             font_px;
    int             scale_idx; /* 0 = 1.0x, 1 = 1.5x, 2 = 2.0x */
    int             reduce_motion;
    int             enable_shadows;
    int             pending_dlg;
    vanilla_theme_t active_theme;

    char            font_str[32];
    char            status_str[96];
    char            sys_mem_str[96];
    char            sys_fb_str[96];
    char            sys_net_str[96];
    char            sys_inp_str[96];

    ui_ctx_t       *ui_ctx;
    ui_widget_t    *root;
    ui_widget_t    *tab_bar;
    ui_widget_t    *page_appear;
    ui_widget_t    *page_sys;
    ui_widget_t    *page_about;

    ui_widget_t    *card_preset;
    ui_widget_t    *card_typo;
    ui_widget_t    *card_os;
    ui_widget_t    *card_hw;
    ui_widget_t    *card_about;
    ui_widget_t    *appear_footer;
    ui_widget_t    *sys_footer;

    ui_widget_t    *hdr_preset;
    ui_widget_t    *desc_preset;
    ui_widget_t    *preset_list;
    ui_widget_t    *hdr_typo;
    ui_widget_t    *font_label;
    ui_widget_t    *font_slider;
    ui_widget_t    *scale_label;
    ui_widget_t    *radio_1x;
    ui_widget_t    *radio_15x;
    ui_widget_t    *radio_2x;
    ui_widget_t    *chk_motion;
    ui_widget_t    *chk_shadows;
    ui_widget_t    *status_label;

    ui_widget_t    *hdr_os;
    ui_widget_t    *hdr_hw;
    ui_widget_t    *os_labels[4];
    ui_widget_t    *sys_mem_label;
    ui_widget_t    *sys_fb_label;
    ui_widget_t    *sys_net_label;
    ui_widget_t    *sys_inp_label;
    ui_widget_t    *sys_status_label;

    ui_widget_t    *about_title;
    ui_widget_t    *about_sub;
    ui_widget_t    *about_labels[5];
    ui_widget_t    *confirm_dlg;
} settings_state_t;

static unsigned long read_key_val(const char *fn, const char *key)
{
    FILE *fp = fopen(fn, "r");
    if (!fp)
        return 0;

    char line[160];
    size_t klen = strlen(key);
    unsigned long val = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            val = strtoul(p, NULL, 10);
            break;
        }
    }
    fclose(fp);
    return val;
}

static int read_key_str(const char *fn, const char *key, char *out, size_t out_cap)
{
    if (!out || out_cap == 0)
        return -1;
    out[0] = '\0';

    FILE *fp = fopen(fn, "r");
    if (!fp)
        return -1;

    char line[160];
    size_t klen = strlen(key);
    int found = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            size_t n = 0;
            while (p[n] && p[n] != '\n' && p[n] != '\r' && n + 1 < out_cap) {
                out[n] = p[n];
                n++;
            }
            out[n] = '\0';
            found = 0;
            break;
        }
    }
    fclose(fp);
    return found;
}

static int ipc_write_exact(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    int retries = 0;

    while (written < count) {
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else if (errno == EAGAIN || errno == EINTR) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            if (++retries > 200)
                return -1;
            poll(&pfd, 1, 5);
        } else {
            return -1;
        }
    }
    return 0;
}

static int ipc_read_exact(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;

    while (received < count) {
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else if (errno == EAGAIN || errno == EINTR) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            if (++retries > 200)
                return -1;
            poll(&pfd, 1, 5);
        } else {
            return -1;
        }
    }
    return 0;
}

static int ipc_discard_exact(int fd, size_t count)
{
    uint8_t buf[128];
    while (count > 0) {
        size_t chunk = count < sizeof(buf) ? count : sizeof(buf);
        if (ipc_read_exact(fd, buf, chunk) < 0)
            return -1;
        count -= chunk;
    }
    return 0;
}

static int ipc_open_debug_conn(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VANILLA_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t hdr;
    vanilla_msg_hello_t hello;
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_HELLO;
    hdr.payload_len = (uint16_t)sizeof(hello);
    hdr.window_id = 0;

    memset(&hello, 0, sizeof(hello));
    hello.client_version = VANILLA_IPC_VERSION;
    strncpy(hello.client_name, "settings", sizeof(hello.client_name) - 1);

    if (ipc_write_exact(fd, &hdr, sizeof(hdr)) < 0 ||
        ipc_write_exact(fd, &hello, sizeof(hello)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack_body;
    int skipped = 0;
    for (;;) {
        if (ipc_read_exact(fd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
            ack_hdr.magic != VANILLA_IPC_MAGIC) {
            close(fd);
            return -1;
        }
        if (ack_hdr.msg_type == MSG_HELLO_ACK)
            break;
        if (ack_hdr.payload_len > 0 &&
            ipc_discard_exact(fd, ack_hdr.payload_len) < 0) {
            close(fd);
            return -1;
        }
        if (++skipped > 64) {
            close(fd);
            return -1;
        }
    }
    if (ack_hdr.payload_len < sizeof(ack_body) ||
        ipc_read_exact(fd, &ack_body, sizeof(ack_body)) < 0) {
        close(fd);
        return -1;
    }
    if (ack_hdr.payload_len > sizeof(ack_body) &&
        ipc_discard_exact(fd, ack_hdr.payload_len - sizeof(ack_body)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int ipc_query_theme(vanilla_theme_t *out_th)
{
    if (!out_th)
        return -1;

    int fd = ipc_open_debug_conn();
    if (fd < 0)
        return -1;

    vanilla_msg_hdr_t qhdr;
    vanilla_msg_debug_query_t qpayload;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(qpayload);
    qhdr.window_id = 0;
    qpayload.query_type = DBGQ_THEME;

    if (ipc_write_exact(fd, &qhdr, sizeof(qhdr)) < 0 ||
        ipc_write_exact(fd, &qpayload, sizeof(qpayload)) < 0) {
        close(fd);
        return -1;
    }

    int skipped = 0;
    for (;;) {
        vanilla_msg_hdr_t rhdr;
        if (ipc_read_exact(fd, &rhdr, sizeof(rhdr)) < 0 ||
            rhdr.magic != VANILLA_IPC_MAGIC) {
            close(fd);
            return -1;
        }
        if (rhdr.msg_type != MSG_DEBUG_QUERY_RESP) {
            if (rhdr.payload_len > 0 && ipc_discard_exact(fd, rhdr.payload_len) < 0) {
                close(fd);
                return -1;
            }
            if (++skipped > 64) {
                close(fd);
                return -1;
            }
            continue;
        }
        vanilla_msg_debug_query_resp_t rpayload;
        if (rhdr.payload_len < sizeof(rpayload) + sizeof(vanilla_theme_t) ||
            ipc_read_exact(fd, &rpayload, sizeof(rpayload)) < 0 ||
            ipc_read_exact(fd, out_th, sizeof(*out_th)) < 0) {
            close(fd);
            return -1;
        }
        if (rhdr.payload_len > sizeof(rpayload) + sizeof(vanilla_theme_t)) {
            size_t rem = rhdr.payload_len - (sizeof(rpayload) + sizeof(vanilla_theme_t));
            if (ipc_discard_exact(fd, rem) < 0) {
                close(fd);
                return -1;
            }
        }
        close(fd);
        return 0;
    }
}

static int ipc_apply_theme(const vanilla_theme_t *th)
{
    if (!th)
        return -1;

    int fd = ipc_open_debug_conn();
    if (fd < 0)
        return -1;

    uint8_t buf[sizeof(vanilla_msg_debug_query_t) + sizeof(vanilla_theme_t)];
    vanilla_msg_debug_query_t *q = (vanilla_msg_debug_query_t *)buf;
    q->query_type = DBGQ_SET_THEME;
    memcpy(buf + sizeof(vanilla_msg_debug_query_t), th, sizeof(vanilla_theme_t));

    vanilla_msg_hdr_t qhdr;
    qhdr.magic = VANILLA_IPC_MAGIC;
    qhdr.msg_type = MSG_DEBUG_QUERY;
    qhdr.payload_len = (uint16_t)sizeof(buf);
    qhdr.window_id = 0;

    if (ipc_write_exact(fd, &qhdr, sizeof(qhdr)) < 0 ||
        ipc_write_exact(fd, buf, sizeof(buf)) < 0) {
        close(fd);
        return -1;
    }

    int skipped = 0;
    for (;;) {
        vanilla_msg_hdr_t rhdr;
        if (ipc_read_exact(fd, &rhdr, sizeof(rhdr)) < 0 ||
            rhdr.magic != VANILLA_IPC_MAGIC) {
            close(fd);
            return -1;
        }
        if (rhdr.msg_type != MSG_DEBUG_QUERY_RESP) {
            if (rhdr.payload_len > 0 && ipc_discard_exact(fd, rhdr.payload_len) < 0) {
                close(fd);
                return -1;
            }
            if (++skipped > 64) {
                close(fd);
                return -1;
            }
            continue;
        }
        vanilla_msg_debug_query_resp_t rpayload;
        if (rhdr.payload_len >= sizeof(rpayload)) {
            if (ipc_read_exact(fd, &rpayload, sizeof(rpayload)) < 0) {
                close(fd);
                return -1;
            }
            if (rhdr.payload_len > sizeof(rpayload) &&
                ipc_discard_exact(fd, rhdr.payload_len - sizeof(rpayload)) < 0) {
                close(fd);
                return -1;
            }
        }
        close(fd);
        return 0;
    }
}

static void build_theme_preset(vanilla_theme_t *th, int preset_idx,
                               int font_px, int scale_idx,
                               int reduce_motion, int enable_shadows)
{
    const vanilla_theme_t *base = theme_defaults_nord_dark();
    *th = *base;

    if (preset_idx == PRESET_NORD_LIGHT) {
        th->bg_base = 0xFFECEFF4u;
        th->bg_elevated = 0xFFE5E9F0u;
        th->bg_overlay = 0xCCD8DEE9u;
        th->fg_primary = 0xFF2E3440u;
        th->fg_muted = 0xFF4C566Au;
        th->fg_dim = 0xFF98A2B3u;
        th->border = 0xFFD8DEE9u;
        th->border_focus = 0xFF5E81ACu;
        th->accent = 0xFF5E81ACu;
        th->accent_hover = 0xFF81A1C1u;
        th->accent_pressed = 0xFF4C566Au;
        th->selection = 0x445E81ACu;
        th->success = 0xFFA3BE8Cu;
        th->warning = 0xFFD08770u;
        th->danger = 0xFFBF616Au;
        th->titlebar_active = 0xFFE5E9F0u;
        th->titlebar_inactive = 0xFFECEFF4u;
        th->titlebar_text_active = 0xFF2E3440u;
        th->titlebar_text_inactive = 0xFF4C566Au;
        th->titlebar_btn_bg = 0xFFD8DEE9u;
        th->titlebar_btn_bg_hover = 0xFFC2D0E0u;
        th->titlebar_btn_icon = 0xFF2E3440u;
        th->taskbar_bg = 0xFFE5E9F0u;
        th->taskbar_item_active = 0xFF5E81ACu;
        th->taskbar_item_open = 0xFFD8DEE9u;
        th->taskbar_text = 0xFF2E3440u;
        th->menu_bg = 0xFFE5E9F0u;
        th->menu_border = 0xFFB8C2D1u;
        th->menu_divider = 0xFFD8DEE9u;
    } else if (preset_idx == PRESET_HIGH_CONTRAST) {
        th->bg_base = 0xFF000000u;
        th->bg_elevated = 0xFF121212u;
        th->bg_overlay = 0xE6000000u;
        th->fg_primary = 0xFFFFFFFFu;
        th->fg_muted = 0xFFDDDDDDu;
        th->fg_dim = 0xFFAAAAAAu;
        th->border = 0xFFFFFFFFu;
        th->border_focus = 0xFFFFFF00u;
        th->accent = 0xFFFFFF00u;
        th->accent_hover = 0xFFFFFF80u;
        th->accent_pressed = 0xFFCCCC00u;
        th->selection = 0x66FFFF00u;
        th->success = 0xFF00FF00u;
        th->warning = 0xFFFFFF00u;
        th->danger = 0xFFFF0000u;
        th->titlebar_active = 0xFF000000u;
        th->titlebar_inactive = 0xFF1A1A1Au;
        th->titlebar_text_active = 0xFFFFFFFFu;
        th->titlebar_text_inactive = 0xFFAAAAAAu;
        th->titlebar_btn_bg = 0xFF333333u;
        th->titlebar_btn_bg_hover = 0xFF555555u;
        th->titlebar_btn_icon = 0xFFFFFFFFu;
        th->taskbar_bg = 0xFF000000u;
        th->taskbar_item_active = 0xFFFFFF00u;
        th->taskbar_item_open = 0xFF444444u;
        th->taskbar_text = 0xFFFFFFFFu;
        th->menu_bg = 0xFF121212u;
        th->menu_border = 0xFFFFFFFFu;
        th->menu_divider = 0xFF666666u;
        th->border_width = 2;
        th->radius_sm = 0;
        th->radius_md = 0;
        th->radius_lg = 0;
        th->radius_menu = 0;
        th->shadow_radius = 0;
        th->shadow_alpha = 0;
    }

    float scale = 1.0f;
    if (scale_idx == 1)
        scale = 1.5f;
    else if (scale_idx == 2)
        scale = 2.0f;

    th->scale_factor = scale;
    th->title_font_size = (float)font_px * scale;
    th->titlebar_height = (int32_t)(24.0f * scale + 0.5f);
    th->btn_size = (int32_t)(12.0f * scale + 0.5f);
    th->border_width = (int32_t)((float)th->border_width * scale + 0.5f);
    th->space_1 = (int32_t)(4.0f * scale + 0.5f);
    th->space_2 = (int32_t)(8.0f * scale + 0.5f);
    th->space_4 = (int32_t)(16.0f * scale + 0.5f);
    th->space_8 = (int32_t)(32.0f * scale + 0.5f);

    if (preset_idx != PRESET_HIGH_CONTRAST) {
        th->radius_sm = (int32_t)(4.0f * scale + 0.5f);
        th->radius_md = (int32_t)(8.0f * scale + 0.5f);
        th->radius_lg = (int32_t)(12.0f * scale + 0.5f);
        th->shadow_radius = enable_shadows ? (int32_t)(16.0f * scale + 0.5f) : 0;
        th->shadow_alpha = enable_shadows ? 140 : 0;
    } else if (!enable_shadows) {
        th->shadow_radius = 0;
        th->shadow_alpha = 0;
    }

    th->reduce_motion = reduce_motion ? 1 : 0;
    if (th->reduce_motion) {
        th->dur_fast = 0;
        th->dur_base = 0;
        th->dur_slow = 0;
    }
}

static int detect_preset_from_theme(const vanilla_theme_t *th)
{
    if (!th)
        return PRESET_NORD_DARK;
    if (th->bg_base == 0xFFECEFF4u)
        return PRESET_NORD_LIGHT;
    if (th->bg_base == 0xFF000000u)
        return PRESET_HIGH_CONTRAST;
    return PRESET_NORD_DARK;
}

static void refresh_system_info(settings_state_t *st)
{
    unsigned long tot = read_key_val("/sys/memory", "pmm_total_pages");
    unsigned long usd = read_key_val("/sys/memory", "pmm_used_pages");
    unsigned long fre = read_key_val("/sys/memory", "pmm_free_pages");
    if (tot > 0) {
        snprintf(st->sys_mem_str, sizeof(st->sys_mem_str),
                 "Physical Memory: %lu MB Total, %lu MB Used, %lu MB Free",
                 (tot * 4UL) / 1024, (usd * 4UL) / 1024, (fre * 4UL) / 1024);
    } else {
        snprintf(st->sys_mem_str, sizeof(st->sys_mem_str),
                 "Physical Memory: 256 MB Total (PMM Monitored)");
    }

    unsigned long fbw = read_key_val("/sys/devices/summary", "framebuffer.width");
    unsigned long fbh = read_key_val("/sys/devices/summary", "framebuffer.height");
    unsigned long bpp = read_key_val("/sys/devices/summary", "framebuffer.bpp");
    if (fbw > 0 && fbh > 0) {
        snprintf(st->sys_fb_str, sizeof(st->sys_fb_str),
                 "Display Output:  /dev/fb0 (%lux%lu, %lu bpp ARGB32)",
                 fbw, fbh, bpp ? bpp : 32);
    } else {
        snprintf(st->sys_fb_str, sizeof(st->sys_fb_str),
                 "Display Output:  /dev/fb0 (32 bpp ARGB32 VESA/GOP)");
    }

    char nic_drv[40];
    char nic_ip[32];
    if (read_key_str("/sys/net/status", "nic.driver", nic_drv, sizeof(nic_drv)) == 0 &&
        read_key_str("/sys/net/status", "nic.ipv4", nic_ip, sizeof(nic_ip)) == 0) {
        snprintf(st->sys_net_str, sizeof(st->sys_net_str),
                 "Network NIC:     %s (IPv4 %s)", nic_drv, nic_ip);
    } else {
        snprintf(st->sys_net_str, sizeof(st->sys_net_str),
                 "Network NIC:     Intel 8254x / VirtIO-Net Gigabit");
    }

    snprintf(st->sys_inp_str, sizeof(st->sys_inp_str),
             "Input Stream:    /dev/input/events (Unified Evdev)");

    if (st->sys_mem_label)
        ui_label_set_text(st->sys_mem_label, st->sys_mem_str);
    if (st->sys_fb_label)
        ui_label_set_text(st->sys_fb_label, st->sys_fb_str);
    if (st->sys_net_label)
        ui_label_set_text(st->sys_net_label, st->sys_net_str);
    if (st->sys_inp_label)
        ui_label_set_text(st->sys_inp_label, st->sys_inp_str);
}

static void write_theme_config_file(const vanilla_theme_t *th)
{
    FILE *fp = fopen("/tmp/vanilla_theme.ini", "w");
    if (!fp)
        return;

    int scale_pct = (int)(th->scale_factor * 100.0f + 0.5f);
    int font_10 = (int)(th->title_font_size * 10.0f + 0.5f);
    fprintf(fp,
            "[colours]\n"
            "bg_base = 0x%08X\n"
            "bg_elevated = 0x%08X\n"
            "fg_primary = 0x%08X\n"
            "accent = 0x%08X\n"
            "\n[metrics]\n"
            "title_font_size = %d.%d\n"
            "scale_factor = %d.%02d\n"
            "\n[motion]\n"
            "reduce_motion = %d\n",
            th->bg_base, th->bg_elevated, th->fg_primary, th->accent,
            font_10 / 10, font_10 % 10,
            scale_pct / 100, scale_pct % 100,
            th->reduce_motion);
    fclose(fp);
}

static void sync_local_theme_colors(settings_state_t *st)
{
    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t acc = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t fg = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;

    st->root->layout_elem->bg_color = bg;

    ui_widget_t *cards[] = {
        st->card_preset, st->card_typo, st->card_os, st->card_hw, st->card_about
    };
    for (size_t i = 0; i < sizeof(cards) / sizeof(cards[0]); i++) {
        if (cards[i] && cards[i]->layout_elem) {
            cards[i]->layout_elem->bg_color = elev;
            cards[i]->layout_elem->border_color = bdr;
        }
    }

    ui_widget_t *footers[] = { st->appear_footer, st->sys_footer };
    for (size_t i = 0; i < sizeof(footers) / sizeof(footers[0]); i++) {
        if (footers[i] && footers[i]->layout_elem) {
            footers[i]->layout_elem->bg_color = elev;
            footers[i]->layout_elem->border_color = bdr;
        }
    }

    ui_widget_t *headers[] = {
        st->hdr_preset, st->hdr_typo, st->hdr_os, st->hdr_hw, st->about_title
    };
    for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); i++) {
        if (headers[i]) {
            headers[i]->label.color = acc;
            ui_widget_invalidate(headers[i]);
        }
    }

    ui_widget_t *prim[] = {
        st->font_label, st->scale_label, st->sys_mem_label, st->sys_fb_label,
        st->sys_net_label, st->sys_inp_label, st->about_sub
    };
    for (size_t i = 0; i < sizeof(prim) / sizeof(prim[0]); i++) {
        if (prim[i]) {
            prim[i]->label.color = fg;
            ui_widget_invalidate(prim[i]);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (st->os_labels[i]) {
            st->os_labels[i]->label.color = fg;
            ui_widget_invalidate(st->os_labels[i]);
        }
    }

    ui_widget_t *mutes[] = { st->desc_preset, st->status_label, st->sys_status_label };
    for (size_t i = 0; i < sizeof(mutes) / sizeof(mutes[0]); i++) {
        if (mutes[i]) {
            mutes[i]->label.color = muted;
            ui_widget_invalidate(mutes[i]);
        }
    }
    for (int i = 0; i < 5; i++) {
        if (st->about_labels[i]) {
            st->about_labels[i]->label.color = muted;
            ui_widget_invalidate(st->about_labels[i]);
        }
    }

    ui_widget_invalidate(st->root);
}

static void sync_controls_from_state(settings_state_t *st)
{
    snprintf(st->font_str, sizeof(st->font_str), "Font Size: %d px", st->font_px);
    if (st->font_label)
        ui_label_set_text(st->font_label, st->font_str);

    if (st->font_slider) {
        st->font_slider->slider.value = (float)st->font_px;
        ui_widget_invalidate(st->font_slider);
    }

    if (st->preset_list) {
        st->preset_list->list.selected = st->preset_idx;
        ui_widget_invalidate(st->preset_list);
    }

    if (st->radio_1x && st->radio_15x && st->radio_2x) {
        st->radio_1x->radio.checked = (st->scale_idx == 0);
        st->radio_15x->radio.checked = (st->scale_idx == 1);
        st->radio_2x->radio.checked = (st->scale_idx == 2);
        ui_widget_invalidate(st->radio_1x);
        ui_widget_invalidate(st->radio_15x);
        ui_widget_invalidate(st->radio_2x);
    }

    if (st->chk_motion) {
        st->chk_motion->checkbox.checked = st->reduce_motion ? 1 : 0;
        ui_widget_invalidate(st->chk_motion);
    }
    if (st->chk_shadows) {
        st->chk_shadows->checkbox.checked = st->enable_shadows ? 1 : 0;
        ui_widget_invalidate(st->chk_shadows);
    }
}

static void set_appear_status(settings_state_t *st, const char *msg, uint32_t color)
{
    snprintf(st->status_str, sizeof(st->status_str), "%s", msg);
    if (st->status_label) {
        st->status_label->label.color = color;
        ui_label_set_text(st->status_label, st->status_str);
    }
}

static void mark_pending_changes(settings_state_t *st)
{
    uint32_t warn = g_theme ? g_theme->warning : 0xFFEBCB8Bu;
    set_appear_status(st, "Pending changes. Click Apply to update desktop.", warn);
}

static void apply_current_settings(settings_state_t *st, const char *notice)
{
    vanilla_theme_t next_th;
    build_theme_preset(&next_th, st->preset_idx, st->font_px, st->scale_idx,
                       st->reduce_motion, st->enable_shadows);

    int ipc_rc = ipc_apply_theme(&next_th);
    write_theme_config_file(&next_th);

    /* Keep local widget UI readable at the chosen base font size without 2x clipping. */
    vanilla_theme_t local_th = next_th;
    local_th.scale_factor = 1.0f;
    local_th.title_font_size = (float)st->font_px;
    theme_set(&local_th);
    st->active_theme = next_th;

    sync_local_theme_colors(st);

    uint32_t ok_col = g_theme ? g_theme->success : 0xFFA3BE8Cu;
    if (ipc_rc == 0) {
        set_appear_status(st, notice, ok_col);
        notify_send("Settings", notice, "info", 3000, NULL, 0);
    } else {
        char local_msg[128];
        snprintf(local_msg, sizeof(local_msg), "%s (saved locally)", notice);
        set_appear_status(st, local_msg, ok_col);
    }
}

static void update_active_tab(settings_state_t *st, int tab)
{
    if (tab < 0 || tab > 2)
        tab = 0;
    st->current_tab = tab;

    ui_widget_t *pages[3] = { st->page_appear, st->page_sys, st->page_about };
    for (int i = 0; i < 3; i++) {
        if (!pages[i] || !pages[i]->layout_elem)
            continue;
        int active = (i == tab);
        ui_widget_set_hidden(pages[i], !active);
        pages[i]->layout_elem->h_mode = active ? VSIZE_GROW : VSIZE_FIXED;
        pages[i]->layout_elem->h_px = 0;
        pages[i]->layout_elem->clip_children = !active;
    }

    if (tab == 1)
        refresh_system_info(st);

    ui_widget_invalidate(st->root);
}

static void on_tab_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        update_active_tab((settings_state_t *)ud, ev->toggle.state);
}

static void on_preset_selected(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_VALUE_CHANGED)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    if (ev->toggle.state >= 0 && ev->toggle.state < PRESET_COUNT) {
        st->preset_idx = ev->toggle.state;
        mark_pending_changes(st);
    }
}

static void on_font_slider(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_VALUE_CHANGED)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    int px = (int)(ev->slider.value + 0.5f);
    if (px < 11) px = 11;
    if (px > 16) px = 16;
    st->font_px = px;
    snprintf(st->font_str, sizeof(st->font_str), "Font Size: %d px", st->font_px);
    ui_label_set_text(st->font_label, st->font_str);
    mark_pending_changes(st);
}

static void on_scale_radio(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    if (w == st->radio_1x)
        st->scale_idx = 0;
    else if (w == st->radio_15x)
        st->scale_idx = 1;
    else if (w == st->radio_2x)
        st->scale_idx = 2;

    mark_pending_changes(st);
}

static void on_option_toggle(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    if (w == st->chk_motion)
        st->reduce_motion = ev->toggle.state ? 1 : 0;
    else if (w == st->chk_shadows)
        st->enable_shadows = ev->toggle.state ? 1 : 0;

    mark_pending_changes(st);
}

static void on_confirm_modal(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)ev;
    settings_state_t *st = (settings_state_t *)ud;
    if (!w || w->modal.result != UI_DIALOG_RES_OK) {
        st->pending_dlg = DLG_NONE;
        return;
    }

    if (st->pending_dlg == DLG_APPLY) {
        apply_current_settings(st, "Theme applied to display server.");
    } else if (st->pending_dlg == DLG_RESET) {
        st->preset_idx = PRESET_NORD_DARK;
        st->font_px = 13;
        st->scale_idx = 0;
        st->reduce_motion = 0;
        st->enable_shadows = 1;
        sync_controls_from_state(st);
        apply_current_settings(st, "Restored Nord Dark defaults.");
    }
    st->pending_dlg = DLG_NONE;
}

static void show_confirm_modal(settings_state_t *st, int kind,
                               const char *title, const char *body,
                               const char *ok_lbl, const char *cancel_lbl)
{
    if (!st->confirm_dlg)
        return;
    st->pending_dlg = kind;
    st->confirm_dlg->modal.title = title;
    st->confirm_dlg->modal.body = body;
    st->confirm_dlg->modal.ok_label = ok_lbl;
    st->confirm_dlg->modal.cancel_label = cancel_lbl;
    ui_ctx_show_modal(st->ui_ctx, st->confirm_dlg);
}

static void on_btn_apply(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    show_confirm_modal(st, DLG_APPLY,
                       "Apply Appearance Settings",
                       "Update the active compositor theme, scale,\nand typography settings now?",
                       "Apply", "Cancel");
}

static void on_btn_reset(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    show_confirm_modal(st, DLG_RESET,
                       "Reset Appearance Defaults",
                       "Restore Nord Dark palette, 13 px font,\nand 1.0x display scaling?",
                       "Reset", "Cancel");
}

static void on_btn_sys_refresh(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    refresh_system_info(st);
    ui_label_set_text(st->sys_status_label, "System telemetry refreshed.");
}

static void on_btn_license(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    settings_state_t *st = (settings_state_t *)ud;
    show_confirm_modal(st, DLG_ABOUT,
                       "GNU General Public License v3.0",
                       "Project Tsukasa is free software licensed\nunder the GNU GPLv3.\nSee the top-level LICENSE file for terms.",
                       "OK", "Close");
}

static ui_widget_t *make_card_box(ui_ctx_t *ctx, uint32_t bg, uint32_t bdr)
{
    ui_widget_t *c = ui_box(ctx, VDIR_COLUMN);
    c->layout_elem->w_mode = VSIZE_GROW;
    c->layout_elem->h_mode = VSIZE_FIT;
    c->layout_elem->pad_left = 14;
    c->layout_elem->pad_right = 14;
    c->layout_elem->pad_top = 12;
    c->layout_elem->pad_bottom = 12;
    c->layout_elem->gap = 8;
    c->layout_elem->bg_color = bg;
    c->layout_elem->border_color = bdr;
    c->layout_elem->border_width = 1;
    c->layout_elem->corner_radius = g_theme ? g_theme->radius_sm : 4;
    c->layout_elem->clip_children = 1;
    return c;
}

static ui_widget_t *make_footer_bar(ui_ctx_t *ctx, uint32_t bg, uint32_t bdr)
{
    ui_widget_t *bar = ui_box(ctx, VDIR_ROW);
    bar->layout_elem->w_mode = VSIZE_GROW;
    bar->layout_elem->h_mode = VSIZE_FIXED;
    bar->layout_elem->h_px = 46;
    bar->layout_elem->pad_left = 14;
    bar->layout_elem->pad_right = 14;
    bar->layout_elem->gap = 8;
    bar->layout_elem->align_items = VALIGN_CENTER;
    bar->layout_elem->bg_color = bg;
    bar->layout_elem->border_color = bdr;
    bar->layout_elem->border_width = 1;
    return bar;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    settings_state_t st;
    memset(&st, 0, sizeof(st));
    st.font_px = 13;
    st.enable_shadows = 1;

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client)
        return 1;

    if (ipc_query_theme(&st.active_theme) == 0) {
        st.preset_idx = detect_preset_from_theme(&st.active_theme);
        float sc = st.active_theme.scale_factor > 0.5f ? st.active_theme.scale_factor : 1.0f;
        if (sc >= 1.75f)
            st.scale_idx = 2;
        else if (sc >= 1.25f)
            st.scale_idx = 1;
        else
            st.scale_idx = 0;

        int base_font = (int)(st.active_theme.title_font_size / sc + 0.5f);
        if (base_font < 11) base_font = 11;
        if (base_font > 16) base_font = 16;
        st.font_px = base_font;
        st.reduce_motion = st.active_theme.reduce_motion ? 1 : 0;
        st.enable_shadows = (st.active_theme.shadow_alpha > 0) ? 1 : 0;

        vanilla_theme_t local_th = st.active_theme;
        local_th.scale_factor = 1.0f;
        local_th.title_font_size = (float)st.font_px;
        theme_set(&local_th);
    } else {
        st.active_theme = *theme_defaults_nord_dark();
    }

    vanilla_window_t *win = vanilla_create_window(client, "Settings", 340, 110,
                                                  SETTINGS_WIDTH, SETTINGS_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 380, 300, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t layout_arena[64 * 1024];
    static uint8_t ui_arena[192 * 1024];
    vanilla_layout_t *lctx = vlayout_init(layout_arena, sizeof(layout_arena));
    st.ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), lctx);
    if (!st.ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 1;
    }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t acc = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t fg = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;

    st.root = ui_box(st.ui_ctx, VDIR_COLUMN);
    st.root->layout_elem->w_mode = VSIZE_GROW;
    st.root->layout_elem->h_mode = VSIZE_GROW;
    st.root->layout_elem->bg_color = bg;

    static const char *tabs[] = { "Appearance", "System Info", "About" };
    st.tab_bar = ui_tab_bar(st.ui_ctx, tabs, 3, on_tab_event, &st);
    ui_widget_add_child(st.root, st.tab_bar);

    ui_widget_t *tab_host = ui_box(st.ui_ctx, VDIR_COLUMN);
    tab_host->layout_elem->w_mode = VSIZE_GROW;
    tab_host->layout_elem->h_mode = VSIZE_GROW;
    tab_host->layout_elem->gap = 0;
    ui_widget_add_child(st.root, tab_host);

    /* Tab 0: Appearance */
    st.page_appear = ui_box(st.ui_ctx, VDIR_COLUMN);
    st.page_appear->layout_elem->w_mode = VSIZE_GROW;
    st.page_appear->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(tab_host, st.page_appear);

    ui_widget_t *appear_scroll = ui_scroll_view(st.ui_ctx);
    appear_scroll->layout_elem->w_mode = VSIZE_GROW;
    appear_scroll->layout_elem->h_mode = VSIZE_GROW;
    appear_scroll->layout_elem->pad_left = 14;
    appear_scroll->layout_elem->pad_right = 18;
    appear_scroll->layout_elem->pad_top = 12;
    appear_scroll->layout_elem->pad_bottom = 12;
    appear_scroll->layout_elem->gap = 10;
    ui_widget_add_child(st.page_appear, appear_scroll);

    st.card_preset = make_card_box(st.ui_ctx, elev, bdr);
    ui_widget_add_child(appear_scroll, st.card_preset);

    st.hdr_preset = ui_label_ex(st.ui_ctx, "Theme Preset", acc, 14.0f, APP_FONT_BOLD);
    ui_widget_add_child(st.card_preset, st.hdr_preset);

    st.desc_preset = ui_label_ex(
        st.ui_ctx,
        "Choose a desktop colour palette and click Apply.",
        muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(st.card_preset, st.desc_preset);

    static const char *preset_names[] = {
        "Nord Dark (Default Polar Night)",
        "Nord Light (Snow Storm)",
        "High Contrast (Accessibility)"
    };
    st.preset_list = ui_list(st.ui_ctx, preset_names, PRESET_COUNT, on_preset_selected, &st);
    st.preset_list->layout_elem->w_mode = VSIZE_GROW;
    st.preset_list->layout_elem->h_mode = VSIZE_FIXED;
    st.preset_list->layout_elem->h_px = 76;
    ui_widget_add_child(st.card_preset, st.preset_list);

    st.card_typo = make_card_box(st.ui_ctx, elev, bdr);
    ui_widget_add_child(appear_scroll, st.card_typo);

    st.hdr_typo = ui_label_ex(st.ui_ctx, "Typography & Display", acc, 14.0f, APP_FONT_BOLD);
    ui_widget_add_child(st.card_typo, st.hdr_typo);

    ui_widget_t *font_row = ui_box(st.ui_ctx, VDIR_ROW);
    font_row->layout_elem->w_mode = VSIZE_GROW;
    font_row->layout_elem->h_mode = VSIZE_FIXED;
    font_row->layout_elem->h_px = 26;
    font_row->layout_elem->gap = 10;
    font_row->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(st.card_typo, font_row);

    snprintf(st.font_str, sizeof(st.font_str), "Font Size: %d px", st.font_px);
    st.font_label = ui_label_ex(st.ui_ctx, st.font_str, fg, 13.0f, APP_FONT_REGULAR);
    st.font_label->layout_elem->w_mode = VSIZE_FIXED;
    st.font_label->layout_elem->w_px = 136;
    ui_widget_add_child(font_row, st.font_label);

    st.font_slider = ui_slider(st.ui_ctx, (float)st.font_px, 11.0f, 16.0f, on_font_slider, &st);
    st.font_slider->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(font_row, st.font_slider);

    ui_widget_t *scale_row = ui_box(st.ui_ctx, VDIR_ROW);
    scale_row->layout_elem->w_mode = VSIZE_GROW;
    scale_row->layout_elem->h_mode = VSIZE_FIXED;
    scale_row->layout_elem->h_px = 26;
    scale_row->layout_elem->gap = 12;
    scale_row->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(st.card_typo, scale_row);

    st.scale_label = ui_label_ex(st.ui_ctx, "Scale:", fg, 13.0f, APP_FONT_REGULAR);
    st.scale_label->layout_elem->w_mode = VSIZE_FIXED;
    st.scale_label->layout_elem->w_px = 52;
    ui_widget_add_child(scale_row, st.scale_label);

    st.radio_1x = ui_radio(st.ui_ctx, "1.0x", 1, st.scale_idx == 0, on_scale_radio, &st);
    st.radio_15x = ui_radio(st.ui_ctx, "1.5x", 1, st.scale_idx == 1, on_scale_radio, &st);
    st.radio_2x = ui_radio(st.ui_ctx, "2.0x", 1, st.scale_idx == 2, on_scale_radio, &st);
    ui_widget_add_child(scale_row, st.radio_1x);
    ui_widget_add_child(scale_row, st.radio_15x);
    ui_widget_add_child(scale_row, st.radio_2x);

    ui_widget_t *opt_row = ui_box(st.ui_ctx, VDIR_ROW);
    opt_row->layout_elem->w_mode = VSIZE_GROW;
    opt_row->layout_elem->h_mode = VSIZE_FIXED;
    opt_row->layout_elem->h_px = 26;
    opt_row->layout_elem->gap = 16;
    opt_row->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(st.card_typo, opt_row);

    st.chk_motion = ui_checkbox(st.ui_ctx, "Reduce Motion", st.reduce_motion, on_option_toggle, &st);
    st.chk_shadows = ui_checkbox(st.ui_ctx, "Window Shadows", st.enable_shadows, on_option_toggle, &st);
    ui_widget_add_child(opt_row, st.chk_motion);
    ui_widget_add_child(opt_row, st.chk_shadows);

    st.appear_footer = make_footer_bar(st.ui_ctx, elev, bdr);
    ui_widget_add_child(st.page_appear, st.appear_footer);

    snprintf(st.status_str, sizeof(st.status_str), "Ready.");
    st.status_label = ui_label_ex(st.ui_ctx, st.status_str, muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(st.appear_footer, st.status_label);

    ui_widget_t *appear_spacer = ui_box(st.ui_ctx, VDIR_ROW);
    appear_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(st.appear_footer, appear_spacer);

    ui_widget_t *btn_reset = ui_button(st.ui_ctx, "Defaults", on_btn_reset, &st);
    btn_reset->layout_elem->w_px = 86;
    ui_widget_add_child(st.appear_footer, btn_reset);

    ui_widget_t *btn_apply = ui_button(st.ui_ctx, "Apply", on_btn_apply, &st);
    btn_apply->layout_elem->w_px = 86;
    ui_widget_add_child(st.appear_footer, btn_apply);

    /* Tab 1: System Info */
    st.page_sys = ui_box(st.ui_ctx, VDIR_COLUMN);
    st.page_sys->layout_elem->w_mode = VSIZE_GROW;
    st.page_sys->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(tab_host, st.page_sys);

    ui_widget_t *sys_scroll = ui_scroll_view(st.ui_ctx);
    sys_scroll->layout_elem->w_mode = VSIZE_GROW;
    sys_scroll->layout_elem->h_mode = VSIZE_GROW;
    sys_scroll->layout_elem->pad_left = 14;
    sys_scroll->layout_elem->pad_right = 18;
    sys_scroll->layout_elem->pad_top = 12;
    sys_scroll->layout_elem->pad_bottom = 12;
    sys_scroll->layout_elem->gap = 10;
    ui_widget_add_child(st.page_sys, sys_scroll);

    st.card_os = make_card_box(st.ui_ctx, elev, bdr);
    ui_widget_add_child(sys_scroll, st.card_os);

    st.hdr_os = ui_label_ex(st.ui_ctx, "Operating System & Display", acc, 14.0f, APP_FONT_BOLD);
    ui_widget_add_child(st.card_os, st.hdr_os);

    static const char *os_lines[] = {
        "Distribution:    Project Tsukasa OS (x86_64 Long Mode)",
        "Architecture:    x86_64 (SMP, SSE2, Ring 3 Isolated)",
        "Display Server:  Project Vanilla 1.0 (Zero-Copy SHM)",
        "Typography:      stb_truetype Vector Antialiased"
    };
    for (int i = 0; i < 4; i++) {
        st.os_labels[i] = ui_label_ex(st.ui_ctx, os_lines[i], fg, 13.0f, APP_FONT_REGULAR);
        ui_widget_add_child(st.card_os, st.os_labels[i]);
    }

    st.card_hw = make_card_box(st.ui_ctx, elev, bdr);
    ui_widget_add_child(sys_scroll, st.card_hw);

    st.hdr_hw = ui_label_ex(st.ui_ctx, "Hardware & Memory", acc, 14.0f, APP_FONT_BOLD);
    ui_widget_add_child(st.card_hw, st.hdr_hw);

    st.sys_mem_label = ui_label_ex(st.ui_ctx, st.sys_mem_str, fg, 13.0f, APP_FONT_REGULAR);
    st.sys_fb_label = ui_label_ex(st.ui_ctx, st.sys_fb_str, fg, 13.0f, APP_FONT_REGULAR);
    st.sys_inp_label = ui_label_ex(st.ui_ctx, st.sys_inp_str, fg, 13.0f, APP_FONT_REGULAR);
    st.sys_net_label = ui_label_ex(st.ui_ctx, st.sys_net_str, fg, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(st.card_hw, st.sys_mem_label);
    ui_widget_add_child(st.card_hw, st.sys_fb_label);
    ui_widget_add_child(st.card_hw, st.sys_inp_label);
    ui_widget_add_child(st.card_hw, st.sys_net_label);

    st.sys_footer = make_footer_bar(st.ui_ctx, elev, bdr);
    ui_widget_add_child(st.page_sys, st.sys_footer);

    st.sys_status_label = ui_label_ex(st.ui_ctx, "Source: /sys/memory, /sys/devices, /sys/net",
                                      muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(st.sys_footer, st.sys_status_label);

    ui_widget_t *sys_spacer = ui_box(st.ui_ctx, VDIR_ROW);
    sys_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(st.sys_footer, sys_spacer);

    ui_widget_t *btn_sys_ref = ui_button(st.ui_ctx, "Refresh", on_btn_sys_refresh, &st);
    btn_sys_ref->layout_elem->w_px = 90;
    ui_widget_add_child(st.sys_footer, btn_sys_ref);

    /* Tab 2: About */
    st.page_about = ui_box(st.ui_ctx, VDIR_COLUMN);
    st.page_about->layout_elem->w_mode = VSIZE_GROW;
    st.page_about->layout_elem->h_mode = VSIZE_GROW;
    st.page_about->layout_elem->pad_left = 14;
    st.page_about->layout_elem->pad_right = 14;
    st.page_about->layout_elem->pad_top = 12;
    st.page_about->layout_elem->pad_bottom = 12;
    ui_widget_add_child(tab_host, st.page_about);

    st.card_about = make_card_box(st.ui_ctx, elev, bdr);
    ui_widget_add_child(st.page_about, st.card_about);

    st.about_title = ui_label_ex(st.ui_ctx, "Project Tsukasa", acc, 18.0f, APP_FONT_BOLD);
    ui_widget_add_child(st.card_about, st.about_title);

    st.about_sub = ui_label_ex(st.ui_ctx,
                               "Freestanding x86_64 Desktop Operating System",
                               fg, 13.0f, APP_FONT_MEDIUM);
    ui_widget_add_child(st.card_about, st.about_sub);

    static const char *about_lines[] = {
        "Compositor:   Project Vanilla Display Server",
        "Toolkit:      libvanilla-ui + Flex Layout Engine",
        "Copyright:    (C) 2025-2026 frosty (@enafrosty)",
        "              and Project Tsukasa contributors",
        "License:      GNU General Public License v3.0"
    };
    for (int i = 0; i < 5; i++) {
        st.about_labels[i] = ui_label_ex(st.ui_ctx, about_lines[i], muted, 12.0f, APP_FONT_REGULAR);
        ui_widget_add_child(st.card_about, st.about_labels[i]);
    }

    ui_widget_t *about_btn_row = ui_box(st.ui_ctx, VDIR_ROW);
    about_btn_row->layout_elem->w_mode = VSIZE_GROW;
    about_btn_row->layout_elem->h_mode = VSIZE_FIXED;
    about_btn_row->layout_elem->h_px = 36;
    about_btn_row->layout_elem->pad_top = 4;
    ui_widget_add_child(st.card_about, about_btn_row);

    ui_widget_t *btn_license = ui_button(st.ui_ctx, "License Info", on_btn_license, &st);
    btn_license->layout_elem->w_px = 112;
    ui_widget_add_child(about_btn_row, btn_license);

    st.confirm_dlg = ui_dialog_confirm(st.ui_ctx, "", "", "OK", "Cancel",
                                       on_confirm_modal, &st);

    refresh_system_info(&st);
    sync_controls_from_state(&st);
    update_active_tab(&st, 0);
    ui_widget_set_focus(st.ui_ctx, st.tab_bar);

    ui_render(st.ui_ctx, st.root, &win->surface, NULL);
    vanilla_present(win, NULL);

    ui_widget_t *prev_popup = ui_ctx_get_popup(st.ui_ctx);
    ui_widget_t *prev_modal = ui_ctx_get_modal(st.ui_ctx);

    struct pollfd pfd;
    pfd.fd = vanilla_client_get_fd(client);
    pfd.events = POLLIN;
    pfd.revents = 0;

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        int ev_ret;
        int force_present = 0;

        while ((ev_ret = vanilla_poll_event(client, &ev)) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            }
            if (ev.type == VANILLA_EVENT_CONFIGURE) {
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(st.root);
                force_present = 1;
            }
            if (ev.type == VANILLA_EVENT_INPUT ||
                ev.type == VANILLA_EVENT_DOUBLE_CLICK ||
                ev.type == VANILLA_EVENT_TRIPLE_CLICK) {
                ui_handle_event(st.ui_ctx, st.root, &ev.input);
            }
        }

        if (!running || ev_ret < 0)
            break;

        ui_widget_t *cur_popup = ui_ctx_get_popup(st.ui_ctx);
        ui_widget_t *cur_modal = ui_ctx_get_modal(st.ui_ctx);
        int dirty = force_present ||
                    st.root->dirty ||
                    (cur_popup && !cur_popup->hidden && cur_popup->dirty) ||
                    (cur_modal && !cur_modal->hidden && cur_modal->dirty) ||
                    (cur_popup != prev_popup) ||
                    (cur_modal != prev_modal);

        if (dirty) {
            ui_render(st.ui_ctx, st.root, &win->surface, NULL);
            vanilla_present(win, NULL);
            prev_popup = cur_popup;
            prev_modal = cur_modal;
        }

        pfd.revents = 0;
        int pret = poll(&pfd, 1, -1);
        if (pret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) &&
            !(pfd.revents & POLLIN)) {
            break;
        }
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
