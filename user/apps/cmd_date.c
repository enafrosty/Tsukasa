#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/time.h"

static int cmd_date_main(int argc, char **argv)
{
    time_t now;
    struct tm tmv;
    char out[64];
    (void)argc;
    (void)argv;

    now = time(0);
    if (now < 0) {
        dprintf(2, "date: failed to read RTC time\n");
        return 1;
    }
    if (!gmtime_r(&now, &tmv)) {
        dprintf(2, "date: failed to convert time\n");
        return 1;
    }
    if (!strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S UTC", &tmv)) {
        dprintf(2, "date: formatting failed\n");
        return 1;
    }
    dprintf(1, "%s\n", out);
    return 0;
}

void app_cmd_date_entry(void)
{
    _exit(app_run_main(cmd_date_main));
}
