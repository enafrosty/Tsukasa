/*
 * bmp.h  -  Minimal true-color BMP parser and wallpaper renderer.
 *
 * Supports: 24-bit and 32-bit uncompressed BMPs.
 * Zero external dependencies; operates on VFS file descriptors.
 */

#ifndef BMP_H
#define BMP_H

#include <stdint.h>
#include <stddef.h>

/**
 * Load a BMP from the VFS, scale it to the framebuffer dimensions
 * (nearest-neighbor), and blit it directly to the screen.
 *
 * @param vfs_path  Path known to the VFS (e.g. "/wallpaper.bmp").
 * @return 0 on success, -1 on unsupported/not-found.
 */
int bmp_draw_wallpaper(const char *vfs_path);

/**
 * Load a BMP from the VFS into a heap-allocated pixel buffer.
 * Caller must kfree(*out_pixels) when done.
 *
 * @param vfs_path   Source file.
 * @param out_pixels Receives pointer to ARGB pixel array (w*h entries).
 * @param out_w      Receives image width.
 * @param out_h      Receives image height.
 * @return 0 on success, -1 on failure.
 */
int bmp_load_to_buf(const char *vfs_path,
                    uint32_t **out_pixels,
                    int *out_w, int *out_h);

#endif /* BMP_H */
