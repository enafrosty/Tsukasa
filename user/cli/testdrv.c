/*
 * Project Tsukasa — In-Guest Automated Test Suite Runner Daemon
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

#include "tsukasa_sdk.h"
#include <stdint.h>

#define TSK_WIFEXITED(status)   (((status) & 0x7f) == 0)
#define TSK_WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define TSK_WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define TSK_WTERMSIG(status)    ((status) & 0x7f)
#define TSK_SIGSEGV             11
#define TSK_EINVAL_RC           (-22)

struct test_spawn_request {
    const char *path;
    const char *args;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int tty_id;
};

static inline long sys_spawn(const char *path, const char *args)
{
    register long r10 __asm__("r10") = 0;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(317L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)args), "d"(0L), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_spawn_ex(const struct test_spawn_request *req)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(318L), "D"((long)(uintptr_t)req)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_wait4(long pid, int *wstatus, long options)
{
    register long r10 __asm__("r10") = 0;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(61L), "D"(pid), "S"((long)(uintptr_t)wstatus), "d"(options), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_poweroff(void)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(169L), "D"(0L), "S"(0L), "d"(1L)
                      : "rcx", "r11", "memory");
    (void)ret;
}

static inline long sys_stat(const char *path, void *st)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(4L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)st), "d"(0L)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_mkdir(const char *path, long mode)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(83L), "D"((long)(uintptr_t)path), "S"(mode)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_rmdir(const char *path)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(84L), "D"((long)(uintptr_t)path)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_unlink(const char *path)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(87L), "D"((long)(uintptr_t)path)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_rename(const char *oldpath, const char *newpath)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(82L), "D"((long)(uintptr_t)oldpath), "S"((long)(uintptr_t)newpath)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_lstat(const char *path, void *st)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(6L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)st), "d"(0L)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_truncate(const char *path, long length)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(76L), "D"((long)(uintptr_t)path), "S"(length)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_ftruncate(int fd, long length)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(77L), "D"((long)fd), "S"(length)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_symlink(const char *target, const char *linkpath)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(88L), "D"((long)(uintptr_t)target), "S"((long)(uintptr_t)linkpath)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_readlink(const char *path, char *buf, long bufsiz)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(89L), "D"((long)(uintptr_t)path), "S"((long)(uintptr_t)buf), "d"(bufsiz)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_chmod(const char *path, long mode)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(90L), "D"((long)(uintptr_t)path), "S"(mode)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_chown(const char *path, long uid, long gid)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(92L), "D"((long)(uintptr_t)path), "S"(uid), "d"(gid)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_mount(const char *source, const char *target,
                             const char *fstype, unsigned long flags)
{
    register long r10 __asm__("r10") = (long)flags;
    register long r8  __asm__("r8")  = 0;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(165L), "D"((long)(uintptr_t)source),
                        "S"((long)(uintptr_t)target),
                        "d"((long)(uintptr_t)fstype), "r"(r10), "r"(r8)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_umount2(const char *target, int flags)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(166L), "D"((long)(uintptr_t)target), "S"((long)flags)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_getdents64(int fd, void *dirp, long count)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(217L), "D"((long)fd), "S"((long)(uintptr_t)dirp), "d"(count)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_nanosleep(long ms)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(35L), "D"(ms)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long sys_ticks(void)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(320L)
                      : "rcx", "r11", "memory");
    return ret;
}

static int wait_for_pid(long pid, int *wstatus_out)
{
    int status = 0;
    for (int fast = 0; fast < 64; fast++) {
        long ret = sys_wait4(pid, &status, 1 /* WNOHANG */);
        if (ret == pid) {
            if (wstatus_out)
                *wstatus_out = status;
            return 0;
        }
        sched_yield();
    }
    for (int wait_loop = 0; wait_loop < 3000; wait_loop++) {
        long ret = sys_wait4(pid, &status, 1 /* WNOHANG */);
        if (ret == pid) {
            if (wstatus_out)
                *wstatus_out = status;
            return 0;
        }
        (void)sys_nanosleep(10);
    }
    return -1;
}

static int spawn_and_wait(const char *path, const char *args,
                          int *wstatus_out, long *detail_out)
{
    long pid = -1;
    for (int retry = 0; retry < 50 && pid < 0; retry++) {
        pid = sys_spawn(path, args);
        if (pid < 0)
            sched_yield();
    }
    if (pid <= 0) {
        if (detail_out)
            *detail_out = pid;
        return -1;
    }

    if (wait_for_pid(pid, wstatus_out) != 0) {
        if (detail_out)
            *detail_out = pid;
        return -2;
    }

    if (detail_out)
        *detail_out = pid;
    return 0;
}

static void test_spawn_argv(void)
{
    int status = 0;
    long detail = 0;
    int rc = spawn_and_wait("/bin/argvchk.elf", "argvchk alpha beta gamma",
                            &status, &detail);
    if (rc == -1) {
        dprintf(1, "[TEST] spawn_argv FAIL: SYS_spawn failed (rc=%ld)\n", detail);
        return;
    }
    if (rc == -2) {
        dprintf(1, "[TEST] spawn_argv FAIL: wait timeout (pid=%ld)\n", detail);
        return;
    }
    if (!TSK_WIFEXITED(status) || TSK_WEXITSTATUS(status) != 42) {
        dprintf(1, "[TEST] spawn_argv FAIL: expected exit 42, status=0x%x code=%d\n",
                (unsigned)status, TSK_WEXITSTATUS(status));
        return;
    }

    struct test_spawn_request req;
    req.path = "/bin/exit7.elf";
    req.args = "/bin/exit7.elf";
    req.stdin_fd = -1;
    req.stdout_fd = -1;
    req.stderr_fd = -1;
    req.tty_id = -1;

    long pid = -1;
    for (int retry = 0; retry < 50 && pid < 0; retry++) {
        pid = sys_spawn_ex(&req);
        if (pid < 0)
            sched_yield();
    }
    if (pid <= 0) {
        dprintf(1, "[TEST] spawn_argv FAIL: SYS_spawn_ex failed (rc=%ld)\n", pid);
        return;
    }
    status = 0;
    if (wait_for_pid(pid, &status) != 0) {
        dprintf(1, "[TEST] spawn_argv FAIL: SYS_spawn_ex timeout (pid=%ld)\n", pid);
        return;
    }
    if (!TSK_WIFEXITED(status) || TSK_WEXITSTATUS(status) != 7) {
        dprintf(1, "[TEST] spawn_argv FAIL: SYS_spawn_ex status=0x%x code=%d\n",
                (unsigned)status, TSK_WEXITSTATUS(status));
        return;
    }

    dprintf(1, "[TEST] spawn_argv PASS\n");
}

static void test_pmm_reclaim(void)
{
    for (int i = 0; i < 25; i++) {
        int status = 0;
        long detail = 0;
        int rc = spawn_and_wait("/bin/exit7.elf", NULL, &status, &detail);
        if (rc == -1) {
            dprintf(1, "[TEST] pmm_reclaim FAIL: iter %d spawn failed (rc=%ld)\n",
                    i, detail);
            return;
        }
        if (rc == -2) {
            dprintf(1, "[TEST] pmm_reclaim FAIL: iter %d wait timeout (pid=%ld)\n",
                    i, detail);
            return;
        }
        if (!TSK_WIFEXITED(status) || TSK_WEXITSTATUS(status) != 7) {
            dprintf(1, "[TEST] pmm_reclaim FAIL: iter %d expected exit 7, got %d\n",
                    i, TSK_WEXITSTATUS(status));
            return;
        }
    }

    dprintf(1, "[TEST] pmm_reclaim PASS\n");
}

static void test_munmap_safety(void)
{
    void *p = mmap(NULL, 4096, VFS_PROT_READ | VFS_PROT_WRITE,
                   VFS_MAP_PRIVATE | VFS_MAP_ANONYMOUS, -1, 0);
    if (!p || (long)(uintptr_t)p < 0) {
        dprintf(1, "[TEST] munmap_safety FAIL: initial mmap failed\n");
        return;
    }

    volatile uint32_t *w = (volatile uint32_t *)p;
    *w = 0xCAFEBABEu;
    if (*w != 0xCAFEBABEu) {
        dprintf(1, "[TEST] munmap_safety FAIL: sentinel mismatch\n");
        (void)munmap(p, 4096);
        return;
    }

    int urc = munmap(p, 8192);
    if (urc != 0 && urc != TSK_EINVAL_RC) {
        dprintf(1, "[TEST] munmap_safety FAIL: unexpected munmap rc=%d\n", urc);
        return;
    }

    void *p2 = mmap(NULL, 4096, VFS_PROT_READ | VFS_PROT_WRITE,
                    VFS_MAP_PRIVATE | VFS_MAP_ANONYMOUS, -1, 0);
    if (!p2 || (long)(uintptr_t)p2 < 0) {
        dprintf(1, "[TEST] munmap_safety FAIL: post-munmap mmap failed\n");
        return;
    }

    volatile uint32_t *w2 = (volatile uint32_t *)p2;
    *w2 = 0x12345678u;
    if (*w2 != 0x12345678u) {
        dprintf(1, "[TEST] munmap_safety FAIL: post-munmap readback mismatch\n");
        (void)munmap(p2, 4096);
        return;
    }

    if (munmap(p2, 4096) != 0) {
        dprintf(1, "[TEST] munmap_safety FAIL: cleanup munmap failed\n");
        return;
    }

    dprintf(1, "[TEST] munmap_safety PASS\n");
}

static void test_urandom_entropy(void)
{
    int fd = open("/dev/urandom", TSK_O_RDONLY);
    if (fd < 0) {
        dprintf(1, "[TEST] urandom_entropy FAIL: open /dev/urandom failed (rc=%d)\n", fd);
        return;
    }

    uint8_t buf1[16];
    uint8_t buf2[16];
    long r1 = read(fd, buf1, sizeof(buf1));
    long r2 = read(fd, buf2, sizeof(buf2));
    close(fd);

    if (r1 != (long)sizeof(buf1) || r2 != (long)sizeof(buf2)) {
        dprintf(1, "[TEST] urandom_entropy FAIL: short read (r1=%ld r2=%ld)\n", r1, r2);
        return;
    }

    int nz1 = 0;
    int nz2 = 0;
    int diff = 0;
    for (size_t i = 0; i < sizeof(buf1); i++) {
        if (buf1[i] != 0)
            nz1 = 1;
        if (buf2[i] != 0)
            nz2 = 1;
        if (buf1[i] != buf2[i])
            diff = 1;
    }

    if (!nz1 || !nz2) {
        dprintf(1, "[TEST] urandom_entropy FAIL: all-zero entropy buffer\n");
        return;
    }
    if (!diff) {
        dprintf(1, "[TEST] urandom_entropy FAIL: consecutive reads identical\n");
        return;
    }

    dprintf(1, "[TEST] urandom_entropy PASS\n");
}

static void test_vfs_tmp_crud(void)
{
    static const char dir[] = "/tmp/tskcrud";
    static const char file[] = "/tmp/tskcrud/data.txt";
    static const char payload[] = "tsukasa-tmp-crud";
    const size_t payload_len = sizeof(payload) - 1;
    char rbuf[32];

    (void)sys_unlink(file);

    long mkrc = sys_mkdir(dir, 0755);
    if (mkrc != 0) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: mkdir failed (rc=%ld)\n", mkrc);
        return;
    }

    int fd = open(file, TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: create failed (rc=%d)\n", fd);
        (void)sys_rmdir(dir);
        return;
    }

    long wr = write(fd, payload, payload_len);
    close(fd);
    if (wr != (long)payload_len) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: write count mismatch (wr=%ld)\n", wr);
        (void)sys_unlink(file);
        (void)sys_rmdir(dir);
        return;
    }

    fd = open(file, TSK_O_RDONLY);
    if (fd < 0) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: reopen failed (rc=%d)\n", fd);
        (void)sys_unlink(file);
        (void)sys_rmdir(dir);
        return;
    }

    long rd = read(fd, rbuf, sizeof(rbuf));
    close(fd);
    if (rd != (long)payload_len) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: read count mismatch (rd=%ld)\n", rd);
        (void)sys_unlink(file);
        (void)sys_rmdir(dir);
        return;
    }

    for (size_t i = 0; i < payload_len; i++) {
        if (rbuf[i] != payload[i]) {
            dprintf(1, "[TEST] vfs_tmp_crud FAIL: content mismatch at byte %u\n",
                    (unsigned)i);
            (void)sys_unlink(file);
            (void)sys_rmdir(dir);
            return;
        }
    }

    long urc = sys_unlink(file);
    if (urc != 0) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: unlink failed (rc=%ld)\n", urc);
        (void)sys_rmdir(dir);
        return;
    }

    fd = open(file, TSK_O_RDONLY);
    if (fd >= 0) {
        close(fd);
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: file still accessible after unlink\n");
        return;
    }

    long rmrc = sys_rmdir(dir);
    if (rmrc != 0) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: rmdir unexpected rc=%ld\n", rmrc);
        return;
    }

    dprintf(1, "[TEST] vfs_tmp_crud PASS\n");
}

static void test_tsh_pipeline(void)
{
    static const char script_path[] = "/tmp/pipe_test.sh";
    static const char out_path[] = "/tmp/pipe_out.txt";
    static const char script[] = "echo pipe_magic_42 | /bin/cat.elf > /tmp/pipe_out.txt\n";
    static const char expected[] = "pipe_magic_42";
    const size_t script_len = sizeof(script) - 1;
    const size_t expected_len = sizeof(expected) - 1;
    char rbuf[64];

    (void)sys_unlink(script_path);
    (void)sys_unlink(out_path);

    int fd = open(script_path, TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0) {
        dprintf(1, "[TEST] tsh_pipeline FAIL: create script failed (rc=%d)\n", fd);
        return;
    }

    long wr = write(fd, script, script_len);
    int crc = close(fd);
    if (wr != (long)script_len) {
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: write script mismatch (wr=%ld)\n", wr);
        return;
    }
    if (crc != 0) {
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: close script failed (rc=%d)\n", crc);
        return;
    }

    int status = 0;
    long detail = 0;
    int rc = spawn_and_wait("/bin/tsh", "tsh /tmp/pipe_test.sh",
                            &status, &detail);
    if (rc == -1) {
        (void)sys_unlink(out_path);
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: SYS_spawn failed (rc=%ld)\n", detail);
        return;
    }
    if (rc == -2) {
        (void)sys_unlink(out_path);
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: wait timeout (pid=%ld)\n", detail);
        return;
    }
    if (!TSK_WIFEXITED(status) || TSK_WEXITSTATUS(status) != 0) {
        (void)sys_unlink(out_path);
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: expected exit 0, status=0x%x code=%d\n",
                (unsigned)status, TSK_WEXITSTATUS(status));
        return;
    }

    fd = open(out_path, TSK_O_RDONLY);
    if (fd < 0) {
        (void)sys_unlink(out_path);
        (void)sys_unlink(script_path);
        dprintf(1, "[TEST] tsh_pipeline FAIL: open output failed (rc=%d)\n", fd);
        return;
    }

    long n = 0;
    while (n < (long)sizeof(rbuf)) {
        long r = read(fd, rbuf + n, sizeof(rbuf) - (size_t)n);
        if (r < 0) {
            (void)close(fd);
            (void)sys_unlink(out_path);
            (void)sys_unlink(script_path);
            dprintf(1, "[TEST] tsh_pipeline FAIL: read output failed (rc=%ld)\n", r);
            return;
        }
        if (r == 0)
            break;
        n += r;
    }

    int out_crc = close(fd);
    (void)sys_unlink(out_path);
    (void)sys_unlink(script_path);

    if (out_crc != 0) {
        dprintf(1, "[TEST] tsh_pipeline FAIL: close output failed (rc=%d)\n", out_crc);
        return;
    }

    if (n < (long)expected_len) {
        dprintf(1, "[TEST] tsh_pipeline FAIL: short output (n=%ld)\n", n);
        return;
    }

    for (size_t i = 0; i < expected_len; i++) {
        if (rbuf[i] != expected[i]) {
            dprintf(1, "[TEST] tsh_pipeline FAIL: output mismatch at byte %u\n",
                    (unsigned)i);
            return;
        }
    }

    dprintf(1, "[TEST] tsh_pipeline PASS\n");
}

static void test_ring3_fault_isolation(void)
{
    int status = 0;
    long detail = 0;
    int rc = spawn_and_wait("/bin/fault.elf", NULL, &status, &detail);
    if (rc == -1) {
        dprintf(1, "[TEST] ring3_fault_isolation FAIL: SYS_spawn failed (rc=%ld)\n", detail);
        return;
    }
    if (rc == -2) {
        dprintf(1, "[TEST] ring3_fault_isolation FAIL: wait timeout (pid=%ld)\n", detail);
        return;
    }
    if (TSK_WIFEXITED(status) || !TSK_WIFSIGNALED(status) ||
        TSK_WTERMSIG(status) != TSK_SIGSEGV) {
        dprintf(1, "[TEST] ring3_fault_isolation FAIL: expected SIGSEGV, status=0x%x sig=%d\n",
                (unsigned)status, TSK_WTERMSIG(status));
        return;
    }

    status = 0;
    detail = 0;
    rc = spawn_and_wait("/bin/exit7.elf", NULL, &status, &detail);
    if (rc != 0 || !TSK_WIFEXITED(status) || TSK_WEXITSTATUS(status) != 7) {
        dprintf(1, "[TEST] ring3_fault_isolation FAIL: post-fault spawn rc=%d status=0x%x\n",
                rc, (unsigned)status);
        return;
    }

    dprintf(1, "[TEST] ring3_fault_isolation PASS\n");
}

static int has_substr(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0])
        return 0;
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (needle[j] && haystack[i + j] == needle[j])
            j++;
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

static long read_self_ticks(void)
{
    int fd = open("/proc/self/status", TSK_O_RDONLY);
    char buf[512];
    long n;
    if (fd < 0)
        return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';

    const char *key = "ticks: ";
    for (long i = 0; i < n; i++) {
        int match = (i == 0 || buf[i - 1] == '\n');
        size_t j = 0;
        if (!match)
            continue;
        while (key[j] && (i + (long)j) < n && buf[i + (long)j] == key[j])
            j++;
        if (key[j] == '\0') {
            long val = 0;
            const char *p = buf + i + j;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
            }
            return val;
        }
    }
    return -1;
}

static void test_yield_no_tick_forge(void)
{
    long t0 = sys_ticks();
    for (int i = 0; i < 200; i++)
        sched_yield();
    long dt = sys_ticks() - t0;
    if (dt < 0 || dt >= 25) {
        dprintf(1, "[TEST] yield_no_tick_forge FAIL: dt=%ld\n", dt);
        return;
    }
    dprintf(1, "[TEST] yield_no_tick_forge PASS\n");
}

static void test_nanosleep_blocking(void)
{
    long t0 = sys_ticks();
    long p0 = read_self_ticks();
    long rc = sys_nanosleep(200);
    long dt = sys_ticks() - t0;
    long p1 = read_self_ticks();
    long pdt = (p0 >= 0 && p1 >= p0) ? (p1 - p0) : -1;

    if (rc != 0 || dt < 18 || dt > 80 || pdt < 0 || pdt > 3) {
        dprintf(1, "[TEST] nanosleep_blocking FAIL: rc=%ld dt=%ld pdt=%ld\n",
                rc, dt, pdt);
        return;
    }
    dprintf(1, "[TEST] nanosleep_blocking PASS\n");
}

static void test_proc_stat(void)
{
    int fd = open("/proc/stat", TSK_O_RDONLY);
    char buf[512];
    long n;
    if (fd < 0) {
        dprintf(1, "[TEST] proc_stat FAIL: open failed (rc=%d)\n", fd);
        return;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        dprintf(1, "[TEST] proc_stat FAIL: short read (n=%ld)\n", n);
        return;
    }
    buf[n] = '\0';
    if (!has_substr(buf, "cpu ") || !has_substr(buf, "cpu0 ")) {
        dprintf(1, "[TEST] proc_stat FAIL: missing cpu/cpu0 fields\n");
        return;
    }
    dprintf(1, "[TEST] proc_stat PASS\n");
}

struct test_vfs_stat {
    uint64_t size;
    uint32_t type;
    uint32_t mode;
    uint64_t blocks;
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

static void test_initrd_rootfs_layout(void)
{
    static const char *const required[] = {
        "/bin/ls",
        "/bin/reboot",
        "/bin/ps",
        "/etc/vanilla/themes/nord-dark.ini",
        "/usr/share/vanilla/cursors/arrow.argb",
        "/etc/services.conf",
    };

    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        struct test_vfs_stat st;
        if (sys_stat(required[i], &st) != 0 || st.size == 0) {
            dprintf(1, "[TEST] initrd_rootfs_layout FAIL: missing or empty %s\n",
                    required[i]);
            return;
        }
    }

    /* Verify symlink resolution works (/bin/VSRV.ELF -> vsrv) */
    struct test_vfs_stat st_sym;
    if (sys_stat("/bin/VSRV.ELF", &st_sym) != 0 || st_sym.size == 0) {
        dprintf(1, "[TEST] initrd_rootfs_layout FAIL: symlink /bin/VSRV.ELF broken\n");
        return;
    }

    dprintf(1, "[TEST] initrd_rootfs_layout PASS\n");
}

static void test_tmpfs_isolation(void)
{
    static const char root_file[] = "/root_iso_probe.txt";
    static const char tmp_file[] = "/tmp/tmp_iso_probe.txt";
    static const char run_file[] = "/var/run/run_iso_probe.txt";
    static const char moved_file[] = "/tmp/tmp_iso_moved.txt";
    struct test_vfs_stat st;

    (void)sys_unlink(root_file);
    (void)sys_unlink(tmp_file);
    (void)sys_unlink(run_file);
    (void)sys_unlink(moved_file);

    int fd1 = open(root_file, TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    int fd2 = open(tmp_file, TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    int fd3 = open(run_file, TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
        if (fd1 >= 0) close(fd1);
        if (fd2 >= 0) close(fd2);
        if (fd3 >= 0) close(fd3);
        dprintf(1, "[TEST] tmpfs_isolation FAIL: open failed (%d %d %d)\n", fd1, fd2, fd3);
        return;
    }

    (void)write(fd1, "ROOT", 4);
    (void)write(fd2, "TMP!", 4);
    (void)write(fd3, "RUN!", 4);
    close(fd1);
    close(fd2);
    close(fd3);

    /* Ensure instances do not alias each other */
    if (sys_stat("/tmp/root_iso_probe.txt", &st) == 0 ||
        sys_stat("/tmp_iso_probe.txt", &st) == 0 ||
        sys_stat("/var/run/tmp_iso_probe.txt", &st) == 0 ||
        sys_stat("/tmp/run_iso_probe.txt", &st) == 0) {
        (void)sys_unlink(root_file);
        (void)sys_unlink(tmp_file);
        (void)sys_unlink(run_file);
        dprintf(1, "[TEST] tmpfs_isolation FAIL: cross-mount aliasing detected\n");
        return;
    }

    /* Verify rename + truncate within /tmp */
    if (sys_rename(tmp_file, moved_file) != 0 ||
        sys_stat(tmp_file, &st) == 0 ||
        sys_stat(moved_file, &st) != 0 || st.size != 4) {
        (void)sys_unlink(root_file);
        (void)sys_unlink(tmp_file);
        (void)sys_unlink(moved_file);
        (void)sys_unlink(run_file);
        dprintf(1, "[TEST] tmpfs_isolation FAIL: rename check failed\n");
        return;
    }

    int fd_tr = open(moved_file, TSK_O_WRONLY | TSK_O_TRUNC);
    if (fd_tr < 0) {
        (void)sys_unlink(root_file);
        (void)sys_unlink(moved_file);
        (void)sys_unlink(run_file);
        dprintf(1, "[TEST] tmpfs_isolation FAIL: truncate reopen failed\n");
        return;
    }
    close(fd_tr);

    int ok_trunc = (sys_stat(moved_file, &st) == 0 && st.size == 0);
    (void)sys_unlink(root_file);
    (void)sys_unlink(moved_file);
    (void)sys_unlink(run_file);

    if (!ok_trunc) {
        dprintf(1, "[TEST] tmpfs_isolation FAIL: O_TRUNC did not zero size\n");
        return;
    }

    dprintf(1, "[TEST] tmpfs_isolation PASS\n");
}

static uint8_t g_fs_io_buf[32768];

static int str_eq_local(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int verify_big_file(const char *path, size_t expected_min_bytes)
{
    int fd = open(path, TSK_O_RDONLY);
    if (fd < 0)
        return -1;
    size_t total = 0;
    for (;;) {
        long r = read(fd, g_fs_io_buf, sizeof(g_fs_io_buf));
        if (r < 0) {
            close(fd);
            return -2;
        }
        if (r == 0)
            break;
        for (long j = 0; j < r; j++) {
            size_t off = total + (size_t)j;
            uint8_t exp = (uint8_t)(((off >> 12) * 37u + (off & 0xFFu) * 13u + 0x5Au) & 0xFFu);
            if (g_fs_io_buf[j] != exp) {
                close(fd);
                return -3;
            }
        }
        total += (size_t)r;
    }
    close(fd);
    if (total < expected_min_bytes)
        return -4;
    return 0;
}

static int read_exact_small(const char *path, char *out, size_t cap, long *out_len)
{
    int fd = open(path, TSK_O_RDONLY);
    if (fd < 0)
        return -1;
    long n = read(fd, out, cap - 1);
    close(fd);
    if (n < 0)
        return -2;
    out[n] = '\0';
    if (out_len)
        *out_len = n;
    return 0;
}

static int check_dirent_present(const char *dir_path, const char *target_name)
{
    int fd = open(dir_path, TSK_O_RDONLY);
    if (fd < 0)
        return 0;
    uint8_t dbuf[2048];
    int found = 0;
    for (;;) {
        long n = sys_getdents64(fd, dbuf, sizeof(dbuf));
        if (n <= 0)
            break;
        long pos = 0;
        while (pos < n) {
            struct linux_dirent64 *de = (struct linux_dirent64 *)(dbuf + pos);
            if (de->d_reclen == 0)
                break;
            if (str_eq_local(de->d_name, target_name)) {
                found = 1;
                break;
            }
            pos += de->d_reclen;
        }
        if (found)
            break;
    }
    close(fd);
    return found;
}

static void test_mount_negative(void)
{
    struct test_vfs_stat st;
    long r1 = sys_mount("/dev/no_such_blk", "/mnt", "ext4", 0);
    long r2 = sys_mount("/dev/no_such_blk", "/bin/ls", "ext4", 0);
    long r3 = sys_umount2("/no_such_mnt", 0);
    if (r1 != -19 || r2 != -20 || r3 != -22) {
        dprintf(1, "[TEST] mount_negative FAIL: r1=%ld r2=%ld r3=%ld\n", r1, r2, r3);
        return;
    }
    if (sys_stat("/dev/hda", &st) == 0) {
        long r4 = sys_mount("/dev/hda", "/mnt", "ext4", 0);
        if (r4 != -22) {
            dprintf(1, "[TEST] mount_negative FAIL: blank hda rc=%ld\n", r4);
            return;
        }
    }
    dprintf(1, "[TEST] mount_negative PASS\n");
}

static void test_ext4_fat32_images(void)
{
    struct test_vfs_stat st;
    const char *ext4_dev = NULL;
    const char *fat_dev = NULL;

    if (sys_stat("/dev/vda", &st) == 0)
        ext4_dev = "/dev/vda";
    else if (sys_stat("/dev/sda", &st) == 0)
        ext4_dev = "/dev/sda";

    if (sys_stat("/dev/vdb", &st) == 0)
        fat_dev = "/dev/vdb";
    else if (sys_stat("/dev/sdb", &st) == 0)
        fat_dev = "/dev/sdb";

    if (!ext4_dev && !fat_dev)
        return;

    if (ext4_dev && !fat_dev) {
        (void)sys_mkdir("/mnt/neg", 0755);
        long mrc = sys_mount(ext4_dev, "/mnt/neg", "ext4", 0);
        if (mrc == -22) {
            dprintf(1, "[TEST] ext4_corrupt_sb_reject PASS rc=%ld\n", mrc);
        } else {
            if (mrc == 0)
                (void)sys_umount2("/mnt/neg", 0);
            dprintf(1, "[TEST] ext4_corrupt_sb_reject FAIL rc=%ld\n", mrc);
        }
        return;
    }

    if (!ext4_dev || !fat_dev) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: expected both ext4 and fat32 drives\n");
        return;
    }

    (void)sys_umount2("/disk", 0);
    (void)sys_mkdir("/mnt/e4", 0755);
    (void)sys_mkdir("/mnt/fat", 0755);

    long m_e4 = sys_mount(ext4_dev, "/mnt/e4", "ext4", 0);
    if (m_e4 != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: mount %s ext4 rc=%ld\n", ext4_dev, m_e4);
        return;
    }
    long m_fat = sys_mount(fat_dev, "/mnt/fat", "fat32", 0);
    if (m_fat != 0) {
        (void)sys_umount2("/mnt/e4", 0);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: mount %s fat32 rc=%ld\n", fat_dev, m_fat);
        return;
    }

    /* 1. Verify ext4 pre-populated contents (>10 MiB file, nested file, symlink, getdents64). */
    int v_big = verify_big_file("/mnt/e4/big.bin", 10u * 1024u * 1024u + 1u);
    if (v_big != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 big.bin verify=%d\n", v_big);
        goto umount_fail;
    }

    char sbuf[128];
    long slen = 0;
    if (read_exact_small("/mnt/e4/a/b/c/hello.txt", sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "hello from nested ext4\n")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 nested hello.txt mismatch\n");
        goto umount_fail;
    }

    if (sys_lstat("/mnt/e4/link_hello", &st) != 0 || st.type != 7) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 lstat symlink type=%u\n", st.type);
        goto umount_fail;
    }

    long rl = sys_readlink("/mnt/e4/link_hello", sbuf, sizeof(sbuf) - 1);
    if (rl <= 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 readlink rc=%ld\n", rl);
        goto umount_fail;
    }
    sbuf[rl] = '\0';
    if (!str_eq_local(sbuf, "a/b/c/hello.txt")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 readlink target=%s\n", sbuf);
        goto umount_fail;
    }

    if (read_exact_small("/mnt/e4/link_hello", sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "hello from nested ext4\n")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 follow symlink content mismatch\n");
        goto umount_fail;
    }

    if (!check_dirent_present("/mnt/e4", "big.bin") ||
        !check_dirent_present("/mnt/e4", "link_hello")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 getdents64 missing entries\n");
        goto umount_fail;
    }

    /* 2. ext4 R/W mutations: mkdir, create, append, truncate shrink, symlink, chmod/chown, rename, unlink, rmdir */
    if (sys_mkdir("/mnt/e4/wdir", 0755) != 0 ||
        sys_mkdir("/mnt/e4/wdir/sub", 0755) != 0 ||
        sys_mkdir("/mnt/e4/wdir/tempdir", 0755) != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 mkdir\n");
        goto umount_fail;
    }

    int fd = open("/mnt/e4/wdir/sub/new.txt", TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0 || write(fd, "ABCD", 4) != 4) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 create/write\n");
        goto umount_fail;
    }
    close(fd);

    fd = open("/mnt/e4/wdir/sub/new.txt", TSK_O_WRONLY | TSK_O_APPEND);
    if (fd < 0 || write(fd, "EFGH", 4) != 4) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 append\n");
        goto umount_fail;
    }
    close(fd);

    if (sys_truncate("/mnt/e4/wdir/sub/new.txt", 5) != 0 ||
        sys_stat("/mnt/e4/wdir/sub/new.txt", &st) != 0 || st.size != 5 ||
        read_exact_small("/mnt/e4/wdir/sub/new.txt", sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "ABCDE")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 truncate shrink\n");
        goto umount_fail;
    }

    if (sys_chmod("/mnt/e4/wdir/sub/new.txt", 0640) != 0 ||
        sys_chown("/mnt/e4/wdir/sub/new.txt", 1000, 1000) != 0 ||
        sys_stat("/mnt/e4/wdir/sub/new.txt", &st) != 0 ||
        (st.st_mode & 0777u) != 0640u || st.st_uid != 1000u || st.st_gid != 1000u) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 chmod/chown\n");
        goto umount_fail;
    }

    if (sys_symlink("/mnt/e4/a/b/c/hello.txt", "/mnt/e4/wdir/abs_link") != 0 ||
        sys_rename("/mnt/e4/wdir/sub/new.txt", "/mnt/e4/wdir/renamed.txt") != 0 ||
        sys_stat("/mnt/e4/wdir/sub/new.txt", &st) == 0 ||
        sys_stat("/mnt/e4/wdir/renamed.txt", &st) != 0 || st.size != 5) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 symlink/rename\n");
        goto umount_fail;
    }

    fd = open("/mnt/e4/wdir/tmpdel.txt", TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0 || write(fd, "bye", 3) != 3) {
        if (fd >= 0) close(fd);
        goto umount_fail;
    }
    close(fd);
    if (sys_unlink("/mnt/e4/wdir/tmpdel.txt") != 0 ||
        sys_rmdir("/mnt/e4/wdir/tempdir") != 0 ||
        sys_rmdir("/mnt/e4/wdir") == 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 unlink/rmdir\n");
        goto umount_fail;
    }

    fd = open("/mnt/e4/wdir/ftrunc.bin", TSK_O_RDWR | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0)
        goto umount_fail;
    for (int i = 0; i < 8192; i++)
        g_fs_io_buf[i] = (uint8_t)(i & 0xFFu);
    if (write(fd, g_fs_io_buf, 8192) != 8192 || sys_ftruncate(fd, 1234) != 0) {
        close(fd);
        goto umount_fail;
    }
    close(fd);
    if (sys_stat("/mnt/e4/wdir/ftrunc.bin", &st) != 0 || st.size != 1234) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 ftruncate\n");
        goto umount_fail;
    }

    /* 2b. ext4 demand-paged file mmap (close(fd), MAP_PRIVATE COW, MAP_SHARED + msync). */
    fd = open("/mnt/e4/wdir/mmap_e4.bin", TSK_O_RDWR | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0)
        goto umount_fail;
    for (int i = 0; i < 9000; i++)
        g_fs_io_buf[i] = (uint8_t)((i * 19 + 5) & 0xFFu);
    if (write(fd, g_fs_io_buf, 9000) != 9000) {
        close(fd);
        goto umount_fail;
    }
    close(fd);

    fd = open("/mnt/e4/wdir/mmap_e4.bin", TSK_O_RDONLY);
    if (fd < 0)
        goto umount_fail;
    volatile uint8_t *e4p = (volatile uint8_t *)mmap(
        NULL, 9000, VFS_PROT_READ | VFS_PROT_WRITE, VFS_MAP_PRIVATE, fd, 0);
    close(fd);
    if (!e4p || (long)(uintptr_t)e4p < 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_PRIVATE mmap failed\n");
        goto umount_fail;
    }
    for (int i = 0; i < 9000; i++) {
        if (e4p[i] != (uint8_t)((i * 19 + 5) & 0xFFu)) {
            (void)munmap((void *)(uintptr_t)e4p, 9000);
            dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_PRIVATE read mismatch at %d\n", i);
            goto umount_fail;
        }
    }
    e4p[0] ^= 0xFFu;
    e4p[4096] ^= 0xFFu;
    e4p[8500] ^= 0xFFu;
    (void)munmap((void *)(uintptr_t)e4p, 9000);

    fd = open("/mnt/e4/wdir/mmap_e4.bin", TSK_O_RDWR);
    if (fd < 0)
        goto umount_fail;
    e4p = (volatile uint8_t *)mmap(
        NULL, 9000, VFS_PROT_READ | VFS_PROT_WRITE, VFS_MAP_SHARED, fd, 0);
    close(fd);
    if (!e4p || (long)(uintptr_t)e4p < 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_SHARED mmap failed\n");
        goto umount_fail;
    }
    if (e4p[0] != 5u || e4p[4096] != (uint8_t)((4096 * 19 + 5) & 0xFFu)) {
        (void)munmap((void *)(uintptr_t)e4p, 9000);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_PRIVATE mutated disk\n");
        goto umount_fail;
    }
    e4p[17] = 0xA5u;
    e4p[5000] = 0x5Au;
    e4p[8999] = 0x3Cu;
    if (msync((void *)(uintptr_t)e4p, 9000, VFS_MS_SYNC) != 0) {
        (void)munmap((void *)(uintptr_t)e4p, 9000);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 msync failed\n");
        goto umount_fail;
    }
    e4p[18] = 0x7Eu;
    if (msync((void *)(uintptr_t)e4p, 4096, VFS_MS_SYNC) != 0) {
        (void)munmap((void *)(uintptr_t)e4p, 9000);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 second msync failed\n");
        goto umount_fail;
    }
    (void)munmap((void *)(uintptr_t)e4p, 9000);

    if (sys_stat("/mnt/e4/wdir/mmap_e4.bin", &st) != 0 || st.size != 9000) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_SHARED changed file size\n");
        goto umount_fail;
    }
    fd = open("/mnt/e4/wdir/mmap_e4.bin", TSK_O_RDONLY);
    if (fd < 0 || read(fd, g_fs_io_buf, 9000) != 9000 ||
        g_fs_io_buf[17] != 0xA5u || g_fs_io_buf[18] != 0x7Eu ||
        g_fs_io_buf[5000] != 0x5Au || g_fs_io_buf[8999] != 0x3Cu) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: ext4 MAP_SHARED persistence mismatch\n");
        goto umount_fail;
    }
    close(fd);

    /* 3. Verify FAT32 pre-populated contents (>10 MiB file, nested dir, mixed-case LFN). */
    v_big = verify_big_file("/mnt/fat/BIGFILE.BIN", 10u * 1024u * 1024u + 1u);
    if (v_big != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 BIGFILE.BIN verify=%d\n", v_big);
        goto umount_fail;
    }

    if (read_exact_small("/mnt/fat/dir1/dir2/nested.txt", sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "hello from nested fat32\n")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 nested.txt mismatch\n");
        goto umount_fail;
    }

    if (read_exact_small("/mnt/fat/Long Mixed Case File.txt", sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "mixed case LFN payload\n")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 LFN read mismatch\n");
        goto umount_fail;
    }

    if (!check_dirent_present("/mnt/fat", "BIGFILE.BIN") ||
        !check_dirent_present("/mnt/fat", "Long Mixed Case File.txt")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 getdents64 missing entries\n");
        goto umount_fail;
    }

    /* 4. FAT32 R/W mutations: nested mkdir, nested LFN create+append, cluster-chain shrink, LFN rename, unlink, rmdir */
    if (sys_mkdir("/mnt/fat/newdir", 0755) != 0 ||
        sys_mkdir("/mnt/fat/newdir/nested_sub", 0755) != 0 ||
        sys_mkdir("/mnt/fat/newdir/del_dir", 0755) != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 mkdir\n");
        goto umount_fail;
    }

    fd = open("/mnt/fat/newdir/nested_sub/Nested Mixed Case.txt",
              TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0 || write(fd, "FAT1-", 5) != 5) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 nested LFN create\n");
        goto umount_fail;
    }
    close(fd);

    fd = open("/mnt/fat/newdir/nested_sub/Nested Mixed Case.txt", TSK_O_WRONLY | TSK_O_APPEND);
    if (fd < 0 || write(fd, "FAT2", 4) != 4) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 nested LFN append\n");
        goto umount_fail;
    }
    close(fd);

    if (read_exact_small("/mnt/fat/newdir/nested_sub/Nested Mixed Case.txt",
                         sbuf, sizeof(sbuf), &slen) != 0 ||
        !str_eq_local(sbuf, "FAT1-FAT2")) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 nested LFN readback\n");
        goto umount_fail;
    }

    fd = open("/mnt/fat/shrink.bin", TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0)
        goto umount_fail;
    for (int i = 0; i < 16384; i++)
        g_fs_io_buf[i] = (uint8_t)((i * 7 + 3) & 0xFFu);
    if (write(fd, g_fs_io_buf, 16384) != 16384) {
        close(fd);
        goto umount_fail;
    }
    close(fd);

    if (sys_truncate("/mnt/fat/shrink.bin", 100) != 0 ||
        sys_stat("/mnt/fat/shrink.bin", &st) != 0 || st.size != 100) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 truncate shrink\n");
        goto umount_fail;
    }

    if (sys_rename("/mnt/fat/Long Mixed Case File.txt",
                   "/mnt/fat/newdir/Renamed Mixed Case File.txt") != 0 ||
        sys_stat("/mnt/fat/Long Mixed Case File.txt", &st) == 0 ||
        sys_stat("/mnt/fat/newdir/Renamed Mixed Case File.txt", &st) != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 LFN rename\n");
        goto umount_fail;
    }

    if (sys_unlink("/mnt/fat/newdir/nested_sub/Nested Mixed Case.txt") != 0 ||
        sys_rmdir("/mnt/fat/newdir/nested_sub") != 0 ||
        sys_rmdir("/mnt/fat/newdir/del_dir") != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: fat32 unlink/rmdir\n");
        goto umount_fail;
    }

    if (sys_umount2("/mnt/e4", 0) != 0 || sys_umount2("/mnt/fat", 0) != 0) {
        dprintf(1, "[TEST] ext4_fat32_images FAIL: umount2 failed\n");
        return;
    }

    dprintf(1, "[TEST] ext4_fat32_images PASS\n");
    return;

umount_fail:
    (void)sys_umount2("/mnt/e4", 0);
    (void)sys_umount2("/mnt/fat", 0);
}

static void test_demand_file_mmap(void)
{
    const char *path = "/tmp/mmap_surv.bin";
    const size_t flen = 12345u;
    int fd = open(path, TSK_O_RDWR | TSK_O_CREAT | TSK_O_TRUNC);
    if (fd < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: create failed\n");
        return;
    }
    for (size_t i = 0; i < flen; i++)
        g_fs_io_buf[i] = (uint8_t)((i * 37u + 13u) & 0xFFu);
    if (write(fd, g_fs_io_buf, flen) != (long)flen) {
        close(fd);
        dprintf(1, "[TEST] demand_file_mmap FAIL: seed write failed\n");
        return;
    }
    close(fd);

    /* 1. Survives close(fd) + partial EOF tail zeroing + multi-page demand reads. */
    fd = open(path, TSK_O_RDONLY);
    if (fd < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: reopen rdonly\n");
        return;
    }
    volatile uint8_t *p = (volatile uint8_t *)mmap(
        NULL, flen, VFS_PROT_READ, VFS_MAP_PRIVATE, fd, 0);
    close(fd);
    if (!p || (long)(uintptr_t)p < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_PRIVATE ro mmap failed\n");
        return;
    }
    for (size_t i = 0; i < flen; i++) {
        if (p[i] != (uint8_t)((i * 37u + 13u) & 0xFFu)) {
            (void)munmap((void *)(uintptr_t)p, flen);
            dprintf(1, "[TEST] demand_file_mmap FAIL: read mismatch at %u\n", (unsigned)i);
            return;
        }
    }
    for (size_t i = flen; i < 16384u; i++) {
        if (p[i] != 0u) {
            (void)munmap((void *)(uintptr_t)p, flen);
            dprintf(1, "[TEST] demand_file_mmap FAIL: EOF tail nonzero at %u\n", (unsigned)i);
            return;
        }
    }
    if (munmap((void *)(uintptr_t)p, flen) != 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: munmap 1 failed\n");
        return;
    }

    /* 2. MAP_PRIVATE COW isolation (writes stay private, file unchanged). */
    fd = open(path, TSK_O_RDONLY);
    if (fd < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: reopen for COW\n");
        return;
    }
    p = (volatile uint8_t *)mmap(NULL, flen, VFS_PROT_READ | VFS_PROT_WRITE,
                                 VFS_MAP_PRIVATE, fd, 0);
    close(fd);
    if (!p || (long)(uintptr_t)p < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_PRIVATE rw mmap failed\n");
        return;
    }
    p[0] ^= 0xFFu;
    p[4096] ^= 0xFFu;
    p[9000] ^= 0xFFu;
    if (p[0] != (uint8_t)(13u ^ 0xFFu) ||
        p[4096] != (uint8_t)(((4096u * 37u + 13u) & 0xFFu) ^ 0xFFu) ||
        p[9000] != (uint8_t)(((9000u * 37u + 13u) & 0xFFu) ^ 0xFFu)) {
        (void)munmap((void *)(uintptr_t)p, flen);
        dprintf(1, "[TEST] demand_file_mmap FAIL: COW write not visible in mapping\n");
        return;
    }
    (void)munmap((void *)(uintptr_t)p, flen);

    fd = open(path, TSK_O_RDONLY);
    if (fd < 0 || read(fd, g_fs_io_buf, flen) != (long)flen) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] demand_file_mmap FAIL: verify read failed\n");
        return;
    }
    close(fd);
    if (g_fs_io_buf[0] != 13u ||
        g_fs_io_buf[4096] != (uint8_t)((4096u * 37u + 13u) & 0xFFu) ||
        g_fs_io_buf[9000] != (uint8_t)((9000u * 37u + 13u) & 0xFFu)) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_PRIVATE mutated underlying file\n");
        return;
    }

    /* 3. MAP_SHARED + page offset + msync + re-dirty writeback. */
    fd = open(path, TSK_O_RDWR);
    if (fd < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: open rdwr for MAP_SHARED\n");
        return;
    }
    p = (volatile uint8_t *)mmap(NULL, 8192u, VFS_PROT_READ | VFS_PROT_WRITE,
                                 VFS_MAP_SHARED, fd, 4096L);
    close(fd);
    if (!p || (long)(uintptr_t)p < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_SHARED mmap failed\n");
        return;
    }
    if (p[0] != (uint8_t)((4096u * 37u + 13u) & 0xFFu)) {
        (void)munmap((void *)(uintptr_t)p, 8192u);
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_SHARED offset mismatch\n");
        return;
    }
    p[0] = 0xAAu;
    p[4095] = 0xBBu;
    p[4096] = 0xCCu;
    if (msync((void *)(uintptr_t)p, 8192u, VFS_MS_SYNC) != 0) {
        (void)munmap((void *)(uintptr_t)p, 8192u);
        dprintf(1, "[TEST] demand_file_mmap FAIL: msync 1 failed\n");
        return;
    }
    p[10] = 0xDDu;
    if (msync((void *)(uintptr_t)p, 4096u, VFS_MS_SYNC) != 0) {
        (void)munmap((void *)(uintptr_t)p, 8192u);
        dprintf(1, "[TEST] demand_file_mmap FAIL: msync 2 failed\n");
        return;
    }
    if (munmap((void *)(uintptr_t)p, 8192u) != 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: munmap shared failed\n");
        return;
    }

    fd = open(path, TSK_O_RDONLY);
    if (fd < 0 || read(fd, g_fs_io_buf, flen) != (long)flen) {
        if (fd >= 0) close(fd);
        dprintf(1, "[TEST] demand_file_mmap FAIL: shared readback failed\n");
        return;
    }
    close(fd);
    if (g_fs_io_buf[4096] != 0xAAu || g_fs_io_buf[8191] != 0xBBu ||
        g_fs_io_buf[8192] != 0xCCu || g_fs_io_buf[4106] != 0xDDu) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: MAP_SHARED changes lost\n");
        return;
    }

    /* 4. Negative argument validation for mmap and msync. */
    fd = open(path, TSK_O_RDONLY);
    if (fd < 0) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: open rdonly for negative checks\n");
        return;
    }
    if (mmap(NULL, 4096, VFS_PROT_READ, VFS_MAP_PRIVATE, fd, 123L) != (void *)-1 ||
        mmap(NULL, 4096, VFS_PROT_READ | VFS_PROT_WRITE, VFS_MAP_SHARED, fd, 0) != (void *)-1 ||
        mmap(NULL, 4096, VFS_PROT_READ, VFS_MAP_SHARED | VFS_MAP_PRIVATE, fd, 0) != (void *)-1 ||
        mmap(NULL, 4096, VFS_PROT_READ, VFS_MAP_PRIVATE, 999, 0) != (void *)-1) {
        close(fd);
        dprintf(1, "[TEST] demand_file_mmap FAIL: negative mmap accepted invalid args\n");
        return;
    }
    close(fd);

    if (msync((void *)0x80000123ULL, 4096u, VFS_MS_SYNC) != -22 ||
        msync((void *)0x80000000ULL, 4096u, VFS_MS_ASYNC | VFS_MS_SYNC) != -22 ||
        msync((void *)0x80000000ULL, 4096u, 0x80) != -22 ||
        msync((void *)0x50000000ULL, 4096u, VFS_MS_SYNC) != -12) {
        dprintf(1, "[TEST] demand_file_mmap FAIL: negative msync validation failed\n");
        return;
    }

    /* 5. Negative Ring-3 #PF protection & NX enforcement + positive PROT_EXEC. */
    int rfd = open("/tmp/mmap_ro.dat", TSK_O_WRONLY | TSK_O_CREAT | TSK_O_TRUNC);
    if (rfd < 0 || write(rfd, "READONLY", 8) != 8) {
        if (rfd >= 0) close(rfd);
        dprintf(1, "[TEST] demand_file_mmap FAIL: seed mmap_ro.dat failed\n");
        return;
    }
    close(rfd);

    const char *fault_modes[3] = { "ro_write", "nx_exec", "prot_none" };
    for (int m = 0; m < 3; m++) {
        char cmdline[64];
        const char *pfx = "mmapflt ";
        int k = 0;
        while (pfx[k]) {
            cmdline[k] = pfx[k];
            k++;
        }
        int j = 0;
        while (fault_modes[m][j]) {
            cmdline[k++] = fault_modes[m][j++];
        }
        cmdline[k] = '\0';

        int st = 0;
        long detail = 0;
        int rc = spawn_and_wait("/bin/mmapflt.elf", cmdline, &st, &detail);
        if (rc != 0 || TSK_WIFEXITED(st) || !TSK_WIFSIGNALED(st) ||
            TSK_WTERMSIG(st) != TSK_SIGSEGV) {
            dprintf(1, "[TEST] demand_file_mmap FAIL: %s expected SIGSEGV rc=%d st=0x%x\n",
                    fault_modes[m], rc, (unsigned)st);
            return;
        }
    }

    {
        int st = 0;
        long detail = 0;
        int rc = spawn_and_wait("/bin/mmapflt.elf", "mmapflt exec_ok", &st, &detail);
        if (rc != 0 || !TSK_WIFEXITED(st) || TSK_WEXITSTATUS(st) != 42) {
            dprintf(1, "[TEST] demand_file_mmap FAIL: exec_ok expected exit 42 rc=%d st=0x%x\n",
                    rc, (unsigned)st);
            return;
        }
    }

    (void)sys_unlink(path);
    (void)sys_unlink("/tmp/mmap_ro.dat");
    dprintf(1, "[TEST] demand_file_mmap PASS\n");
}

static void run_kernel_regression_tests(void)
{
    test_spawn_argv();
    test_pmm_reclaim();
    test_munmap_safety();
    test_demand_file_mmap();
    test_urandom_entropy();
    test_vfs_tmp_crud();
    test_initrd_rootfs_layout();
    test_tmpfs_isolation();
    test_mount_negative();
    test_ext4_fat32_images();
    test_tsh_pipeline();
    test_ring3_fault_isolation();
    test_yield_no_tick_forge();
    test_nanosleep_blocking();
    test_proc_stat();
}

static int is_vanilla_test(const char *bin_path)
{
    const char * const targets[] = {
        "/bin/TESTIPC.ELF",
        "/bin/TESTCOMP.ELF",
        "/bin/TESTTYPO.ELF",
        "/bin/TSHELL.ELF",
        0
    };

    for (int i = 0; targets[i] != 0; i++) {
        const char *p = bin_path;
        const char *t = targets[i];
        while (*p && *t && *p == *t) {
            p++;
            t++;
        }
        if (*p == '\0' && *t == '\0')
            return 1;
    }
    return 0;
}

static const char * const g_test_binaries[] = {
    "/bin/TCRT0.ELF",
    "/bin/TSYSC.ELF",
    "/bin/TLIBC.ELF",
    "/bin/TMATH.ELF",
    "/bin/TSTDIO.ELF",
    "/bin/TESTIPC.ELF",
    "/bin/TESTCOMP.ELF",
    "/bin/TESTTYPO.ELF",
    "/bin/TSHELL.ELF",
    NULL
};

static void setup_stdio(void)
{
    int fd = open("/dev/tty0", TSK_O_RDWR);
    if (fd < 0)
        fd = open("/dev/tty0", TSK_O_WRONLY);
    if (fd < 0)
        fd = open("/dev/ttyS0", TSK_O_RDWR);
    if (fd < 0)
        fd = open("/dev/console", TSK_O_RDWR);

    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2)
            close(fd);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    setup_stdio();

    dprintf(1, "[testdrv] Starting in-guest test suite runner...\n");

    run_kernel_regression_tests();

    int vanilla_ready = 0;

    for (int i = 0; g_test_binaries[i] != NULL; i++) {
        const char *bin_path = g_test_binaries[i];

        if (!vanilla_ready && is_vanilla_test(bin_path)) {
            char stat_buf[128];
            for (int wait_sock = 0; wait_sock < 500; wait_sock++) {
                if (sys_stat("/tmp/vanilla.sock", stat_buf) == 0)
                    break;
                (void)sys_nanosleep(20);
            }
            vanilla_ready = 1;
        }

        dprintf(1, "[testdrv] Executing %s\n", bin_path);

        int status = 0;
        long detail = 0;
        int rc = spawn_and_wait(bin_path, NULL, &status, &detail);
        if (rc == -1) {
            dprintf(1, "[TEST] %s.spawn FAIL: could not spawn process (rc=%ld)\n",
                    bin_path, detail);
            continue;
        }
        if (rc == -2) {
            dprintf(1, "[TEST] %s.execution FAIL: timeout waiting for process pid=%ld\n",
                    bin_path, detail);
        }
    }

    dprintf(1, "[TEST] ALL DONE\n");
    sys_poweroff();

    for (;;) {
        sched_yield();
    }

    return 0;
}
