#include "../include/app_runtime.h"
#include "../include/fcntl.h"
#include "../include/libui.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

#define GUI_PHASE2_HANDLE_FILE "/tmp/gui_phase2_owner_handle.txt"
#define GUI_PHASE2_RESULT_FILE "/tmp/gui_phase2_isolation_result.txt"

static int read_int_file(const char *path, int *out_value)
{
    char buf[64];
    int fd;
    int n;

    if (!path || !out_value)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;

    buf[n] = '\0';
    *out_value = atoi(buf);
    return 0;
}

static int write_int_file(const char *path, int value)
{
    char buf[64];
    int fd;
    int len;

    if (!path)
        return -1;

    len = snprintf(buf, sizeof(buf), "%d\n", value);
    if (len <= 0)
        return -1;

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
        return -1;
    if (write(fd, buf, (size_t)len) != len) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int gui_phase2_isolation_helper_main(int argc, char **argv)
{
    int target_handle = -1;
    int rc;
    (void)argc;
    (void)argv;

    if (read_int_file(GUI_PHASE2_HANDLE_FILE, &target_handle) != 0) {
        dprintf(2, "gui-phase2-helper: failed to read %s\n", GUI_PHASE2_HANDLE_FILE);
        return 2;
    }

    rc = ui_draw_rect_ex((ui_window_t)(uint64_t)target_handle, 2, 2, 12, 12, 0xFFFF0066u);
    if (write_int_file(GUI_PHASE2_RESULT_FILE, rc) != 0) {
        dprintf(2, "gui-phase2-helper: failed to write %s\n", GUI_PHASE2_RESULT_FILE);
        return 3;
    }

    return (rc == UI_ERR_PERM) ? 0 : 1;
}

void app_gui_phase2_isolation_helper_entry(void)
{
    _exit(app_run_main(gui_phase2_isolation_helper_main));
}
