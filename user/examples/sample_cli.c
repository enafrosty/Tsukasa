#include "../include/fcntl.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/time.h"
#include "../include/unistd.h"

int sample_cli_main(int argc, char **argv)
{
    char cwd[256];
    char stamp[64];
    struct tm tmv;
    time_t now;
    int fd;
    (void)argc;
    (void)argv;

    if (!getcwd(cwd, sizeof(cwd)))
        dprintf(2, "sample_cli: getcwd failed\n");

    now = time(0);
    if (now >= 0 && gmtime_r(&now, &tmv) &&
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", &tmv)) {
        dprintf(1, "Sample CLI running at %s\n", stamp);
    }
    dprintf(1, "cwd=%s\n", cwd);

    fd = open("/tmp/sample_cli.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd >= 0) {
        dprintf(fd, "hello from sample_cli\n");
        close(fd);
        dprintf(1, "Wrote /tmp/sample_cli.txt\n");
    } else {
        dprintf(2, "sample_cli: failed to write /tmp/sample_cli.txt\n");
    }

    return 0;
}
