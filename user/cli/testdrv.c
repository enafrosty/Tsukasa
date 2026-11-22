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
    int rc = spawn_and_wait("/fat12/argvchk.elf", "argvchk alpha beta gamma",
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
    req.path = "/fat12/exit7.elf";
    req.args = "/fat12/exit7.elf";
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
        int rc = spawn_and_wait("/fat12/exit7.elf", NULL, &status, &detail);
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

    /* memfs on /tmp is flat and has no directory inodes, so rmdir returns -EINVAL */
    long rmrc = sys_rmdir(dir);
    if (rmrc != 0 && rmrc != TSK_EINVAL_RC) {
        dprintf(1, "[TEST] vfs_tmp_crud FAIL: rmdir unexpected rc=%ld\n", rmrc);
        return;
    }

    dprintf(1, "[TEST] vfs_tmp_crud PASS\n");
}

static void test_tsh_pipeline(void)
{
    static const char script_path[] = "/tmp/pipe_test.sh";
    static const char out_path[] = "/tmp/pipe_out.txt";
    static const char script[] = "echo pipe_magic_42 | /fat12/cat.elf > /tmp/pipe_out.txt\n";
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
    int rc = spawn_and_wait("/fat12/TSH.ELF", "tsh /tmp/pipe_test.sh",
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
    int rc = spawn_and_wait("/fat12/fault.elf", NULL, &status, &detail);
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
    rc = spawn_and_wait("/fat12/exit7.elf", NULL, &status, &detail);
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

static void run_kernel_regression_tests(void)
{
    test_spawn_argv();
    test_pmm_reclaim();
    test_munmap_safety();
    test_urandom_entropy();
    test_vfs_tmp_crud();
    test_tsh_pipeline();
    test_ring3_fault_isolation();
    test_yield_no_tick_forge();
    test_nanosleep_blocking();
    test_proc_stat();
}

static int is_vanilla_test(const char *bin_path)
{
    const char * const targets[] = {
        "/fat12/TESTIPC.ELF",
        "/fat12/TESTCOMP.ELF",
        "/fat12/TESTTYPO.ELF",
        "/fat12/TSHELL.ELF",
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
    "/fat12/TCRT0.ELF",
    "/fat12/TSYSC.ELF",
    "/fat12/TLIBC.ELF",
    "/fat12/TMATH.ELF",
    "/fat12/TSTDIO.ELF",
    "/fat12/TESTIPC.ELF",
    "/fat12/TESTCOMP.ELF",
    "/fat12/TESTTYPO.ELF",
    "/fat12/TSHELL.ELF",
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
