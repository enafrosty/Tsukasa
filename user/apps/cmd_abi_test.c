#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/signal.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/sys/ioctl.h"
#include "../include/sys/kd.h"
#include "../include/sys/mman.h"
#include "../include/sys/poll.h"
#include "../include/sys/stat.h"
#include "../include/time.h"
#include "../include/unistd.h"

static int cmd_abi_test_main(int argc, char **argv)
{
    int fail = 0;
    int fd;
    char buf[32];
    struct stat st;
    time_t now;
    int pipefd[2];
    sigset_t mask = 0;
    sigset_t oldmask = 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    struct pollfd pfd;
    void *fb_map;
    (void)argc;
    (void)argv;

    fd = open("/tmp/abi_smoke.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0 || write(fd, "phase7-abi", 10) != 10 || close(fd) != 0)
        fail = 1;

    fd = open("/tmp/abi_smoke.txt", O_RDONLY, 0);
    memset(buf, 0, sizeof(buf));
    if (fd < 0 || read(fd, buf, 10) != 10 || strcmp(buf, "phase7-abi") != 0)
        fail = 1;
    if (fd >= 0)
        close(fd);

    if (stat("/tmp/abi_smoke.txt", &st) != 0 || st.st_size != 10)
        fail = 1;

    if (pipe(pipefd) != 0)
        fail = 1;
    else {
        if (write(pipefd[1], "ok", 2) != 2)
            fail = 1;
        memset(buf, 0, sizeof(buf));
        if (read(pipefd[0], buf, 2) != 2 || strcmp(buf, "ok") != 0)
            fail = 1;
        close(pipefd[0]);
        close(pipefd[1]);
    }

    mask |= (1ULL << SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &oldmask) != 0)
        fail = 1;
    if (sigprocmask(SIG_SETMASK, &oldmask, 0) != 0)
        fail = 1;

    now = time(0);
    if (now < 0)
        fail = 1;

    fd = open("/dev/fb0", O_RDWR, 0);
    if (fd < 0)
        fail = 1;
    else {
        memset(&vinfo, 0, sizeof(vinfo));
        memset(&finfo, 0, sizeof(finfo));
        if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0 ||
            ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0 ||
            vinfo.xres == 0 || vinfo.yres == 0 || finfo.line_length == 0)
            fail = 1;
        pfd.fd = fd;
        pfd.events = POLLIN | POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) <= 0 || (pfd.revents & POLLOUT) == 0)
            fail = 1;
        fb_map = mmap(0, finfo.smem_len > 16 ? 16 : finfo.smem_len,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (fb_map == MAP_FAILED)
            fail = 1;
        else if (munmap(fb_map, finfo.smem_len > 16 ? 16 : finfo.smem_len) != 0)
            fail = 1;
        if (ioctl(fd, KDSETMODE, (void *)KD_GRAPHICS) != 0 ||
            ioctl(fd, KDSETMODE, (void *)KD_TEXT) != 0)
            fail = 1;
        close(fd);
    }

    if (fail) {
        dprintf(2, "abi-test: FAIL\n");
        return 1;
    }
    dprintf(1, "abi-test: PASS\n");
    return 0;
}

void app_cmd_abi_test_entry(void)
{
    _exit(app_run_main(cmd_abi_test_main));
}
