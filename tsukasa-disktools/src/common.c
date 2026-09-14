/*
 * Project Tsukasa — Storage Suite Common Block Device Helpers
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

#include "../include/disktools.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <io.h>
#define fsync(fd) _commit(fd)
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

int device_open(const char *path, int flags, device_handle_t *dev)
{
    if (!path || !dev)
        return -1;

    memset(dev, 0, sizeof(*dev));
    strncpy(dev->path, path, sizeof(dev->path) - 1);
    dev->path[sizeof(dev->path) - 1] = '\0';

    dev->fd = open(path, flags | O_BINARY);
    if (dev->fd < 0)
        return -1;

    off_t sz = lseek(dev->fd, 0, SEEK_END);
    if (sz < 0) {
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    lseek(dev->fd, 0, SEEK_SET);

    dev->sector_size = SECTOR_SIZE;
    dev->sector_count = (uint64_t)sz / dev->sector_size;

    return 0;
}

void device_close(device_handle_t *dev)
{
    if (!dev)
        return;

    if (dev->fd >= 0) {
        fsync(dev->fd);
        close(dev->fd);
        dev->fd = -1;
    }
}

int device_read_sectors(device_handle_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    if (!dev || dev->fd < 0 || !buf || count == 0)
        return -1;

    if (lba + count > dev->sector_count)
        return -1;

    off_t offset = (off_t)(lba * dev->sector_size);
    if (lseek(dev->fd, offset, SEEK_SET) < 0)
        return -1;

    size_t total = (size_t)count * dev->sector_size;
    size_t read_bytes = 0;
    uint8_t *dst = (uint8_t *)buf;

    while (read_bytes < total) {
        ssize_t n = read(dev->fd, dst + read_bytes, total - read_bytes);
        if (n <= 0)
            return -1;
        read_bytes += (size_t)n;
    }

    return 0;
}

int device_write_sectors(device_handle_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    if (!dev || dev->fd < 0 || !buf || count == 0)
        return -1;

    if (lba + count > dev->sector_count)
        return -1;

    off_t offset = (off_t)(lba * dev->sector_size);
    if (lseek(dev->fd, offset, SEEK_SET) < 0)
        return -1;

    size_t total = (size_t)count * dev->sector_size;
    size_t written_bytes = 0;
    const uint8_t *src = (const uint8_t *)buf;

    while (written_bytes < total) {
        ssize_t n = write(dev->fd, src + written_bytes, total - written_bytes);
        if (n <= 0)
            return -1;
        written_bytes += (size_t)n;
    }

    return 0;
}

int device_sync(device_handle_t *dev)
{
    if (!dev || dev->fd < 0)
        return -1;

    return fsync(dev->fd);
}

uint64_t device_get_size(device_handle_t *dev)
{
    if (!dev)
        return 0;

    return dev->sector_count * dev->sector_size;
}

void format_size(uint64_t bytes, char *out, size_t max)
{
    if (!out || max == 0)
        return;

    const uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    const uint64_t mib = 1024ULL * 1024ULL;
    const uint64_t kib = 1024ULL;

    if (bytes >= gib) {
        uint32_t whole = (uint32_t)(bytes / gib);
        uint32_t frac = (uint32_t)(((bytes % gib) * 10ULL) / gib);
        snprintf(out, max, "%u.%u GiB", whole, frac);
    } else if (bytes >= mib) {
        uint32_t whole = (uint32_t)(bytes / mib);
        uint32_t frac = (uint32_t)(((bytes % mib) * 10ULL) / mib);
        snprintf(out, max, "%u.%u MiB", whole, frac);
    } else if (bytes >= kib) {
        uint32_t whole = (uint32_t)(bytes / kib);
        snprintf(out, max, "%u KiB", whole);
    } else {
        snprintf(out, max, "%u bytes", (uint32_t)bytes);
    }
}
