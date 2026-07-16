/*
 * fat32.c - FAT32 read/write filesystem driver.
 * Uses ata_read_sectors / ata_write_sectors for disk I/O.
 *
 * Supported:
 *   - Normalized absolute paths with nested directory traversal.
 *   - LFN-aware read/stat/list.
 *   - In-place overwrite writes when existing cluster capacity is enough.
 *   - Same-directory 8.3 rename.
 *
 * Limitations:
 *   - Reads BPB from LBA 0 of the ATA master drive.
 *   - No cluster-chain growth on write.
 *   - No cross-directory rename.
 *   - No LFN rename.
 */

#include "fat32.h"
#include "../drv/ata.h"
#include <stdint.h>
#include <stddef.h>

/* ---- BPB / FAT32 on-disk structures ----------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;   /* 0 for FAT32 */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;        /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
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
    char     name[11];     /* 8.3 padded with spaces */
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

/* ---- Driver state ----------------------------------------------------- */

static int      g_ready = 0;
static uint32_t g_fat_lba;        /* LBA of first FAT */
static uint32_t g_data_lba;       /* LBA of cluster 2 */
static uint32_t g_root_cluster;
static uint32_t g_sectors_per_cluster;
static uint32_t g_fat_size;       /* in sectors */
static uint32_t g_bytes_per_sector;

/* Scratch sector buffer (512 bytes). */
static uint8_t g_sector[512];

/* ---- Helpers ---------------------------------------------------------- */

static int k_strncmpi(const char *a, const char *b, int n)
{
    /* Case-insensitive compare for first n chars. */
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

/* Convert FAT cluster number to the LBA of its first sector. */
static uint32_t cluster_to_lba(uint32_t cluster)
{
    return g_data_lba + (cluster - 2) * g_sectors_per_cluster;
}

/* Read the FAT entry for a cluster (returns next cluster or 0x0FFFFFFF for EOC). */
static uint32_t fat_next(uint32_t cluster)
{
    /* Each FAT32 entry is 4 bytes. */
    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector = g_fat_lba + fat_offset / g_bytes_per_sector;
    uint32_t entry_off  = fat_offset % g_bytes_per_sector;

    if (ata_read_sectors(fat_sector, 1, g_sector) < 0) return 0x0FFFFFFFu;
    uint32_t entry;
    /* Byte-wise read to avoid alignment issues. */
    entry = (uint32_t)g_sector[entry_off]
          | ((uint32_t)g_sector[entry_off+1] << 8)
          | ((uint32_t)g_sector[entry_off+2] << 16)
          | ((uint32_t)g_sector[entry_off+3] << 24);
    return entry & 0x0FFFFFFFu;
}

/* Strip trailing spaces from an 11-char 8.3 name and convert to NUL-terminated. */
static void parse_83name(const char src[11], char *dst, int dstlen)
{
    /* Copy 8-char base name, trimming spaces. */
    int i = 0, j = 0;
    for (; i < 8 && src[i] != ' '; i++) {
        if (j < dstlen - 2) dst[j++] = src[i];
    }
    /* Extension. */
    if (src[8] != ' ') {
        if (j < dstlen - 2) dst[j++] = '.';
        for (int k = 8; k < 11 && src[k] != ' '; k++)
            if (j < dstlen - 2) dst[j++] = src[k];
    }
    dst[j] = '\0';
}

/* ---- Directory iterator ----------------------------------------------- */

/* Iterate over all entries of a directory cluster chain.
 * Calls `cb` for each 8.3 or LFN-named valid entry.
 * Returns 1 to stop iteration early, 0 to continue.
 */
typedef int (*dir_cb)(const dir_entry_t *de, const char *name, void *ctx);

static void iterate_dir(uint32_t start_cluster, dir_cb cb, void *ctx)
{
    char lfn_buf[FAT32_NAME_MAX];
    int  lfn_len = 0;
    int  has_lfn = 0;

    uint32_t cluster = start_cluster;
    static uint8_t cluster_buf[512 * 64];   /* up to 64 sectors = 32KB */

    while (cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < g_sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, g_sector) < 0) return;

            dir_entry_t *entries = (dir_entry_t *)g_sector;
            int per_sector = (int)(g_bytes_per_sector / DIR_ENTRY_SIZE);

            for (int i = 0; i < per_sector; i++) {
                dir_entry_t *de = &entries[i];
                uint8_t first = (uint8_t)de->name[0];

                if (first == 0x00u) return;    /* end of directory */
                if (first == 0xE5u) {           /* deleted */
                    has_lfn = 0; lfn_len = 0;
                    continue;
                }
                if (de->attr == ATTR_LFN) {
                    lfn_entry_t *lfn = (lfn_entry_t *)de;
                    /* Accumulate LFN characters (order byte indicates sequence). */
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

                /* Skip volume labels. */
                if (de->attr & ATTR_VOLUME_ID) { has_lfn = 0; lfn_len = 0; continue; }
                /* Skip . and .. */
                if (de->name[0] == '.' ) { has_lfn = 0; lfn_len = 0; continue; }

                char name83[13];
                parse_83name(de->name, name83, sizeof(name83));

                const char *display = has_lfn ? lfn_buf : name83;
                int stop = cb(de, display, ctx);
                has_lfn = 0; lfn_len = 0;
                if (stop) return;
            }
        }
        cluster = fat_next(cluster);
    }
    (void)cluster_buf;
}

/* ---- Path parsing ---------------------------------------------------- */

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
} find_dir_ctx_t;

static int find_dir_cb(const dir_entry_t *de, const char *name, void *ctx)
{
    find_dir_ctx_t *f = (find_dir_ctx_t *)ctx;
    if ((de->attr & ATTR_DIR) && k_strncmpi(name, f->target, FAT32_NAME_MAX) == 0) {
        f->found_cluster = ((uint32_t)de->first_clus_hi << 16) | de->first_clus_lo;
        if (f->found_cluster == 0) f->found_cluster = g_root_cluster;
        f->found = 1;
        return 1;
    }
    return 0;
}

/* Resolve a path like "/dir1/dir2/file" to return the cluster of its parent dir
 * and point *leaf at the final component name. */
static uint32_t resolve_parent(const char *path, const char **leaf)
{
    /* Build list of path components. */
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

    if (ncomp == 0) { *leaf = NULL; return g_root_cluster; }
    *leaf = comps[ncomp - 1];

    /* Walk directories. */
    uint32_t cur = g_root_cluster;
    for (int ci = 0; ci < ncomp - 1; ci++) {
        find_dir_ctx_t fc;
        fc.target = comps[ci];
        fc.found  = 0;
        iterate_dir(cur, find_dir_cb, &fc);
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

static int find_entry_raw(uint32_t start_cluster,
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
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < g_sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, g_sector) < 0)
                return -1;

            dir_entry_t *entries = (dir_entry_t *)g_sector;
            int per_sector = (int)(g_bytes_per_sector / DIR_ENTRY_SIZE);
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
        cluster = fat_next(cluster);
    }
    return -1;
}

/* ---- stat / find file -------------------------------------------------- */

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

/* ---- list dir callback ------------------------------------------------- */

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

/* ---- Public API ------------------------------------------------------- */

int fat32_init(void)
{
    g_ready = 0;

    if (ata_read_sectors(0, 1, g_sector) < 0) return -1;
    bpb_t *bpb = (bpb_t *)g_sector;

    /* Validate signature. */
    if (g_sector[510] != 0x55u || g_sector[511] != 0xAAu) return -1;
    if (bpb->bytes_per_sector == 0) return -1;

    /* Check it's FAT32 (root_entry_count == 0, fat_size_16 == 0). */
    if (bpb->root_entry_count != 0 || bpb->fat_size_16 != 0) return -1;
    if (bpb->fat_size_32 == 0) return -1;

    g_bytes_per_sector    = bpb->bytes_per_sector;
    g_sectors_per_cluster = bpb->sectors_per_cluster;
    g_fat_size            = bpb->fat_size_32;
    g_root_cluster        = bpb->root_cluster;
    g_fat_lba             = bpb->hidden_sectors + bpb->reserved_sectors;
    g_data_lba            = g_fat_lba + bpb->fat_count * g_fat_size;

    g_ready = 1;
    return 0;
}

int fat32_stat(const char *path, fat32_dirent_t *out)
{
    char norm[FAT32_NAME_MAX];
    if (!g_ready || !path || !out) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    /* Root directory itself. */
    if (norm[0] == '/' && norm[1] == '\0') {
        k_strcpy(out->name, "/", FAT32_NAME_MAX);
        out->size = 0; out->is_dir = 1;
        out->first_cluster = g_root_cluster;
        return 0;
    }

    const char *leaf;
    uint32_t parent = resolve_parent(norm, &leaf);
    if (!parent || !leaf) return -1;

    stat_ctx_t sc;
    sc.name  = leaf;
    sc.found = 0;
    iterate_dir(parent, stat_cb, &sc);
    if (!sc.found) return -1;
    *out = sc.out;
    return 0;
}

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max)
{
    char norm[FAT32_NAME_MAX];
    if (!g_ready || !entries || max <= 0) return -1;
    if (!path || normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    uint32_t dir_cluster;
    if (norm[0] == '/' && (norm[1] == '\0')) {
        dir_cluster = g_root_cluster;
    } else {
        fat32_dirent_t de;
        if (fat32_stat(norm, &de) < 0 || !de.is_dir) return -1;
        dir_cluster = de.first_cluster;
    }

    list_ctx_t lc = { entries, max, 0 };
    iterate_dir(dir_cluster, list_cb, &lc);
    return lc.count;
}

int fat32_read_file(const char *path, void *buf, size_t max_bytes)
{
    char norm[FAT32_NAME_MAX];
    if (!g_ready || !path || !buf) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    fat32_dirent_t de;
    if (fat32_stat(norm, &de) < 0 || de.is_dir) return -1;

    size_t to_read = de.size < max_bytes ? de.size : max_bytes;
    size_t done = 0;
    uint32_t cluster = de.first_cluster;
    uint8_t *dst = (uint8_t *)buf;
    static uint8_t sec_buf[512];

    while (cluster < 0x0FFFFFF8u && done < to_read) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < g_sectors_per_cluster && done < to_read; s++) {
            if (ata_read_sectors(lba + s, 1, sec_buf) < 0) return (int)done;
            size_t chunk = g_bytes_per_sector;
            if (chunk > to_read - done) chunk = to_read - done;
            for (size_t i = 0; i < chunk; i++) dst[done++] = sec_buf[i];
        }
        cluster = fat_next(cluster);
    }
    return (int)done;
}

int fat32_write_file(const char *path, const void *buf, size_t size)
{
    char norm[FAT32_NAME_MAX];
    const char *leaf = NULL;
    uint32_t parent = 0;
    find_entry_raw_ctx_t raw;
    size_t chain_capacity = 0;
    size_t done = 0;
    uint32_t cluster = 0;
    const uint8_t *src = (const uint8_t *)buf;
    static uint8_t sec_buf[512];

    if (!g_ready || !path || (!buf && size > 0)) return -1;
    if (normalize_path(path, norm, FAT32_NAME_MAX) != 0)
        return -1;

    parent = resolve_parent(norm, &leaf);
    if (!parent || !leaf)
        return -1;

    raw.found = 0;
    if (find_entry_raw(parent, leaf, &raw) != 0 || !raw.found)
        return -1;
    if (raw.out.attr & ATTR_DIR)
        return -1;

    cluster = ((uint32_t)raw.out.first_clus_hi << 16) | raw.out.first_clus_lo;
    if (cluster == 0 && size > 0)
        return -1;

    {
        uint32_t c = cluster;
        size_t cbytes = (size_t)g_sectors_per_cluster * (size_t)g_bytes_per_sector;
        while (c >= 2 && c < 0x0FFFFFF8u) {
            chain_capacity += cbytes;
            c = fat_next(c);
        }
    }
    if (size > chain_capacity)
        return -1;

    while (cluster >= 2 && cluster < 0x0FFFFFF8u) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < g_sectors_per_cluster; s++) {
            size_t chunk = 0;
            for (uint32_t i = 0; i < g_bytes_per_sector; i++)
                sec_buf[i] = 0;
            if (done < size) {
                chunk = g_bytes_per_sector;
                if (chunk > size - done)
                    chunk = size - done;
                for (size_t i = 0; i < chunk; i++)
                    sec_buf[i] = src[done + i];
                done += chunk;
            }
            if (ata_write_sectors(lba + s, 1, sec_buf) < 0)
                return -1;
        }
        cluster = fat_next(cluster);
    }
    if (done != size)
        return -1;

    if (ata_read_sectors(raw.lba, 1, g_sector) < 0)
        return -1;
    {
        dir_entry_t *ent = (dir_entry_t *)(g_sector + raw.entry_off);
        ent->file_size = (uint32_t)size;
    }
    if (ata_write_sectors(raw.lba, 1, g_sector) < 0)
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

int fat32_rename(const char *old_path, const char *new_path)
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

    if (!g_ready || !old_path || !new_path)
        return -1;
    if (normalize_path(old_path, old_norm, FAT32_NAME_MAX) != 0)
        return -1;
    if (normalize_path(new_path, new_norm, FAT32_NAME_MAX) != 0)
        return -1;

    old_parent = resolve_parent(old_norm, &old_leaf);
    new_parent = resolve_parent(new_norm, &new_leaf);
    if (!old_parent || !new_parent || !old_leaf || !new_leaf)
        return -1;
    if (old_parent != new_parent)
        return -1;
    if (validate_short_name(new_leaf, new83) != 0)
        return -1;

    src.found = 0;
    dst.found = 0;
    if (find_entry_raw(old_parent, old_leaf, &src) != 0 || !src.found)
        return -1;
    if (find_entry_raw(new_parent, new_leaf, &dst) == 0 && dst.found)
        return -1;

    if (ata_read_sectors(src.lba, 1, g_sector) < 0)
        return -1;
    {
        dir_entry_t *ent = (dir_entry_t *)(g_sector + src.entry_off);
        for (int i = 0; i < 11; i++)
            ent->name[i] = (char)new83[i];
    }
    if (ata_write_sectors(src.lba, 1, g_sector) < 0)
        return -1;
    return 0;
}
