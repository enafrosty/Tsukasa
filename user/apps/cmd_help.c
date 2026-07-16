#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

#include "man_pages.h"

static int cmd_help_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Tsukasa userspace commands:\n");
    for (int i = 0; i < man_page_count(); i++)
        printf("  %s\n", g_man_pages[i].name);
    printf("Use 'man <command>' for details.\n");
    return 0;
}

void app_cmd_help_entry(void)
{
    _exit(app_run_main(cmd_help_main));
}
