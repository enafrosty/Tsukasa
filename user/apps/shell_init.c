#include "../include/shell.h"
#include "../include/stdlib.h"

void app_shell_init_entry(void)
{
    /*
     * Startup script probing order:
     *   1) /etc/tsukasa.rc
     *   2) /tmp/.tsukasarc
     */
    shell_run_rc_file("/etc/tsukasa.rc", 1, 2);
    shell_run_rc_file("/tmp/.tsukasarc", 1, 2);
    _exit(0);
}
