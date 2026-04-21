/*
 * fat32.h - Minimal FAT32 filesystem driver.
 * Layered on ata_read_sectors / ata_write_sectors.
 * Mounted at /disk/ in the VFS.
 */

#ifndef FAT32_H
#define FAT32_H

#include <stddef.h>
#include <stdint.h>

/* Maximum length of a file/directory name (8.3 long-name path). */
#define FAT32_NAME_MAX  256
#define FAT32_MAX_DIRENT 64

typedef struct {
    char   name[FAT32_NAME_MAX];
    uint32_t size;
    int    is_dir;
    uint32_t first_cluster;
} fat32_dirent_t;

/**
 * Initialise the FAT32 driver.
 * Reads the BPB from LBA 0 of the ATA drive.
 * Returns 0 on success, -1 if not a valid FAT32 volume.
 */
int fat32_init(void);

/**
 * List directory at `path` (e.g. "/" or "/mydir").
 * Fills `entries` with up to `max` entries.
 * Returns count, or -1 on error.
 */
int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max);

/**
 * Stat a path.  Returns 0 on success, -1 if not found.
 */
int fat32_stat(const char *path, fat32_dirent_t *out);

/**
 * Read a file into `buf` (up to `max_bytes`).
 * Returns bytes read, or -1 on error.
 */
int fat32_read_file(const char *path, void *buf, size_t max_bytes);

/**
 * Write/create a file at `path` with `buf` contents.
 * If the file exists it is overwritten.
 * Returns 0 on success, -1 on error.
 */
int fat32_write_file(const char *path, const void *buf, size_t size);

#endif /* FAT32_H */
