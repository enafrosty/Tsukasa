/*
 * bmp.c  -  BMP parser: 24-bit and 32-bit uncompressed, bottom-up rows.
 *
 * Uses VFS for file I/O and kmalloc for scratch buffers.
 * No libc, no floats — pure integer arithmetic throughout.
 */

#include "bmp.h"
#include "../drv/fb.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>

/* ---- BMP on-disk structures (packed, little-endian) ------------------- */

/* We parse fields by offset to avoid alignment padding issues. */
static inline uint16_t u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t s32le(const uint8_t *p)
{
    return (int32_t)u32le(p);
}

#define BFH_SIZE        14   /* BITMAPFILEHEADER size */
#define BIH_SIZE_OFF     0   /* relative to info header start */
#define BI_RGB           0
#define BI_BITFIELDS     3

/* ---- Internal helpers ------------------------------------------------- */

/* Integer division that rounds to nearest (for scaling). */
static inline int scale_coord(int src, int src_max, int dst_max)
{
    /* src_pos in destination coords → source pixel index */
    return (int)((uint32_t)src * (uint32_t)src_max / (uint32_t)dst_max);
}

/*
 * Read the whole file into a kmalloc'd buffer.
 * Returns pointer (caller must kfree) and sets *fsize, or NULL on failure.
 */
static uint8_t *read_vfs_file(const char *path, size_t *fsize)
{
    int fd = vfs_open(path);
    if (fd < 0) return NULL;

    /* Seek to end to get size. */
    size_t sz = vfs_seek(fd, 0, VFS_SEEK_END);
    if (sz == (size_t)-1 || sz == 0) { vfs_close(fd); return NULL; }

    vfs_seek(fd, 0, VFS_SEEK_SET);

    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf) { vfs_close(fd); return NULL; }

    size_t got = vfs_read(fd, buf, sz);
    vfs_close(fd);

    if (got < sz) { kfree(buf); return NULL; }

    *fsize = sz;
    return buf;
}

/*
 * Parse the header and decode all pixels into an ARGB buffer.
 * Returns kmalloc'd pixel array (w*h uint32_t, top-to-bottom),
 * or NULL on format errors.
 */
static uint32_t *bmp_decode(const uint8_t *data, size_t size,
                             int *out_w, int *out_h)
{
    if (size < BFH_SIZE + 4) return NULL;

    /* Check magic 'BM'. */
    if (data[0] != 'B' || data[1] != 'M') return NULL;

    uint32_t pixel_offset = u32le(data + 10);
    if (pixel_offset >= size) return NULL;

    /* Info header starts at offset 14. */
    const uint8_t *bih = data + BFH_SIZE;
    uint32_t bih_size  = u32le(bih + 0);
    if (BFH_SIZE + bih_size > size) return NULL;

    int32_t bmp_w = s32le(bih + 4);
    int32_t bmp_h = s32le(bih + 8);
    uint16_t bpp  = u16le(bih + 14);
    uint32_t compr = (bih_size >= 20) ? u32le(bih + 16) : BI_RGB;

    if (bmp_w <= 0 || bmp_w > 4096 || bmp_h == 0 || bmp_h < -4096)
        return NULL;
    if (bpp != 24 && bpp != 32) return NULL;
    if (compr != BI_RGB && compr != BI_BITFIELDS) return NULL;

    int bottom_up = (bmp_h > 0);
    int img_w = bmp_w;
    int img_h = bottom_up ? (int)bmp_h : -(int)bmp_h;

    /* Row stride: each row is padded to a 4-byte boundary. */
    int bytes_per_px = (int)(bpp / 8u);
    int row_stride   = (img_w * bytes_per_px + 3) & ~3;

    if (pixel_offset + (size_t)row_stride * (size_t)img_h > size)
        return NULL;

    uint32_t *pixels = (uint32_t *)kmalloc((size_t)(img_w * img_h) * sizeof(uint32_t));
    if (!pixels) return NULL;

    for (int row = 0; row < img_h; row++) {
        /* BMP rows are stored bottom-up when bmp_h > 0. */
        int bmp_row = bottom_up ? (img_h - 1 - row) : row;
        const uint8_t *src = data + pixel_offset + (size_t)bmp_row * (size_t)row_stride;

        for (int col = 0; col < img_w; col++) {
            uint8_t b = src[0], g = src[1], r = src[2];
            uint8_t a = (bpp == 32) ? src[3] : 0xFF;
            pixels[row * img_w + col] =
                ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                ((uint32_t)g << 8)  | (uint32_t)b;
            src += bytes_per_px;
        }
    }

    *out_w = img_w;
    *out_h = img_h;
    return pixels;
}

/* ---- Public API ------------------------------------------------------- */

int bmp_draw_wallpaper(const char *vfs_path)
{
    size_t fsize = 0;
    uint8_t *data = read_vfs_file(vfs_path, &fsize);
    if (!data) return -1;

    int bw, bh;
    uint32_t *pixels = bmp_decode(data, fsize, &bw, &bh);
    kfree(data);
    if (!pixels) return -1;

    int sw = (int)fb_info.width;
    int sh = (int)fb_info.height;

    /* Nearest-neighbor scale: for each destination pixel, look up source. */
    for (int y = 0; y < sh; y++) {
        int src_y = scale_coord(y, bh, sh);
        if (src_y >= bh) src_y = bh - 1;

        uint32_t *fb_row = (uint32_t *)((char *)fb_info.addr +
                                        (uint32_t)y * fb_info.pitch);
        for (int x = 0; x < sw; x++) {
            int src_x = scale_coord(x, bw, sw);
            if (src_x >= bw) src_x = bw - 1;
            uint32_t px = pixels[src_y * bw + src_x];
            /* Force opaque for framebuffer write. */
            fb_row[x] = px | 0xFF000000u;
        }
    }

    kfree(pixels);
    return 0;
}

int bmp_load_to_buf(const char *vfs_path,
                    uint32_t **out_pixels, int *out_w, int *out_h)
{
    if (!out_pixels || !out_w || !out_h) return -1;

    size_t fsize = 0;
    uint8_t *data = read_vfs_file(vfs_path, &fsize);
    if (!data) return -1;

    uint32_t *pixels = bmp_decode(data, fsize, out_w, out_h);
    kfree(data);

    if (!pixels) return -1;
    *out_pixels = pixels;
    return 0;
}
