#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"

static int cmp_lines(const void *a, const void *b)
{
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    return strcmp(*sa, *sb);
}

static int cmd_sort_main(int argc, char **argv)
{
    char buf[4096];
    const char *lines[256];
    size_t len = 0;
    ssize_t n;
    int line_count = 0;
    (void)argc;
    (void)argv;

    while (len + 1 < sizeof(buf)) {
        n = read(0, buf + len, sizeof(buf) - len - 1);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    buf[len] = '\0';
    if (len == 0)
        return 0;

    {
        char *p = buf;
        lines[line_count++] = p;
        while (*p && line_count < (int)(sizeof(lines) / sizeof(lines[0]))) {
            if (*p == '\n') {
                *p = '\0';
                if (p[1])
                    lines[line_count++] = p + 1;
            }
            p++;
        }
    }

    qsort((void *)lines, (size_t)line_count, sizeof(lines[0]), cmp_lines);
    for (int i = 0; i < line_count; i++)
        dprintf(1, "%s\n", lines[i]);
    return 0;
}

void app_cmd_sort_entry(void)
{
    _exit(app_run_main(cmd_sort_main));
}
