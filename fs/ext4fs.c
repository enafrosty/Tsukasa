/*
 * Project Tsukasa - Journaled ext2/ext3/ext4 filesystem adapter
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

#include "ext4fs.h"

#include "../drv/blockdev.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../mm/heap.h"
#include "../mm/pagecache.h"
#include "../proc/process.h"

#include <ext4.h>
#include <ext4_inode.h>
#include <ext4_super.h>
#include <ext4_fs.h>

#define EXT4FS_MAX_MOUNTS 8
#define EXT4FS_OBJ_ID_BASE 0x4558543400000000ULL

struct ext4fs {
    int used;
    int slot;
    block_dev_t *bd;
    uint64_t pc_obj_id;
    int read_only;
    int journal_active;
    volatile uint32_t lock;
    char dev_name[32];
    char mp_name[32];
    struct ext4_blockdev bdev;
    struct ext4_blockdev_iface iface;
    uint8_t ph_bbuf[4096];
};

static struct ext4fs g_ext4_mounts[EXT4FS_MAX_MOUNTS];
static volatile uint32_t g_ext4_reg_lock;

static void fs_mutex_lock(volatile uint32_t *m)
{
    while (__atomic_exchange_n(m, 1U, __ATOMIC_ACQUIRE) != 0U) {
        if (process_current())
            process_yield();
        else
            __asm__ volatile ("pause");
    }
}

static void fs_mutex_unlock(volatile uint32_t *m)
{
    __atomic_store_n(m, 0U, __ATOMIC_RELEASE);
}

void *ext4_user_malloc(size_t size)
{
    return kmalloc(size ? size : 1U);
}

void *ext4_user_calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > ((size_t)-1) / nmemb)
        return NULL;
    size_t total = nmemb * size;
    void *p = kmalloc(total ? total : 1U);
    if (p && total > 0)
        k_memset(p, 0, total);
    return p;
}

void *ext4_user_realloc(void *ptr, size_t size)
{
    if (!ptr)
        return ext4_user_malloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    void *np = ext4_user_malloc(size);
    if (!np)
        return NULL;
    k_memcpy(np, ptr, size);
    kfree(ptr);
    return np;
}

void ext4_user_free(void *ptr)
{
    kfree(ptr);
}

static int ext4_pc_readpage(void *ctx, uint64_t obj_id, uint64_t page_index, void *buf)
{
    ext4fs_t *fs = (ext4fs_t *)ctx;
    (void)obj_id;
    if (!fs || !fs->bd || !fs->bd->read)
        return -EIO;

    uint32_t ss = fs->bd->sector_size ? fs->bd->sector_size : 512U;
    uint64_t secs_per_page = PAGECACHE_PAGE_SIZE / ss;
    uint64_t start_lba = page_index * secs_per_page;
    if (secs_per_page == 0 || start_lba >= fs->bd->sector_count)
        return -EIO;

    uint64_t avail = fs->bd->sector_count - start_lba;
    uint32_t to_read = (avail < secs_per_page) ? (uint32_t)avail : (uint32_t)secs_per_page;
    if (to_read < secs_per_page)
        k_memset(buf, 0, PAGECACHE_PAGE_SIZE);

    if (fs->bd->read(fs->bd, start_lba, to_read, buf) != 0)
        return -EIO;
    return 0;
}

static int ext4_pc_writepage(void *ctx, uint64_t obj_id, uint64_t page_index, const void *buf)
{
    ext4fs_t *fs = (ext4fs_t *)ctx;
    (void)obj_id;
    if (!fs || !fs->bd)
        return -EIO;
    if (fs->read_only || !fs->bd->write)
        return -EROFS;

    uint32_t ss = fs->bd->sector_size ? fs->bd->sector_size : 512U;
    uint64_t secs_per_page = PAGECACHE_PAGE_SIZE / ss;
    uint64_t start_lba = page_index * secs_per_page;
    if (secs_per_page == 0 || start_lba >= fs->bd->sector_count)
        return -EIO;

    uint64_t avail = fs->bd->sector_count - start_lba;
    uint32_t to_write = (avail < secs_per_page) ? (uint32_t)avail : (uint32_t)secs_per_page;
    if (fs->bd->write(fs->bd, start_lba, to_write, buf) != 0)
        return -EIO;
    return 0;
}

static const pagecache_ops_t g_ext4_pc_ops = {
    .readpage = ext4_pc_readpage,
    .writepage = ext4_pc_writepage,
};

static int ext4_bd_open(struct ext4_blockdev *bdev)
{
    (void)bdev;
    return EOK;
}

static int ext4_bd_close(struct ext4_blockdev *bdev)
{
    (void)bdev;
    return EOK;
}

static int ext4_bd_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
    if (!bdev || !bdev->bdif || !buf)
        return EINVAL;
    ext4fs_t *fs = (ext4fs_t *)bdev->bdif->p_user;
    if (!fs || !fs->bd)
        return EIO;
    if (blk_cnt == 0)
        return EOK;
    if (blk_id >= bdev->bdif->ph_bcnt || blk_cnt > bdev->bdif->ph_bcnt - blk_id)
        return EIO;

    uint64_t byte_off = blk_id * (uint64_t)bdev->bdif->ph_bsize;
    uint64_t total = (uint64_t)blk_cnt * (uint64_t)bdev->bdif->ph_bsize;
    uint64_t done = 0;

    while (done < total) {
        uint64_t cur = byte_off + done;
        uint64_t page_idx = cur >> 12;
        size_t off_in_page = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off_in_page;
        if (chunk > total - done)
            chunk = (size_t)(total - done);

        pagecache_page_t *pg = pagecache_get_page(fs->pc_obj_id, page_idx, &g_ext4_pc_ops, fs);
        if (!pg)
            return EIO;
        k_memcpy((uint8_t *)buf + done, (const uint8_t *)pg->data + off_in_page, chunk);
        pagecache_put_page(pg);
        done += chunk;
    }
    return EOK;
}

static int ext4_bd_bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
    if (!bdev || !bdev->bdif || !buf)
        return EINVAL;
    ext4fs_t *fs = (ext4fs_t *)bdev->bdif->p_user;
    if (!fs || !fs->bd)
        return EIO;
    if (fs->read_only || !fs->bd->write)
        return EROFS;
    if (blk_cnt == 0)
        return EOK;
    if (blk_id >= bdev->bdif->ph_bcnt || blk_cnt > bdev->bdif->ph_bcnt - blk_id)
        return EIO;

    uint64_t byte_off = blk_id * (uint64_t)bdev->bdif->ph_bsize;
    uint64_t total = (uint64_t)blk_cnt * (uint64_t)bdev->bdif->ph_bsize;
    uint64_t done = 0;

    while (done < total) {
        uint64_t cur = byte_off + done;
        uint64_t page_idx = cur >> 12;
        size_t off_in_page = (size_t)(cur & 0xFFFULL);
        size_t chunk = PAGECACHE_PAGE_SIZE - off_in_page;
        if (chunk > total - done)
            chunk = (size_t)(total - done);

        pagecache_page_t *pg = pagecache_get_page(fs->pc_obj_id, page_idx, &g_ext4_pc_ops, fs);
        if (!pg)
            return EIO;
        k_memcpy((uint8_t *)pg->data + off_in_page, (const uint8_t *)buf + done, chunk);
        pagecache_mark_dirty(pg);
        pagecache_put_page(pg);
        done += chunk;
    }
    return EOK;
}

static int lwext4_to_vfs_err(int r)
{
    if (r == 0)
        return 0;
    if (r == ENOTSUP)
        return -EINVAL;
    return (r > 0) ? -r : r;
}

static uint64_t find_blockdev_index(const block_dev_t *bd)
{
    int n = blockdev_count();
    for (int i = 0; i < n; i++) {
        if (blockdev_at(i) == bd)
            return (uint64_t)i;
    }
    return 0x10000ULL;
}

static int ext4fs_make_path(const ext4fs_t *fs, const char *subpath, char *out, size_t cap)
{
    size_t mplen = k_strlen(fs->mp_name);
    if (!subpath || mplen + 1 > cap)
        return -EINVAL;
    k_memcpy(out, fs->mp_name, mplen + 1);
    while (*subpath == '/')
        subpath++;
    size_t slen = k_strlen(subpath);
    if (mplen + slen + 1 > cap)
        return -ENAMETOOLONG;
    if (slen > 0)
        k_memcpy(out + mplen, subpath, slen + 1);
    return 0;
}

static uint32_t ext4_imode_to_vfs_type(uint32_t imode)
{
    switch (imode) {
    case EXT4_INODE_MODE_FILE:      return VFS_TYPE_FILE;
    case EXT4_INODE_MODE_DIRECTORY: return VFS_TYPE_DIR;
    case EXT4_INODE_MODE_SOFTLINK:  return VFS_TYPE_SYMLINK;
    case EXT4_INODE_MODE_CHARDEV:   return VFS_TYPE_CHAR;
    case EXT4_INODE_MODE_BLOCKDEV:  return VFS_TYPE_BLOCK;
    case EXT4_INODE_MODE_FIFO:      return VFS_TYPE_PIPE;
    case EXT4_INODE_MODE_SOCKET:    return VFS_TYPE_SOCKET;
    default:                        return VFS_TYPE_UNKNOWN;
    }
}

static int ext4fs_is_root_subpath(const char *subpath)
{
    if (!subpath || !subpath[0])
        return 1;
    while (*subpath == '/')
        subpath++;
    return *subpath == '\0';
}

static int ext4fs_is_dir_empty_locked(const char *full_path)
{
    ext4_dir dir;
    int r = ext4_dir_open(&dir, full_path);
    if (r != EOK)
        return lwext4_to_vfs_err(r);

    int empty = 1;
    const ext4_direntry *de;
    while ((de = ext4_dir_entry_next(&dir)) != NULL) {
        if (de->name_length == 1 && de->name[0] == '.')
            continue;
        if (de->name_length == 2 && de->name[0] == '.' && de->name[1] == '.')
            continue;
        empty = 0;
        break;
    }
    ext4_dir_close(&dir);
    return empty ? 1 : 0;
}

static int ext4fs_fill_stat_from_path_locked(ext4fs_t *fs, const char *full_path, vfs_stat_t *out)
{
    struct ext4_sblock *sb = NULL;
    int r = ext4_get_sblock(fs->mp_name, &sb);
    if (r != EOK || !sb)
        return -EIO;

    uint32_t ino = 0;
    struct ext4_inode inode;
    r = ext4_raw_inode_fill(full_path, &ino, &inode);
    if (r != EOK)
        return lwext4_to_vfs_err(r);

    uint32_t imode = ext4_inode_type(sb, &inode);
    uint32_t raw_mode = ext4_inode_get_mode(sb, &inode);
    uint64_t sz = ext4_inode_get_size(sb, &inode);
    uint32_t bsz = ext4_sb_get_block_size(sb);

    k_memset(out, 0, sizeof(*out));
    out->type = ext4_imode_to_vfs_type(imode);
    out->mode = VFS_MODE_READ | (fs->read_only ? 0U : VFS_MODE_WRITE);
    out->size = sz;
    out->st_size = sz;
    out->blocks = (sz + 511ULL) / 512ULL;
    out->st_blocks = out->blocks;
    out->st_blksize = bsz ? bsz : 4096U;
    out->st_ino = (uint64_t)ino;
    out->st_mode = raw_mode;
    out->st_nlink = ext4_inode_get_links_cnt(&inode);
    out->st_uid = ext4_inode_get_uid(&inode);
    out->st_gid = ext4_inode_get_gid(&inode);
    out->st_atime = (int64_t)ext4_inode_get_access_time(&inode);
    out->st_mtime = (int64_t)ext4_inode_get_modif_time(&inode);
    out->st_ctime = (int64_t)ext4_inode_get_change_inode_time(&inode);
    return 0;
}

ext4fs_t *ext4fs_mount(struct block_dev *bd, int read_only, int *err_out)
{
    if (err_out)
        *err_out = -EINVAL;
    if (!bd || !bd->read || bd->sector_size == 0 || bd->sector_size > 4096U || bd->sector_count == 0)
        return NULL;

    fs_mutex_lock(&g_ext4_reg_lock);

    int slot = -1;
    for (int i = 0; i < EXT4FS_MAX_MOUNTS; i++) {
        if (g_ext4_mounts[i].used && g_ext4_mounts[i].bd == bd) {
            fs_mutex_unlock(&g_ext4_reg_lock);
            if (err_out)
                *err_out = -EBUSY;
            return NULL;
        }
        if (!g_ext4_mounts[i].used && slot < 0)
            slot = i;
    }

    if (slot < 0) {
        fs_mutex_unlock(&g_ext4_reg_lock);
        if (err_out)
            *err_out = -ENOSPC;
        return NULL;
    }

    ext4fs_t *fs = &g_ext4_mounts[slot];
    k_memset(fs, 0, sizeof(*fs));
    fs->used = 1;
    fs->slot = slot;
    fs->bd = bd;

    uint64_t dev_idx = find_blockdev_index(bd);
    if (dev_idx >= 0x10000ULL)
        dev_idx = (uint64_t)slot;
    fs->pc_obj_id = EXT4FS_OBJ_ID_BASE | dev_idx;
    fs->read_only = (read_only || !bd->write) ? 1 : 0;

    fs->dev_name[0] = 'e'; fs->dev_name[1] = '4'; fs->dev_name[2] = 'd';
    fs->dev_name[3] = (char)('0' + slot); fs->dev_name[4] = '\0';

    fs->mp_name[0] = '/'; fs->mp_name[1] = 'e'; fs->mp_name[2] = '4';
    fs->mp_name[3] = '_'; fs->mp_name[4] = (char)('0' + slot);
    fs->mp_name[5] = '/'; fs->mp_name[6] = '\0';

    fs->iface.open = ext4_bd_open;
    fs->iface.bread = ext4_bd_bread;
    fs->iface.bwrite = ext4_bd_bwrite;
    fs->iface.close = ext4_bd_close;
    fs->iface.lock = NULL;
    fs->iface.unlock = NULL;
    fs->iface.ph_bsize = bd->sector_size;
    fs->iface.ph_bcnt = bd->sector_count;
    fs->iface.ph_bbuf = fs->ph_bbuf;
    fs->iface.p_user = fs;

    fs->bdev.bdif = &fs->iface;
    fs->bdev.part_offset = 0;
    fs->bdev.part_size = bd->sector_count * (uint64_t)bd->sector_size;

    pagecache_invalidate_obj(fs->pc_obj_id);

    int r = ext4_device_register(&fs->bdev, fs->dev_name);
    if (r != EOK) {
        fs->used = 0;
        fs_mutex_unlock(&g_ext4_reg_lock);
        if (err_out)
            *err_out = lwext4_to_vfs_err(r);
        return NULL;
    }

    r = ext4_mount(fs->dev_name, fs->mp_name, fs->read_only ? true : false);
    if (r != EOK) {
        ext4_device_unregister(fs->dev_name);
        pagecache_invalidate_obj(fs->pc_obj_id);
        fs->used = 0;
        fs_mutex_unlock(&g_ext4_reg_lock);
        if (err_out)
            *err_out = lwext4_to_vfs_err(r);
        return NULL;
    }

    if (!fs->read_only) {
        r = ext4_recover(fs->mp_name);
        if (r != EOK && r != ENOTSUP) {
            ext4_umount(fs->mp_name);
            ext4_device_unregister(fs->dev_name);
            pagecache_invalidate_obj(fs->pc_obj_id);
            fs->used = 0;
            fs_mutex_unlock(&g_ext4_reg_lock);
            if (err_out)
                *err_out = lwext4_to_vfs_err(r);
            return NULL;
        }

        r = ext4_journal_start(fs->mp_name);
        if (r != EOK) {
            ext4_umount(fs->mp_name);
            ext4_device_unregister(fs->dev_name);
            pagecache_invalidate_obj(fs->pc_obj_id);
            fs->used = 0;
            fs_mutex_unlock(&g_ext4_reg_lock);
            if (err_out)
                *err_out = lwext4_to_vfs_err(r);
            return NULL;
        }
        fs->journal_active = 1;
        pagecache_sync_obj(fs->pc_obj_id);
    }

    fs_mutex_unlock(&g_ext4_reg_lock);
    if (err_out)
        *err_out = 0;
    return fs;
}

int ext4fs_sync(ext4fs_t *fs)
{
    if (!fs || !fs->used)
        return -EINVAL;
    fs_mutex_lock(&fs->lock);
    int r = ext4_cache_flush(fs->mp_name);
    int pc_rc = pagecache_sync_obj(fs->pc_obj_id);
    int bd_rc = (fs->bd && fs->bd->sync) ? fs->bd->sync(fs->bd) : 0;
    fs_mutex_unlock(&fs->lock);
    if (r != EOK)
        return lwext4_to_vfs_err(r);
    if (pc_rc != 0)
        return pc_rc;
    return (bd_rc != 0) ? -EIO : 0;
}

int ext4fs_umount(ext4fs_t *fs)
{
    if (!fs || !fs->used)
        return -EINVAL;

    fs_mutex_lock(&g_ext4_reg_lock);
    fs_mutex_lock(&fs->lock);

    if (fs->journal_active) {
        (void)ext4_journal_stop(fs->mp_name);
        fs->journal_active = 0;
    }
    (void)ext4_cache_flush(fs->mp_name);
    int r = ext4_umount(fs->mp_name);
    int pc_rc = pagecache_sync_obj(fs->pc_obj_id);
    if (fs->bd && fs->bd->sync)
        (void)fs->bd->sync(fs->bd);
    pagecache_invalidate_obj(fs->pc_obj_id);
    (void)ext4_device_unregister(fs->dev_name);

    fs->used = 0;
    fs->bd = NULL;

    fs_mutex_unlock(&fs->lock);
    fs_mutex_unlock(&g_ext4_reg_lock);

    if (r != EOK)
        return lwext4_to_vfs_err(r);
    return pc_rc;
}

int ext4fs_stat(ext4fs_t *fs, const char *subpath, int follow_last, vfs_stat_t *out)
{
    (void)follow_last;
    if (!fs || !fs->used || !subpath || !out)
        return -EINVAL;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    rc = ext4fs_fill_stat_from_path_locked(fs, full, out);
    fs_mutex_unlock(&fs->lock);
    return rc;
}

int ext4fs_open(ext4fs_t *fs, const char *subpath, int vfs_flags, uint32_t create_mode,
                void **handle_out, int *is_dir_out, uint64_t *ino_out, size_t *size_out)
{
    if (!fs || !fs->used || !subpath || !handle_out || !is_dir_out || !ino_out || !size_out)
        return -EINVAL;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);

    struct ext4_sblock *sb = NULL;
    if (ext4_get_sblock(fs->mp_name, &sb) != EOK || !sb) {
        fs_mutex_unlock(&fs->lock);
        return -EIO;
    }

    uint32_t existing_ino = 0;
    struct ext4_inode raw_ino;
    int ex_r = ext4_raw_inode_fill(full, &existing_ino, &raw_ino);
    int created = 0;

    if (ex_r == EOK) {
        if ((vfs_flags & VFS_O_CREAT) && (vfs_flags & VFS_O_EXCL)) {
            fs_mutex_unlock(&fs->lock);
            return -EEXIST;
        }
        uint32_t imode = ext4_inode_type(sb, &raw_ino);
        if (imode == EXT4_INODE_MODE_DIRECTORY) {
            if (vfs_flags & (VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC)) {
                fs_mutex_unlock(&fs->lock);
                return -EISDIR;
            }
            *handle_out = NULL;
            *is_dir_out = 1;
            *ino_out = (uint64_t)existing_ino;
            *size_out = 0;
            fs_mutex_unlock(&fs->lock);
            return 0;
        }
        if (vfs_flags & VFS_O_DIRECTORY) {
            fs_mutex_unlock(&fs->lock);
            return -ENOTDIR;
        }
        if (imode != EXT4_INODE_MODE_FILE) {
            fs_mutex_unlock(&fs->lock);
            return -EINVAL;
        }
    } else if (ex_r == ENOENT) {
        if (!(vfs_flags & VFS_O_CREAT)) {
            fs_mutex_unlock(&fs->lock);
            return -ENOENT;
        }
        if (vfs_flags & VFS_O_DIRECTORY) {
            fs_mutex_unlock(&fs->lock);
            return -ENOTDIR;
        }
        if (fs->read_only) {
            fs_mutex_unlock(&fs->lock);
            return -EROFS;
        }
        created = 1;
    } else {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(ex_r);
    }

    int oflags;
    if ((vfs_flags & VFS_O_RDWR) == VFS_O_RDWR)
        oflags = O_RDWR;
    else if (vfs_flags & VFS_O_WRONLY)
        oflags = O_WRONLY;
    else
        oflags = O_RDONLY;

    if (vfs_flags & VFS_O_CREAT)
        oflags |= O_CREAT;
    if (vfs_flags & VFS_O_TRUNC)
        oflags |= O_TRUNC;
    if (vfs_flags & VFS_O_APPEND)
        oflags |= O_APPEND;

    ext4_file *ef = (ext4_file *)kmalloc(sizeof(ext4_file));
    if (!ef) {
        fs_mutex_unlock(&fs->lock);
        return -ENOMEM;
    }
    k_memset(ef, 0, sizeof(*ef));

    int r = ext4_fopen2(ef, full, oflags);
    if (r != EOK) {
        kfree(ef);
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }

    if (created && create_mode != 0)
        (void)ext4_mode_set(full, create_mode & 0777U);

    *handle_out = ef;
    *is_dir_out = 0;
    *ino_out = (uint64_t)ef->inode;
    *size_out = (size_t)ext4_fsize(ef);
    fs_mutex_unlock(&fs->lock);
    return 0;
}

void ext4fs_close(ext4fs_t *fs, void *handle, int dirty)
{
    if (!fs || !fs->used) {
        if (handle)
            kfree(handle);
        return;
    }
    fs_mutex_lock(&fs->lock);
    if (handle) {
        ext4_file *ef = (ext4_file *)handle;
        if (ef->mp)
            (void)ext4_fclose(ef);
        kfree(ef);
    }
    if (dirty && !fs->read_only) {
        (void)ext4_cache_flush(fs->mp_name);
        (void)pagecache_sync_obj(fs->pc_obj_id);
        if (fs->bd && fs->bd->sync)
            (void)fs->bd->sync(fs->bd);
    }
    fs_mutex_unlock(&fs->lock);
}

static size_t ext4fs_refresh_size_locked(ext4fs_t *fs, ext4_file *ef)
{
    struct ext4_inode_ref ref;
    if (!fs || !fs->bdev.fs || !ef || !ef->mp)
        return 0;
    if (ext4_fs_get_inode_ref(fs->bdev.fs, ef->inode, &ref) == EOK) {
        ef->fsize = ext4_inode_get_size(&fs->bdev.fs->sb, ref.inode);
        ext4_fs_put_inode_ref(&ref);
    }
    return (size_t)ef->fsize;
}

long ext4fs_read(ext4fs_t *fs, void *handle, size_t pos, void *buf, size_t count)
{
    if (!fs || !fs->used || !handle || !buf)
        return -EINVAL;
    if (count == 0)
        return 0;

    fs_mutex_lock(&fs->lock);
    ext4_file *ef = (ext4_file *)handle;
    size_t cur_sz = ext4fs_refresh_size_locked(fs, ef);
    if (pos >= cur_sz) {
        fs_mutex_unlock(&fs->lock);
        return 0;
    }

    int r = ext4_fseek(ef, (int64_t)pos, SEEK_SET);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }

    size_t rcnt = 0;
    r = ext4_fread(ef, buf, count, &rcnt);
    fs_mutex_unlock(&fs->lock);
    if (r != EOK)
        return lwext4_to_vfs_err(r);
    return (long)rcnt;
}

static int ext4fs_zero_extend_locked(ext4fs_t *fs, ext4_file *ef, uint64_t target_len)
{
    static const uint8_t zbuf[512] = { 0 };
    uint64_t cur = (uint64_t)ext4fs_refresh_size_locked(fs, ef);
    if (target_len <= cur)
        return 0;

    int r = ext4_fseek(ef, (int64_t)cur, SEEK_SET);
    if (r != EOK)
        return lwext4_to_vfs_err(r);

    while (cur < target_len) {
        size_t chunk = (target_len - cur > sizeof(zbuf)) ? sizeof(zbuf) : (size_t)(target_len - cur);
        size_t wcnt = 0;
        r = ext4_fwrite(ef, zbuf, chunk, &wcnt);
        if (r != EOK || wcnt == 0)
            return (r != EOK) ? lwext4_to_vfs_err(r) : -EIO;
        cur += wcnt;
    }
    return 0;
}

long ext4fs_write(ext4fs_t *fs, void *handle, size_t pos, const void *buf, size_t count)
{
    if (!fs || !fs->used || !handle || !buf)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;
    if (count == 0)
        return 0;

    fs_mutex_lock(&fs->lock);
    ext4_file *ef = (ext4_file *)handle;
    size_t cur_sz = ext4fs_refresh_size_locked(fs, ef);

    if ((uint64_t)pos > (uint64_t)cur_sz) {
        int zrc = ext4fs_zero_extend_locked(fs, ef, (uint64_t)pos);
        if (zrc != 0) {
            fs_mutex_unlock(&fs->lock);
            return zrc;
        }
    }

    int r = ext4_fseek(ef, (int64_t)pos, SEEK_SET);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }

    size_t wcnt = 0;
    r = ext4_fwrite(ef, buf, count, &wcnt);
    fs_mutex_unlock(&fs->lock);
    if (r != EOK)
        return lwext4_to_vfs_err(r);
    return (long)wcnt;
}

size_t ext4fs_file_size(ext4fs_t *fs, void *handle, const char *subpath)
{
    if (!fs || !fs->used)
        return 0;
    fs_mutex_lock(&fs->lock);
    size_t sz = 0;
    if (handle) {
        sz = ext4fs_refresh_size_locked(fs, (ext4_file *)handle);
    } else if (subpath) {
        char full[VFS_PATH_MAX + 32];
        vfs_stat_t st;
        if (ext4fs_make_path(fs, subpath, full, sizeof(full)) == 0 &&
            ext4fs_fill_stat_from_path_locked(fs, full, &st) == 0) {
            sz = (size_t)st.size;
        }
    }
    fs_mutex_unlock(&fs->lock);
    return sz;
}

int ext4fs_fstat(ext4fs_t *fs, void *handle, const char *subpath, vfs_stat_t *out)
{
    if (!fs || !fs->used || !subpath || !out)
        return -EINVAL;
    int rc = ext4fs_stat(fs, subpath, 1, out);
    if (rc == 0 && handle) {
        fs_mutex_lock(&fs->lock);
        size_t sz = ext4fs_refresh_size_locked(fs, (ext4_file *)handle);
        out->size = sz;
        out->st_size = sz;
        out->blocks = (sz + 511ULL) / 512ULL;
        out->st_blocks = out->blocks;
        fs_mutex_unlock(&fs->lock);
    }
    return rc;
}

int ext4fs_mkdir(ext4fs_t *fs, const char *subpath, uint32_t mode)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    uint32_t ino = 0;
    struct ext4_inode raw;
    if (ext4_raw_inode_fill(full, &ino, &raw) == EOK) {
        fs_mutex_unlock(&fs->lock);
        return -EEXIST;
    }

    int r = ext4_dir_mk(full);
    if (r == EOK && mode != 0)
        (void)ext4_mode_set(full, mode & 0777U);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_rmdir(ext4fs_t *fs, const char *subpath)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;
    if (ext4fs_is_root_subpath(subpath))
        return -EBUSY;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    struct ext4_sblock *sb = NULL;
    if (ext4_get_sblock(fs->mp_name, &sb) != EOK || !sb) {
        fs_mutex_unlock(&fs->lock);
        return -EIO;
    }

    uint32_t ino = 0;
    struct ext4_inode raw;
    int r = ext4_raw_inode_fill(full, &ino, &raw);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }
    if (ext4_inode_type(sb, &raw) != EXT4_INODE_MODE_DIRECTORY) {
        fs_mutex_unlock(&fs->lock);
        return -ENOTDIR;
    }

    int empty = ext4fs_is_dir_empty_locked(full);
    if (empty < 0) {
        fs_mutex_unlock(&fs->lock);
        return empty;
    }
    if (!empty) {
        fs_mutex_unlock(&fs->lock);
        return -ENOTEMPTY;
    }

    r = ext4_dir_rm(full);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_unlink(ext4fs_t *fs, const char *subpath)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;
    if (ext4fs_is_root_subpath(subpath))
        return -EISDIR;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    int r = ext4_fremove(full);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_rename(ext4fs_t *fs, const char *old_sub, const char *new_sub)
{
    if (!fs || !fs->used || !old_sub || !new_sub)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;
    if (ext4fs_is_root_subpath(old_sub) || ext4fs_is_root_subpath(new_sub))
        return -EBUSY;

    char old_full[VFS_PATH_MAX + 32];
    char new_full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, old_sub, old_full, sizeof(old_full));
    if (rc != 0)
        return rc;
    rc = ext4fs_make_path(fs, new_sub, new_full, sizeof(new_full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    struct ext4_sblock *sb = NULL;
    if (ext4_get_sblock(fs->mp_name, &sb) != EOK || !sb) {
        fs_mutex_unlock(&fs->lock);
        return -EIO;
    }

    uint32_t old_ino = 0;
    struct ext4_inode old_raw;
    int r = ext4_raw_inode_fill(old_full, &old_ino, &old_raw);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }
    if (k_strcmp(old_full, new_full) == 0) {
        fs_mutex_unlock(&fs->lock);
        return 0;
    }

    int old_is_dir = (ext4_inode_type(sb, &old_raw) == EXT4_INODE_MODE_DIRECTORY);
    uint32_t new_ino = 0;
    struct ext4_inode new_raw;
    if (ext4_raw_inode_fill(new_full, &new_ino, &new_raw) == EOK) {
        int new_is_dir = (ext4_inode_type(sb, &new_raw) == EXT4_INODE_MODE_DIRECTORY);
        if (old_is_dir && !new_is_dir) {
            fs_mutex_unlock(&fs->lock);
            return -ENOTDIR;
        }
        if (!old_is_dir && new_is_dir) {
            fs_mutex_unlock(&fs->lock);
            return -EISDIR;
        }
        if (new_is_dir) {
            int empty = ext4fs_is_dir_empty_locked(new_full);
            if (empty < 0) {
                fs_mutex_unlock(&fs->lock);
                return empty;
            }
            if (!empty) {
                fs_mutex_unlock(&fs->lock);
                return -ENOTEMPTY;
            }
            r = ext4_dir_rm(new_full);
        } else {
            r = ext4_fremove(new_full);
        }
        if (r != EOK) {
            fs_mutex_unlock(&fs->lock);
            return lwext4_to_vfs_err(r);
        }
    }

    r = ext4_frename(old_full, new_full);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_symlink(ext4fs_t *fs, const char *target, const char *link_sub)
{
    if (!fs || !fs->used || !target || !target[0] || !link_sub)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, link_sub, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    uint32_t ino = 0;
    struct ext4_inode raw;
    if (ext4_raw_inode_fill(full, &ino, &raw) == EOK) {
        fs_mutex_unlock(&fs->lock);
        return -EEXIST;
    }
    int r = ext4_fsymlink(target, full);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_readlink(ext4fs_t *fs, const char *subpath, char *buf, size_t cap)
{
    if (!fs || !fs->used || !subpath || !buf || cap == 0)
        return -EINVAL;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    size_t rcnt = 0;
    int r = ext4_readlink(full, buf, cap, &rcnt);
    fs_mutex_unlock(&fs->lock);
    if (r != EOK)
        return lwext4_to_vfs_err(r);
    if (rcnt < cap)
        buf[rcnt] = '\0';
    return (int)rcnt;
}

int ext4fs_truncate(ext4fs_t *fs, const char *subpath, size_t length)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    ext4_file ef;
    k_memset(&ef, 0, sizeof(ef));
    int r = ext4_fopen2(&ef, full, O_RDWR);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }

    uint64_t cur = (uint64_t)ext4_fsize(&ef);
    if ((uint64_t)length < cur) {
        r = ext4_ftruncate(&ef, (uint64_t)length);
        rc = lwext4_to_vfs_err(r);
    } else if ((uint64_t)length > cur) {
        rc = ext4fs_zero_extend_locked(fs, &ef, (uint64_t)length);
    } else {
        rc = 0;
    }
    (void)ext4_fclose(&ef);
    fs_mutex_unlock(&fs->lock);
    return rc;
}

int ext4fs_ftruncate(ext4fs_t *fs, void *handle, size_t length)
{
    if (!fs || !fs->used || !handle)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    fs_mutex_lock(&fs->lock);
    ext4_file *ef = (ext4_file *)handle;
    size_t cur = ext4fs_refresh_size_locked(fs, ef);
    int rc = 0;
    if ((uint64_t)length < (uint64_t)cur) {
        int r = ext4_ftruncate(ef, (uint64_t)length);
        rc = lwext4_to_vfs_err(r);
    } else if ((uint64_t)length > (uint64_t)cur) {
        rc = ext4fs_zero_extend_locked(fs, ef, (uint64_t)length);
    }
    fs_mutex_unlock(&fs->lock);
    return rc;
}

int ext4fs_chmod(ext4fs_t *fs, const char *subpath, uint32_t mode)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    int r = ext4_mode_set(full, mode & 07777U);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_chown(ext4fs_t *fs, const char *subpath, uint32_t uid, uint32_t gid)
{
    if (!fs || !fs->used || !subpath)
        return -EINVAL;
    if (fs->read_only)
        return -EROFS;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    int r = ext4_owner_set(full, uid, gid);
    fs_mutex_unlock(&fs->lock);
    return lwext4_to_vfs_err(r);
}

int ext4fs_readdir(ext4fs_t *fs, const char *subpath, char names[][VFS_NAME_MAX], int max)
{
    if (!fs || !fs->used || !subpath || !names || max <= 0)
        return -EINVAL;

    char full[VFS_PATH_MAX + 32];
    int rc = ext4fs_make_path(fs, subpath, full, sizeof(full));
    if (rc != 0)
        return rc;

    fs_mutex_lock(&fs->lock);
    ext4_dir dir;
    int r = ext4_dir_open(&dir, full);
    if (r != EOK) {
        fs_mutex_unlock(&fs->lock);
        return lwext4_to_vfs_err(r);
    }

    int count = 0;
    const ext4_direntry *de;
    while (count < max && (de = ext4_dir_entry_next(&dir)) != NULL) {
        if (de->name_length == 0)
            continue;
        if (de->name_length == 1 && de->name[0] == '.')
            continue;
        if (de->name_length == 2 && de->name[0] == '.' && de->name[1] == '.')
            continue;

        size_t nlen = de->name_length;
        if (nlen >= VFS_NAME_MAX)
            nlen = VFS_NAME_MAX - 1;
        k_memcpy(names[count], de->name, nlen);
        names[count][nlen] = '\0';
        count++;
    }

    ext4_dir_close(&dir);
    fs_mutex_unlock(&fs->lock);
    return count;
}
