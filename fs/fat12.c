/*
 * fat12.c  -  FAT12 filesystem driver.
 *
 * Supports: read + write on a RAM-resident FAT12 disk image.
 *   - Root directory access (flat, no subdirectories traversed).
 *   - 8.3 short filenames (uppercase).
 *   - BI_RGB only; no FAT12 Variants needing BPB32.
 *
 * FAT12 image layout (all offsets from image base):
 *   0         Boot Sector (BPB).
 *   BPB.ReservedSectors * BPB.BytesPerSector   FAT copy 1.
 *   ... + BPB.NumFATs * BPB.FATSz16 * BPBBytesPerSector  Root dir.
 *   ... + RootDirSectors                        Data area (cluster 2 …).
 */

#include "fat12.h"
#include <stdint.h>
#include <stddef.h>

/* ---- Little-endian accessors ----------------------------------------- */

static inline uint16_t u16le(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t u32le(const uint8_t *p)
{ return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static inline void w16le(uint8_t *p, uint16_t v)
{ p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)(v>>8); }
static inline void w32le(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

/* ---- Driver state ----------------------------------------------------- */

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

/* ---- FAT12 cluster chain helpers -------------------------------------- */

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

/* Set FAT12 entry for cluster clus to value val (also writes FAT2). */
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

/* Find a free cluster (FAT entry == 0). Returns 0 if disk full. */
static uint16_t fat12_alloc_cluster(void)
{
    for (uint16_t c = 2; c < g_total_clusters + 2; c++) {
        if (fat12_get_next_cluster(c) == 0x000) {
            fat12_set_cluster(c, 0xFFF);  /* mark end-of-chain */
            return c;
        }
    }
    return 0;  /* full */
}

/* Byte address of the data area for cluster clus. */
static uint32_t cluster_to_offset(uint16_t clus)
{
    if (clus < 2) return g_data_offset;
    return g_data_offset + (uint32_t)(clus - 2) *
           (uint32_t)g_secs_per_clus * (uint32_t)g_bytes_per_sec;
}

/* ---- 8.3 name helpers ------------------------------------------------ */

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
    /* Init with spaces. */
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    /* Copy name part (up to 8 chars). */
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

/* ---- Initialization --------------------------------------------------- */

int fat12_init(void *disk, size_t size)
{
    if (!disk || size < 512) return -1;

    uint8_t *d = (uint8_t *)disk;

    /* Check boot signature. */
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

/* ---- Directory listing ------------------------------------------------ */

int fat12_list_dir(const char *path, fat12_dirent_t *out, int max)
{
    if (!g_disk || !out || max <= 0) return -1;
    /* Only root supported. */
    if (!path || (path[0] != '/' && path[0] != '\0')) return -1;

    int count = 0;
    uint32_t off = g_root_dir_offset;
    uint32_t end = off + (uint32_t)g_root_entry_cnt * DIR_ENTRY_SIZE;

    while (off < end && off + DIR_ENTRY_SIZE <= g_size && count < max) {
        const uint8_t *e = g_disk + off;
        off += DIR_ENTRY_SIZE;

        uint8_t first = e[0];
        if (first == 0x00) break;        /* no more entries */
        if (first == 0xE5) continue;     /* deleted */

        uint8_t attr = e[11];
        if (attr == 0x0F) continue;      /* LFN entry */
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

/* ---- File lookup ------------------------------------------------------ */

/* Find a root-dir entry matching the given short name.
 * Returns byte offset into g_disk, or 0 if not found. */
static uint32_t find_dirent(const char *short_name)
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

/* Strip leading slash and return pointer to short-name portion. */
static const char *path_to_name(const char *path)
{
    if (!path) return path;
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

/* ---- File read -------------------------------------------------------- */

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

/* ---- File write ------------------------------------------------------- */

/*
 * fat12_write_file
 * 1. Find or create a directory entry.
 * 2. Free the old cluster chain.
 * 3. Allocate new clusters and write data.
 * 4. Update directory entry (size + first_cluster).
 */
int fat12_write_file(const char *path, const void *data, size_t len)
{
    if (!g_disk || !data) return -1;
    const char *name = path_to_name(path);
    if (!name || !name[0]) return -1;

    uint32_t cluster_bytes = (uint32_t)g_secs_per_clus * g_bytes_per_sec;

    /* --- Find or create directory entry. --- */
    uint32_t doff = find_dirent(name);
    if (!doff) {
        /* Find a free (0x00 or 0xE5) slot. */
        uint32_t off = g_root_dir_offset;
        uint32_t end = off + (uint32_t)g_root_entry_cnt * DIR_ENTRY_SIZE;
        while (off < end && off + DIR_ENTRY_SIZE <= g_size) {
            uint8_t f = g_disk[off];
            if (f == 0x00 || f == 0xE5) { doff = off; break; }
            off += DIR_ENTRY_SIZE;
        }
        if (!doff) return -1;  /* root dir full */

        /* Initialise the new entry. */
        uint8_t *e = g_disk + doff;
        for (int i = 0; i < DIR_ENTRY_SIZE; i++) e[i] = 0;
        str_to_fat83(name, e);
        e[11] = FAT_ATTR_ARCHIVE;
    }

    uint8_t *e = g_disk + doff;

    /* --- Free old cluster chain. --- */
    uint16_t old_clus = u16le(e + 26);
    while (old_clus >= 2 && old_clus < 0xFF8) {
        uint16_t next = fat12_get_next_cluster(old_clus);
        fat12_set_cluster(old_clus, 0x000);
        old_clus = next;
    }
    w16le(e + 26, 0);
    w32le(e + 28, 0);

    if (len == 0) return 0;

    /* --- Allocate clusters and write data. --- */
    uint16_t first_clus = 0;
    uint16_t prev_clus  = 0;
    const uint8_t *src  = (const uint8_t *)data;
    size_t remaining    = len;

    while (remaining > 0) {
        uint16_t c = fat12_alloc_cluster();
        if (c == 0) return -1;  /* disk full */

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

    /* --- Update directory entry. --- */
    w16le(e + 26, first_clus);
    w32le(e + 28, (uint32_t)len);

    return 0;
}
