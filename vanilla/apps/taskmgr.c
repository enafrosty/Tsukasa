/*
 * Project Tsukasa — Project Vanilla Task Manager
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
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/poll.h>

#ifndef SIGTERM
#define SIGTERM 15
#endif

#define TM_DEFAULT_W      596
#define TM_DEFAULT_H      456
#define TM_REFRESH_MS     1000
#define MAX_TASKS         64

enum {
    TM_CMD_REFRESH = 1,
    TM_CMD_EXIT,
    TM_CMD_TAB_OVERVIEW,
    TM_CMD_TAB_PROCS,
    TM_CMD_TAB_SYSINFO,
    TM_CMD_ABOUT
};

typedef struct {
    int           pid;
    int           ppid;
    unsigned long ticks;
    char          state[16];
    char          name[32];
} task_entry_t;

typedef struct {
    int           pid;
    unsigned long ticks;
} pid_tick_sample_t;

typedef struct {
    task_entry_t      tasks[MAX_TASKS];
    char              item_labels[MAX_TASKS][96];
    const char       *item_ptrs[MAX_TASKS];
    int               task_count;
    int               selected_idx;

    pid_tick_sample_t prev_ticks[MAX_TASKS];
    int               prev_tick_count;
    int64_t           prev_sample_ms;
    int64_t           last_refresh_ms;

    unsigned long     mem_used_mb;
    unsigned long     mem_total_mb;
    unsigned long     mem_free_mb;
    int               mem_pct;
    int               cpu_pct;
    int               running_cnt;
    int               sleeping_cnt;
    int               blocked_cnt;

    int               current_tab;
    int               running;
    int               open_menu;
    int               ctrl_down;
    int               pending_kill_pid;

    char              cpu_str[96];
    char              mem_str[96];
    char              proc_sum_str[96];
    char              heap_sum_str[96];
    char              ov_status_str[64];
    char              count_str[80];
    char              sys_mem_detail[96];
    char              sys_heap_detail[96];
    char              sys_dev_detail[96];
    char              sys_net_detail[96];
    char              sys_pkt_detail[96];
    char              confirm_body[160];

    ui_ctx_t         *ui_ctx;
    ui_widget_t      *root;
    ui_widget_t      *menubar;
    ui_widget_t      *file_menu;
    ui_widget_t      *view_menu;
    ui_widget_t      *help_menu;
    ui_menu_item_t    view_items[3];
    ui_widget_t      *tab_bar;

    ui_widget_t      *page_overview;
    ui_widget_t      *page_procs;
    ui_widget_t      *page_sysinfo;

    ui_widget_t      *cpu_label;
    ui_widget_t      *cpu_bar;
    ui_widget_t      *mem_label;
    ui_widget_t      *mem_bar;
    ui_widget_t      *proc_sum_label;
    ui_widget_t      *heap_sum_label;
    ui_widget_t      *ov_status_label;

    ui_widget_t      *list_widget;
    ui_widget_t      *count_label;

    ui_widget_t      *sys_mem_label;
    ui_widget_t      *sys_heap_label;
    ui_widget_t      *sys_dev_label;
    ui_widget_t      *sys_net_label;
    ui_widget_t      *sys_pkt_label;

    ui_widget_t      *confirm_dlg;
    ui_widget_t      *notice_dlg;
    ui_widget_t      *about_dlg;
} taskmgr_state_t;

static int64_t tm_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
    return 0;
}

static unsigned long tm_read_key_val(const char *filename, const char *key)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[256];
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

static int tm_read_key_str(const char *filename, const char *key, char *out, size_t cap)
{
    if (!out || cap == 0)
        return -1;
    out[0] = '\0';

    FILE *fp = fopen(filename, "r");
    if (!fp)
        return -1;

    char line[256];
    size_t klen = strlen(key);
    int found = -1;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            size_t n = 0;
            while (p[n] && p[n] != '\n' && p[n] != '\r' && n + 1 < cap) {
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

static unsigned long tm_read_proc_ticks(int pid)
{
    char path[48];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    return tm_read_key_val(path, "ticks");
}

static unsigned long tm_find_prev_ticks(const taskmgr_state_t *st, int pid, int *found)
{
    for (int i = 0; i < st->prev_tick_count; i++) {
        if (st->prev_ticks[i].pid == pid) {
            if (found)
                *found = 1;
            return st->prev_ticks[i].ticks;
        }
    }
    if (found)
        *found = 0;
    return 0;
}

static void tm_update_count_label(taskmgr_state_t *st)
{
    if (st->selected_idx >= 0 && st->selected_idx < st->task_count) {
        snprintf(st->count_str, sizeof(st->count_str),
                 "Processes: %d   |   Selected: %s (PID %d)",
                 st->task_count,
                 st->tasks[st->selected_idx].name,
                 st->tasks[st->selected_idx].pid);
    } else {
        snprintf(st->count_str, sizeof(st->count_str),
                 "Processes: %d", st->task_count);
    }
    if (st->count_label)
        ui_label_set_text(st->count_label, st->count_str);
}

static void tm_refresh(taskmgr_state_t *st)
{
    int prev_sel_pid = -1;
    if (st->selected_idx >= 0 && st->selected_idx < st->task_count)
        prev_sel_pid = st->tasks[st->selected_idx].pid;

    st->task_count = 0;
    st->running_cnt = 0;
    st->sleeping_cnt = 0;
    st->blocked_cnt = 0;

    unsigned long total_pages = tm_read_key_val("/sys/memory", "pmm_total_pages");
    unsigned long used_pages = tm_read_key_val("/sys/memory", "pmm_used_pages");
    unsigned long free_pages = tm_read_key_val("/sys/memory", "pmm_free_pages");
    unsigned long heap_pool = tm_read_key_val("/sys/memory", "heap_pool_bytes");
    unsigned long heap_used = tm_read_key_val("/sys/memory", "heap_used_bytes");
    unsigned long heap_peak = tm_read_key_val("/sys/memory", "heap_peak_bytes");
    unsigned long shm_regions = tm_read_key_val("/sys/memory", "shm_regions");
    unsigned long shm_pages = tm_read_key_val("/sys/memory", "shm_reserved_pages");

    if (total_pages > 0) {
        st->mem_total_mb = (total_pages * 4UL) / 1024UL;
        st->mem_used_mb = (used_pages * 4UL) / 1024UL;
        st->mem_free_mb = (free_pages * 4UL) / 1024UL;
        st->mem_pct = (int)((used_pages * 100UL) / total_pages);
        if (st->mem_pct > 100)
            st->mem_pct = 100;
    } else {
        st->mem_total_mb = 256;
        st->mem_used_mb = 96;
        st->mem_free_mb = 160;
        st->mem_pct = 37;
    }

    int64_t now_ms = tm_now_ms();
    unsigned long delta_proc_ticks = 0;

    FILE *fp = fopen("/proc/processes", "r");
    if (fp) {
        char line[256];
        int is_header = 1;
        while (fgets(line, sizeof(line), fp) && st->task_count < MAX_TASKS) {
            if (is_header) {
                is_header = 0;
                continue;
            }
            task_entry_t *t = &st->tasks[st->task_count];
            char *p = line;
            while (*p == ' ') p++;
            t->pid = (int)strtol(p, &p, 10);

            while (*p == ' ') p++;
            t->ppid = (int)strtol(p, &p, 10);

            while (*p == ' ') p++;
            char *s_start = p;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r') p++;
            size_t s_len = (size_t)(p - s_start);
            if (s_len >= sizeof(t->state))
                s_len = sizeof(t->state) - 1;
            memcpy(t->state, s_start, s_len);
            t->state[s_len] = '\0';

            while (*p == ' ') p++;
            char *n_start = p;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r') p++;
            size_t n_len = (size_t)(p - n_start);
            if (n_len >= sizeof(t->name))
                n_len = sizeof(t->name) - 1;
            memcpy(t->name, n_start, n_len);
            t->name[n_len] = '\0';

            if (t->pid > 0) {
                t->ticks = tm_read_proc_ticks(t->pid);
                if (strcmp(t->state, "running") == 0 || strcmp(t->state, "ready") == 0)
                    st->running_cnt++;
                else if (strcmp(t->state, "sleeping") == 0)
                    st->sleeping_cnt++;
                else if (strcmp(t->state, "blocked") == 0)
                    st->blocked_cnt++;

                int was_seen = 0;
                unsigned long prev = tm_find_prev_ticks(st, t->pid, &was_seen);
                if (was_seen && t->ticks >= prev)
                    delta_proc_ticks += (t->ticks - prev);

                snprintf(st->item_labels[st->task_count], sizeof(st->item_labels[0]),
                         "%-6d %-6d %-10s %-7lu %s",
                         t->pid, t->ppid, t->state, t->ticks, t->name);
                st->item_ptrs[st->task_count] = st->item_labels[st->task_count];
                st->task_count++;
            }
        }
        fclose(fp);
    }

    if (st->prev_sample_ms > 0 && now_ms > st->prev_sample_ms) {
        int64_t elapsed_ms = now_ms - st->prev_sample_ms;
        unsigned long wall_ticks = (unsigned long)(elapsed_ms / 10);
        if (wall_ticks > 0) {
            unsigned long pct = (delta_proc_ticks * 100UL) / wall_ticks;
            if (pct > 100UL)
                pct = 100UL;
            st->cpu_pct = (int)pct;
        }
    } else {
        st->cpu_pct = (st->running_cnt > 0) ? (st->running_cnt * 12) : 4;
        if (st->cpu_pct > 100)
            st->cpu_pct = 100;
    }

    st->prev_tick_count = st->task_count;
    for (int i = 0; i < st->task_count; i++) {
        st->prev_ticks[i].pid = st->tasks[i].pid;
        st->prev_ticks[i].ticks = st->tasks[i].ticks;
    }
    st->prev_sample_ms = now_ms;

    if (prev_sel_pid > 0) {
        int matched = -1;
        for (int i = 0; i < st->task_count; i++) {
            if (st->tasks[i].pid == prev_sel_pid) {
                matched = i;
                break;
            }
        }
        if (matched >= 0)
            st->selected_idx = matched;
    }
    if (st->selected_idx >= st->task_count)
        st->selected_idx = (st->task_count > 0) ? (st->task_count - 1) : 0;
    if (st->selected_idx < 0)
        st->selected_idx = 0;

    /* Update Overview widgets */
    snprintf(st->cpu_str, sizeof(st->cpu_str),
             "Load: %d%%  (%d active, %d sleeping, %d blocked)",
             st->cpu_pct, st->running_cnt, st->sleeping_cnt, st->blocked_cnt);
    if (st->cpu_label)
        ui_label_set_text(st->cpu_label, st->cpu_str);
    if (st->cpu_bar) {
        uint32_t cpu_col = g_theme ? g_theme->success : 0xFFA3BE8Cu;
        if (st->cpu_pct >= 85)
            cpu_col = g_theme ? g_theme->danger : 0xFFBF616Au;
        else if (st->cpu_pct >= 60)
            cpu_col = g_theme ? g_theme->warning : 0xFFEBCB8Bu;
        ui_progress_bar_set_color(st->cpu_bar, cpu_col);
        ui_progress_bar_set(st->cpu_bar, (float)st->cpu_pct);
    }

    snprintf(st->mem_str, sizeof(st->mem_str),
             "Used: %lu MB / %lu MB (%d%%)   |   Free: %lu MB",
             st->mem_used_mb, st->mem_total_mb, st->mem_pct, st->mem_free_mb);
    if (st->mem_label)
        ui_label_set_text(st->mem_label, st->mem_str);
    if (st->mem_bar) {
        uint32_t mem_col = g_theme ? g_theme->accent : 0xFF88C0D0u;
        if (st->mem_pct >= 90)
            mem_col = g_theme ? g_theme->danger : 0xFFBF616Au;
        else if (st->mem_pct >= 75)
            mem_col = g_theme ? g_theme->warning : 0xFFEBCB8Bu;
        ui_progress_bar_set_color(st->mem_bar, mem_col);
        ui_progress_bar_set(st->mem_bar, (float)st->mem_pct);
    }

    snprintf(st->proc_sum_str, sizeof(st->proc_sum_str),
             "Total Processes: %d   |   Running/Ready: %d   |   Blocked: %d",
             st->task_count, st->running_cnt, st->blocked_cnt);
    if (st->proc_sum_label)
        ui_label_set_text(st->proc_sum_label, st->proc_sum_str);

    snprintf(st->heap_sum_str, sizeof(st->heap_sum_str),
             "Kernel Heap: %lu KB / %lu KB   |   SHM Regions: %lu (%lu pages)",
             heap_used / 1024UL, heap_pool / 1024UL, shm_regions, shm_pages);
    if (st->heap_sum_label)
        ui_label_set_text(st->heap_sum_label, st->heap_sum_str);

    snprintf(st->ov_status_str, sizeof(st->ov_status_str),
             "Auto-refresh: 1.0 s   |   Tasks: %d", st->task_count);
    if (st->ov_status_label)
        ui_label_set_text(st->ov_status_label, st->ov_status_str);

    /* Update Process List preserving user scroll offset unless list shrunk */
    if (st->list_widget) {
        st->list_widget->list.items = st->item_ptrs;
        st->list_widget->list.count = st->task_count;
        st->list_widget->list.selected = st->selected_idx;

        int32_t row_h = 24;
        int32_t inner_h = (st->list_widget->layout_elem &&
                           st->list_widget->layout_elem->computed_h > 2)
                          ? (st->list_widget->layout_elem->computed_h - 2)
                          : 200;
        int32_t max_scroll = st->task_count * row_h - inner_h;
        if (max_scroll < 0)
            max_scroll = 0;
        if (st->list_widget->list.scroll_top > max_scroll)
            st->list_widget->list.scroll_top = max_scroll;
        if (st->list_widget->list.scroll_top < 0)
            st->list_widget->list.scroll_top = 0;
        ui_widget_invalidate(st->list_widget);
    }

    tm_update_count_label(st);

    /* Update System Info metrics */
    snprintf(st->sys_mem_detail, sizeof(st->sys_mem_detail),
             "PMM Pages:   %lu total, %lu used, %lu free (4 KiB/page)",
             total_pages, used_pages, free_pages);
    snprintf(st->sys_heap_detail, sizeof(st->sys_heap_detail),
             "Heap Usage:  %lu KB used / %lu KB pool (peak %lu KB)",
             heap_used / 1024UL, heap_pool / 1024UL, heap_peak / 1024UL);

    unsigned long fbw = tm_read_key_val("/sys/devices/summary", "framebuffer.width");
    unsigned long fbh = tm_read_key_val("/sys/devices/summary", "framebuffer.height");
    unsigned long ata_sec = tm_read_key_val("/sys/devices/summary", "ata0.sectors");
    snprintf(st->sys_dev_detail, sizeof(st->sys_dev_detail),
             "Framebuffer: %lux%lu ARGB32   |   ATA0 Sectors: %lu",
             fbw ? fbw : 1280UL, fbh ? fbh : 800UL, ata_sec);

    char nic_drv[40];
    char nic_ip[32];
    if (tm_read_key_str("/sys/net/status", "nic.driver", nic_drv, sizeof(nic_drv)) == 0 &&
        tm_read_key_str("/sys/net/status", "nic.ipv4", nic_ip, sizeof(nic_ip)) == 0) {
        snprintf(st->sys_net_detail, sizeof(st->sys_net_detail),
                 "Network NIC: %s   |   IPv4: %s", nic_drv, nic_ip);
    } else {
        snprintf(st->sys_net_detail, sizeof(st->sys_net_detail),
                 "Network NIC: Unavailable or unconfigured");
    }

    unsigned long tx_pkts = tm_read_key_val("/sys/net/stats", "tx_packets");
    unsigned long rx_pkts = tm_read_key_val("/sys/net/stats", "rx_packets");
    snprintf(st->sys_pkt_detail, sizeof(st->sys_pkt_detail),
             "Net Packets: TX %lu   |   RX %lu", tx_pkts, rx_pkts);

    if (st->sys_mem_label)
        ui_label_set_text(st->sys_mem_label, st->sys_mem_detail);
    if (st->sys_heap_label)
        ui_label_set_text(st->sys_heap_label, st->sys_heap_detail);
    if (st->sys_dev_label)
        ui_label_set_text(st->sys_dev_label, st->sys_dev_detail);
    if (st->sys_net_label)
        ui_label_set_text(st->sys_net_label, st->sys_net_detail);
    if (st->sys_pkt_label)
        ui_label_set_text(st->sys_pkt_label, st->sys_pkt_detail);
}

static void tm_switch_tab(taskmgr_state_t *st, int tab)
{
    if (tab < 0 || tab > 2)
        tab = 0;
    st->current_tab = tab;

    if (st->tab_bar && st->tab_bar->tab_bar.active_tab != tab) {
        st->tab_bar->tab_bar.active_tab = tab;
        ui_widget_invalidate(st->tab_bar);
    }

    ui_widget_t *pages[3] = { st->page_overview, st->page_procs, st->page_sysinfo };
    for (int i = 0; i < 3; i++) {
        if (!pages[i] || !pages[i]->layout_elem)
            continue;
        int active = (i == tab);
        ui_widget_set_hidden(pages[i], !active);
        pages[i]->layout_elem->h_mode = active ? VSIZE_GROW : VSIZE_FIXED;
        pages[i]->layout_elem->h_px = 0;
        pages[i]->layout_elem->clip_children = !active;

        st->view_items[i].flags = active ? UI_MENU_ITEM_CHECKED : 0u;
    }

    if (st->view_menu)
        ui_menu_set_rich_items(st->view_menu, st->view_items, 3);

    if (tab == 1 && st->list_widget)
        ui_widget_set_focus(st->ui_ctx, st->list_widget);

    ui_widget_invalidate(st->root);
}

static void tm_request_kill_selected(taskmgr_state_t *st)
{
    if (st->selected_idx < 0 || st->selected_idx >= st->task_count)
        return;

    task_entry_t *t = &st->tasks[st->selected_idx];
    if (t->pid <= 1 || t->pid == (int)getpid()) {
        snprintf(st->confirm_body, sizeof(st->confirm_body),
                 "Process \"%s\" (PID %d) is protected\nand cannot be terminated.",
                 t->name, t->pid);
        if (st->notice_dlg) {
            st->notice_dlg->modal.body = st->confirm_body;
            ui_ctx_show_modal(st->ui_ctx, st->notice_dlg);
        }
        return;
    }

    st->pending_kill_pid = t->pid;
    snprintf(st->confirm_body, sizeof(st->confirm_body),
             "End process \"%s\" (PID %d)?\nUnsaved state in that process may be lost.",
             t->name, t->pid);
    if (st->confirm_dlg) {
        st->confirm_dlg->modal.body = st->confirm_body;
        ui_ctx_show_modal(st->ui_ctx, st->confirm_dlg);
    }
}

static void on_kill_confirm(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)ev;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    int pid = st->pending_kill_pid;
    st->pending_kill_pid = 0;

    if (w && w->modal.result == UI_DIALOG_RES_OK && pid > 1 && pid != (int)getpid()) {
        kill(pid, SIGTERM);
        tm_refresh(st);
        st->last_refresh_ms = tm_now_ms();
    }
}

static void on_tab_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type == UI_EVENT_VALUE_CHANGED)
        tm_switch_tab((taskmgr_state_t *)ud, ev->toggle.state);
}

static void on_btn_refresh(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    tm_refresh(st);
    st->last_refresh_ms = tm_now_ms();
}

static void on_btn_kill(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK)
        return;
    tm_request_kill_selected((taskmgr_state_t *)ud);
}

static void on_task_selected(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    if (ev->type == UI_EVENT_VALUE_CHANGED) {
        st->selected_idx = ev->toggle.state;
        tm_update_count_label(st);
    }
}

static void on_menu_item(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED || !w || !w->menu.rich_items)
        return;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    int32_t idx = ev->toggle.state;
    if (idx < 0 || idx >= w->menu.count)
        return;

    st->open_menu = -1;
    if (st->menubar) {
        st->menubar->menu.selected = -1;
        ui_widget_invalidate(st->menubar);
    }

    switch (w->menu.rich_items[idx].id) {
    case TM_CMD_REFRESH:
        tm_refresh(st);
        st->last_refresh_ms = tm_now_ms();
        break;
    case TM_CMD_EXIT:
        st->running = 0;
        break;
    case TM_CMD_TAB_OVERVIEW:
        tm_switch_tab(st, 0);
        break;
    case TM_CMD_TAB_PROCS:
        tm_switch_tab(st, 1);
        break;
    case TM_CMD_TAB_SYSINFO:
        tm_switch_tab(st, 2);
        break;
    case TM_CMD_ABOUT:
        if (st->about_dlg)
            ui_ctx_show_modal(st->ui_ctx, st->about_dlg);
        break;
    default:
        break;
    }
}

static void on_menubar_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED || !w)
        return;
    taskmgr_state_t *st = (taskmgr_state_t *)ud;
    int32_t idx = ev->toggle.state;

    if (st->open_menu == idx) {
        st->open_menu = -1;
        w->menu.selected = -1;
        ui_ctx_hide_popup(st->ui_ctx);
        ui_widget_invalidate(w);
        return;
    }

    ui_widget_t *pop = NULL;
    if (idx == 0)
        pop = st->file_menu;
    else if (idx == 1)
        pop = st->view_menu;
    else if (idx == 2)
        pop = st->help_menu;

    if (!pop)
        return;

    st->open_menu = idx;
    int32_t px = ui_menubar_item_x(w, idx);
    int32_t py = w->layout_elem ? (w->layout_elem->computed_y + w->layout_elem->computed_h) : 28;
    ui_ctx_show_popup(st->ui_ctx, pop, px, py);
}

static ui_widget_t *make_card(ui_ctx_t *ctx, uint32_t bg, uint32_t bdr)
{
    ui_widget_t *c = ui_box(ctx, VDIR_COLUMN);
    c->layout_elem->w_mode = VSIZE_GROW;
    c->layout_elem->h_mode = VSIZE_FIT;
    c->layout_elem->pad_left = 14;
    c->layout_elem->pad_right = 14;
    c->layout_elem->pad_top = 12;
    c->layout_elem->pad_bottom = 12;
    c->layout_elem->gap = 6;
    c->layout_elem->bg_color = bg;
    c->layout_elem->border_color = bdr;
    c->layout_elem->border_width = 1;
    c->layout_elem->corner_radius = g_theme ? g_theme->radius_sm : 4;
    c->layout_elem->clip_children = 1;
    return c;
}

static ui_widget_t *make_footer(ui_ctx_t *ctx, uint32_t bg, uint32_t bdr)
{
    ui_widget_t *bar = ui_box(ctx, VDIR_ROW);
    bar->layout_elem->w_mode = VSIZE_GROW;
    bar->layout_elem->h_mode = VSIZE_FIXED;
    bar->layout_elem->h_px = 42;
    bar->layout_elem->pad_left = 12;
    bar->layout_elem->pad_right = 12;
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

    taskmgr_state_t state;
    memset(&state, 0, sizeof(state));
    state.running = 1;
    state.open_menu = -1;

    const char *sock_path = VANILLA_SOCKET_PATH;
    vanilla_client_t *client = NULL;
    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(sock_path);
        if (client)
            break;
        poll(NULL, 0, 10);
    }
    if (!client) {
        fprintf(stderr, "taskmgr: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Task Manager", 648, 58,
                                                  TM_DEFAULT_W, TM_DEFAULT_H,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 380, 280, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t layout_arena[64 * 1024];
    static uint8_t ui_arena[192 * 1024];
    vanilla_layout_t *lctx = vlayout_init(layout_arena, sizeof(layout_arena));
    state.ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), lctx);
    if (!state.ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 1;
    }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t acc = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t suc = g_theme ? g_theme->success : 0xFFA3BE8Cu;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;

    state.root = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.root->layout_elem->w_mode = VSIZE_GROW;
    state.root->layout_elem->h_mode = VSIZE_GROW;
    state.root->layout_elem->bg_color = bg;

    static const char *bar_labels[] = { "File", "View", "Help" };
    state.menubar = ui_menubar(state.ui_ctx, bar_labels, 3, on_menubar_event, &state);
    ui_widget_add_child(state.root, state.menubar);

    static const ui_menu_item_t file_items[] = {
        { "Refresh Now", "F5",     0,                      TM_CMD_REFRESH },
        { NULL,          NULL,     UI_MENU_ITEM_SEPARATOR, 0 },
        { "Exit",        "Ctrl+Q", 0,                      TM_CMD_EXIT },
    };
    state.file_menu = ui_menu_ex(state.ui_ctx, file_items, 3, on_menu_item, &state);

    state.view_items[0] = (ui_menu_item_t){ "Overview",    NULL, UI_MENU_ITEM_CHECKED, TM_CMD_TAB_OVERVIEW };
    state.view_items[1] = (ui_menu_item_t){ "Processes",   NULL, 0,                    TM_CMD_TAB_PROCS };
    state.view_items[2] = (ui_menu_item_t){ "System Info", NULL, 0,                    TM_CMD_TAB_SYSINFO };
    state.view_menu = ui_menu_ex(state.ui_ctx, state.view_items, 3, on_menu_item, &state);

    static const ui_menu_item_t help_items[] = {
        { "About Task Manager", NULL, 0, TM_CMD_ABOUT },
    };
    state.help_menu = ui_menu_ex(state.ui_ctx, help_items, 1, on_menu_item, &state);

    static const char *tabs[] = { "Overview", "Processes", "System Info" };
    state.tab_bar = ui_tab_bar(state.ui_ctx, tabs, 3, on_tab_event, &state);
    ui_widget_add_child(state.root, state.tab_bar);

    ui_widget_t *tab_host = ui_box(state.ui_ctx, VDIR_COLUMN);
    tab_host->layout_elem->w_mode = VSIZE_GROW;
    tab_host->layout_elem->h_mode = VSIZE_GROW;
    tab_host->layout_elem->gap = 0;
    ui_widget_add_child(state.root, tab_host);

    /* Tab 0: Overview */
    state.page_overview = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.page_overview->layout_elem->w_mode = VSIZE_GROW;
    state.page_overview->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(tab_host, state.page_overview);

    ui_widget_t *ov_scroll = ui_scroll_view(state.ui_ctx);
    ov_scroll->layout_elem->w_mode = VSIZE_GROW;
    ov_scroll->layout_elem->h_mode = VSIZE_GROW;
    ov_scroll->layout_elem->pad_left = 14;
    ov_scroll->layout_elem->pad_right = 18;
    ov_scroll->layout_elem->pad_top = 12;
    ov_scroll->layout_elem->pad_bottom = 12;
    ov_scroll->layout_elem->gap = 10;
    ui_widget_add_child(state.page_overview, ov_scroll);

    ui_widget_t *cpu_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(ov_scroll, cpu_card);
    ui_widget_add_child(cpu_card,
                        ui_label_ex(state.ui_ctx, "CPU Utilization", acc, 14.0f, APP_FONT_BOLD));
    state.cpu_label = ui_label_ex(state.ui_ctx, state.cpu_str, fg_primary, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(cpu_card, state.cpu_label);
    state.cpu_bar = ui_progress_bar_create(state.ui_ctx, 0.0f);
    state.cpu_bar->layout_elem->h_px = 20;
    ui_progress_bar_set_color(state.cpu_bar, suc);
    ui_widget_add_child(cpu_card, state.cpu_bar);

    ui_widget_t *mem_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(ov_scroll, mem_card);
    ui_widget_add_child(mem_card,
                        ui_label_ex(state.ui_ctx, "Physical Memory", acc, 14.0f, APP_FONT_BOLD));
    state.mem_label = ui_label_ex(state.ui_ctx, state.mem_str, fg_primary, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(mem_card, state.mem_label);
    state.mem_bar = ui_progress_bar(state.ui_ctx, 0.0f, 100.0f, acc, 1);
    state.mem_bar->layout_elem->h_px = 20;
    ui_widget_add_child(mem_card, state.mem_bar);

    ui_widget_t *sum_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(ov_scroll, sum_card);
    ui_widget_add_child(sum_card,
                        ui_label_ex(state.ui_ctx, "Kernel & Process Summary", acc, 14.0f, APP_FONT_BOLD));
    state.proc_sum_label = ui_label_ex(state.ui_ctx, state.proc_sum_str, fg_primary, 13.0f, APP_FONT_REGULAR);
    state.heap_sum_label = ui_label_ex(state.ui_ctx, state.heap_sum_str, fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(sum_card, state.proc_sum_label);
    ui_widget_add_child(sum_card, state.heap_sum_label);

    ui_widget_t *ov_footer = make_footer(state.ui_ctx, elev, bdr);
    ui_widget_add_child(state.page_overview, ov_footer);
    state.ov_status_label = ui_label_ex(state.ui_ctx, state.ov_status_str, fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(ov_footer, state.ov_status_label);

    ui_widget_t *ov_spacer = ui_box(state.ui_ctx, VDIR_ROW);
    ov_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(ov_footer, ov_spacer);

    ui_widget_t *ov_btn_ref = ui_button(state.ui_ctx, "Refresh", on_btn_refresh, &state);
    ov_btn_ref->layout_elem->w_px = 90;
    ui_widget_add_child(ov_footer, ov_btn_ref);

    /* Tab 1: Processes */
    state.page_procs = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.page_procs->layout_elem->w_mode = VSIZE_GROW;
    state.page_procs->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(tab_host, state.page_procs);

    ui_widget_t *tbl_hdr = ui_box(state.ui_ctx, VDIR_ROW);
    tbl_hdr->layout_elem->w_mode = VSIZE_GROW;
    tbl_hdr->layout_elem->h_mode = VSIZE_FIXED;
    tbl_hdr->layout_elem->h_px = 26;
    tbl_hdr->layout_elem->pad_left = 10;
    tbl_hdr->layout_elem->align_items = VALIGN_CENTER;
    tbl_hdr->layout_elem->bg_color = elev;
    tbl_hdr->layout_elem->border_color = bdr;
    tbl_hdr->layout_elem->border_width = 1;
    ui_widget_add_child(state.page_procs, tbl_hdr);

    ui_widget_add_child(tbl_hdr,
                        ui_label_ex(state.ui_ctx,
                                    "PID    PPID   STATE      TICKS   PROCESS NAME",
                                    fg_muted, 12.0f, APP_FONT_BOLD));

    state.list_widget = ui_list(state.ui_ctx, state.item_ptrs, state.task_count,
                                on_task_selected, &state);
    state.list_widget->layout_elem->w_mode = VSIZE_GROW;
    state.list_widget->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(state.page_procs, state.list_widget);

    ui_widget_t *action_bar = make_footer(state.ui_ctx, elev, bdr);
    ui_widget_add_child(state.page_procs, action_bar);

    state.count_label = ui_label_ex(state.ui_ctx, state.count_str, fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(action_bar, state.count_label);

    ui_widget_t *proc_spacer = ui_box(state.ui_ctx, VDIR_ROW);
    proc_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(action_bar, proc_spacer);

    ui_widget_t *btn_ref = ui_button(state.ui_ctx, "Refresh", on_btn_refresh, &state);
    btn_ref->layout_elem->w_px = 84;
    ui_widget_add_child(action_bar, btn_ref);

    ui_widget_t *btn_end = ui_button(state.ui_ctx, "End Process", on_btn_kill, &state);
    btn_end->layout_elem->w_px = 108;
    ui_widget_add_child(action_bar, btn_end);

    /* Tab 2: System Info */
    state.page_sysinfo = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.page_sysinfo->layout_elem->w_mode = VSIZE_GROW;
    state.page_sysinfo->layout_elem->h_mode = VSIZE_GROW;
    ui_widget_add_child(tab_host, state.page_sysinfo);

    ui_widget_t *sys_scroll = ui_scroll_view(state.ui_ctx);
    sys_scroll->layout_elem->w_mode = VSIZE_GROW;
    sys_scroll->layout_elem->h_mode = VSIZE_GROW;
    sys_scroll->layout_elem->pad_left = 14;
    sys_scroll->layout_elem->pad_right = 18;
    sys_scroll->layout_elem->pad_top = 12;
    sys_scroll->layout_elem->pad_bottom = 12;
    sys_scroll->layout_elem->gap = 10;
    ui_widget_add_child(state.page_sysinfo, sys_scroll);

    ui_widget_t *sys_mem_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(sys_scroll, sys_mem_card);
    ui_widget_add_child(sys_mem_card,
                        ui_label_ex(state.ui_ctx, "Memory & Allocator", acc, 14.0f, APP_FONT_BOLD));
    state.sys_mem_label = ui_label_ex(state.ui_ctx, state.sys_mem_detail, fg_primary, 13.0f, APP_FONT_REGULAR);
    state.sys_heap_label = ui_label_ex(state.ui_ctx, state.sys_heap_detail, fg_muted, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(sys_mem_card, state.sys_mem_label);
    ui_widget_add_child(sys_mem_card, state.sys_heap_label);

    ui_widget_t *sys_dev_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(sys_scroll, sys_dev_card);
    ui_widget_add_child(sys_dev_card,
                        ui_label_ex(state.ui_ctx, "Devices & Storage", acc, 14.0f, APP_FONT_BOLD));
    state.sys_dev_label = ui_label_ex(state.ui_ctx, state.sys_dev_detail, fg_primary, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(sys_dev_card, state.sys_dev_label);

    ui_widget_t *sys_net_card = make_card(state.ui_ctx, elev, bdr);
    ui_widget_add_child(sys_scroll, sys_net_card);
    ui_widget_add_child(sys_net_card,
                        ui_label_ex(state.ui_ctx, "Network Interface", acc, 14.0f, APP_FONT_BOLD));
    state.sys_net_label = ui_label_ex(state.ui_ctx, state.sys_net_detail, fg_primary, 13.0f, APP_FONT_REGULAR);
    state.sys_pkt_label = ui_label_ex(state.ui_ctx, state.sys_pkt_detail, fg_muted, 13.0f, APP_FONT_REGULAR);
    ui_widget_add_child(sys_net_card, state.sys_net_label);
    ui_widget_add_child(sys_net_card, state.sys_pkt_label);

    ui_widget_t *sys_footer = make_footer(state.ui_ctx, elev, bdr);
    ui_widget_add_child(state.page_sysinfo, sys_footer);
    ui_widget_add_child(sys_footer,
                        ui_label_ex(state.ui_ctx, "Live /proc and /sys telemetry",
                                    fg_muted, 12.0f, APP_FONT_REGULAR));
    ui_widget_t *sys_spacer = ui_box(state.ui_ctx, VDIR_ROW);
    sys_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(sys_footer, sys_spacer);
    ui_widget_t *sys_btn_ref = ui_button(state.ui_ctx, "Refresh", on_btn_refresh, &state);
    sys_btn_ref->layout_elem->w_px = 90;
    ui_widget_add_child(sys_footer, sys_btn_ref);

    /* Modals */
    state.confirm_dlg = ui_dialog_confirm(
        state.ui_ctx, "End Process", "", "End Process", "Cancel",
        on_kill_confirm, &state);
    state.notice_dlg = ui_modal_dialog(
        state.ui_ctx, "Protected Process", "", "OK", NULL, NULL);
    state.about_dlg = ui_dialog_confirm(
        state.ui_ctx,
        "About Task Manager",
        "Project Vanilla Task Manager\nReal-time CPU, memory, and process monitor\nF5: Refresh   Delete: End Process",
        "OK", "Close", NULL, NULL);

    tm_refresh(&state);
    state.last_refresh_ms = tm_now_ms();
    tm_switch_tab(&state, 0);
    ui_widget_set_focus(state.ui_ctx, state.tab_bar);

    ui_render(state.ui_ctx, state.root, &win->surface, NULL);
    vanilla_present(win, NULL);

    ui_widget_t *prev_popup = ui_ctx_get_popup(state.ui_ctx);
    ui_widget_t *prev_modal = ui_ctx_get_modal(state.ui_ctx);

    struct pollfd pfd;
    pfd.fd = vanilla_client_get_fd(client);
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (state.running) {
        vanilla_event_t ev;
        int ev_ret;
        int force_present = 0;

        while ((ev_ret = vanilla_poll_event(client, &ev)) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                state.running = 0;
                break;
            }
            if (ev.type == VANILLA_EVENT_CONFIGURE && ev.window_id == win->window_id) {
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state.root);
                force_present = 1;
            }
            if (ev.type == VANILLA_EVENT_INPUT ||
                ev.type == VANILLA_EVENT_DOUBLE_CLICK ||
                ev.type == VANILLA_EVENT_TRIPLE_CLICK) {
                struct input_event *iev = &ev.input;
                int modal_open = (ui_ctx_get_modal(state.ui_ctx) != NULL);
                int popup_open = (ui_ctx_get_popup(state.ui_ctx) != NULL);

                ui_handle_event(state.ui_ctx, state.root, iev);

                if (ui_ctx_get_popup(state.ui_ctx) == NULL && state.open_menu != -1) {
                    state.open_menu = -1;
                    state.menubar->menu.selected = -1;
                    ui_widget_invalidate(state.menubar);
                }

                if (ev.type == VANILLA_EVENT_INPUT && iev->type == EV_KEY) {
                    if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL) {
                        state.ctrl_down = (iev->value != 0);
                    } else if (iev->value == 1 && !modal_open && !popup_open) {
                        if (state.ctrl_down && iev->code == KEY_Q) {
                            state.running = 0;
                            break;
                        } else if (iev->code == KEY_F5) {
                            tm_refresh(&state);
                            state.last_refresh_ms = tm_now_ms();
                        } else if (iev->code == KEY_DELETE && state.current_tab == 1) {
                            tm_request_kill_selected(&state);
                        }
                    }
                }
            }
        }

        if (!state.running || ev_ret < 0)
            break;

        int64_t now_ms = tm_now_ms();
        if (now_ms - state.last_refresh_ms >= TM_REFRESH_MS ||
            now_ms < state.last_refresh_ms) {
            tm_refresh(&state);
            state.last_refresh_ms = now_ms;
        }

        ui_widget_t *cur_popup = ui_ctx_get_popup(state.ui_ctx);
        ui_widget_t *cur_modal = ui_ctx_get_modal(state.ui_ctx);
        int dirty = force_present ||
                    state.root->dirty ||
                    (cur_popup && !cur_popup->hidden && cur_popup->dirty) ||
                    (cur_modal && !cur_modal->hidden && cur_modal->dirty) ||
                    (cur_popup != prev_popup) ||
                    (cur_modal != prev_modal);

        if (dirty) {
            ui_render(state.ui_ctx, state.root, &win->surface, NULL);
            vanilla_present(win, NULL);
            prev_popup = cur_popup;
            prev_modal = cur_modal;
        }

        int64_t elapsed = tm_now_ms() - state.last_refresh_ms;
        int timeout_ms = TM_REFRESH_MS - (int)elapsed;
        if (timeout_ms < 1)
            timeout_ms = 1;
        if (timeout_ms > TM_REFRESH_MS)
            timeout_ms = TM_REFRESH_MS;

        pfd.revents = 0;
        int pret = poll(&pfd, 1, timeout_ms);
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
