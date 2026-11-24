/*
 * Project Tsukasa - FAT32 read/write filesystem driver (pagecache-backed)
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
#include "../include/errno.h"
#include "../include/kutils.h"
#include "../include/spinlock.h"
#include "../mm/pagecache.h"
#include "../proc/process.h"

#include <stddef.h>
#include <stdint.h>

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
#define ATTR_ARCHIVE    0x20u
#define ATTR_LFN        0x0Fu
#define ATTR_VOLUME_ID  0x08u
#define DIR_ENTRY_SIZE  32u
#define MAX_LFN_SLOTS   20

#define FAT32_OBJ_ID_BASE 0x4641543300000000ULL
#define FAT32_MAX_VOLUMES 8

struct fat32_volume {
    int               ready;
    uint32_t          slot;
    block_dev_t      *bd;
    uint64_t          pc_obj_id;
    volatile uint32_t lock;
    uint64_t          trunc_gen;
    uint32_t          fat_lba;
    uint32_t          data_lba;
    uint32_t          root_cluster;
    uint32_t          sectors_per_cluster;
    uint32_t          fat_size;
    uint32_t          bytes_per_sector;
    uint32_t          fat_count;
    uint32_t          cluster_limit;
    uint32_t          next_free_hint;
    uint16_t          fs_info_lba;
    int               fs_info_valid;
};

static struct fat32_volume g_volumes[FAT32_MAX_VOLUMES];
static volatile uint32_t g_volumes_lock;
static struct fat32_volume *g_primary = NULL;

static void vol_mutex_lock(volatile uint32_t *m)
{
    while (__atomic_exchange_n(m, 1U, __ATOMIC_ACQUIRE) != 0U) {
        if (process_current())
            process_yield();
        else
            __asm__ volatile ("pause");
    }
}

static void vol_mutex_unlock(volatile uint32_t *m)
{
    __atomic_store_n(m, 0U, __ATOMIC_RELEASE);
}

static int fat32_pc_readpage(void *ctx, uint64_t obj_id, uint64_t page_index, void *buf)
{
    struct fat32_volume *v = (struct fat32_volume *)ctx;
    (void)obj_id;
    if (!v || !v->bd || !v->bd->read)
        return -EIO;

    uint32_t ss = v->bd->sector_size ? v->bd->sector_size : 512U;
    uint64_t secs_per_page = PAGECACHE_PAGE_SIZE / ss;
    uint64_t start_lba = page_index * secs_per_page;
    if (secs_per_page == 0 || start_lba >= v->bd->sector_count)
        return -EIO;

    uint64_t avail = v->bd->sector_count - start_lba;
    uint32_t to_read = (avail < secs_per_page) ? (uint32_t)avail : (uint32_t)secs_per_page;
    if (to_read < secs_per_page)
        k_memset(buf, 0, PAGECACHE_PAGE_SIZE);

    if (v->bd->read(v->bd, start_lba, to_read, buf) != 0)
        return -EIO;
    return 0;
}

static int fat32_pc_writepage(void *ctx, uint64_t obj_id, uint64_t page_index, const void *buf)
{
    struct fat32_volume *v = (struct fat32_volume *)ctx;
    (void)obj_id;
    if (!v || !v->bd || !v->bd->write)
        return -EIO;

    uint32_t ss = v->bd->sector_size ? v->bd->sector_size : 512U;
    uint64_t secs_per_page = PAGECACHE_PAGE_SIZE / ss;
    uint64_t start_lba = page_index * secs_per_page;
    if (secs_per_page == 0 || start_lba >= v->bd->sector_count)
        return -EIO;

    uint64_t avail = v->bd->sector_count - start_lba;
    uint32_t to_write = (avail < secs_per_page) ? (uint32_t)avail : (uint32_t)secs_per_page;
    if (v->bd->write(v->bd, start_lba, to_write, buf) != 0)
        return -EIO;
    return 0;
}

static const pagecache_ops_t g_fat32_pc_ops = {
    .readpage = fat32_pc_readpage,
    .writepage = fat32_pc_writepage,
};

static uint32_t dev_sector_size(const struct fat32_volume *v)
{
    if (v->bytes_per_sector)
        return v->bytes_per_sector;
    return (v->bd && v->bd->sector_size) ? v->bd->sector_size : 512U;
}

static int pc_read_bytes(struct fat32_volume *v, uint64_t byte_off, void *dst, size_t len)
{
    uint8_t *out = (uint8_t *)dst;
    size_t done = 0;

    while (done < len) {
        uint64_t cur = byte_off + done;
        uint64_t page_idx = cur >> 12;
        size_t off_in_page = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off_in_page;
        if (chunk > len - done)
            chunk = len - done;

        pagecache_page_t *pg = pagecache_get_page(v->pc_obj_id, page_idx, &g_fat32_pc_ops, v);
        if (!pg)
            return -EIO;
        k_memcpy(out + done, (const uint8_t *)pg->data + off_in_page, chunk);
        pagecache_put_page(pg);
        done += chunk;
    }
    return 0;
}

static int pc_write_bytes(struct fat32_volume *v, uint64_t byte_off, const void *src, size_t len)
{
    const uint8_t *in = (const uint8_t *)src;
    size_t done = 0;

    if (!v->bd || !v->bd->write)
        return -EROFS;

    while (done < len) {
        uint64_t cur = byte_off + done;
        uint64_t page_idx = cur >> 12;
        size_t off_in_page = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off_in_page;
        if (chunk > len - done)
            chunk = len - done;

        pagecache_page_t *pg = pagecache_get_page(v->pc_obj_id, page_idx, &g_fat32_pc_ops, v);
        if (!pg)
            return -EIO;
        k_memcpy((uint8_t *)pg->data + off_in_page, in + done, chunk);
        pagecache_mark_dirty(pg);
        pagecache_put_page(pg);
        done += chunk;
    }
    return 0;
}

static int pc_zero_bytes(struct fat32_volume *v, uint64_t byte_off, size_t len)
{
    size_t done = 0;

    if (!v->bd || !v->bd->write)
        return -EROFS;

    while (done < len) {
        uint64_t cur = byte_off + done;
        uint64_t page_idx = cur >> 12;
        size_t off_in_page = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off_in_page;
        if (chunk > len - done)
            chunk = len - done;

        pagecache_page_t *pg = pagecache_get_page(v->pc_obj_id, page_idx, &g_fat32_pc_ops, v);
        if (!pg)
            return -EIO;
        k_memset((uint8_t *)pg->data + off_in_page, 0, chunk);
        pagecache_mark_dirty(pg);
        pagecache_put_page(pg);
        done += chunk;
    }
    return 0;
}

static int disk_read(struct fat32_volume *v, uint32_t lba, void *buf)
{
    uint32_t ss = dev_sector_size(v);
    return pc_read_bytes(v, (uint64_t)lba * ss, buf, ss);
}

static int disk_write(struct fat32_volume *v, uint32_t lba, const void *buf)
{
    uint32_t ss = dev_sector_size(v);
    return pc_write_bytes(v, (uint64_t)lba * ss, buf, ss);
}

static int k_strncmpi(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 1;
        if (!ca) break;
    }
    return 0;
}

static void fat_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    if (!dst || max <= 0)
        return;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t cluster_to_lba(const struct fat32_volume *v, uint32_t cluster)
{
    return v->data_lba + (cluster - 2u) * v->sectors_per_cluster;
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void fsinfo_adjust(struct fat32_volume *v, int32_t delta, uint32_t next_hint)
{
    uint8_t buf[512];
    if (!v->fs_info_valid || !v->bd || !v->bd->write)
        return;
    if (pc_read_bytes(v, (uint64_t)v->fs_info_lba * dev_sector_size(v), buf, sizeof(buf)) != 0)
        return;
    if (read_le32(buf) != 0x41615252u ||
        read_le32(buf + 484) != 0x61417272u ||
        read_le32(buf + 508) != 0xAA550000u) {
        v->fs_info_valid = 0;
        return;
    }
    uint32_t free_cnt = read_le32(buf + 488);
    if (free_cnt != 0xFFFFFFFFu) {
        int64_t nf = (int64_t)free_cnt + (int64_t)delta;
        if (nf >= 0)
            write_le32(buf + 488, (uint32_t)nf);
    }
    if (next_hint >= 2u && next_hint < v->cluster_limit)
        write_le32(buf + 492, next_hint);
    (void)pc_write_bytes(v, (uint64_t)v->fs_info_lba * dev_sector_size(v), buf, sizeof(buf));
}

/* Read the FAT entry for a cluster (returns next cluster or 0x0FFFFFFF on EOC/error). */
static uint32_t fat_next(struct fat32_volume *v, uint32_t cluster)
{
    uint8_t raw[4];
    if (cluster < 2u || cluster >= v->cluster_limit)
        return 0x0FFFFFFFu;
    uint64_t byte_off = (uint64_t)v->fat_lba * dev_sector_size(v) + (uint64_t)cluster * 4u;
    if (pc_read_bytes(v, byte_off, raw, 4) != 0)
        return 0x0FFFFFFFu;
    return read_le32(raw) & 0x0FFFFFFFu;
}

static int fat_set_entry(struct fat32_volume *v, uint32_t cluster, uint32_t value)
{
    uint32_t ss = dev_sector_size(v);
    uint8_t raw[4];
    if (cluster < 2u || cluster >= v->cluster_limit)
        return -EINVAL;

    for (uint32_t f = 0; f < v->fat_count; f++) {
        uint64_t byte_off = ((uint64_t)v->fat_lba + (uint64_t)f * v->fat_size) * ss +
                            (uint64_t)cluster * 4u;
        if (pc_read_bytes(v, byte_off, raw, 4) != 0)
            return -EIO;
        uint32_t old = read_le32(raw);
        uint32_t nv = (old & 0xF0000000u) | (value & 0x0FFFFFFFu);
        write_le32(raw, nv);
        if (pc_write_bytes(v, byte_off, raw, 4) != 0)
            return -EIO;
    }
    return 0;
}

static uint32_t fat_alloc_cluster(struct fat32_volume *v)
{
    uint32_t start = v->next_free_hint;
    uint32_t c;

    if (start < 3u || start >= v->cluster_limit)
        start = 3u;
    c = start;
    do {
        uint32_t entry = fat_next(v, c);
        if (entry == 0u) {
            if (fat_set_entry(v, c, 0x0FFFFFFFu) != 0)
                return 0;
            v->next_free_hint = (c + 1u < v->cluster_limit) ? (c + 1u) : 3u;
            fsinfo_adjust(v, -1, v->next_free_hint);
            return c;
        }
        c++;
        if (c >= v->cluster_limit)
            c = 3u;
    } while (c != start);
    return 0;
}

static int fat_free_chain(struct fat32_volume *v, uint32_t first_cluster)
{
    uint32_t c = first_cluster;
    uint32_t limit = v->cluster_limit;
    int32_t freed = 0;

    while (c >= 2u && c < 0x0FFFFFF8u && limit > 0u) {
        uint32_t nxt = fat_next(v, c);
        if (fat_set_entry(v, c, 0u) != 0)
            return -EIO;
        if (c < v->next_free_hint)
            v->next_free_hint = c;
        freed++;
        c = nxt;
        limit--;
    }
    if (freed > 0)
        fsinfo_adjust(v, freed, v->next_free_hint);
    return 0;
}

static int zero_cluster(struct fat32_volume *v, uint32_t cluster)
{
    uint64_t cbytes = (uint64_t)v->sectors_per_cluster * dev_sector_size(v);
    uint64_t byte_off = (uint64_t)cluster_to_lba(v, cluster) * dev_sector_size(v);
    return pc_zero_bytes(v, byte_off, (size_t)cbytes);
}

static uint8_t lfn_checksum(const char dos11[11])
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1u) << 7) + (sum >> 1) + (uint8_t)dos11[i]);
    return sum;
}

static int is_valid_83_upper_char(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return 1;
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'':
    case '(': case ')': case '-': case '@': case '^': case '_':
    case '`': case '{': case '}': case '~':
        return 1;
    default:
        return 0;
    }
}

static char to_valid_83_char(char c, int *had_invalid_or_lower)
{
    if (c >= 'a' && c <= 'z') {
        *had_invalid_or_lower = 1;
        return (char)(c - 32);
    }
    if (is_valid_83_upper_char(c))
        return c;
    *had_invalid_or_lower = 1;
    return '\0';
}

/* Strip trailing spaces from an 11-char 8.3 name and convert to NUL-terminated. */
static void parse_83name(const char src[11], char *dst, int dstlen)
{
    int i = 0, j = 0;
    for (; i < 8 && src[i] != ' '; i++) {
        if (j < dstlen - 2)
            dst[j++] = src[i];
    }
    if (src[8] != ' ') {
        if (j < dstlen - 2)
            dst[j++] = '.';
        for (int k = 8; k < 11 && src[k] != ' '; k++) {
            if (j < dstlen - 2)
                dst[j++] = src[k];
        }
    }
    dst[j] = '\0';
}

typedef int (*dir_cb)(const dir_entry_t *de, const char *name, void *ctx);

static void iterate_dir(struct fat32_volume *v, uint32_t start_cluster,
                        dir_cb cb, void *ctx)
{
    char lfn_buf[FAT32_NAME_MAX];
    int lfn_len = 0;
    int has_lfn = 0;
    uint8_t lfn_cksum = 0;
    uint32_t cluster = start_cluster;
    uint8_t sec_buf[512];
    uint32_t ss = dev_sector_size(v);
    if (ss > sizeof(sec_buf))
        return;

    while (cluster >= 2u && cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            if (disk_read(v, lba + s, sec_buf) != 0)
                return;

            dir_entry_t *entries = (dir_entry_t *)sec_buf;
            int per_sector = (int)(ss / DIR_ENTRY_SIZE);

            for (int i = 0; i < per_sector; i++) {
                dir_entry_t *de = &entries[i];
                uint8_t first = (uint8_t)de->name[0];

                if (first == 0x00u)
                    return;
                if (first == 0xE5u) {
                    has_lfn = 0;
                    lfn_len = 0;
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    lfn_entry_t *lfn = (lfn_entry_t *)de;
                    if (lfn->order & 0x40u) {
                        lfn_len = 0;
                        lfn_cksum = lfn->checksum;
                        has_lfn = 1;
                    } else if (!has_lfn || lfn->checksum != lfn_cksum) {
                        has_lfn = 0;
                        lfn_len = 0;
                        continue;
                    }
                    int seq = (int)(lfn->order & 0x1Fu) - 1;
                    if (seq < 0) {
                        has_lfn = 0;
                        lfn_len = 0;
                        continue;
                    }
                    int off = seq * 13;
                    for (int k = 0; k < 5 && off + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name1[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_done;
                        lfn_buf[off + k] = (char)(wc & 0xFFu);
                        if (off + k + 1 > lfn_len) lfn_len = off + k + 1;
                    }
                    for (int k = 0; k < 6 && off + 5 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name2[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_done;
                        lfn_buf[off + 5 + k] = (char)(wc & 0xFFu);
                        if (off + 5 + k + 1 > lfn_len) lfn_len = off + 5 + k + 1;
                    }
                    for (int k = 0; k < 2 && off + 11 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name3[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_done;
                        lfn_buf[off + 11 + k] = (char)(wc & 0xFFu);
                        if (off + 11 + k + 1 > lfn_len) lfn_len = off + 11 + k + 1;
                    }
lfn_done:
                    lfn_buf[lfn_len] = '\0';
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

                char name83[13];
                parse_83name(de->name, name83, sizeof(name83));
                int use_lfn = (has_lfn && lfn_len > 0 && lfn_checksum(de->name) == lfn_cksum);
                const char *display = use_lfn ? lfn_buf : name83;
                int stop = cb(de, display, ctx);
                has_lfn = 0;
                lfn_len = 0;
                if (stop)
                    return;
            }
        }
        cluster = fat_next(v, cluster);
    }
}

typedef struct {
    const char *dos11;
    int         exists;
} dos83_chk_ctx_t;

static int dos83_chk_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    dos83_chk_ctx_t *c = (dos83_chk_ctx_t *)ctx;
    (void)name;
    for (int i = 0; i < 11; i++) {
        if (de->name[i] != c->dos11[i])
            return 0;
    }
    c->exists = 1;
    return 1;
}

static int dos83_exists_in_dir(struct fat32_volume *v, uint32_t dir_cluster,
                               const char dos11[11])
{
    dos83_chk_ctx_t chk = { dos11, 0 };
    iterate_dir(v, dir_cluster, dos83_chk_cb, &chk);
    return chk.exists;
}

static int build_unique_dos_83(struct fat32_volume *v, uint32_t dir_cluster,
                               const char *name, char out11[11], int *needs_lfn_out)
{
    int len = 0;
    int last_dot = -1;
    int dot_count = 0;
    int needs_lfn = 0;

    while (name[len]) {
        if (name[len] == '.') {
            last_dot = len;
            dot_count++;
        }
        len++;
    }
    if (len == 0)
        return -EINVAL;

    /* Check if name fits strictly in 8.3 without LFN. */
    if (dot_count > 1 || last_dot == 0) {
        needs_lfn = 1;
    } else {
        int base_len = (last_dot < 0) ? len : last_dot;
        int ext_len = (last_dot < 0) ? 0 : (len - last_dot - 1);
        if (base_len == 0 || base_len > 8 || ext_len > 3)
            needs_lfn = 1;
        for (int i = 0; i < len && !needs_lfn; i++) {
            if (name[i] == '.')
                continue;
            if (!is_valid_83_upper_char(name[i]))
                needs_lfn = 1;
        }
    }

    if (!needs_lfn) {
        for (int i = 0; i < 11; i++)
            out11[i] = ' ';
        int base_len = (last_dot < 0) ? len : last_dot;
        for (int i = 0; i < base_len; i++)
            out11[i] = name[i];
        if (last_dot >= 0) {
            for (int i = 0; i < 3 && name[last_dot + 1 + i]; i++)
                out11[8 + i] = name[last_dot + 1 + i];
        }
        *needs_lfn_out = 0;
        return 0;
    }

    char base_clean[8];
    int bcnt = 0;
    int base_end = (last_dot > 0) ? last_dot : len;
    int dummy = 0;

    for (int i = 0; i < base_end && bcnt < 6; i++) {
        if (name[i] == '.' || name[i] == ' ')
            continue;
        char c = to_valid_83_char(name[i], &dummy);
        if (c)
            base_clean[bcnt++] = c;
    }
    if (bcnt == 0) {
        base_clean[0] = 'F'; base_clean[1] = 'I';
        base_clean[2] = 'L'; base_clean[3] = 'E';
        bcnt = 4;
    }

    char ext_clean[3];
    int ecnt = 0;
    if (last_dot >= 0) {
        for (int i = last_dot + 1; i < len && ecnt < 3; i++) {
            if (name[i] == ' ')
                continue;
            char c = to_valid_83_char(name[i], &dummy);
            if (c)
                ext_clean[ecnt++] = c;
        }
    }

    for (int num = 1; num <= 999; num++) {
        char digits[4];
        int nd = 0;
        int tmp = num;
        while (tmp > 0 && nd < 4) {
            digits[nd++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
        int keep = 8 - 1 - nd;
        if (keep > bcnt)
            keep = bcnt;

        for (int i = 0; i < 11; i++)
            out11[i] = ' ';
        for (int i = 0; i < keep; i++)
            out11[i] = base_clean[i];
        out11[keep] = '~';
        for (int i = 0; i < nd; i++)
            out11[keep + 1 + i] = digits[nd - 1 - i];
        for (int i = 0; i < ecnt; i++)
            out11[8 + i] = ext_clean[i];

        if (!dos83_exists_in_dir(v, dir_cluster, out11)) {
            *needs_lfn_out = 1;
            return 0;
        }
    }
    return -ENOSPC;
}

static int normalize_path(const char *in, char *out, int out_cap)
{
    int seg_starts[32];
    int seg_count = 0;
    int oi = 1;
    const char *p;

    if (!in || !out || out_cap <= 2 || in[0] != '/')
        return -1;

    out[0] = '/';
    out[1] = '\0';
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
            if (seg_count > 0) {
                oi = seg_starts[--seg_count];
                out[oi] = '\0';
            }
            continue;
        }
        if (seg_count >= 32)
            return -1;
        seg_starts[seg_count++] = oi;
        if (oi > 1) {
            if (oi >= out_cap - 1)
                return -1;
            out[oi++] = '/';
        }
        for (int j = 0; j < si; j++) {
            if (oi >= out_cap - 1)
                return -1;
            out[oi++] = seg[j];
        }
        out[oi] = '\0';
    }
    return 0;
}

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
        if (f->found_cluster == 0u)
            f->found_cluster = f->vol->root_cluster;
        f->found = 1;
        return 1;
    }
    return 0;
}

static uint32_t resolve_parent(struct fat32_volume *v, const char *path,
                               char *leaf_out, int leaf_cap)
{
    const char *p = path;
    uint32_t cur = v->root_cluster;

    if (!leaf_out || leaf_cap <= 0)
        return 0;
    leaf_out[0] = '\0';

    while (*p == '/')
        p++;
    if (*p == '\0')
        return v->root_cluster;

    while (*p) {
        char comp[FAT32_NAME_MAX];
        int i = 0;
        while (*p && *p != '/' && i < FAT32_NAME_MAX - 1)
            comp[i++] = *p++;
        comp[i] = '\0';
        while (*p == '/')
            p++;

        if (*p == '\0') {
            fat_strcpy(leaf_out, comp, leaf_cap);
            return cur;
        }

        find_dir_ctx_t fc;
        fc.target = comp;
        fc.found = 0;
        fc.vol = v;
        iterate_dir(v, cur, find_dir_cb, &fc);
        if (!fc.found)
            return 0;
        cur = fc.found_cluster;
    }
    return 0;
}

typedef struct {
    dir_entry_t out;
    uint32_t lba;
    uint32_t entry_off;
    int      lfn_count;
    uint32_t lfn_lba[MAX_LFN_SLOTS];
    uint32_t lfn_off[MAX_LFN_SLOTS];
    int      found;
} find_entry_raw_ctx_t;

static int find_entry_raw(struct fat32_volume *v, uint32_t start_cluster,
                          const char *target,
                          find_entry_raw_ctx_t *out_ctx)
{
    char lfn_buf[FAT32_NAME_MAX];
    int lfn_len = 0;
    int has_lfn = 0;
    uint8_t lfn_cksum = 0;
    int lfn_count = 0;
    uint32_t lfn_lba[MAX_LFN_SLOTS];
    uint32_t lfn_off[MAX_LFN_SLOTS];
    uint32_t cluster = start_cluster;
    uint8_t sec_buf[512];
    uint32_t ss = dev_sector_size(v);

    if (!target || !out_ctx || ss > sizeof(sec_buf))
        return -EINVAL;
    out_ctx->found = 0;

    while (cluster >= 2u && cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            if (disk_read(v, lba + s, sec_buf) != 0)
                return -EIO;

            dir_entry_t *entries = (dir_entry_t *)sec_buf;
            int per_sector = (int)(ss / DIR_ENTRY_SIZE);
            for (int i = 0; i < per_sector; i++) {
                dir_entry_t *de = &entries[i];
                uint8_t first = (uint8_t)de->name[0];
                if (first == 0x00u)
                    return -ENOENT;
                if (first == 0xE5u) {
                    has_lfn = 0;
                    lfn_len = 0;
                    lfn_count = 0;
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    lfn_entry_t *lfn = (lfn_entry_t *)de;
                    if (lfn->order & 0x40u) {
                        lfn_len = 0;
                        lfn_count = 0;
                        lfn_cksum = lfn->checksum;
                        has_lfn = 1;
                    } else if (!has_lfn || lfn->checksum != lfn_cksum) {
                        has_lfn = 0;
                        lfn_len = 0;
                        lfn_count = 0;
                        continue;
                    }
                    if (lfn_count < MAX_LFN_SLOTS) {
                        lfn_lba[lfn_count] = lba + s;
                        lfn_off[lfn_count] = (uint32_t)i * DIR_ENTRY_SIZE;
                        lfn_count++;
                    }
                    int seq = (int)(lfn->order & 0x1Fu) - 1;
                    if (seq < 0) {
                        has_lfn = 0;
                        lfn_len = 0;
                        lfn_count = 0;
                        continue;
                    }
                    int off = seq * 13;
                    for (int k = 0; k < 5 && off + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name1[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_raw_done;
                        lfn_buf[off + k] = (char)(wc & 0xFFu);
                        if (off + k + 1 > lfn_len) lfn_len = off + k + 1;
                    }
                    for (int k = 0; k < 6 && off + 5 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name2[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_raw_done;
                        lfn_buf[off + 5 + k] = (char)(wc & 0xFFu);
                        if (off + 5 + k + 1 > lfn_len) lfn_len = off + 5 + k + 1;
                    }
                    for (int k = 0; k < 2 && off + 11 + k < FAT32_NAME_MAX - 1; k++) {
                        uint16_t wc = lfn->name3[k];
                        if (wc == 0xFFFFu || wc == 0u) goto lfn_raw_done;
                        lfn_buf[off + 11 + k] = (char)(wc & 0xFFu);
                        if (off + 11 + k + 1 > lfn_len) lfn_len = off + 11 + k + 1;
                    }
lfn_raw_done:
                    lfn_buf[lfn_len] = '\0';
                    continue;
                }
                if ((de->attr & ATTR_VOLUME_ID) || de->name[0] == '.') {
                    has_lfn = 0;
                    lfn_len = 0;
                    lfn_count = 0;
                    continue;
                }

                char name83[13];
                parse_83name(de->name, name83, sizeof(name83));
                int use_lfn = (has_lfn && lfn_len > 0 && lfn_checksum(de->name) == lfn_cksum);
                const char *name = use_lfn ? lfn_buf : name83;
                int cur_lfn_cnt = use_lfn ? lfn_count : 0;

                has_lfn = 0;
                lfn_len = 0;
                lfn_count = 0;

                if (k_strncmpi(name, target, FAT32_NAME_MAX) != 0)
                    continue;

                out_ctx->out = *de;
                out_ctx->lba = lba + s;
                out_ctx->entry_off = (uint32_t)i * DIR_ENTRY_SIZE;
                out_ctx->lfn_count = cur_lfn_cnt;
                for (int k = 0; k < cur_lfn_cnt; k++) {
                    out_ctx->lfn_lba[k] = lfn_lba[k];
                    out_ctx->lfn_off[k] = lfn_off[k];
                }
                out_ctx->found = 1;
                return 0;
            }
        }
        cluster = fat_next(v, cluster);
    }
    return -ENOENT;
}

static int mark_entry_deleted(struct fat32_volume *v, const find_entry_raw_ctx_t *raw)
{
    uint8_t del_byte = 0xE5u;
    uint32_t ss = dev_sector_size(v);

    for (int k = 0; k < raw->lfn_count; k++) {
        uint64_t off = (uint64_t)raw->lfn_lba[k] * ss + raw->lfn_off[k];
        if (pc_write_bytes(v, off, &del_byte, 1) != 0)
            return -EIO;
    }
    uint64_t de_off = (uint64_t)raw->lba * ss + raw->entry_off;
    if (pc_write_bytes(v, de_off, &del_byte, 1) != 0)
        return -EIO;
    return 0;
}

/* Find `nslots` contiguous free slots within a single directory sector. */
static int dir_find_slots(struct fat32_volume *v, uint32_t dir_cluster,
                          int nslots, uint32_t *out_lba, int *out_slot)
{
    uint32_t cluster = dir_cluster, last = dir_cluster;
    uint32_t ss = dev_sector_size(v);
    int per_sector = (int)(ss / DIR_ENTRY_SIZE);
    uint8_t sec_buf[512];
    if (ss > sizeof(sec_buf))
        return -EINVAL;

    while (cluster >= 2u && cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(v, cluster);
        for (uint32_t s = 0; s < v->sectors_per_cluster; s++) {
            int run = 0;
            if (disk_read(v, lba + s, sec_buf) != 0)
                return -EIO;
            for (int i = 0; i < per_sector; i++) {
                uint8_t first = sec_buf[i * DIR_ENTRY_SIZE];
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

    uint32_t nc = fat_alloc_cluster(v);
    if (!nc)
        return -ENOSPC;
    if (zero_cluster(v, nc) != 0)
        return -EIO;
    if (fat_set_entry(v, last, nc) != 0)
        return -EIO;
    *out_lba = cluster_to_lba(v, nc);
    *out_slot = 0;
    return 0;
}

static int create_dir_entry_locked(struct fat32_volume *v, uint32_t dir_cluster,
                                   const char *name, const dir_entry_t *template_de)
{
    char dos11[11];
    int needs_lfn = 0;
    int name_len = 0;
    uint32_t ss = dev_sector_size(v);
    uint8_t sec_buf[512];

    if (ss > sizeof(sec_buf))
        return -EINVAL;

    while (name[name_len])
        name_len++;
    if (name_len == 0 || name_len >= FAT32_NAME_MAX)
        return -ENAMETOOLONG;

    int rc = build_unique_dos_83(v, dir_cluster, name, dos11, &needs_lfn);
    if (rc != 0)
        return rc;

    int lfn_entries = needs_lfn ? ((name_len + 12) / 13) : 0;
    int nslots = lfn_entries + 1;
    if (nslots > (int)(ss / DIR_ENTRY_SIZE))
        return -ENAMETOOLONG;

    uint32_t lba = 0;
    int slot = 0;
    rc = dir_find_slots(v, dir_cluster, nslots, &lba, &slot);
    if (rc != 0)
        return rc;

    if (disk_read(v, lba, sec_buf) != 0)
        return -EIO;

    if (needs_lfn) {
        uint8_t sum = lfn_checksum(dos11);
        for (int i = 0; i < lfn_entries; i++) {
            lfn_entry_t *lfn = (lfn_entry_t *)(sec_buf + (slot + i) * DIR_ENTRY_SIZE);
            int chunk = lfn_entries - i - 1;
            k_memset(lfn, 0, DIR_ENTRY_SIZE);
            lfn->order = (uint8_t)(lfn_entries - i);
            if (i == 0)
                lfn->order |= 0x40u;
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
                    wc = 0x0000u;
                else
                    wc = 0xFFFFu;
                if (k < 5)
                    lfn->name1[k] = wc;
                else if (k < 11)
                    lfn->name2[k - 5] = wc;
                else
                    lfn->name3[k - 11] = wc;
            }
        }
    }

    dir_entry_t *de = (dir_entry_t *)(sec_buf + (slot + lfn_entries) * DIR_ENTRY_SIZE);
    *de = *template_de;
    for (int i = 0; i < 11; i++)
        de->name[i] = dos11[i];

    return disk_write(v, lba, sec_buf);
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
        fat_strcpy(s->out.name, name, FAT32_NAME_MAX);
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
    if (l->count >= l->max)
        return 1;
    fat_strcpy(l->entries[l->count].name, name, FAT32_NAME_MAX);
    l->entries[l->count].size = de->file_size;
    l->entries[l->count].is_dir = (de->attr & ATTR_DIR) ? 1 : 0;
    l->entries[l->count].first_cluster =
        ((uint32_t)de->first_clus_hi << 16) | de->first_clus_lo;
    l->count++;
    return 0;
}

typedef struct {
    int non_empty;
} empty_chk_ctx_t;

static int empty_chk_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    (void)de;
    (void)name;
    empty_chk_ctx_t *e = (empty_chk_ctx_t *)ctx;
    e->non_empty = 1;
    return 1;
}

static int stat_locked(struct fat32_volume *v, const char *norm, fat32_dirent_t *out)
{
    if (norm[0] == '/' && norm[1] == '\0') {
        fat_strcpy(out->name, "/", FAT32_NAME_MAX);
        out->size = 0;
        out->is_dir = 1;
        out->first_cluster = v->root_cluster;
        return 0;
    }

    char leaf[FAT32_NAME_MAX];
    uint32_t parent = resolve_parent(v, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0])
        return -ENOENT;

    stat_ctx_t sc;
    sc.name = leaf;
    sc.found = 0;
    iterate_dir(v, parent, stat_cb, &sc);
    if (!sc.found)
        return -ENOENT;
    *out = sc.out;
    return 0;
}

/* Per-volume public API */

fat32_volume_t *fat32_vol_mount(struct block_dev *bd)
{
    struct fat32_volume *v = NULL;
    uint8_t sec_buf[512];

    if (!bd || !bd->read || bd->sector_size == 0 || bd->sector_size > sizeof(sec_buf))
        return NULL;

    vol_mutex_lock(&g_volumes_lock);
    for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
        if (g_volumes[i].ready && g_volumes[i].bd == bd) {
            v = &g_volumes[i];
            vol_mutex_lock(&v->lock);
            (void)pagecache_sync_obj(v->pc_obj_id);
            pagecache_invalidate_obj(v->pc_obj_id);
            v->ready = 0;
            vol_mutex_unlock(&v->lock);
            break;
        }
    }
    if (!v) {
        for (int i = 0; i < FAT32_MAX_VOLUMES; i++) {
            if (!g_volumes[i].ready) {
                v = &g_volumes[i];
                v->slot = (uint32_t)i;
                v->pc_obj_id = FAT32_OBJ_ID_BASE | (uint64_t)i;
                break;
            }
        }
    }
    if (!v) {
        vol_mutex_unlock(&g_volumes_lock);
        return NULL;
    }

    vol_mutex_lock(&v->lock);
    v->bd = bd;
    v->bytes_per_sector = bd->sector_size;
    pagecache_invalidate_obj(v->pc_obj_id);

    if (disk_read(v, 0, sec_buf) != 0) {
        vol_mutex_unlock(&v->lock);
        vol_mutex_unlock(&g_volumes_lock);
        return NULL;
    }

    bpb_t *bpb = (bpb_t *)sec_buf;
    if (sec_buf[510] != 0x55u || sec_buf[511] != 0xAAu ||
        bpb->bytes_per_sector == 0 || bpb->bytes_per_sector > sizeof(sec_buf) ||
        bpb->sectors_per_cluster == 0 ||
        bpb->root_entry_count != 0 || bpb->fat_size_16 != 0 ||
        bpb->fat_size_32 == 0) {
        pagecache_invalidate_obj(v->pc_obj_id);
        vol_mutex_unlock(&v->lock);
        vol_mutex_unlock(&g_volumes_lock);
        return NULL;
    }

    v->bytes_per_sector    = bpb->bytes_per_sector;
    v->sectors_per_cluster = bpb->sectors_per_cluster;
    v->fat_size            = bpb->fat_size_32;
    v->root_cluster        = bpb->root_cluster;
    v->fat_lba             = bpb->reserved_sectors;
    v->fat_count           = bpb->fat_count;
    v->data_lba            = v->fat_lba + bpb->fat_count * v->fat_size;
    v->fs_info_lba         = bpb->fs_info;
    v->fs_info_valid       = (bpb->fs_info > 0 && bpb->fs_info < bpb->reserved_sectors) ? 1 : 0;

    uint64_t dev_sectors = v->bd->sector_count;
    uint32_t by_data = 2u;
    if (dev_sectors > v->data_lba)
        by_data = 2u + (uint32_t)((dev_sectors - v->data_lba) / v->sectors_per_cluster);
    uint32_t by_fat = (v->fat_size * v->bytes_per_sector) / 4u;
    v->cluster_limit = (by_data < by_fat) ? by_data : by_fat;
    v->next_free_hint = 3u;
    v->trunc_gen = 1ULL;
    v->ready = 1;

    vol_mutex_unlock(&v->lock);
    vol_mutex_unlock(&g_volumes_lock);
    return v;
}

int fat32_vol_sync(fat32_volume_t *vol)
{
    if (!vol || !vol->ready)
        return -EINVAL;
    vol_mutex_lock(&vol->lock);
    int rc = pagecache_sync_obj(vol->pc_obj_id);
    int bd_rc = (vol->bd && vol->bd->sync) ? vol->bd->sync(vol->bd) : 0;
    vol_mutex_unlock(&vol->lock);
    if (rc != 0)
        return rc;
    return (bd_rc != 0) ? -EIO : 0;
}

int fat32_vol_umount(fat32_volume_t *vol)
{
    if (!vol || !vol->ready)
        return -EINVAL;
    vol_mutex_lock(&g_volumes_lock);
    vol_mutex_lock(&vol->lock);
    int rc = pagecache_sync_obj(vol->pc_obj_id);
    int bd_rc = (vol->bd && vol->bd->sync) ? vol->bd->sync(vol->bd) : 0;
    pagecache_invalidate_obj(vol->pc_obj_id);
    vol->ready = 0;
    if (g_primary == vol)
        g_primary = NULL;
    vol_mutex_unlock(&vol->lock);
    vol_mutex_unlock(&g_volumes_lock);
    if (rc != 0)
        return rc;
    return (bd_rc != 0) ? -EIO : 0;
}

int fat32_vol_create_file(fat32_volume_t *vol, const char *path)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (norm[0] != '/' || norm[1] == '\0')
        return -EISDIR;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t existing;
    if (find_entry_raw(vol, parent, leaf, &existing) == 0 && existing.found) {
        vol_mutex_unlock(&vol->lock);
        return -EEXIST;
    }

    dir_entry_t de;
    k_memset(&de, 0, sizeof(de));
    de.attr = ATTR_ARCHIVE;
    int rc = create_dir_entry_locked(vol, parent, leaf, &de);
    vol_mutex_unlock(&vol->lock);
    return rc;
}

int fat32_vol_mkdir(fat32_volume_t *vol, const char *path)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EEXIST;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t existing;
    if (find_entry_raw(vol, parent, leaf, &existing) == 0 && existing.found) {
        vol_mutex_unlock(&vol->lock);
        return -EEXIST;
    }

    uint32_t nc = fat_alloc_cluster(vol);
    if (!nc) {
        vol_mutex_unlock(&vol->lock);
        return -ENOSPC;
    }
    if (zero_cluster(vol, nc) != 0) {
        (void)fat_free_chain(vol, nc);
        vol_mutex_unlock(&vol->lock);
        return -EIO;
    }

    dir_entry_t dots[2];
    k_memset(dots, 0, sizeof(dots));
    for (int i = 0; i < 11; i++) {
        dots[0].name[i] = ' ';
        dots[1].name[i] = ' ';
    }
    dots[0].name[0] = '.';
    dots[0].attr = ATTR_DIR;
    dots[0].first_clus_hi = (uint16_t)(nc >> 16);
    dots[0].first_clus_lo = (uint16_t)(nc & 0xFFFFu);

    uint32_t dotdot_clus = (parent == vol->root_cluster) ? 0u : parent;
    dots[1].name[0] = '.';
    dots[1].name[1] = '.';
    dots[1].attr = ATTR_DIR;
    dots[1].first_clus_hi = (uint16_t)(dotdot_clus >> 16);
    dots[1].first_clus_lo = (uint16_t)(dotdot_clus & 0xFFFFu);

    uint64_t nc_off = (uint64_t)cluster_to_lba(vol, nc) * dev_sector_size(vol);
    if (pc_write_bytes(vol, nc_off, dots, sizeof(dots)) != 0) {
        (void)fat_free_chain(vol, nc);
        vol_mutex_unlock(&vol->lock);
        return -EIO;
    }

    dir_entry_t de;
    k_memset(&de, 0, sizeof(de));
    de.attr = ATTR_DIR;
    de.first_clus_hi = (uint16_t)(nc >> 16);
    de.first_clus_lo = (uint16_t)(nc & 0xFFFFu);
    int rc = create_dir_entry_locked(vol, parent, leaf, &de);
    if (rc != 0)
        (void)fat_free_chain(vol, nc);

    vol_mutex_unlock(&vol->lock);
    return rc;
}

int fat32_vol_unlink(fat32_volume_t *vol, const char *path)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EISDIR;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t raw;
    if (find_entry_raw(vol, parent, leaf, &raw) != 0 || !raw.found) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }
    if (raw.out.attr & ATTR_DIR) {
        vol_mutex_unlock(&vol->lock);
        return -EISDIR;
    }

    uint32_t first_clus = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;
    if (first_clus >= 2u) {
        int frc = fat_free_chain(vol, first_clus);
        if (frc != 0) {
            vol_mutex_unlock(&vol->lock);
            return frc;
        }
    }

    int rc = mark_entry_deleted(vol, &raw);
    vol->trunc_gen++;
    vol_mutex_unlock(&vol->lock);
    return rc;
}

int fat32_vol_rmdir(fat32_volume_t *vol, const char *path)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EBUSY;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t raw;
    if (find_entry_raw(vol, parent, leaf, &raw) != 0 || !raw.found) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }
    if (!(raw.out.attr & ATTR_DIR)) {
        vol_mutex_unlock(&vol->lock);
        return -ENOTDIR;
    }

    uint32_t dir_clus = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;
    if (dir_clus >= 2u) {
        empty_chk_ctx_t ec = { 0 };
        iterate_dir(vol, dir_clus, empty_chk_cb, &ec);
        if (ec.non_empty) {
            vol_mutex_unlock(&vol->lock);
            return -ENOTEMPTY;
        }
        int frc = fat_free_chain(vol, dir_clus);
        if (frc != 0) {
            vol_mutex_unlock(&vol->lock);
            return frc;
        }
    }

    int rc = mark_entry_deleted(vol, &raw);
    vol_mutex_unlock(&vol->lock);
    return rc;
}

static int truncate_raw_locked(struct fat32_volume *v, find_entry_raw_ctx_t *raw,
                               uint32_t new_size)
{
    uint32_t ss = dev_sector_size(v);
    uint64_t cbytes = (uint64_t)v->sectors_per_cluster * ss;
    uint32_t old_size = raw->out.file_size;
    uint32_t first_clus = ((uint32_t)raw->out.first_clus_hi << 16) | raw->out.first_clus_lo;
    uint32_t need_clusters = (new_size == 0u)
                                 ? 0u
                                 : (uint32_t)(((uint64_t)new_size + cbytes - 1u) / cbytes);

    if (need_clusters == 0u) {
        if (first_clus >= 2u) {
            int frc = fat_free_chain(v, first_clus);
            if (frc != 0)
                return frc;
        }
        first_clus = 0u;
    } else {
        uint32_t count = 0u;
        uint32_t c = first_clus;
        uint32_t prev = 0u;
        uint32_t last_kept = 0u;

        while (c >= 2u && c < 0x0FFFFFF8u && count < need_clusters) {
            count++;
            last_kept = c;
            prev = c;
            c = fat_next(v, c);
        }

        if (count == need_clusters) {
            if (c >= 2u && c < 0x0FFFFFF8u) {
                if (fat_set_entry(v, last_kept, 0x0FFFFFFFu) != 0)
                    return -EIO;
                if (fat_free_chain(v, c) != 0)
                    return -EIO;
            }
        } else {
            /* Zero-fill unused tail in last existing cluster before appending new ones. */
            if (prev >= 2u && (old_size % cbytes) != 0u) {
                size_t rem_in_clus = (size_t)(cbytes - (old_size % cbytes));
                size_t to_zero = (new_size - old_size < rem_in_clus)
                                     ? (size_t)(new_size - old_size)
                                     : rem_in_clus;
                uint64_t zoff = (uint64_t)cluster_to_lba(v, prev) * ss + (old_size % cbytes);
                if (pc_zero_bytes(v, zoff, to_zero) != 0)
                    return -EIO;
            }
            while (count < need_clusters) {
                uint32_t nc = fat_alloc_cluster(v);
                if (!nc)
                    return -ENOSPC;
                if (zero_cluster(v, nc) != 0)
                    return -EIO;
                if (prev >= 2u) {
                    if (fat_set_entry(v, prev, nc) != 0)
                        return -EIO;
                } else {
                    first_clus = nc;
                }
                prev = nc;
                count++;
            }
        }
    }

    dir_entry_t updated = raw->out;
    updated.file_size = new_size;
    updated.first_clus_hi = (uint16_t)(first_clus >> 16);
    updated.first_clus_lo = (uint16_t)(first_clus & 0xFFFFu);
    uint64_t de_off = (uint64_t)raw->lba * ss + raw->entry_off;
    if (pc_write_bytes(v, de_off, &updated, sizeof(updated)) != 0)
        return -EIO;
    raw->out = updated;
    v->trunc_gen++;
    return 0;
}

int fat32_vol_truncate(fat32_volume_t *vol, const char *path, size_t new_size)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path)
        return -EINVAL;
    if (new_size > 0xFFFFFFFFULL)
        return -EFBIG;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EISDIR;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t raw;
    if (find_entry_raw(vol, parent, leaf, &raw) != 0 || !raw.found) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }
    if (raw.out.attr & ATTR_DIR) {
        vol_mutex_unlock(&vol->lock);
        return -EISDIR;
    }

    int rc = truncate_raw_locked(vol, &raw, (uint32_t)new_size);
    vol_mutex_unlock(&vol->lock);
    return rc;
}

int fat32_vol_stat(fat32_volume_t *vol, const char *path, fat32_dirent_t *out)
{
    char norm[FAT32_NAME_MAX];
    if (!vol || !vol->ready || !path || !out)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;

    vol_mutex_lock(&vol->lock);
    int rc = stat_locked(vol, norm, out);
    vol_mutex_unlock(&vol->lock);
    return rc;
}

int fat32_vol_list_dir(fat32_volume_t *vol, const char *path,
                       fat32_dirent_t *entries, int max)
{
    char norm[FAT32_NAME_MAX];
    if (!vol || !vol->ready || !entries || max <= 0 || !path)
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;

    vol_mutex_lock(&vol->lock);
    uint32_t dir_cluster;
    if (norm[0] == '/' && norm[1] == '\0') {
        dir_cluster = vol->root_cluster;
    } else {
        fat32_dirent_t de;
        if (stat_locked(vol, norm, &de) != 0 || !de.is_dir) {
            vol_mutex_unlock(&vol->lock);
            return -ENOENT;
        }
        dir_cluster = de.first_cluster ? de.first_cluster : vol->root_cluster;
    }

    list_ctx_t lc = { entries, max, 0 };
    iterate_dir(vol, dir_cluster, list_cb, &lc);
    vol_mutex_unlock(&vol->lock);
    return lc.count;
}

static uint32_t seek_cluster_in_chain(struct fat32_volume *v, uint32_t first_clus,
                                      uint32_t target_idx, uint32_t *hint_idx,
                                      uint32_t *hint_clus, uint64_t *hint_gen)
{
    uint32_t cur_idx = 0;
    uint32_t cur_clus = first_clus;

    if (hint_idx && hint_clus && hint_gen && *hint_gen == v->trunc_gen &&
        *hint_clus >= 2u && *hint_idx <= target_idx) {
        cur_idx = *hint_idx;
        cur_clus = *hint_clus;
    }

    while (cur_idx < target_idx && cur_clus >= 2u && cur_clus < 0x0FFFFFF8u) {
        cur_clus = fat_next(v, cur_clus);
        cur_idx++;
    }

    if (hint_idx && hint_clus && hint_gen && cur_clus >= 2u && cur_clus < 0x0FFFFFF8u) {
        *hint_idx = cur_idx;
        *hint_clus = cur_clus;
        *hint_gen = v->trunc_gen;
    }
    return cur_clus;
}

int fat32_vol_read_at(fat32_volume_t *vol, const char *path, size_t offset,
                      void *buf, size_t count, size_t *out_read,
                      uint32_t *hint_idx, uint32_t *hint_clus, uint64_t *hint_gen)
{
    char norm[FAT32_NAME_MAX];
    if (out_read)
        *out_read = 0;
    if (!vol || !vol->ready || !path || (!buf && count > 0))
        return -EINVAL;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (count == 0)
        return 0;

    vol_mutex_lock(&vol->lock);
    fat32_dirent_t de;
    int src = stat_locked(vol, norm, &de);
    if (src != 0) {
        vol_mutex_unlock(&vol->lock);
        return src;
    }
    if (de.is_dir) {
        vol_mutex_unlock(&vol->lock);
        return -EISDIR;
    }
    if (offset >= de.size || de.first_cluster < 2u) {
        vol_mutex_unlock(&vol->lock);
        return 0;
    }

    size_t to_read = de.size - offset;
    if (to_read > count)
        to_read = count;

    uint32_t ss = dev_sector_size(vol);
    size_t cbytes = (size_t)vol->sectors_per_cluster * ss;
    uint32_t clus_idx = (uint32_t)(offset / cbytes);
    size_t off_in_clus = offset % cbytes;
    uint32_t cluster = seek_cluster_in_chain(vol, de.first_cluster, clus_idx,
                                             hint_idx, hint_clus, hint_gen);

    size_t done = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (done < to_read && cluster >= 2u && cluster < 0x0FFFFFF8u) {
        size_t chunk = cbytes - off_in_clus;
        if (chunk > to_read - done)
            chunk = to_read - done;
        uint64_t byte_off = (uint64_t)cluster_to_lba(vol, cluster) * ss + off_in_clus;
        if (pc_read_bytes(vol, byte_off, dst + done, chunk) != 0) {
            vol_mutex_unlock(&vol->lock);
            if (out_read)
                *out_read = done;
            return (done > 0) ? 0 : -EIO;
        }
        done += chunk;
        off_in_clus = 0;
        if (done < to_read) {
            cluster = fat_next(vol, cluster);
            clus_idx++;
            if (hint_idx && hint_clus && hint_gen && cluster >= 2u && cluster < 0x0FFFFFF8u) {
                *hint_idx = clus_idx;
                *hint_clus = cluster;
                *hint_gen = vol->trunc_gen;
            }
        }
    }

    if (out_read)
        *out_read = done;
    vol_mutex_unlock(&vol->lock);
    return 0;
}

int fat32_vol_write_at(fat32_volume_t *vol, const char *path, size_t offset,
                       const void *buf, size_t count, size_t *out_written,
                       uint32_t *hint_idx, uint32_t *hint_clus, uint64_t *hint_gen)
{
    char norm[FAT32_NAME_MAX];
    char leaf[FAT32_NAME_MAX];
    if (out_written)
        *out_written = 0;
    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !path || (!buf && count > 0))
        return -EINVAL;
    if (offset + count < offset || offset + count > 0xFFFFFFFFULL)
        return -EFBIG;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if (count == 0)
        return 0;

    vol_mutex_lock(&vol->lock);
    uint32_t parent = resolve_parent(vol, norm, leaf, sizeof(leaf));
    if (!parent || !leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    find_entry_raw_ctx_t raw;
    if (find_entry_raw(vol, parent, leaf, &raw) != 0 || !raw.found) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }
    if (raw.out.attr & ATTR_DIR) {
        vol_mutex_unlock(&vol->lock);
        return -EISDIR;
    }

    uint32_t ss = dev_sector_size(vol);
    size_t cbytes = (size_t)vol->sectors_per_cluster * ss;
    uint32_t end_size = (uint32_t)(offset + count);
    if (end_size > raw.out.file_size) {
        uint32_t old_clus = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;
        uint32_t need_clus = (uint32_t)(((uint64_t)end_size + cbytes - 1u) / cbytes);
        uint32_t have_clus = 0;
        uint32_t c = old_clus;
        uint32_t last = 0;
        while (c >= 2u && c < 0x0FFFFFF8u) {
            have_clus++;
            last = c;
            c = fat_next(vol, c);
        }
        if (need_clus > have_clus) {
            if (last >= 2u && offset > raw.out.file_size && (raw.out.file_size % cbytes) != 0u) {
                size_t rem_in_clus = cbytes - (raw.out.file_size % cbytes);
                size_t gap = offset - raw.out.file_size;
                size_t to_zero = (gap < rem_in_clus) ? gap : rem_in_clus;
                uint64_t zoff = (uint64_t)cluster_to_lba(vol, last) * ss +
                                (raw.out.file_size % cbytes);
                if (pc_zero_bytes(vol, zoff, to_zero) != 0) {
                    vol_mutex_unlock(&vol->lock);
                    return -EIO;
                }
            }
            while (have_clus < need_clus) {
                uint32_t nc = fat_alloc_cluster(vol);
                if (!nc) {
                    vol_mutex_unlock(&vol->lock);
                    return -ENOSPC;
                }
                if (zero_cluster(vol, nc) != 0) {
                    vol_mutex_unlock(&vol->lock);
                    return -EIO;
                }
                if (last >= 2u) {
                    if (fat_set_entry(vol, last, nc) != 0) {
                        vol_mutex_unlock(&vol->lock);
                        return -EIO;
                    }
                } else {
                    old_clus = nc;
                }
                last = nc;
                have_clus++;
            }
            raw.out.first_clus_hi = (uint16_t)(old_clus >> 16);
            raw.out.first_clus_lo = (uint16_t)(old_clus & 0xFFFFu);
        }
        raw.out.file_size = end_size;
    }

    uint32_t first_clus = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;
    uint32_t clus_idx = (uint32_t)(offset / cbytes);
    size_t off_in_clus = offset % cbytes;
    uint32_t cluster = seek_cluster_in_chain(vol, first_clus, clus_idx,
                                             hint_idx, hint_clus, hint_gen);

    size_t done = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (done < count && cluster >= 2u && cluster < 0x0FFFFFF8u) {
        size_t chunk = cbytes - off_in_clus;
        if (chunk > count - done)
            chunk = count - done;
        uint64_t byte_off = (uint64_t)cluster_to_lba(vol, cluster) * ss + off_in_clus;
        if (pc_write_bytes(vol, byte_off, src + done, chunk) != 0) {
            vol_mutex_unlock(&vol->lock);
            if (out_written)
                *out_written = done;
            return -EIO;
        }
        done += chunk;
        off_in_clus = 0;
        if (done < count) {
            cluster = fat_next(vol, cluster);
            clus_idx++;
            if (hint_idx && hint_clus && hint_gen && cluster >= 2u && cluster < 0x0FFFFFF8u) {
                *hint_idx = clus_idx;
                *hint_clus = cluster;
                *hint_gen = vol->trunc_gen;
            }
        }
    }

    uint64_t de_off = (uint64_t)raw.lba * ss + raw.entry_off;
    if (pc_write_bytes(vol, de_off, &raw.out, sizeof(raw.out)) != 0) {
        vol_mutex_unlock(&vol->lock);
        return -EIO;
    }

    if (out_written)
        *out_written = done;
    vol_mutex_unlock(&vol->lock);
    return (done == count) ? 0 : -EIO;
}

int fat32_vol_read_file(fat32_volume_t *vol, const char *path,
                        void *buf, size_t max_bytes)
{
    size_t got = 0;
    int rc = fat32_vol_read_at(vol, path, 0, buf, max_bytes, &got, NULL, NULL, NULL);
    return (rc < 0) ? -1 : (int)got;
}

int fat32_vol_write_file(fat32_volume_t *vol, const char *path,
                         const void *buf, size_t size)
{
    int rc = fat32_vol_truncate(vol, path, size);
    if (rc != 0)
        return -1;
    if (size > 0) {
        size_t wr = 0;
        rc = fat32_vol_write_at(vol, path, 0, buf, size, &wr, NULL, NULL, NULL);
        if (rc != 0 || wr != size)
            return -1;
    }
    return 0;
}

int fat32_vol_rename(fat32_volume_t *vol, const char *old_path,
                     const char *new_path)
{
    char old_norm[FAT32_NAME_MAX];
    char new_norm[FAT32_NAME_MAX];
    char old_leaf[FAT32_NAME_MAX];
    char new_leaf[FAT32_NAME_MAX];
    uint32_t old_parent = 0;
    uint32_t new_parent = 0;
    find_entry_raw_ctx_t src;
    find_entry_raw_ctx_t dst;

    if (!vol || !vol->ready || !vol->bd || !vol->bd->write || !old_path || !new_path)
        return -EINVAL;
    if (normalize_path(old_path, old_norm, FAT32_NAME_MAX) != 0 ||
        normalize_path(new_path, new_norm, FAT32_NAME_MAX) != 0)
        return -EINVAL;
    if ((old_norm[0] == '/' && old_norm[1] == '\0') ||
        (new_norm[0] == '/' && new_norm[1] == '\0'))
        return -EBUSY;

    vol_mutex_lock(&vol->lock);
    old_parent = resolve_parent(vol, old_norm, old_leaf, sizeof(old_leaf));
    new_parent = resolve_parent(vol, new_norm, new_leaf, sizeof(new_leaf));
    if (!old_parent || !old_leaf[0] || !new_parent || !new_leaf[0]) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    if (find_entry_raw(vol, old_parent, old_leaf, &src) != 0 || !src.found) {
        vol_mutex_unlock(&vol->lock);
        return -ENOENT;
    }

    /* Exact same name (including case): no-op. */
    if (old_parent == new_parent && k_strcmp(old_leaf, new_leaf) == 0) {
        vol_mutex_unlock(&vol->lock);
        return 0;
    }

    int src_is_dir = (src.out.attr & ATTR_DIR) ? 1 : 0;
    if (find_entry_raw(vol, new_parent, new_leaf, &dst) == 0 && dst.found) {
        /* If dst is NOT the exact same directory slot (e.g. case-only rename), handle replacement. */
        if (dst.lba != src.lba || dst.entry_off != src.entry_off) {
            int dst_is_dir = (dst.out.attr & ATTR_DIR) ? 1 : 0;
            if (src_is_dir && !dst_is_dir) {
                vol_mutex_unlock(&vol->lock);
                return -ENOTDIR;
            }
            if (!src_is_dir && dst_is_dir) {
                vol_mutex_unlock(&vol->lock);
                return -EISDIR;
            }
            uint32_t dclus = ((uint32_t)dst.out.first_clus_hi << 16) | dst.out.first_clus_lo;
            if (dst_is_dir && dclus >= 2u) {
                empty_chk_ctx_t ec = { 0 };
                iterate_dir(vol, dclus, empty_chk_cb, &ec);
                if (ec.non_empty) {
                    vol_mutex_unlock(&vol->lock);
                    return -ENOTEMPTY;
                }
            }
            if (dclus >= 2u && fat_free_chain(vol, dclus) != 0) {
                vol_mutex_unlock(&vol->lock);
                return -EIO;
            }
            if (mark_entry_deleted(vol, &dst) != 0) {
                vol_mutex_unlock(&vol->lock);
                return -EIO;
            }
        }
    }

    dir_entry_t saved = src.out;

    if (src_is_dir && old_parent != new_parent) {
        uint32_t src_clus = ((uint32_t)saved.first_clus_hi << 16) | saved.first_clus_lo;
        if (src_clus >= 2u) {
            uint32_t ss = dev_sector_size(vol);
            uint32_t dotdot_clus = (new_parent == vol->root_cluster) ? 0u : new_parent;
            dir_entry_t dotdot;
            uint64_t dd_off = (uint64_t)cluster_to_lba(vol, src_clus) * ss + DIR_ENTRY_SIZE;
            if (pc_read_bytes(vol, dd_off, &dotdot, sizeof(dotdot)) == 0 &&
                dotdot.name[0] == '.' && dotdot.name[1] == '.') {
                dotdot.first_clus_hi = (uint16_t)(dotdot_clus >> 16);
                dotdot.first_clus_lo = (uint16_t)(dotdot_clus & 0xFFFFu);
                (void)pc_write_bytes(vol, dd_off, &dotdot, sizeof(dotdot));
            }
        }
    }

    if (mark_entry_deleted(vol, &src) != 0) {
        vol_mutex_unlock(&vol->lock);
        return -EIO;
    }

    int rc = create_dir_entry_locked(vol, new_parent, new_leaf, &saved);
    vol->trunc_gen++;
    vol_mutex_unlock(&vol->lock);
    return rc;
}

/* Legacy single-volume API (the /disk mount) */

int fat32_init(void)
{
    block_dev_t *bd = blockdev_first_raw_fat32();
    g_primary = bd ? fat32_vol_mount(bd) : NULL;
    return g_primary ? 0 : -1;
}

fat32_volume_t *fat32_primary_vol(void)
{
    return g_primary;
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
