/*
 * Project Tsukasa — FAT32 read/write filesystem driver (multi-volume, guide 12)
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

#include "fat32.h"
#include "../drv/blockdev.h"
#include <stdint.h>
#include <stddef.h>

/* BPB / FAT32 on-disk structures */

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    char     vol_label[11];
    char     fs_type[8];
} bpb_t;

typedef struct __attribute__((packed)) {
    char     name[11];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t first_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_clus_lo;
    uint32_t file_size;
} dir_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t first_clus;
    uint16_t name3[2];
} lfn_entry_t;

#define ATTR_DIR        0x10u
#define ATTR_LFN        0x0Fu
#define ATTR_VOLUME_ID  0x08u
#define DIR_ENTRY_SIZE  32u

struct fat32_volume {
    int          ready;
    block_dev_t *bd;
    uint32_t     fat_lba;
    uint32_t     data_lba;
    uint32_t     root_cluster;
    uint32_t     sectors_per_cluster;
    uint32_t     fat_size;
    uint32_t     bytes_per_sector;
    uint32_t     fat_count;
    uint32_t     cluster_limit;
    uint32_t     next_free_hint; 
    uint32_t     fat_cache_lba;
    /* uint32_t     fat_cache_lba;   volume-relative LBA in fat_cache; 0 = invalid (LBA 0 is never a FAT sector... */
    uint8_t      fat_cache[512];
    /* uint8_t      fat_cache[512];  FAT read cache: fat_next() on a long chain otherwise re-reads the same FAT... */
    uint8_t      sector[512];
    uint8_t      io_buf[512];
};

#define FAT32_MAX_VOLUMES 4
static struct fat32_volume g_volumes[FAT32_MAX_VOLUMES];

static struct fat32_volume *g_primary = NULL;

static int disk_read(struct fat32_volume *v, uint32_t lba, void *buf)
{
    return (v && v->bd && v->bd->read)
               ? v->bd->read(v->bd, (uint64_t)lba, 1, buf)
               : -1;
}

static int disk_write(struct fat32_volume *v, uint32_t lba, const void *buf)
{
    return (v && v->bd && v->bd->write)
               ? v->bd->write(v->bd, (uint64_t)lba, 1, buf)
               : -1;
}

static int k_strncmpi(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 1;
        if (!ca) break;
    }
    return 0;
}

static void k_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static uint32_t cluster_to_lba(struct fat32_volume *v, uint32_t cluster)
{
    return v->data_lba + (cluster - 2) * v->sectors_per_cluster;
}

/* Read the FAT entry for a cluster (returns next cluster or 0x0FFFFFFF for EOC). */
static uint32_t fat_next(struct fat32_volume *v, uint32_t cluster)
{
    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector = v->fat_lba + fat_offset / v->bytes_per_sector;
    uint32_t entry_off  = fat_offset % v->bytes_per_sector;

    if (v->fat_cache_lba != fat_sector) {
        if (disk_read(v, fat_sector, v->fat_cache) < 0) {
            v->fat_cache_lba = 0;
            return 0x0FFFFFFFu;
        }
        v->fat_cache_lba = fat_sector;
    }
    uint32_t entry;
    entry = (uint32_t)v->fat_cache[entry_off]
          | ((uint32_t)v->fat_cache[entry_off+1] << 8)
          | ((uint32_t)v->fat_cache[entry_off+2] << 16)
          | ((uint32_t)v->fat_cache[entry_off+3] << 24);
    return entry & 0x0FFFFFFFu;
}

static int fat_set_entry(struct fat32_volume *v, uint32_t cluster,
                         uint32_t value)
{
    uint32_t off = (cluster * 4u) % v->bytes_per_sector;
    uint32_t sec = (cluster * 4u) / v->bytes_per_sector;

    v->fat_cache_lba = 0;

    for (uint32_t f = 0; f < v->fat_count; f++) {
        uint32_t lba = v->fat_lba + f * v->fat_size + sec;
        uint32_t old, nv;
        if (disk_read(v, lba, v->sector) < 0)
            return -1;
        old = (uint32_t)v->sector[off] | ((uint32_t)v->sector[off + 1] << 8) |
              ((uint32_t)v->sector[off + 2] << 16) |
              ((uint32_t)v->sector[off + 3] << 24);
        nv = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        v->sector[off]     = (uint8_t)nv;
        v->sector[off + 1] = (uint8_t)(nv >> 8);
        v->sector[off + 2] = (uint8_t)(nv >> 16);
        v->sector[off + 3] = (uint8_t)(nv >> 24);
        if (disk_write(v, lba, v->sector) < 0)
            return -1;
    }
    return 0;
}

static uint32_t fat_alloc_cluster(struct fat32_volume *v)
{
    uint32_t start = v->next_free_hint;
    uint32_t c;

    if (start < 3 || start >= v->cluster_limit)
        start = 3;
    c = start;
    do {
        uint32_t fat_offset = c * 4u;
        uint32_t lba = v->fat_lba + fat_offset / v->bytes_per_sector;
        uint32_t off = fat_offset % v->bytes_per_sector;
        uint32_t entry;

        if (disk_read(v, lba, v->sector) < 0)
            return 0;
        entry = ((uint32_t)v->sector[off] |
                 ((uint32_t)v->sector[off + 1] << 8) |
                 ((uint32_t)v->sector[off + 2] << 16) |
                 ((uint32_t)v->sector[off + 3] << 24)) & 0x0FFFFFFFu;
        if (entry == 0) {
            if (fat_set_entry(v, c, 0x0FFFFFFFu) != 0)
                return 0;
            v->next_free_hint = c + 1;
            return c;
        }
        c++;
        if (c >= v->cluster_limit)
            c = 3;
    } while (c != start);
    return 0;
}

static uint32_t fat_find_free_run(struct fat32_volume *v, uint32_t count)
{
    uint32_t run_start = 0, run_len = 0;
    uint32_t cached = 0;

    if (count == 0)
        return 0;
    for (uint32_t c = 3; c < v->cluster_limit; c++) {
        uint32_t sec = v->fat_lba + (c * 4u) / v->bytes_per_sector;
        uint32_t off = (c * 4u) % v->bytes_per_sector;
        uint32_t entry;

        if (cached != sec) {
            if (disk_read(v, sec, v->sector) < 0)
                return 0;
            cached = sec;
        }
        entry = ((uint32_t)v->sector[off] |
                 ((uint32_t)v->sector[off + 1] << 8) |
                 ((uint32_t)v->sector[off + 2] << 16) |
                 ((uint32_t)v->sector[off + 3] << 24)) & 0x0FFFFFFFu;
        if (entry == 0) {
            if (run_len == 0)
                run_start = c;
            if (++run_len >= count)
                return run_start;
        } else {
            run_len = 0;
        }
    }
    return 0;
}

/* Chain the contiguous run [first, first+count): each entry links to its successor, the last is EOC. */
static int fat_write_run(struct fat32_volume *v, uint32_t first,
                         uint32_t count)
{
    uint32_t c = first;
    uint32_t end = first + count;

    v->fat_cache_lba = 0;

    while (c < end) {
        uint32_t sec = (c * 4u) / v->bytes_per_sector;

        if (disk_read(v, v->fat_lba + sec, v->sector) < 0)
            return -1;
        while (c < end && (c * 4u) / v->bytes_per_sector == sec) {
            uint32_t off = (c * 4u) % v->bytes_per_sector;
            uint32_t val = (c + 1 < end) ? (c + 1) : 0x0FFFFFFFu;
            uint32_t old = (uint32_t)v->sector[off] |
                           ((uint32_t)v->sector[off + 1] << 8) |
                           ((uint32_t)v->sector[off + 2] << 16) |
                           ((uint32_t)v->sector[off + 3] << 24);
            uint32_t nv = (old & 0xF0000000u) | (val & 0x0FFFFFFFu);
            v->sector[off]     = (uint8_t)nv;
            v->sector[off + 1] = (uint8_t)(nv >> 8);
            v->sector[off + 2] = (uint8_t)(nv >> 16);
            v->sector[off + 3] = (uint8_t)(nv >> 24);
            c++;
        }
        for (uint32_t f = 0; f < v->fat_count; f++) {
            if (disk_write(v, v->fat_lba + f * v->fat_size + sec,
                           v->sector) < 0)
                return -1;
        }
    }
    return 0;
}

static int to_dos_83(const char *name, char out11[11])
{
    int dot = -1, len = 0, needs_lfn = 0, i;

    for (len = 0; name[len]; len++) {
        if (name[len] == '.' && dot < 0)
            dot = len;
        else if (name[len] >= 'a' && name[len] <= 'z')
            needs_lfn = 1;
    }
    if (dot < 0) {
        if (len > 8)
            needs_lfn = 1;
    } else {
        if (dot > 8 || (len - dot - 1) > 3)
            needs_lfn = 1;
    }

    for (i = 0; i < 11; i++)
        out11[i] = ' ';
    {
        int base = (dot < 0) ? len : dot;
        for (i = 0; i < base && i < 8; i++) {
            char ch = name[i];
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 32);
            out11[i] = ch;
        }
        if (dot >= 0) {
            for (i = 0; i < 3 && name[dot + 1 + i]; i++) {
                char ch = name[dot + 1 + i];
                if (ch >= 'a' && ch <= 'z')
                    ch = (char)(ch - 32);
                out11[8 + i] = ch;
            }
        }
    }
    return needs_lfn;
}

/* LFN checksum over the 11-byte short name — FAT spec algorithm. */
static uint8_t lfn_checksum(const char dos11[11])
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1u) << 7) + (sum >> 1) + (uint8_t)dos11[i]);
    return sum;
}

/* Find `nslots` contiguous free directory slots WITHIN ONE SECTOR of the directory chain (an LFN run never... */
static int dir_find_slots(struct fat32_volume *v, uint32_t dir_cluster,
                          int nslots, uint32_t *out_lba, int *out_slot)
{
    uint32_t cluster = dir_cluster, last = dir_cluster;
    int per_sector = (int)(v->bytes_per_sector / DIR_ENTRY_SIZE);

    while (cluster >= 2 && cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            int run = 0;
            if (disk_read(v, lba + s, v->sector) < 0)
                return -1;
            for (int i = 0; i < per_sector; i++) {
                uint8_t first = v->sector[i * DIR_ENTRY_SIZE];
                if (first == 0x00u || first == 0xE5u) {
                    if (++run >= nslots) {
                        *out_lba = lba + s;
                        *out_slot = i - nslots + 1;
                        return 0;
                    }
                } else {
                    run = 0;
                }
            }
        }
        last = cluster;
        cluster = fat_next(v, cluster);
    }

    {
        uint32_t nc = fat_alloc_cluster(v);
        uint32_t lba;
        if (!nc)
            return -1;
        lba = cluster_to_lba(v, nc);
        for (uint32_t i = 0; i < v->bytes_per_sector; i++)
            v->io_buf[i] = 0;
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            if (disk_write(v, lba + s, v->io_buf) < 0)
                return -1;
        }
        if (fat_set_entry(v, last, nc) != 0)
            return -1;
        *out_lba = lba;
        *out_slot = 0;
        return 0;
    }
}

/* Strip trailing spaces from an 11-char 8.3 name and convert to NUL-terminated. */
static void parse_83name(const char src[11], char *dst, int dstlen)
{
    int i = 0, j = 0;
    for (; i < 8 && src[i] != ' '; i++) {
        if (j < dstlen - 2) dst[j++] = src[i];
    }
    if (src[8] != ' ') {
        if (j < dstlen - 2) dst[j++] = '.';
        for (int k = 8; k < 11 && src[k] != ' '; k++)
            if (j < dstlen - 2) dst[j++] = src[k];
    }
    dst[j] = '\0';
}

/* Iterate over all entries of a directory cluster chain. */
typedef int (*dir_cb)(const dir_entry_t *de, const char *name, void *ctx);

static void iterate_dir(struct fat32_volume *v, uint32_t start_cluster,
                        dir_cb cb, void *ctx)
{
    char lfn_buf[FAT32_NAME_MAX];
    int  lfn_len = 0;
    int  has_lfn = 0;

    uint32_t cluster = start_cluster;

    while (cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            if (disk_read(v, lba + s, v->sector) < 0) return;

            dir_entry_t *entries = (dir_entry_t *)v->sector;
            int per_sector = (int)(v->bytes_per_sector / DIR_ENTRY_SIZE);

            for (int i = 0; i < per_sector; i++) {
                dir_entry_t *de = &entries[i];
                uint8_t first = (uint8_t)de->name[0];

                if (first == 0x00u) return;
                if (first == 0xE5u) {
                    has_lfn = 0; lfn_len = 0;
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    lfn_entry_t *lfn = (lfn_entry_t *)de;
                    int seq = (lfn->order & 0x1Fu) - 1;
                    int off = seq * 13;
                    for (int k = 0; k < 5 && off + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name1[k];
                        if (wc == 0xFFFFu || wc == 0) goto lfn_done;
                        lfn_buf[off + k] = (char)(wc & 0xFF);
                        if (off + k + 1 > lfn_len) lfn_len = off + k + 1;
                    }
                    for (int k = 0; k < 6 && off + 5 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name2[k];
                        if (wc == 0xFFFFu || wc == 0) goto lfn_done;
                        lfn_buf[off + 5 + k] = (char)(wc & 0xFF);
                        if (off + 5 + k + 1 > lfn_len) lfn_len = off + 5 + k + 1;
                    }
                    for (int k = 0; k < 2 && off + 11 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name3[k];
                        if (wc == 0xFFFFu || wc == 0) goto lfn_done;
                        lfn_buf[off + 11 + k] = (char)(wc & 0xFF);
                        if (off + 11 + k + 1 > lfn_len) lfn_len = off + 11 + k + 1;
                    }
                    lfn_done:
                    lfn_buf[lfn_len] = '\0';
                    has_lfn = 1;
                    continue;
                }

                if (de->attr & ATTR_VOLUME_ID) { has_lfn = 0; lfn_len = 0; continue; }
                if (de->name[0] == '.' ) { has_lfn = 0; lfn_len = 0; continue; }

                char name83[13];
                parse_83name(de->name, name83, sizeof(name83));

                const char *display = has_lfn ? lfn_buf : name83;
                int stop = cb(de, display, ctx);
                has_lfn = 0; lfn_len = 0;
                if (stop) return;
            }
        }
        cluster = fat_next(v, cluster);
    }
}

static int normalize_path(const char *in, char *out, int out_cap)
{
    char segs[32][FAT32_NAME_MAX];
    int seg_count = 0;
    int oi = 0;
    const char *p;

    if (!in || !out || out_cap <= 1 || in[0] != '/')
        return -1;

    p = in;
    while (*p == '/')
        p++;
    while (*p) {
        char seg[FAT32_NAME_MAX];
        int si = 0;
        while (*p && *p != '/') {
            if (si < FAT32_NAME_MAX - 1)
                seg[si++] = *p;
            p++;
        }
        seg[si] = '\0';
        while (*p == '/')
            p++;

        if (seg[0] == '\0' || (seg[0] == '.' && seg[1] == '\0'))
            continue;
        if (seg[0] == '.' && seg[1] == '.' && seg[2] == '\0') {
            if (seg_count > 0)
                seg_count--;
            continue;
        }
        if (seg_count >= 32)
            return -1;
        k_strcpy(segs[seg_count], seg, FAT32_NAME_MAX);
        seg_count++;
    }

    out[oi++] = '/';
    if (seg_count == 0) {
        out[oi] = '\0';
        return 0;
    }
    for (int i = 0; i < seg_count; i++) {
        int j = 0;
        if (i > 0) {
            if (oi >= out_cap - 1)
                return -1;
            out[oi++] = '/';
        }
        while (segs[i][j]) {
            if (oi >= out_cap - 1)
                return -1;
            out[oi++] = segs[i][j++];
        }
    }
    out[oi] = '\0';
    return 0;
}

/* Find the cluster of a directory by traversing path components. */
typedef struct {
    const char *target;
    uint32_t    found_cluster;
    int         found;
    struct fat32_volume *vol;
} find_dir_ctx_t;

static int find_dir_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    find_dir_ctx_t *f = (find_dir_ctx_t *)ctx;
    if ((de->attr & ATTR_DIR) && k_strncmpi(name, f->target, FAT32_NAME_MAX) == 0) {
        f->found_cluster = ((uint32_t)de->first_clus_hi << 16) | de->first_clus_lo;
        if (f->found_cluster == 0) f->found_cluster = f->vol->root_cluster;
        f->found = 1;
        return 1;
    }
    return 0;
}

/* Resolve a path like "/dir1/dir2/file" to return the cluster of its parent dir and point *leaf at the final... */
static uint32_t resolve_parent(struct fat32_volume *v, const char *path,
                               const char **leaf)
{
    static char comps[32][FAT32_NAME_MAX];
    int ncomp = 0;

    const char *p = path;
    while (*p == '/') p++;
    while (*p && ncomp < 32) {
        int i = 0;
        while (*p && *p != '/' && i < FAT32_NAME_MAX - 1)
            comps[ncomp][i++] = *p++;
        comps[ncomp][i] = '\0';
        ncomp++;
        while (*p == '/') p++;
    }

    if (ncomp == 0) { *leaf = NULL; return v->root_cluster; }
    *leaf = comps[ncomp - 1];

    uint32_t cur = v->root_cluster;
    for (int ci = 0; ci < ncomp - 1; ci++) {
        find_dir_ctx_t fc;
        fc.target = comps[ci];
        fc.found  = 0;
        fc.vol    = v;
        iterate_dir(v, cur, find_dir_cb, &fc);
        if (!fc.found) return 0;
        cur = fc.found_cluster;
    }
    return cur;
}

typedef struct {
    const char *target;
    dir_entry_t out;
    uint32_t lba;
    uint32_t entry_off;
    int found;
} find_entry_raw_ctx_t;

static int find_entry_raw(struct fat32_volume *v, uint32_t start_cluster,
                          const char *target,
                          find_entry_raw_ctx_t *out_ctx)
{
    char lfn_buf[FAT32_NAME_MAX];
    int lfn_len = 0;
    int has_lfn = 0;
    uint32_t cluster = start_cluster;

    if (!target || !out_ctx)
        return -1;

    while (cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            if (disk_read(v, lba + s, v->sector) < 0)
                return -1;

            dir_entry_t *entries = (dir_entry_t *)v->sector;
            int per_sector = (int)(v->bytes_per_sector / DIR_ENTRY_SIZE);
            for (int i = 0; i < per_sector; i++) {
                dir_entry_t *de = &entries[i];
                uint8_t first = (uint8_t)de->name[0];
                if (first == 0x00u)
                    return -1;
                if (first == 0xE5u) {
                    has_lfn = 0;
                    lfn_len = 0;
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    lfn_entry_t *lfn = (lfn_entry_t *)de;
                    int seq = (lfn->order & 0x1Fu) - 1;
                    int off = seq * 13;
                    for (int k = 0; k < 5 && off + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name1[k];
                        if (wc == 0xFFFFu || wc == 0) break;
                        lfn_buf[off + k] = (char)(wc & 0xFF);
                        if (off + k + 1 > lfn_len) lfn_len = off + k + 1;
                    }
                    for (int k = 0; k < 6 && off + 5 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name2[k];
                        if (wc == 0xFFFFu || wc == 0) break;
                        lfn_buf[off + 5 + k] = (char)(wc & 0xFF);
                        if (off + 5 + k + 1 > lfn_len) lfn_len = off + 5 + k + 1;
                    }
                    for (int k = 0; k < 2 && off + 11 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name3[k];
                        if (wc == 0xFFFFu || wc == 0) break;
                        lfn_buf[off + 11 + k] = (char)(wc & 0xFF);
                        if (off + 11 + k + 1 > lfn_len) lfn_len = off + 11 + k + 1;
                    }
                    lfn_buf[lfn_len] = '\0';
                    has_lfn = 1;
                    continue;
                }
                if (de->attr & ATTR_VOLUME_ID) {
                    has_lfn = 0;
                    lfn_len = 0;
                    continue;
                }
                if (de->name[0] == '.') {
                    has_lfn = 0;
                    lfn_len = 0;
                    continue;
                }

                {
                    char name83[13];
                    const char *name = NULL;
                    parse_83name(de->name, name83, sizeof(name83));
                    name = has_lfn ? lfn_buf : name83;
                    has_lfn = 0;
                    lfn_len = 0;
                    if (k_strncmpi(name, target, FAT32_NAME_MAX) != 0)
                        continue;

                    out_ctx->out = *de;
                    out_ctx->lba = lba + s;
                    out_ctx->entry_off = (uint32_t)i * DIR_ENTRY_SIZE;
                    out_ctx->found = 1;
                    return 0;
                }
            }
        }
        cluster = fat_next(v, cluster);
    }
    return -1;
}

typedef struct {
    const char    *name;
    fat32_dirent_t out;
    int            found;
} stat_ctx_t;

static int stat_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    stat_ctx_t *s = (stat_ctx_t *)ctx;
    if (k_strncmpi(name, s->name, FAT32_NAME_MAX) == 0) {
        k_strcpy(s->out.name, name, FAT32_NAME_MAX);
        s->out.size          = de->file_size;
        s->out.is_dir        = (de->attr & ATTR_DIR) ? 1 : 0;
        s->out.first_cluster = ((uint32_t)de->first_clus_hi << 16) | de->first_clus_lo;
        s->found = 1;
        return 1;
    }
    return 0;
}

typedef struct {
    fat32_dirent_t *entries;
    int max;
    int count;
} list_ctx_t;

static int list_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    list_ctx_t *l = (list_ctx_t *)ctx;
    if (l->count >= l->max) return 1;
    k_strcpy(l->entries[l->count].name, name, FAT32_NAME_MAX);
    l->entries[l->count].size = de->file_size;
    l->entries[l->count].is_dir = (de->attr & ATTR_DIR) ? 1 : 0;
    l->entries[l->count].first_cluster =
        ((uint32_t)de->first_clus_hi << 16) | de->first_clus_lo;
    l->count++;
    return 0;
}

/* Per-volume public API */

fat32_volume_t *fat32_vol_mount(struct block_dev *bd)
{
    struct fat32_volume *v = NULL;

    if (!bd || !bd->read)
        return NULL;

    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
        if (g_volumes[i].ready && g_volumes[i].bd == bd) {
            v = &g_volumes[i];
            v->ready = 0;
            break;
        }
    }
    if (!v) {
        for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
            if (!g_volumes[i].ready) {
                v = &g_volumes[i];
                break;
            }
        }
    }
    if (!v)
        return NULL;

    v->bd = bd;
    if (disk_read(v, 0, v->sector) < 0) return NULL;
    bpb_t *bpb = (bpb_t *)v->sector;

    if (v->sector[510] != 0x55u || v->sector[511] != 0xAAu) return NULL;
    if (bpb->bytes_per_sector == 0) return NULL;

    if (bpb->root_entry_count != 0 || bpb->fat_size_16 != 0) return NULL;
    if (bpb->fat_size_32 == 0) return NULL;

    v->bytes_per_sector    = bpb->bytes_per_sector;
    v->sectors_per_cluster = bpb->sectors_per_cluster;
    v->fat_size            = bpb->fat_size_32;
    v->root_cluster        = bpb->root_cluster;
    v->fat_lba             = bpb->reserved_sectors;
    v->fat_count           = bpb->fat_count;
    v->data_lba            = v->fat_lba + bpb->fat_count * v->fat_size;

    {
        uint64_t dev_sectors = v->bd->sector_count;
        uint32_t by_data = 2, by_fat;
        if (dev_sectors > v->data_lba)
            by_data = 2 + (uint32_t)((dev_sectors - v->data_lba) /
                                     v->sectors_per_cluster);
        by_fat = (v->fat_size * v->bytes_per_sector) / 4u;
        v->cluster_limit = (by_data < by_fat) ? by_data : by_fat;
    }
    v->next_free_hint = 3;
    v->fat_cache_lba = 0;

    v->ready = 1;
    return v;
}

int fat32_vol_create_file(fat32_volume_t *vol, const char *path)
{
    char norm[FAT32_NAME_MAX];
    const char *name;
    char dos11[11];
    int needs_lfn, lfn_entries, nslots, slot;
    uint32_t lba;
    fat32_dirent_t existing;
    int name_len;

    if (!vol || !vol->ready || !vol->bd->write || !path)
        return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;
    if (norm[0] != '/' || norm[1] == '\0')
        return -1;
    name = norm + 1;
    for (name_len = 0; name[name_len]; name_len++) {
        if (name[name_len] == '/')
            return -1;
    }
    if (name_len > 64)
        return -1;
    if (fat32_vol_stat(vol, norm, &existing) == 0)
        return -1;

    needs_lfn = to_dos_83(name, dos11);
    lfn_entries = needs_lfn ? ((name_len + 12) / 13) : 0;
    nslots = lfn_entries + 1;
    if (nslots > (int)(vol->bytes_per_sector / DIR_ENTRY_SIZE))
        return -1;

    if (dir_find_slots(vol, vol->root_cluster, nslots, &lba, &slot) != 0)
        return -1;

    if (disk_read(vol, lba, vol->sector) < 0)
        return -1;

    if (needs_lfn) {
        uint8_t sum = lfn_checksum(dos11);
        for (int i = 0; i < lfn_entries; i++) {
            lfn_entry_t *lfn =
                (lfn_entry_t *)(vol->sector + (slot + i) * DIR_ENTRY_SIZE);
            int chunk = lfn_entries - i - 1;
            for (uint32_t b = 0; b < DIR_ENTRY_SIZE; b++)
                ((uint8_t *)lfn)[b] = 0;
            lfn->order = (uint8_t)(lfn_entries - i);
            if (i == 0)
                lfn->order |= 0x40;
            lfn->attr = ATTR_LFN;
            lfn->type = 0;
            lfn->checksum = sum;
            lfn->first_clus = 0;
            for (int k = 0; k < 13; k++) {
                int ci = chunk * 13 + k;
                uint16_t wc;
                if (ci < name_len)
                    wc = (uint16_t)(uint8_t)name[ci];
                else if (ci == name_len)
                    wc = 0x0000;
                else
                    wc = 0xFFFF;
                if (k < 5)
                    lfn->name1[k] = wc;
                else if (k < 11)
                    lfn->name2[k - 5] = wc;
                else
                    lfn->name3[k - 11] = wc;
            }
        }
    }

    {
        dir_entry_t *de =
            (dir_entry_t *)(vol->sector + (slot + lfn_entries) * DIR_ENTRY_SIZE);
        for (uint32_t b = 0; b < DIR_ENTRY_SIZE; b++)
            ((uint8_t *)de)[b] = 0;
        for (int i = 0; i < 11; i++)
            de->name[i] = dos11[i];
        de->attr = 0x20;
        de->first_clus_hi = 0;
        de->first_clus_lo = 0;
        de->file_size = 0;
    }

    return disk_write(vol, lba, vol->sector);
}

int fat32_vol_stat(fat32_volume_t *vol, const char *path, fat32_dirent_t *out)
{
    char norm[FAT32_NAME_MAX];
    if (!vol || !vol->ready || !path || !out) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    if (norm[0] == '/' && norm[1] == '\0') {
        k_strcpy(out->name, "/", FAT32_NAME_MAX);
        out->size = 0; out->is_dir = 1;
        out->first_cluster = vol->root_cluster;
        return 0;
    }

    const char *leaf;
    uint32_t parent = resolve_parent(vol, norm, &leaf);
    if (!parent || !leaf) return -1;

    stat_ctx_t sc;
    sc.name  = leaf;
    sc.found = 0;
    iterate_dir(vol, parent, stat_cb, &sc);
    if (!sc.found) return -1;
    *out = sc.out;
    return 0;
}

int fat32_vol_list_dir(fat32_volume_t *vol, const char *path,
                       fat32_dirent_t *entries, int max)
{
    char norm[FAT32_NAME_MAX];
    if (!vol || !vol->ready || !entries || max <= 0) return -1;
    if (!path || normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    uint32_t dir_cluster;
    if (norm[0] == '/' && (norm[1] == '\0')) {
        dir_cluster = vol->root_cluster;
    } else {
        fat32_dirent_t de;
        if (fat32_vol_stat(vol, norm, &de) < 0 || !de.is_dir) return -1;
        dir_cluster = de.first_cluster;
    }

    list_ctx_t lc = { entries, max, 0 };
    iterate_dir(vol, dir_cluster, list_cb, &lc);
    return lc.count;
}

int fat32_vol_read_file(fat32_volume_t *vol, const char *path,
                        void *buf, size_t max_bytes)
{
    char norm[FAT32_NAME_MAX];
    if (!vol || !vol->ready || !path || !buf) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    fat32_dirent_t de;
    if (fat32_vol_stat(vol, norm, &de) < 0 || de.is_dir) return -1;

    size_t to_read = de.size < max_bytes ? de.size : max_bytes;
    size_t done = 0;
    uint32_t cluster = de.first_cluster;
    uint8_t *dst = (uint8_t *)buf;

    while (cluster < 0x0FFFFFF8u && done < to_read) {
        uint32_t lba = cluster_to_lba(vol, cluster);
        for (uint32_t s = 0; s < vol->sectors_per_cluster && done < to_read; s++) {
            if (disk_read(vol, lba + s, vol->io_buf) < 0) return (int)done;
            size_t chunk = vol->bytes_per_sector;
            if (chunk > to_read - done) chunk = to_read - done;
            for (size_t i = 0; i < chunk; i++) dst[done++] = vol->io_buf[i];
        }
        cluster = fat_next(vol, cluster);
    }
    return (int)done;
}

int fat32_vol_write_file(fat32_volume_t *vol, const char *path,
                         const void *buf, size_t size)
{
    char norm[FAT32_NAME_MAX];
    const char *leaf = NULL;
    uint32_t parent = 0;
    find_entry_raw_ctx_t raw;
    size_t chain_capacity = 0;
    size_t done = 0;
    uint32_t cluster = 0;
    uint32_t cluster0 = 0;
    const uint8_t *src = (const uint8_t *)buf;

    if (!vol || !vol->ready || !path || (!buf && size > 0)) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    parent = resolve_parent(vol, norm, &leaf);
    if (!parent || !leaf)
        return -1;

    raw.found = 0;
    if (find_entry_raw(vol, parent, leaf, &raw) != 0 || !raw.found)
        return -1;
    if (raw.out.attr & ATTR_DIR)
        return -1;

    cluster = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;

    {
        uint32_t c = cluster;
        uint32_t last = 0;
        size_t cbytes = (size_t)vol->sectors_per_cluster *
                        (size_t)vol->bytes_per_sector;
        while (c >= 2 && c < 0x0FFFFFF8u) {
            chain_capacity += cbytes;
            last = c;
            c = fat_next(vol, c);
        }
        if (size > chain_capacity) {
            uint32_t need = (uint32_t)((size - chain_capacity + cbytes - 1) /
                                       cbytes);
            uint32_t run = fat_find_free_run(vol, need);
            if (run) {
                if (fat_write_run(vol, run, need) != 0)
                    return -1;
                if (last >= 2 && fat_set_entry(vol, last, run) != 0)
                    return -1;
                if (cluster < 2)
                    cluster = run;
                if (run + need > vol->next_free_hint)
                    vol->next_free_hint = run + need;
                chain_capacity += (size_t)need * cbytes;
            } else {
                while (size > chain_capacity) {
                    uint32_t nc = fat_alloc_cluster(vol);
                    if (!nc)
                        return -1;
                    if (last >= 2 && fat_set_entry(vol, last, nc) != 0)
                        return -1;
                    if (cluster < 2)
                        cluster = nc;
                    last = nc;
                    chain_capacity += cbytes;
                }
            }
        }
    }
    if (size > chain_capacity)
        return -1;

    cluster0 = cluster;

    /* Data writes, batched: contiguous cluster runs (the fast-path allocation always produces one) go to the... */
    while (cluster >= 2 && cluster < 0x0FFFFFF8u) {
        uint32_t run_start = cluster;
        uint32_t run_clusters = 1;
        uint32_t nxt = fat_next(vol, cluster);

        while (nxt == cluster + 1) {
            cluster = nxt;
            run_clusters++;
            nxt = fat_next(vol, cluster);
        }

        {
            uint32_t lba = cluster_to_lba(vol, run_start);
            uint32_t sectors = run_clusters * vol->sectors_per_cluster;
            for (uint32_t s = 0; s < sectors; s++) {
                size_t remain = (size > done) ? (size - done) : 0;
                if (remain >= vol->bytes_per_sector) {
                    uint32_t whole = (uint32_t)(remain / vol->bytes_per_sector);
                    uint32_t n = sectors - s;
                    if (n > whole)
                        n = whole;
                    if (n > 64u)
                        n = 64u;
                    if (vol->bd->write(vol->bd, (uint64_t)(lba + s), n,
                                       src + done) != 0)
                        return -1;
                    done += (size_t)n * vol->bytes_per_sector;
                    s += n - 1;
                    continue;
                }
                for (uint32_t i = 0; i < vol->bytes_per_sector; i++)
                    vol->io_buf[i] = 0;
                for (size_t i = 0; i < remain; i++)
                    vol->io_buf[i] = src[done + i];
                done += remain;
                if (disk_write(vol, lba + s, vol->io_buf) < 0)
                    return -1;
            }
        }

        cluster = nxt;
    }
    if (done != size)
        return -1;

    if (disk_read(vol, raw.lba, vol->sector) < 0)
        return -1;
    {
        dir_entry_t *ent = (dir_entry_t *)(vol->sector + raw.entry_off);
        ent->file_size = (uint32_t)size;
        ent->first_clus_hi = (uint16_t)(cluster0 >> 16);
        ent->first_clus_lo = (uint16_t)(cluster0 & 0xFFFFu);
    }
    if (disk_write(vol, raw.lba, vol->sector) < 0)
        return -1;
    return 0;
}

static int validate_short_name(const char *name, uint8_t out83[11])
{
    int i = 0;
    int dot = -1;
    int base_len = 0;
    int ext_len = 0;

    if (!name || !name[0])
        return -1;
    for (i = 0; name[i]; i++) {
        if (name[i] == '.') {
            if (dot >= 0)
                return -1;
            dot = i;
            continue;
        }
        if (!((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-'))
            return -1;
    }
    if (dot < 0)
        base_len = i;
    else {
        base_len = dot;
        ext_len = i - dot - 1;
    }
    if (base_len <= 0 || base_len > 8 || ext_len > 3)
        return -1;

    for (i = 0; i < 11; i++)
        out83[i] = ' ';
    for (i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        out83[i] = (uint8_t)c;
    }
    for (i = 0; i < ext_len; i++) {
        char c = name[dot + 1 + i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        out83[8 + i] = (uint8_t)c;
    }
    return 0;
}

int fat32_vol_rename(fat32_volume_t *vol, const char *old_path,
                     const char *new_path)
{
    char old_norm[FAT32_NAME_MAX];
    char new_norm[FAT32_NAME_MAX];
    const char *old_leaf = NULL;
    const char *new_leaf = NULL;
    uint32_t old_parent = 0;
    uint32_t new_parent = 0;
    find_entry_raw_ctx_t src;
    find_entry_raw_ctx_t dst;
    uint8_t new83[11];

    if (!vol || !vol->ready || !old_path || !new_path)
        return -1;
    if (normalize_path(old_path, old_norm, FAT32_NAME_MAX) != 0)
        return -1;
    if (normalize_path(new_path, new_norm, FAT32_NAME_MAX) != 0)
        return -1;

    old_parent = resolve_parent(vol, old_norm, &old_leaf);
    if (!old_parent || !old_leaf)
        return -1;
    {
        static char old_leaf_copy[FAT32_NAME_MAX];
        k_strcpy(old_leaf_copy, old_leaf, FAT32_NAME_MAX);
        old_leaf = old_leaf_copy;
    }
    new_parent = resolve_parent(vol, new_norm, &new_leaf);
    if (!new_parent || !new_leaf)
        return -1;
    if (old_parent != new_parent)
        return -1;
    if (validate_short_name(new_leaf, new83) != 0)
        return -1;

    src.found = 0;
    dst.found = 0;
    if (find_entry_raw(vol, old_parent, old_leaf, &src) != 0 || !src.found)
        return -1;
    if (find_entry_raw(vol, new_parent, new_leaf, &dst) == 0 && dst.found)
        return -1;

    if (disk_read(vol, src.lba, vol->sector) < 0)
        return -1;
    {
        dir_entry_t *ent = (dir_entry_t *)(vol->sector + src.entry_off);
        for (int i = 0; i < 11; i++)
            ent->name[i] = (char)new83[i];
    }
    if (disk_write(vol, src.lba, vol->sector) < 0)
        return -1;
    return 0;
}

/* Legacy single-volume API (the /disk mount) */

int fat32_init(void)
{
    block_dev_t *bd = blockdev_first_raw_fat32();
    g_primary = bd ? fat32_vol_mount(bd) : NULL;
    return g_primary ? 0 : -1;
}

int fat32_stat(const char *path, fat32_dirent_t *out)
{
    return fat32_vol_stat(g_primary, path, out);
}

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max)
{
    return fat32_vol_list_dir(g_primary, path, entries, max);
}

int fat32_read_file(const char *path, void *buf, size_t max_bytes)
{
    return fat32_vol_read_file(g_primary, path, buf, max_bytes);
}

int fat32_write_file(const char *path, const void *buf, size_t size)
{
    return fat32_vol_write_file(g_primary, path, buf, size);
}

int fat32_rename(const char *old_path, const char *new_path)
{
    return fat32_vol_rename(g_primary, old_path, new_path);
}
