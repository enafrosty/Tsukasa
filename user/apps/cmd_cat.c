#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/unistd.h"

static int cat_fd(int fd)
{
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write(1, buf, (size_t)n) != n)
            return -1;
    }
    return (n < 0) ? -1 : 0;
}

static int cmd_cat_main(int argc, char **argv)
{
    if (argc <= 1)
        return cat_fd(0);

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) {
            dprintf(2, "cat: cannot open %s\n", argv[i]);
            return 1;
        }
        if (cat_fd(fd) != 0) {
            close(fd);
            dprintf(2, "cat: read error on %s\n", argv[i]);
            return 1;
        }
        close(fd);
    }
    return 0;
}

void app_cmd_cat_entry(void)
{
    _exit(app_run_main(cmd_cat_main));
}
