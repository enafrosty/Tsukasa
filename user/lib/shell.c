#include "../include/shell.h"

#include "../include/fcntl.h"
#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "../lib/syscall.h"

#define SH_MAX_SEGMENTS 8
#define SH_MAX_STAGE_LEN 256

typedef struct sh_stage {
    char text[SH_MAX_STAGE_LEN];
    char path[64];
} sh_stage_t;

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

static int split_pipeline(const char *line, sh_stage_t *stages, int max_stages)
{
    int count = 0;
    int quote = 0;
    int si = 0;
    if (!line || !stages || max_stages <= 0)
        return 0;
    for (int i = 0; line[i]; i++) {
        char c = line[i];
        if (c == '"')
            quote = !quote;
        if (c == '|' && !quote) {
            stages[count].text[si] = '\0';
            char *trimmed = trim_ws(stages[count].text);
            memmove(stages[count].text, trimmed, strlen(trimmed) + 1);
            stages[count].text[SH_MAX_STAGE_LEN - 1] = '\0';
            count++;
            if (count >= max_stages)
                return count;
            si = 0;
            stages[count].text[0] = '\0';
            continue;
        }
        if (si + 1 < SH_MAX_STAGE_LEN)
            stages[count].text[si++] = c;
    }
    stages[count].text[si] = '\0';
    {
        char *trimmed = trim_ws(stages[count].text);
        memmove(stages[count].text, trimmed, strlen(trimmed) + 1);
    }
    stages[count].text[SH_MAX_STAGE_LEN - 1] = '\0';
    if (stages[count].text[0])
        count++;
    return count;
}

static int extract_redirection(char *stage, char *out_path, int out_cap, int *append)
{
    int quote = 0;
    if (!stage || !out_path || out_cap <= 0 || !append)
        return -1;
    out_path[0] = '\0';
    *append = 0;

    for (int i = 0; stage[i]; i++) {
        if (stage[i] == '"')
            quote = !quote;
        if (quote || stage[i] != '>')
            continue;

        {
            int j = i + 1;
            int oi = 0;
            if (stage[j] == '>') {
                *append = 1;
                j++;
            }
            stage[i] = '\0';
            while (stage[j] == ' ' || stage[j] == '\t')
                j++;
            if (stage[j] == '"')
                j++;
            while (stage[j] && stage[j] != '"' &&
                   stage[j] != ' ' && stage[j] != '\t' &&
                   oi + 1 < out_cap) {
                out_path[oi++] = stage[j++];
            }
            out_path[oi] = '\0';
            {
                char *trimmed = trim_ws(stage);
                memmove(stage, trimmed, strlen(trimmed) + 1);
            }
            stage[SH_MAX_STAGE_LEN - 1] = '\0';
            return (out_path[0] != '\0') ? 0 : -1;
        }
    }
    return 0;
}

static int first_token(const char *s, char *tok, int cap)
{
    int i = 0;
    int j = 0;
    if (!s || !tok || cap <= 0)
        return -1;
    tok[0] = '\0';
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    while (s[i] && s[i] != ' ' && s[i] != '\t' && j + 1 < cap)
        tok[j++] = s[i++];
    tok[j] = '\0';
    return (j > 0) ? 0 : -1;
}

static void resolve_command(const char *cmd, char *path, int cap)
{
    if (!cmd || !path || cap <= 0) {
        if (path && cap > 0)
            path[0] = '\0';
        return;
    }
    if (strchr(cmd, '/')) {
        strncpy(path, cmd, (size_t)cap - 1);
        path[cap - 1] = '\0';
        return;
    }

    if (strcmp(cmd, "help") == 0) strncpy(path, "/bin/help", (size_t)cap - 1);
    else if (strcmp(cmd, "echo") == 0) strncpy(path, "/bin/echo", (size_t)cap - 1);
    else if (strcmp(cmd, "pwd") == 0) strncpy(path, "/bin/pwd", (size_t)cap - 1);
    else if (strcmp(cmd, "date") == 0) strncpy(path, "/bin/date", (size_t)cap - 1);
    else if (strcmp(cmd, "sort") == 0) strncpy(path, "/bin/sort", (size_t)cap - 1);
    else if (strcmp(cmd, "sysfetch") == 0) strncpy(path, "/bin/sysfetch", (size_t)cap - 1);
    else if (strcmp(cmd, "man") == 0) strncpy(path, "/bin/man", (size_t)cap - 1);
    else if (strcmp(cmd, "grep") == 0) strncpy(path, "/bin/grep", (size_t)cap - 1);
    else if (strcmp(cmd, "cat") == 0) strncpy(path, "/bin/cat", (size_t)cap - 1);
    else if (strcmp(cmd, "net") == 0) strncpy(path, "/bin/net", (size_t)cap - 1);
    else if (strcmp(cmd, "ping") == 0) strncpy(path, "/bin/ping", (size_t)cap - 1);
    else if (strcmp(cmd, "telnet") == 0) strncpy(path, "/bin/telnet", (size_t)cap - 1);
    else if (strcmp(cmd, "abi-test") == 0) strncpy(path, "/bin/abi-test", (size_t)cap - 1);
    else {
        strncpy(path, "/bin/", (size_t)cap - 1);
        path[cap - 1] = '\0';
        if (strlen(path) + strlen(cmd) + 1 < (size_t)cap)
            strcat(path, cmd);
    }
    path[cap - 1] = '\0';
}

static int run_builtin_cd(const char *stage, int err_fd)
{
    char tmp[SH_MAX_STAGE_LEN];
    char *p = tmp;
    char *arg;
    strncpy(tmp, stage, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    while (*p && *p != ' ' && *p != '\t')
        p++;
    while (*p == ' ' || *p == '\t')
        p++;
    arg = p;
    if (!arg[0])
        arg = "/";
    if (chdir(arg) != 0) {
        dprintf(err_fd, "cd: cannot change directory to %s\n", arg);
        return -1;
    }
    return 0;
}

int shell_exec_line(const char *line, int out_fd, int err_fd)
{
    sh_stage_t stages[SH_MAX_SEGMENTS];
    int pipefds[SH_MAX_SEGMENTS - 1][2];
    int pids[SH_MAX_SEGMENTS];
    int stage_count;
    int redir_fd = -1;
    char redir_path[128];
    int redir_append = 0;
    int status = 0;

    for (int i = 0; i < SH_MAX_SEGMENTS; i++)
        pids[i] = -1;
    for (int i = 0; i < SH_MAX_SEGMENTS - 1; i++) {
        pipefds[i][0] = -1;
        pipefds[i][1] = -1;
    }

    stage_count = split_pipeline(line, stages, SH_MAX_SEGMENTS);
    if (stage_count <= 0)
        return 0;

    if (extract_redirection(stages[stage_count - 1].text,
                            redir_path,
                            sizeof(redir_path),
                            &redir_append) != 0) {
        dprintf(err_fd, "shell: invalid redirection\n");
        return -1;
    }

    {
        char cmd[32];
        if (first_token(stages[0].text, cmd, sizeof(cmd)) == 0 &&
            stage_count == 1 &&
            strcmp(cmd, "cd") == 0) {
            return run_builtin_cd(stages[0].text, err_fd);
        }
    }

    if (redir_path[0]) {
        int flags = O_WRONLY | O_CREAT | (redir_append ? O_APPEND : O_TRUNC);
        redir_fd = open(redir_path, flags, 0);
        if (redir_fd < 0) {
            dprintf(err_fd, "shell: cannot open %s\n", redir_path);
            return -1;
        }
    }

    for (int i = 0; i + 1 < stage_count; i++) {
        if (pipe(pipefds[i]) != 0) {
            dprintf(err_fd, "shell: pipe creation failed\n");
            goto cleanup;
        }
    }

    for (int i = 0; i < stage_count; i++) {
        char cmd[32];
        struct tsukasa_spawn_request req;
        int out_target = (i == stage_count - 1) ?
            (redir_fd >= 0 ? redir_fd : out_fd) : pipefds[i][1];
        int in_target = (i == 0) ? STDIN_FILENO : pipefds[i - 1][0];

        if (first_token(stages[i].text, cmd, sizeof(cmd)) != 0)
            continue;
        resolve_command(cmd, stages[i].path, (int)sizeof(stages[i].path));

        req.path = stages[i].path;
        req.args = stages[i].text;
        req.stdin_fd = in_target;
        req.stdout_fd = out_target;
        req.stderr_fd = err_fd;
        req.tty_id = -1;
        pids[i] = spawn_ex(&req);
        if (pids[i] < 0) {
            dprintf(err_fd, "%s: command not found\n", cmd);
            goto cleanup;
        }
    }

    for (int i = 0; i < stage_count; i++) {
        if (pids[i] >= 0) {
            int st = 0;
            if (waitpid(pids[i], &st, 0) > 0)
                status = st;
        }
    }

cleanup:
    for (int i = 0; i + 1 < stage_count; i++) {
        if (pipefds[i][0] >= 0)
            close(pipefds[i][0]);
        if (pipefds[i][1] >= 0)
            close(pipefds[i][1]);
    }
    if (redir_fd >= 0)
        close(redir_fd);
    return status;
}

int shell_run_rc_file(const char *path, int out_fd, int err_fd)
{
    int fd;
    char buf[768];
    size_t n;
    char *p;
    if (!path)
        return -1;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -1;
    n = (size_t)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n == 0 || n == (size_t)-1)
        return -1;
    buf[n] = '\0';

    p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            *p++ = '\0';
        line = trim_ws(line);
        if (!line[0] || line[0] == '#')
            continue;
        shell_exec_line(line, out_fd, err_fd);
    }
    return 0;
}
