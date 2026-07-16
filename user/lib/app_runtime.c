#include "../include/app_runtime.h"

#include "../lib/syscall.h"
#include "../include/string.h"

static char *trim_ws(char *s)
{
    char *end;
    if (!s)
        return s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    end = s + strlen(s);
    while (end > s &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    *end = '\0';
    return s;
}

int app_get_cmdline(char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return -1;
    buf[0] = '\0';
    if (system_get_cmdline(buf, cap) != 0)
        return -1;
    return 0;
}

int app_tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = trim_ws(line);
    if (!p || !argv || max_args <= 0 || !p[0])
        return 0;

    while (*p && argc < max_args) {
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"')
                p++;
            if (*p == '"')
                *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
        }
        while (*p == ' ' || *p == '\t') {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

int app_run_main(int (*main_fn)(int argc, char **argv))
{
    char cmdline[256];
    char *argv[32];
    int argc = 0;
    if (!main_fn)
        return -1;
    if (app_get_cmdline(cmdline, sizeof(cmdline)) == 0)
        argc = app_tokenize(cmdline, argv, 32);
    return main_fn(argc, argv);
}
