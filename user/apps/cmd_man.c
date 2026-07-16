#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

#include "man_pages.h"

static int cmd_man_main(int argc, char **argv)
{
    if (argc < 2) {
        dprintf(2, "man: usage: man <command>\n");
        return 1;
    }
    for (int i = 0; i < man_page_count(); i++) {
        if (strcmp(argv[1], g_man_pages[i].name) == 0) {
            dprintf(1, "%s", g_man_pages[i].text);
            return 0;
        }
    }
    dprintf(2, "man: no manual entry for %s\n", argv[1]);
    return 1;
}

void app_cmd_man_entry(void)
{
    _exit(app_run_main(cmd_man_main));
}
