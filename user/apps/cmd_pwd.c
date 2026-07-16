#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/unistd.h"

static int cmd_pwd_main(int argc, char **argv)
{
    char cwd[256];
    (void)argc;
    (void)argv;
    if (!getcwd(cwd, sizeof(cwd))) {
        dprintf(2, "pwd: failed to get cwd\n");
        return 1;
    }
    dprintf(1, "%s\n", cwd);
    return 0;
}

void app_cmd_pwd_entry(void)
{
    _exit(app_run_main(cmd_pwd_main));
}
