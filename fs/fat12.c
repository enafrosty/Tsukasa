/*
 * Project Tsukasa — FAT12 filesystem driver
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

#include "fat12.h"
#include <stdint.h>
#include <stddef.h>

/* Little-endian accessors */

static inline uint16_t u16le(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t u32le(const uint8_t *p)
{ return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static inline void w16le(uint8_t *p, uint16_t v)
{ p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)(v>>8); }
static inline void w32le(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static uint8_t  *g_disk   = NULL;
static size_t    g_size   = 0;

/* Cached BPB values. */
static uint16_t g_bytes_per_sec;
static uint8_t  g_secs_per_clus;
static uint16_t g_reserved_secs;
static uint8_t  g_num_fats;
static uint16_t g_root_entry_cnt;
static uint16_t g_fat_sz16;
static uint32_t g_fat_offset;        /* byte offset of FAT1 */
static uint32_t g_root_dir_offset;   /* byte offset of root dir */
static uint32_t g_data_offset;       /* byte offset of cluster 2 */
static uint32_t g_total_clusters;

#define DIR_ENTRY_SIZE  32

/* Get the next cluster number in the chain. 0xFFF = end-of-chain. */
static uint16_t fat12_get_next_cluster(uint16_t clus)
{
    if (!g_disk || clus < 2) return 0xFFF;
    uint32_t fat_byte = g_fat_offset + (uint32_t)clus * 3u / 2u;
    if (fat_byte + 1 >= g_size) return 0xFFF;
    uint16_t val = (uint16_t)(g_disk[fat_byte] | ((uint16_t)g_disk[fat_byte+1] << 8));
    if (clus & 1) val >>= 4;
    else          val &= 0x0FFF;
    return val;
}

static void fat12_set_cluster(uint16_t clus, uint16_t val)
{
    if (!g_disk || clus < 2) return;
    for (uint8_t fi = 0; fi < g_num_fats; fi++) {
        uint32_t foff  = g_fat_offset + fi * (uint32_t)g_fat_sz16 * g_bytes_per_sec;
        uint32_t byte  = foff + (uint32_t)clus * 3u / 2u;
        if (byte + 1 >= g_size) continue;
        if (clus & 1) {
            g_disk[byte]   = (uint8_t)((g_disk[byte]   & 0x0F) | ((val & 0x0F) << 4));
            g_disk[byte+1] = (uint8_t)(val >> 4);
        } else {
            g_disk[byte]   = (uint8_t)(val & 0xFF);
            g_disk[byte+1] = (uint8_t)((g_disk[byte+1] & 0xF0) | ((val >> 8) & 0x0F));
        }
    }
}

static uint16_t fat12_alloc_cluster(void)
{
    for (uint16_t c = 2; c < g_total_clusters + 2; c++) {
        if (fat12_get_next_cluster(c) == 0x000) {
            fat12_set_cluster(c, 0xFFF);
            return c;
        }
    }
    return 0;
}

/* Byte address of the data area for cluster clus. */
static uint32_t cluster_to_offset(uint16_t clus)
{
    if (clus < 2) return g_data_offset;
    return g_data_offset + (uint32_t)(clus - 2) *
           (uint32_t)g_secs_per_clus * (uint32_t)g_bytes_per_sec;
}

/* Convert FAT 8.3 raw entry (11 bytes, space-padded) to "NAME.EXT\0". */
static void fat83_to_str(const uint8_t *raw, char *out)
{
    int i, j = 0;
    for (i = 0; i < 8 && raw[i] != ' '; i++) out[j++] = (char)raw[i];
    if (raw[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && raw[i] != ' '; i++) out[j++] = (char)raw[i];
    }
    out[j] = '\0';
}

/* Convert "NAME.EXT" string to FAT 8.3 uppercase space-padded 11 bytes. */
static void str_to_fat83(const char *name, uint8_t *out)
{
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = name[i++];
        out[j++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            char c = name[i++];
            out[j++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
    }
}

/* Compare a path component against a FAT83 raw name. */
static int fat83_match(const uint8_t *raw, const char *name)
{
    uint8_t ref[11];
    str_to_fat83(name, ref);
    for (int i = 0; i < 11; i++)
        if (raw[i] != ref[i]) return 0;
    return 1;
}

int fat12_init(void *disk, size_t size)
{
    if (!disk || size < 512) return -1;

    uint8_t *d = (uint8_t *)disk;

    if (d[510] != 0x55 || d[511] != 0xAA) return -1;

    g_disk            = d;
    g_size            = size;
    g_bytes_per_sec   = u16le(d + 11);
    g_secs_per_clus   = d[13];
    g_reserved_secs   = u16le(d + 14);
    g_num_fats        = d[16];
    g_root_entry_cnt  = u16le(d + 17);
    g_fat_sz16        = u16le(d + 22);

    if (g_bytes_per_sec == 0 || g_secs_per_clus == 0) return -1;

    g_fat_offset     = (uint32_t)g_reserved_secs * g_bytes_per_sec;
    g_root_dir_offset = g_fat_offset +
                        (uint32_t)g_num_fats * g_fat_sz16 * g_bytes_per_sec;
    uint32_t root_dir_sectors = ((uint32_t)g_root_entry_cnt * 32u +
                                  g_bytes_per_sec - 1u) / g_bytes_per_sec;
    g_data_offset    = g_root_dir_offset + root_dir_sectors * g_bytes_per_sec;

    uint16_t total_sectors = u16le(d + 19);
    if (total_sectors == 0) total_sectors = (uint16_t)(u32le(d + 32) & 0xFFFF);
    g_total_clusters = ((uint32_t)total_sectors - g_data_offset / g_bytes_per_sec)
                       / g_secs_per_clus;

    return 0;
}

int fat12_list_dir(const char *path, fat12_dirent_t *out, int max)
{
    if (!g_disk || !out || max <= 0) return -1;
    if (!path || (path[0] != '/' && path[0] != '\0')) return -1;

    int count = 0;
    uint32_t off = g_root_dir_offset;
    uint32_t end = off + (uint32_t)g_root_entry_cnt * DIR_ENTRY_SIZE;

    while (off < end && off + DIR_ENTRY_SIZE <= g_size && count < max) {
        const uint8_t *e = g_disk + off;
        off += DIR_ENTRY_SIZE;

        uint8_t first = e[0];
        if (first == 0x00) break;
        if (first == 0xE5) continue;

        uint8_t attr = e[11];
        if (attr == 0x0F) continue;
        if (attr & FAT_ATTR_VOLUME_ID)   continue;

        fat83_to_str(e, out[count].name);
        out[count].attr          = attr;
        out[count].is_dir        = (attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
        out[count].size          = u32le(e + 28);
        out[count].first_cluster = u16le(e + 26);
        count++;
    }

    return count;
}

/* Find a root-dir entry matching the given short name. Returns byte offset into g_disk, or 0 if not found. */
static uint32_t find_dirent_raw(const char *short_name)
{
    uint32_t off = g_root_dir_offset;
    uint32_t end = off + (uint32_t)g_root_entry_cnt * DIR_ENTRY_SIZE;
    while (off < end && off + DIR_ENTRY_SIZE <= g_size) {
        uint8_t *e = g_disk + off;
        uint8_t  f = e[0];
        if (f == 0x00) break;
        if (f != 0xE5 && !(e[11] & FAT_ATTR_VOLUME_ID) && e[11] != 0x0F) {
            if (fat83_match(e, short_name))
                return off;
        }
        off += DIR_ENTRY_SIZE;
    }
    return 0;
}

/* Find a root-dir entry with transparent .ELF extension fallback. */
static uint32_t find_dirent(const char *short_name)
{
    if (!short_name || !short_name[0])
        return 0;

    uint32_t off = find_dirent_raw(short_name);
    if (off != 0)
        return off;

    /* If not found and no dot extension is present, try appending ".ELF" */
    const char *dot = NULL;
    for (const char *p = short_name; *p; p++) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    if (!dot) {
        char fallback[32];
        size_t len = 0;
        while (short_name[len] && len < sizeof(fallback) - 5) {
            fallback[len] = short_name[len];
            len++;
        }
        fallback[len++] = '.';
        fallback[len++] = 'E';
        fallback[len++] = 'L';
        fallback[len++] = 'F';
        fallback[len] = '\0';
        return find_dirent_raw(fallback);
    }

    return 0;
}

/* Extract trailing filename component and strip slashes. */
static const char *path_to_name(const char *path)
{
    if (!path) return path;
    const char *last_slash = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (last_slash && *(last_slash + 1)) return last_slash + 1;
    while (*path == '/') path++;
    return path;
}

int fat12_stat(const char *path, fat12_dirent_t *out)
{
    const char *name = path_to_name(path);
    if (!name || !name[0]) return -1;
    uint32_t off = find_dirent(name);
    if (!off) return -1;
    const uint8_t *e = g_disk + off;
    fat83_to_str(e, out->name);
    out->attr          = e[11];
    out->is_dir        = (e[11] & FAT_ATTR_DIRECTORY) ? 1 : 0;
    out->size          = u32le(e + 28);
    out->first_cluster = u16le(e + 26);
    return 0;
}

int fat12_read_file(const char *path, void *buf, size_t max)
{
    if (!g_disk || !buf) return -1;
    const char *name = path_to_name(path);
    if (!name || !name[0]) return -1;

    uint32_t doff = find_dirent(name);
    if (!doff) return -1;

    const uint8_t *e = g_disk + doff;
    if (e[11] & FAT_ATTR_DIRECTORY) return -1;

    uint32_t file_size = u32le(e + 28);
    uint16_t clus      = u16le(e + 26);
    uint32_t cluster_bytes = (uint32_t)g_secs_per_clus * g_bytes_per_sec;

    if (file_size < max) max = file_size;

    uint8_t *dst = (uint8_t *)buf;
    size_t   done = 0;

    while (clus >= 2 && clus < 0xFF8 && done < max) {
        uint32_t src_off = cluster_to_offset(clus);
        uint32_t to_copy = cluster_bytes;
        if (done + to_copy > max) to_copy = (uint32_t)(max - done);
        if (src_off + to_copy > g_size) to_copy = (uint32_t)(g_size - src_off);
        for (uint32_t i = 0; i < to_copy; i++)
            dst[done + i] = g_disk[src_off + i];
        done += to_copy;
        clus  = fat12_get_next_cluster(clus);
    }

    return (int)done;
}

/*
 * Read up to count bytes from path starting at byte offset.
 * Traverses FAT12 cluster chain without heap allocations.
 */
int fat12_read_file_offset(const char *path, void *buf, size_t offset, size_t count)
{
    if (!g_disk || !buf)
        return -1;

    const char *name = path_to_name(path);
    if (!name || !name[0])
        return -1;

    uint32_t doff = find_dirent(name);
    if (!doff)
        return -1;

    const uint8_t *e = g_disk + doff;
    if (e[11] & FAT_ATTR_DIRECTORY)
        return -1;

    uint32_t file_size = u32le(e + 28);
    if (offset >= (size_t)file_size)
        return 0;

    if (offset + count > (size_t)file_size)
        count = (size_t)file_size - offset;

    if (count == 0)
        return 0;

    uint16_t clus = u16le(e + 26);
    uint32_t cluster_bytes = (uint32_t)g_secs_per_clus * (uint32_t)g_bytes_per_sec;
    if (cluster_bytes == 0)
        return -1;

    /* Skip clusters preceding the requested start offset */
    size_t skip_clusters = offset / cluster_bytes;
    size_t in_cluster_offset = offset % cluster_bytes;

    while (skip_clusters > 0 && clus >= 2 && clus < 0xFF8) {
        clus = fat12_get_next_cluster(clus);
        skip_clusters--;
    }

    if (clus < 2 || clus >= 0xFF8)
        return 0;

    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;

    while (clus >= 2 && clus < 0xFF8 && done < count) {
        uint32_t src_off = cluster_to_offset(clus) + (uint32_t)in_cluster_offset;
        uint32_t avail_in_cluster = cluster_bytes - (uint32_t)in_cluster_offset;
        uint32_t to_copy = avail_in_cluster;

        if (done + to_copy > count)
            to_copy = (uint32_t)(count - done);

        if (src_off + to_copy > g_size) {
            if (src_off >= g_size)
                break;
            to_copy = (uint32_t)(g_size - src_off);
        }

        for (uint32_t i = 0; i < to_copy; i++)
            dst[done + i] = g_disk[src_off + i];

        done += to_copy;
        in_cluster_offset = 0;
        clus = fat12_get_next_cluster(clus);
    }

    return (int)done;
}

/* fat12_write_file 1. */
int fat12_write_file(const char *path, const void *data, size_t len)
{
    if (!g_disk || !data) return -1;
    const char *name = path_to_name(path);
    if (!name || !name[0]) return -1;

    uint32_t cluster_bytes = (uint32_t)g_secs_per_clus * g_bytes_per_sec;

    uint32_t doff = find_dirent(name);
    if (!doff) {
        uint32_t off = g_root_dir_offset;
        uint32_t end = off + (uint32_t)g_root_entry_cnt * DIR_ENTRY_SIZE;
        while (off < end && off + DIR_ENTRY_SIZE <= g_size) {
            uint8_t f = g_disk[off];
            if (f == 0x00 || f == 0xE5) { doff = off; break; }
            off += DIR_ENTRY_SIZE;
        }
        if (!doff) return -1;

        uint8_t *e = g_disk + doff;
        for (int i = 0; i < DIR_ENTRY_SIZE; i++) e[i] = 0;
        str_to_fat83(name, e);
        e[11] = FAT_ATTR_ARCHIVE;
    }

    uint8_t *e = g_disk + doff;

    uint16_t old_clus = u16le(e + 26);
    while (old_clus >= 2 && old_clus < 0xFF8) {
        uint16_t next = fat12_get_next_cluster(old_clus);
        fat12_set_cluster(old_clus, 0x000);
        old_clus = next;
    }
    w16le(e + 26, 0);
    w32le(e + 28, 0);

    if (len == 0) return 0;

    uint16_t first_clus = 0;
    uint16_t prev_clus  = 0;
    const uint8_t *src  = (const uint8_t *)data;
    size_t remaining    = len;

    while (remaining > 0) {
        uint16_t c = fat12_alloc_cluster();
        if (c == 0) return -1;

        if (prev_clus) fat12_set_cluster(prev_clus, c);
        else           first_clus = c;
        prev_clus = c;

        uint32_t dst_off  = cluster_to_offset(c);
        uint32_t to_write = (remaining < cluster_bytes) ?
                            (uint32_t)remaining : cluster_bytes;

        for (uint32_t i = 0; i < to_write && dst_off + i < g_size; i++)
            g_disk[dst_off + i] = src[i];

        src       += to_write;
        remaining -= to_write;
    }

    w16le(e + 26, first_clus);
    w32le(e + 28, (uint32_t)len);

    return 0;
}
