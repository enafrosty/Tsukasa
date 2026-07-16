#ifndef TSUKASA_SHELL_H
#define TSUKASA_SHELL_H

int shell_exec_line(const char *line, int out_fd, int err_fd);
int shell_run_rc_file(const char *path, int out_fd, int err_fd);

#endif /* TSUKASA_SHELL_H */
