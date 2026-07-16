#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

static int cmd_echo_main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            putchar(' ');
        dprintf(1, "%s", argv[i]);
    }
    putchar('\n');
    return 0;
}

void app_cmd_echo_entry(void)
{
    _exit(app_run_main(cmd_echo_main));
}
