/*
 * Project Tsukasa — Linear framebuffer driver
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

#include "fb.h"
#include "../include/multiboot.h"
#include "../include/boot_info.h"
#include "../include/vfs_abi.h"
#include "../include/errno.h"
#include "../include/paging.h"
#include "../mm/vmm_x64.h"
#include "../mm/vm_space.h"
#include "../proc/process.h"
#include <stddef.h>
#include <stdint.h>

#define VFS_FB_USER_BASE 0x0000000030000000ULL

struct fb_info fb_info = { NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static int g_kd_mode = VFS_KD_TEXT;

int fb_kd_mode(void)
{
    return g_kd_mode;
}

void fb_set_kd_mode(int mode)
{
    g_kd_mode = mode;
}

int fb_init(const void *mb_info)
{
    if (tsukasa_boot_info_is_valid(mb_info)) {
        const struct tsukasa_boot_info *bi =
            (const struct tsukasa_boot_info *)mb_info;
        uintptr_t fb_virt = vmm_phys_to_virt(bi->framebuffer_addr);

        fb_info.addr = (void *)fb_virt;
        fb_info.phys = (uintptr_t)bi->framebuffer_addr;
        fb_info.pitch = bi->framebuffer_pitch;
        fb_info.width = bi->framebuffer_width;
        fb_info.height = bi->framebuffer_height;
        fb_info.bpp = bi->framebuffer_bpp;
        fb_info.red_mask_size = bi->framebuffer_red_mask_size ? bi->framebuffer_red_mask_size : 8;
        fb_info.red_mask_shift = bi->framebuffer_red_mask_size ? bi->framebuffer_red_mask_shift : 16;
        fb_info.green_mask_size = bi->framebuffer_green_mask_size ? bi->framebuffer_green_mask_size : 8;
        fb_info.green_mask_shift = bi->framebuffer_green_mask_size ? bi->framebuffer_green_mask_shift : 8;
        fb_info.blue_mask_size = bi->framebuffer_blue_mask_size ? bi->framebuffer_blue_mask_size : 8;
        fb_info.blue_mask_shift = bi->framebuffer_blue_mask_size ? bi->framebuffer_blue_mask_shift : 0;

        if (!fb_info.addr || fb_info.width == 0 || fb_info.height == 0)
            return -1;

        return 0;
    }

    const struct multiboot_info *mb = (const struct multiboot_info *)mb_info;

    if (!mb || !(mb->flags & MULTIBOOT_INFO_FRAMEBUFFER))
        return -1;

    fb_info.addr = (void *)(uintptr_t)(mb->framebuffer_addr & 0xFFFFFFFFu);
    fb_info.phys = (uintptr_t)(mb->framebuffer_addr & 0xFFFFFFFFu);
    fb_info.pitch = mb->framebuffer_pitch;
    fb_info.width = mb->framebuffer_width;
    fb_info.height = mb->framebuffer_height;
    fb_info.bpp = mb->framebuffer_bpp;
    fb_info.red_mask_size = 8;
    fb_info.red_mask_shift = 16;
    fb_info.green_mask_size = 8;
    fb_info.green_mask_shift = 8;
    fb_info.blue_mask_size = 8;
    fb_info.blue_mask_shift = 0;

    if (!fb_info.addr || fb_info.width == 0 || fb_info.height == 0)
        return -1;

    return 0;
}

void *fb_addr(void)
{
    return fb_info.addr;
}

uint8_t fb_bpp(void)
{
    return fb_info.bpp;
}

size_t fb_byte_size(void)
{
    if (!fb_info.addr || fb_info.pitch == 0 || fb_info.height == 0)
        return 0;
    return (size_t)fb_info.pitch * (size_t)fb_info.height;
}

size_t fb_read(size_t pos, void *buf, size_t count)
{
    size_t size = fb_byte_size();
    uint8_t *src = (uint8_t *)fb_info.addr;
    if (!src || !buf || pos >= size)
        return 0;
    if (count > size - pos)
        count = size - pos;
    for (size_t i = 0; i < count; i++)
        ((uint8_t *)buf)[i] = src[pos + i];
    return count;
}

size_t fb_write(size_t pos, const void *buf, size_t count)
{
    size_t size = fb_byte_size();
    uint8_t *dst = (uint8_t *)fb_info.addr;
    if (!dst || !buf || pos >= size)
        return 0;
    if (count > size - pos)
        count = size - pos;
    for (size_t i = 0; i < count; i++)
        dst[pos + i] = ((const uint8_t *)buf)[i];
    return count;
}

int fb_ioctl(unsigned long request, void *arg)
{
    if (request == VFS_KDSETMODE) {
        uintptr_t raw = (uintptr_t)arg;
        int mode;
        if (raw <= 0xFFFFu) {
            mode = (int)raw;
        } else {
            if (!vmm_validate_user_ptr(arg, sizeof(int), 0))
                return -EFAULT;
            mode = *(int *)arg;
        }
        if (mode != VFS_KD_TEXT && mode != VFS_KD_GRAPHICS)
            return -EINVAL;
        g_kd_mode = mode;
        return 0;
    }

    if (!fb_info.addr)
        return -ENODEV;

    switch (request) {
    case VFS_FBIOGET_VSCREENINFO: {
        if (!arg)
            return -EFAULT;
        if (!vmm_validate_user_ptr(arg, sizeof(vfs_fb_var_screeninfo_t), 1))
            return -EFAULT;

        vfs_fb_var_screeninfo_t *v = (vfs_fb_var_screeninfo_t *)arg;
        for (size_t i = 0; i < sizeof(*v); i++)
            ((uint8_t *)v)[i] = 0;

        v->xres = fb_info.width;
        v->yres = fb_info.height;
        v->xres_virtual = fb_info.width;
        v->yres_virtual = fb_info.height;
        v->xoffset = 0;
        v->yoffset = 0;
        v->bits_per_pixel = fb_info.bpp;
        v->grayscale = 0;

        v->red.length = fb_info.red_mask_size ? fb_info.red_mask_size : 8;
        v->red.offset = fb_info.red_mask_shift ? fb_info.red_mask_shift : 16;
        v->red.msb_right = 0;

        v->green.length = fb_info.green_mask_size ? fb_info.green_mask_size : 8;
        v->green.offset = fb_info.green_mask_shift ? fb_info.green_mask_shift : 8;
        v->green.msb_right = 0;

        v->blue.length = fb_info.blue_mask_size ? fb_info.blue_mask_size : 8;
        v->blue.offset = fb_info.blue_mask_shift ? fb_info.blue_mask_shift : 0;
        v->blue.msb_right = 0;

        v->transp.length = 8;
        v->transp.offset = 24;
        v->transp.msb_right = 0;

        v->height = fb_info.height;
        v->width = fb_info.width;
        return 0;
    }
    case VFS_FBIOGET_FSCREENINFO: {
        if (!arg)
            return -EFAULT;
        if (!vmm_validate_user_ptr(arg, sizeof(vfs_fb_fix_screeninfo_t), 1))
            return -EFAULT;

        vfs_fb_fix_screeninfo_t *f = (vfs_fb_fix_screeninfo_t *)arg;
        const char id[] = "tsukasa-fb";
        for (size_t i = 0; i < sizeof(f->id); i++)
            f->id[i] = '\0';
        for (size_t i = 0; i < sizeof(id) && i < sizeof(f->id) - 1; i++)
            f->id[i] = id[i];

        f->smem_start = fb_info.phys;
        f->smem_len = (uint32_t)fb_byte_size();
        f->type = VFS_FB_TYPE_PACKED_PIXELS;
        f->visual = VFS_FB_VISUAL_TRUECOLOR;
        f->line_length = fb_info.pitch;
        return 0;
    }
    default:
        return -EINVAL;
    }
}

void *fb_mmap(void *addr, size_t length, int prot, int flags, size_t offset)
{
    process_t *proc = process_current();
    size_t size = fb_byte_size();

    if (!fb_info.addr || length == 0)
        return (void *)-1;
    if (!(flags & VFS_MAP_SHARED) || !(prot & (VFS_PROT_READ | VFS_PROT_WRITE)))
        return (void *)-1;
    if (offset >= size || length > size - offset)
        return (void *)-1;

#ifdef __x86_64__
    if (proc && proc->vm_space.owns_pml4 && fb_info.phys) {
        uintptr_t target_va;
        if (addr != NULL && paging_is_page_aligned_uintptr((uintptr_t)addr) &&
            paging_range_is_user((uintptr_t)addr, length)) {
            target_va = (uintptr_t)addr;
        } else {
            target_va = (uintptr_t)VFS_FB_USER_BASE;
        }

        uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
        size_t page_count = (size_t)((length + page_mask) / PAGE_SIZE);
        uint64_t q_phys = 0;
        uint64_t q_flags = 0;

        if (vmm_query_page(proc->vm_space.pml4_phys, target_va, &q_phys, &q_flags) == 0 &&
            (q_flags & VMM_X64_PTE_PRESENT)) {
            return (void *)(target_va + (offset & page_mask));
        }

        uint64_t phys_start = fb_info.phys + (offset & ~page_mask);
        if (vm_space_map_user_pages(&proc->vm_space,
                                    target_va,
                                    phys_start,
                                    page_count,
                                    PAGING_MAP_READ | PAGING_MAP_WRITE |
                                    PAGING_MAP_USER) != 0) {
            return (void *)-1;
        }
        return (void *)(target_va + (offset & page_mask));
    }
#endif
    return (void *)((uintptr_t)fb_info.addr + offset);
}

int fb_munmap(void *addr, size_t length)
{
    uintptr_t p = (uintptr_t)addr;
    size_t size = fb_byte_size();
    if (!addr || length == 0 || !fb_info.addr)
        return -1;

#ifdef __x86_64__
    process_t *proc = process_current();
    if (proc && proc->vm_space.owns_pml4 &&
        ((p == (uintptr_t)VFS_FB_USER_BASE && length <= size) ||
         paging_range_is_user(p, length))) {
        uintptr_t page_mask = (uintptr_t)PAGE_SIZE - 1;
        size_t page_count = (size_t)((length + page_mask) / PAGE_SIZE);
        return vm_space_unmap_user_pages(&proc->vm_space, p & ~page_mask, page_count);
    }
#endif
    return 0;
}

int fb_poll(int events)
{
    if (fb_info.addr)
        return events & (VFS_POLLIN | VFS_POLLOUT);
    return 0;
}
