/*
 * fat12.h  -  FAT12 filesystem driver (read/write) for ramdisk images.
 *
 * Operates directly on the raw bytes of a FAT12-formatted disk image
 * held in RAM (e.g. the Multiboot initrd module).
 *
 * Limitations:
 *   - Root directory only (no subdirectory traversal).
 *   - 8.3 short filenames only.
 *   - No LFN support.
 *   - Single volume, no partitioning.
 */

#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stddef.h>

/* Maximum entries returned by fat12_list_dir. */
#define FAT12_MAX_DIRENT  64
/* Maximum name length: "FILENAME.EXT\0" */
#define FAT12_NAME_LEN    13

/** A decoded directory entry. */
typedef struct {
    char     name[FAT12_NAME_LEN];   /* null-terminated, e.g. "README.TXT" */
    uint32_t size;                   /* file size in bytes                  */
    uint16_t first_cluster;          /* starting cluster index              */
    uint8_t  attr;                   /* raw FAT attribute byte              */
    int      is_dir;                 /* non-zero if this is a directory     */
} fat12_dirent_t;

/** FAT attribute bits. */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20

/**
 * Initialize the FAT12 driver with a raw disk image in RAM.
 *
 * @param disk  Pointer to beginning of the FAT12 image bytes.
 * @param size  Size of the image in bytes.
 * @return 0 on success, -1 on invalid BPB signature.
 */
int fat12_init(void *disk, size_t size);

/**
 * List entries in the given directory.
 *
 * @param path   Directory path: "/" for root.
 * @param out    Output buffer of fat12_dirent_t structs.
 * @param max    Maximum entries to write into out[].
 * @return Number of entries written, or -1 on error.
 */
int fat12_list_dir(const char *path, fat12_dirent_t *out, int max);

/**
 * Read a file into a caller-supplied buffer.
 *
 * @param path   File path relative to root (e.g. "/README.TXT").
 * @param buf    Output buffer.
 * @param max    Buffer capacity.
 * @return Bytes read, or -1 on not-found / error.
 */
int fat12_read_file(const char *path, void *buf, size_t max);

/**
 * Write (or create) a file.
 * If the file exists its contents are replaced.
 * If it does not exist, a new directory entry is allocated.
 *
 * @param path   File path relative to root (e.g. "/HELLO.TXT").
 * @param data   Data to write.
 * @param len    Number of bytes to write.
 * @return 0 on success, -1 on error (disk full, invalid name, etc.).
 */
int fat12_write_file(const char *path, const void *data, size_t len);

/**
 * Look up a file by path and fill in the dirent.
 * @return 0 on success, -1 if not found.
 */
int fat12_stat(const char *path, fat12_dirent_t *out);

#endif /* FAT12_H */
