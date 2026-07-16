/*
 * sysfs.c - Read-only system pseudo filesystem.
 *
 * Layout:
 *   /sys/memory
 *   /sys/devices/summary
 *   /sys/devices/pci
 *   /sys/mounts
 *   /sys/net/status
 *   /sys/net/stats
 */

#include "sysfs.h"

#include "../drv/ata.h"
#include "../drv/fb.h"
#include "../dev/pci.h"
#include "../ipc/shm.h"
#include "../mm/heap.h"
#include "../mm/pmm.h"
#include "../net/network.h"
#include "../proc/process.h"

#include <stddef.h>
#include <stdint.h>

#ifndef __x86_64__

void sysfs_init(void)
{
}

int sysfs_stat(const char *path, vfs_stat_t *out)
{
    (void)path;
    (void)out;
    return -1;
}

int sysfs_list(const char *path, char names[][VFS_NAME_MAX], int max)
{
    (void)path;
    (void)names;
    (void)max;
    return -1;
}

int sysfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out)
{
    (void)path;
    (void)buf_out;
    (void)size_out;
    return -1;
}

#else

typedef struct out_buf {
    char *data;
    size_t len;
    size_t cap;
} out_buf_t;

static int kstrlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int kstrcmp(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int kstreq(const char *a, const char *b)
{
    return kstrcmp(a, b) == 0;
}

static int kstrncpy(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return -1;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] ? -1 : 0;
}

static int out_reserve(out_buf_t *ob, size_t add)
{
    char *nb;
    size_t ncap;
    if (!ob)
        return -1;
    if (ob->len + add + 1 <= ob->cap)
        return 0;
    ncap = ob->cap ? ob->cap : 128;
    while (ncap < ob->len + add + 1)
        ncap *= 2;
    nb = (char *)kmalloc(ncap);
    if (!nb)
        return -1;
    for (size_t i = 0; i < ob->len; i++)
        nb[i] = ob->data[i];
    if (ob->data)
        kfree(ob->data);
    ob->data = nb;
    ob->cap = ncap;
    if (ob->len == 0)
        ob->data[0] = '\0';
    return 0;
}

static int out_append_str(out_buf_t *ob, const char *s)
{
    int slen = kstrlen(s);
    if (out_reserve(ob, (size_t)slen) != 0)
        return -1;
    for (int i = 0; i < slen; i++)
        ob->data[ob->len++] = s[i];
    ob->data[ob->len] = '\0';
    return 0;
}

static int out_append_u64(out_buf_t *ob, uint64_t v)
{
    char tmp[24];
    int i = 0;
    if (v == 0)
        return out_append_str(ob, "0");
    while (v > 0 && i < (int)(sizeof(tmp) - 1)) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (out_reserve(ob, (size_t)i) != 0)
        return -1;
    while (i > 0)
        ob->data[ob->len++] = tmp[--i];
    ob->data[ob->len] = '\0';
    return 0;
}

static int out_append_hex(out_buf_t *ob, uint64_t v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[18];
    int i;
    if (digits <= 0)
        digits = 1;
    if (digits > (int)sizeof(tmp) - 1)
        digits = (int)sizeof(tmp) - 1;

    for (i = digits - 1; i >= 0; i--) {
        tmp[i] = hex[v & 0xFu];
        v >>= 4u;
    }
    tmp[digits] = '\0';
    return out_append_str(ob, tmp);
}

void sysfs_init(void)
{
}

int sysfs_stat(const char *path, vfs_stat_t *out)
{
    if (!path || !out)
        return -1;
    out->type = VFS_TYPE_UNKNOWN;
    out->mode = VFS_MODE_READ;
    out->size = 0;
    out->blocks = 0;

    if (kstreq(path, "/")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (kstreq(path, "/devices") || kstreq(path, "/net")) {
        out->type = VFS_TYPE_DIR;
        return 0;
    }
    if (kstreq(path, "/memory") ||
        kstreq(path, "/devices/summary") ||
        kstreq(path, "/devices/pci") ||
        kstreq(path, "/mounts") ||
        kstreq(path, "/net/status") ||
        kstreq(path, "/net/stats")) {
        out->type = VFS_TYPE_FILE;
        return 0;
    }
    return -1;
}

int sysfs_list(const char *path, char names[][VFS_NAME_MAX], int max)
{
    if (!path || !names || max <= 0)
        return -1;
    if (kstreq(path, "/")) {
        if (max > 0) kstrncpy(names[0], "memory", VFS_NAME_MAX);
        if (max > 1) kstrncpy(names[1], "devices", VFS_NAME_MAX);
        if (max > 2) kstrncpy(names[2], "mounts", VFS_NAME_MAX);
        if (max > 3) kstrncpy(names[3], "net", VFS_NAME_MAX);
        return max >= 4 ? 4 : max;
    }
    if (kstreq(path, "/devices")) {
        if (max > 0) kstrncpy(names[0], "summary", VFS_NAME_MAX);
        if (max > 1) kstrncpy(names[1], "pci", VFS_NAME_MAX);
        return max >= 2 ? 2 : max;
    }
    if (kstreq(path, "/net")) {
        if (max > 0) kstrncpy(names[0], "status", VFS_NAME_MAX);
        if (max > 1) kstrncpy(names[1], "stats", VFS_NAME_MAX);
        return max >= 2 ? 2 : max;
    }
    return -1;
}

static int build_memory(out_buf_t *ob)
{
    heap_stats_t hs = {0};
    struct shm_stats ss = {0};
    size_t pc = 0;
    size_t pm = 0;
    size_t psp = 0;
    size_t psa = 0;

    heap_get_stats(&hs);
    shm_get_stats(&ss);
    process_get_memory_totals(&pc, &pm, &psp, &psa);

    if (out_append_str(ob, "pmm_total_pages: ") != 0) return -1;
    if (out_append_u64(ob, pmm_total_page_count()) != 0) return -1;
    if (out_append_str(ob, "\npmm_used_pages: ") != 0) return -1;
    if (out_append_u64(ob, pmm_used_page_count()) != 0) return -1;
    if (out_append_str(ob, "\npmm_free_pages: ") != 0) return -1;
    if (out_append_u64(ob, pmm_free_page_count()) != 0) return -1;

    if (out_append_str(ob, "\nheap_pool_bytes: ") != 0) return -1;
    if (out_append_u64(ob, hs.pool_bytes) != 0) return -1;
    if (out_append_str(ob, "\nheap_used_bytes: ") != 0) return -1;
    if (out_append_u64(ob, hs.allocated_bytes) != 0) return -1;
    if (out_append_str(ob, "\nheap_peak_bytes: ") != 0) return -1;
    if (out_append_u64(ob, hs.peak_allocated_bytes) != 0) return -1;

    if (out_append_str(ob, "\nprocess_count: ") != 0) return -1;
    if (out_append_u64(ob, pc) != 0) return -1;
    if (out_append_str(ob, "\nprocess_mapped_pages: ") != 0) return -1;
    if (out_append_u64(ob, pm) != 0) return -1;
    if (out_append_str(ob, "\nprocess_shm_pages: ") != 0) return -1;
    if (out_append_u64(ob, psp) != 0) return -1;
    if (out_append_str(ob, "\nprocess_shm_attachments: ") != 0) return -1;
    if (out_append_u64(ob, psa) != 0) return -1;

    if (out_append_str(ob, "\nshm_regions: ") != 0) return -1;
    if (out_append_u64(ob, ss.region_count) != 0) return -1;
    if (out_append_str(ob, "\nshm_attachments: ") != 0) return -1;
    if (out_append_u64(ob, ss.attachment_count) != 0) return -1;
    if (out_append_str(ob, "\nshm_reserved_pages: ") != 0) return -1;
    if (out_append_u64(ob, ss.reserved_pages) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;
    return 0;
}

static int build_devices(out_buf_t *ob)
{
    if (out_append_str(ob, "ata0.present: ") != 0) return -1;
    if (out_append_str(ob, ata_sector_count() ? "1" : "0") != 0) return -1;
    if (out_append_str(ob, "\nata0.sectors: ") != 0) return -1;
    if (out_append_u64(ob, ata_sector_count()) != 0) return -1;

    if (out_append_str(ob, "\nframebuffer.present: ") != 0) return -1;
    if (out_append_str(ob, fb_info.addr ? "1" : "0") != 0) return -1;
    if (out_append_str(ob, "\nframebuffer.width: ") != 0) return -1;
    if (out_append_u64(ob, fb_info.width) != 0) return -1;
    if (out_append_str(ob, "\nframebuffer.height: ") != 0) return -1;
    if (out_append_u64(ob, fb_info.height) != 0) return -1;
    if (out_append_str(ob, "\nframebuffer.bpp: ") != 0) return -1;
    if (out_append_u64(ob, fb_info.bpp) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;
    return 0;
}

static int out_append_ipv4(out_buf_t *ob, const uint8_t bytes[4])
{
    if (!bytes)
        return out_append_str(ob, "0.0.0.0");
    if (out_append_u64(ob, bytes[0]) != 0) return -1;
    if (out_append_str(ob, ".") != 0) return -1;
    if (out_append_u64(ob, bytes[1]) != 0) return -1;
    if (out_append_str(ob, ".") != 0) return -1;
    if (out_append_u64(ob, bytes[2]) != 0) return -1;
    if (out_append_str(ob, ".") != 0) return -1;
    if (out_append_u64(ob, bytes[3]) != 0) return -1;
    return 0;
}

static int out_append_mac(out_buf_t *ob, const uint8_t bytes[6])
{
    if (!bytes)
        return out_append_str(ob, "00:00:00:00:00:00");
    for (int i = 0; i < 6; i++) {
        if (out_append_hex(ob, bytes[i], 2) != 0)
            return -1;
        if (i != 5 && out_append_str(ob, ":") != 0)
            return -1;
    }
    return 0;
}

static int build_pci(out_buf_t *ob)
{
    int count = pci_device_count();
    if (out_append_str(ob, "pci.count: ") != 0) return -1;
    if (out_append_u64(ob, (uint64_t)count) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;

    for (int i = 0; i < count; i++) {
        const pci_device_info_t *dev = pci_device_at(i);
        if (!dev)
            continue;
        if (out_append_str(ob, "pci.") != 0) return -1;
        if (out_append_u64(ob, (uint64_t)i) != 0) return -1;
        if (out_append_str(ob, ": bus=") != 0) return -1;
        if (out_append_u64(ob, dev->bus) != 0) return -1;
        if (out_append_str(ob, " dev=") != 0) return -1;
        if (out_append_u64(ob, dev->device) != 0) return -1;
        if (out_append_str(ob, " fn=") != 0) return -1;
        if (out_append_u64(ob, dev->function) != 0) return -1;
        if (out_append_str(ob, " vendor=0x") != 0) return -1;
        if (out_append_hex(ob, dev->vendor_id, 4) != 0) return -1;
        if (out_append_str(ob, " device=0x") != 0) return -1;
        if (out_append_hex(ob, dev->device_id, 4) != 0) return -1;
        if (out_append_str(ob, " class=0x") != 0) return -1;
        if (out_append_hex(ob, dev->class_code, 2) != 0) return -1;
        if (out_append_str(ob, ".") != 0) return -1;
        if (out_append_hex(ob, dev->subclass, 2) != 0) return -1;
        if (out_append_str(ob, " if=0x") != 0) return -1;
        if (out_append_hex(ob, dev->prog_if, 2) != 0) return -1;
        if (out_append_str(ob, " irq=") != 0) return -1;
        if (out_append_u64(ob, dev->irq_line) != 0) return -1;
        if (out_append_str(ob, " driver=") != 0) return -1;
        if (out_append_str(ob, dev->bound_driver[0] ? dev->bound_driver : "none") != 0) return -1;
        if (out_append_str(ob, "\n") != 0) return -1;
    }
    return 0;
}

static int build_net_status(out_buf_t *ob)
{
    net_link_info_t info;
    int rc;
    rc = network_get_link_info(&info);
    if (out_append_str(ob, "stack.initialized: ") != 0) return -1;
    if (out_append_u64(ob, (uint64_t)network_is_initialized()) != 0) return -1;
    if (out_append_str(ob, "\nstack.has_ip: ") != 0) return -1;
    if (out_append_u64(ob, (uint64_t)network_has_ipv4()) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;

    if (rc != 0) {
        if (out_append_str(ob, "nic.present: 0\n") != 0) return -1;
        return 0;
    }

    if (out_append_str(ob, "nic.present: 1\nnic.driver: ") != 0) return -1;
    if (out_append_str(ob, info.nic_name[0] ? info.nic_name : "unknown") != 0) return -1;
    if (out_append_str(ob, "\nnic.link_up: ") != 0) return -1;
    if (out_append_u64(ob, info.link_up) != 0) return -1;
    if (out_append_str(ob, "\nnic.mac: ") != 0) return -1;
    if (out_append_mac(ob, info.mac.bytes) != 0) return -1;
    if (out_append_str(ob, "\nnic.ipv4: ") != 0) return -1;
    if (out_append_ipv4(ob, info.ip.bytes) != 0) return -1;
    if (out_append_str(ob, "\nnic.gateway: ") != 0) return -1;
    if (out_append_ipv4(ob, info.gateway.bytes) != 0) return -1;
    if (out_append_str(ob, "\nnic.dns: ") != 0) return -1;
    if (out_append_ipv4(ob, info.dns.bytes) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;
    return 0;
}

static int build_net_stats(out_buf_t *ob)
{
    net_runtime_stats_t st;
    if (network_get_stats(&st) != 0)
        return -1;
    if (out_append_str(ob, "tx_packets: ") != 0) return -1;
    if (out_append_u64(ob, st.tx_packets) != 0) return -1;
    if (out_append_str(ob, "\ntx_bytes: ") != 0) return -1;
    if (out_append_u64(ob, st.tx_bytes) != 0) return -1;
    if (out_append_str(ob, "\nrx_packets: ") != 0) return -1;
    if (out_append_u64(ob, st.rx_packets) != 0) return -1;
    if (out_append_str(ob, "\nrx_bytes: ") != 0) return -1;
    if (out_append_u64(ob, st.rx_bytes) != 0) return -1;
    if (out_append_str(ob, "\nrx_dropped: ") != 0) return -1;
    if (out_append_u64(ob, st.rx_dropped) != 0) return -1;
    if (out_append_str(ob, "\nirq_count: ") != 0) return -1;
    if (out_append_u64(ob, st.irq_count) != 0) return -1;
    if (out_append_str(ob, "\nrx_poll_calls: ") != 0) return -1;
    if (out_append_u64(ob, st.rx_poll_calls) != 0) return -1;
    if (out_append_str(ob, "\nstack_initialized: ") != 0) return -1;
    if (out_append_u64(ob, st.stack_initialized) != 0) return -1;
    if (out_append_str(ob, "\nhas_ip: ") != 0) return -1;
    if (out_append_u64(ob, st.has_ip) != 0) return -1;
    if (out_append_str(ob, "\n") != 0) return -1;
    return 0;
}

static int build_mounts(out_buf_t *ob)
{
    vfs_mount_info_t mounts[16];
    int n = vfs_get_mounts(mounts, 16);
    if (n < 0)
        return -1;
    for (int i = 0; i < n; i++) {
        if (out_append_str(ob, mounts[i].path) != 0) return -1;
        if (out_append_str(ob, " fs=") != 0) return -1;
        if (out_append_str(ob, mounts[i].fs_name) != 0) return -1;
        if (out_append_str(ob, " ro=") != 0) return -1;
        if (out_append_str(ob, mounts[i].read_only ? "1" : "0") != 0) return -1;
        if (out_append_str(ob, "\n") != 0) return -1;
    }
    return 0;
}

int sysfs_read_file(const char *path, uint8_t **buf_out, size_t *size_out)
{
    out_buf_t ob;
    int rc = -1;

    if (!path || !buf_out || !size_out)
        return -1;

    ob.data = NULL;
    ob.len = 0;
    ob.cap = 0;

    if (kstreq(path, "/memory"))
        rc = build_memory(&ob);
    else if (kstreq(path, "/devices") || kstreq(path, "/devices/summary"))
        rc = build_devices(&ob);
    else if (kstreq(path, "/devices/pci"))
        rc = build_pci(&ob);
    else if (kstreq(path, "/mounts"))
        rc = build_mounts(&ob);
    else if (kstreq(path, "/net/status"))
        rc = build_net_status(&ob);
    else if (kstreq(path, "/net/stats"))
        rc = build_net_stats(&ob);
    else
        rc = -1;

    if (rc != 0) {
        if (ob.data)
            kfree(ob.data);
        return -1;
    }

    if (!ob.data) {
        ob.data = (char *)kmalloc(1);
        if (!ob.data)
            return -1;
        ob.data[0] = '\0';
    }

    *buf_out = (uint8_t *)ob.data;
    *size_out = ob.len;
    return 0;
}

#endif /* __x86_64__ */
