#ifndef TSUKASA_APP_RUNTIME_H
#define TSUKASA_APP_RUNTIME_H

#include <stddef.h>

int app_get_cmdline(char *buf, size_t cap);
int app_tokenize(char *line, char *argv[], int max_args);
int app_run_main(int (*main_fn)(int argc, char **argv));

#endif /* TSUKASA_APP_RUNTIME_H */
