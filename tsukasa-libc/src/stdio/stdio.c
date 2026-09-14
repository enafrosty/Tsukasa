/*
 * Project Tsukasa — Standard I/O Output Functions Implementation
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_OPEN_FILES 64

static char stdin_buf_data[BUFSIZ];
static char stdout_buf_data[BUFSIZ];

static FILE stdin_stream = {
    .fd = 0,
    .flags = O_RDONLY,
    .mode = _IOLBF,
    .error = 0,
    .eof = 0,
    .buf = stdin_buf_data,
    .buf_size = sizeof(stdin_buf_data),
    .rpos = 0,
    .rend = 0,
    .wpos = 0,
    .buf_owned = 0,
    .unbuf = {0}
};

static FILE stdout_stream = {
    .fd = 1,
    .flags = O_WRONLY,
    .mode = _IOLBF,
    .error = 0,
    .eof = 0,
    .buf = stdout_buf_data,
    .buf_size = sizeof(stdout_buf_data),
    .rpos = 0,
    .rend = 0,
    .wpos = 0,
    .buf_owned = 0,
    .unbuf = {0}
};

static FILE stderr_stream = {
    .fd = 2,
    .flags = O_WRONLY,
    .mode = _IONBF,
    .error = 0,
    .eof = 0,
    .buf = stderr_stream.unbuf,
    .buf_size = sizeof(stderr_stream.unbuf),
    .rpos = 0,
    .rend = 0,
    .wpos = 0,
    .buf_owned = 0,
    .unbuf = {0}
};

FILE *stdin = &stdin_stream;
FILE *stdout = &stdout_stream;
FILE *stderr = &stderr_stream;

static FILE *open_streams[MAX_OPEN_FILES] = {
    &stdin_stream,
    &stdout_stream,
    &stderr_stream
};

int fflush(FILE *stream)
{
    if (!stream) {
        int ret = 0;
        for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
            if (open_streams[i] && fflush(open_streams[i]) == EOF)
                ret = EOF;
        }
        return ret;
    }

    if (stream->wpos > 0) {
        size_t written = 0;
        while (written < stream->wpos) {
            ssize_t n = write(stream->fd, stream->buf + written, stream->wpos - written);
            if (n <= 0) {
                stream->error = 1;
                return EOF;
            }
            written += (size_t)n;
        }
        stream->wpos = 0;
    }
    return 0;
}

FILE *fopen(const char *pathname, const char *mode)
{
    if (!pathname || !mode) {
        errno = EINVAL;
        return NULL;
    }

    int flags = 0;
    if (mode[0] == 'r') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR;
        else
            flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR | O_CREAT | O_TRUNC;
        else
            flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR | O_CREAT | O_APPEND;
        else
            flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        errno = EINVAL;
        return NULL;
    }

    int slot = -1;
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_streams[i]) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        errno = EMFILE;
        return NULL;
    }

    int fd = open(pathname, flags, 0644);
    if (fd < 0)
        return NULL;

    FILE *fp = (FILE *)malloc(sizeof(FILE));
    if (!fp) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    char *buf = (char *)malloc(BUFSIZ);
    if (!buf) {
        free(fp);
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    fp->fd = fd;
    fp->flags = flags;
    fp->mode = _IOFBF;
    fp->error = 0;
    fp->eof = 0;
    fp->buf = buf;
    fp->buf_size = BUFSIZ;
    fp->rpos = 0;
    fp->rend = 0;
    fp->wpos = 0;
    fp->buf_owned = 1;
    fp->unbuf[0] = 0;

    open_streams[slot] = fp;
    return fp;
}

FILE *fdopen(int fd, const char *mode)
{
    if (fd < 0 || !mode) {
        errno = EINVAL;
        return NULL;
    }

    int flags = 0;
    if (mode[0] == 'r') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR;
        else
            flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR;
        else
            flags = O_WRONLY;
    } else if (mode[0] == 'a') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR | O_APPEND;
        else
            flags = O_WRONLY | O_APPEND;
    } else {
        errno = EINVAL;
        return NULL;
    }

    int slot = -1;
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_streams[i]) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        errno = EMFILE;
        return NULL;
    }

    FILE *fp = (FILE *)malloc(sizeof(FILE));
    if (!fp) {
        errno = ENOMEM;
        return NULL;
    }

    char *buf = (char *)malloc(BUFSIZ);
    if (!buf) {
        free(fp);
        errno = ENOMEM;
        return NULL;
    }

    fp->fd = fd;
    fp->flags = flags;
    fp->mode = _IOFBF;
    fp->error = 0;
    fp->eof = 0;
    fp->buf = buf;
    fp->buf_size = BUFSIZ;
    fp->rpos = 0;
    fp->rend = 0;
    fp->wpos = 0;
    fp->buf_owned = 1;
    fp->unbuf[0] = 0;

    open_streams[slot] = fp;
    return fp;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream)
{
    if (!stream)
        return pathname ? fopen(pathname, mode) : NULL;

    fflush(stream);
    if (stream->fd >= 0)
        close(stream->fd);

    if (!pathname)
        return stream;

    int flags = 0;
    if (mode[0] == 'r') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR;
        else
            flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR | O_CREAT | O_TRUNC;
        else
            flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        if (mode[1] == '+' || (mode[1] == 'b' && mode[2] == '+'))
            flags = O_RDWR | O_CREAT | O_APPEND;
        else
            flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        errno = EINVAL;
        return NULL;
    }

    int fd = open(pathname, flags, 0644);
    if (fd < 0)
        return NULL;

    stream->fd = fd;
    stream->flags = flags;
    stream->rpos = 0;
    stream->rend = 0;
    stream->wpos = 0;
    stream->error = 0;
    stream->eof = 0;
    return stream;
}

int fclose(FILE *stream)
{
    if (!stream)
        return EOF;

    int res = fflush(stream);

    if (close(stream->fd) < 0)
        res = EOF;

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_streams[i] == stream) {
            open_streams[i] = NULL;
            break;
        }
    }

    if (stream->buf_owned && stream->buf && stream->buf != stream->unbuf) {
        free(stream->buf);
        stream->buf = NULL;
    }

    if (stream != &stdin_stream && stream != &stdout_stream && stream != &stderr_stream) {
        free(stream);
    }

    return res;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || size == 0 || nmemb == 0 || !ptr)
        return 0;

    if (stream->wpos > 0)
        fflush(stream);

    size_t total_bytes = size * nmemb;
    size_t bytes_read = 0;
    char *dest = (char *)ptr;

    while (bytes_read < total_bytes) {
        if (stream->rpos < stream->rend) {
            size_t avail = stream->rend - stream->rpos;
            size_t needed = total_bytes - bytes_read;
            size_t to_copy = (avail < needed) ? avail : needed;
            memcpy(dest + bytes_read, stream->buf + stream->rpos, to_copy);
            stream->rpos += to_copy;
            bytes_read += to_copy;
            continue;
        }

        if (stream->eof)
            break;

        if (stream->mode == _IONBF) {
            ssize_t n = read(stream->fd, dest + bytes_read, total_bytes - bytes_read);
            if (n <= 0) {
                if (n == 0)
                    stream->eof = 1;
                else
                    stream->error = 1;
                break;
            }
            bytes_read += (size_t)n;
            continue;
        }

        ssize_t n = read(stream->fd, stream->buf, stream->buf_size);
        if (n <= 0) {
            if (n == 0)
                stream->eof = 1;
            else
                stream->error = 1;
            break;
        }
        stream->rpos = 0;
        stream->rend = (size_t)n;
    }

    return bytes_read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    if (!stream || size == 0 || nmemb == 0 || !ptr)
        return 0;

    if (stream->rend > stream->rpos) {
        lseek(stream->fd, -(off_t)(stream->rend - stream->rpos), SEEK_CUR);
        stream->rpos = stream->rend = 0;
    }

    size_t total_bytes = size * nmemb;
    size_t bytes_written = 0;
    const char *src = (const char *)ptr;

    while (bytes_written < total_bytes) {
        if (stream->mode == _IONBF) {
            ssize_t n = write(stream->fd, src + bytes_written, total_bytes - bytes_written);
            if (n <= 0) {
                stream->error = 1;
                break;
            }
            bytes_written += (size_t)n;
            continue;
        }

        size_t space = stream->buf_size - stream->wpos;
        if (space == 0) {
            if (fflush(stream) == EOF)
                break;
            space = stream->buf_size;
        }

        size_t needed = total_bytes - bytes_written;
        size_t to_copy = (needed < space) ? needed : space;
        memcpy(stream->buf + stream->wpos, src + bytes_written, to_copy);
        stream->wpos += to_copy;
        bytes_written += to_copy;

        if (stream->mode == _IOLBF) {
            for (size_t i = 0; i < to_copy; i++) {
                if (src[bytes_written - to_copy + i] == '\n') {
                    fflush(stream);
                    break;
                }
            }
        }
    }

    return bytes_written / size;
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }

    if (fflush(stream) == EOF)
        return -1;

    if (whence == SEEK_CUR) {
        if (stream->rend > stream->rpos)
            offset -= (long)(stream->rend - stream->rpos);
    }

    stream->rpos = 0;
    stream->rend = 0;
    stream->eof = 0;

    off_t res = lseek(stream->fd, (off_t)offset, whence);
    if (res < 0)
        return -1;

    return 0;
}

long ftell(FILE *stream)
{
    if (!stream) {
        errno = EBADF;
        return -1L;
    }

    off_t pos = lseek(stream->fd, 0, SEEK_CUR);
    if (pos < 0)
        return -1L;

    if (stream->rend > stream->rpos) {
        pos -= (off_t)(stream->rend - stream->rpos);
    }

    if (stream->wpos > 0) {
        pos += (off_t)stream->wpos;
    }

    return (long)pos;
}

int fgetc(FILE *stream)
{
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1)
        return (int)c;
    return EOF;
}

int getchar(void)
{
    return fgetc(stdin);
}

int fputc(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, stream) == 1)
        return (int)ch;
    return EOF;
}

int putchar(int c)
{
    return fputc(c, stdout);
}

char *fgets(char *s, int size, FILE *stream)
{
    if (!s || size <= 0 || !stream)
        return NULL;

    int idx = 0;
    while (idx < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) {
            if (idx == 0)
                return NULL;
            break;
        }
        s[idx++] = (char)c;
        if (c == '\n')
            break;
    }
    s[idx] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream)
{
    if (!s || !stream)
        return EOF;
    size_t len = strlen(s);
    if (fwrite(s, 1, len, stream) == len)
        return 0;
    return EOF;
}

int puts(const char *s)
{
    if (fputs(s ? s : "(null)", stdout) == EOF)
        return EOF;
    if (fputc('\n', stdout) == EOF)
        return EOF;
    return 0;
}

int feof(FILE *stream)
{
    return stream ? stream->eof : 0;
}

int ferror(FILE *stream)
{
    return stream ? stream->error : 0;
}

void clearerr(FILE *stream)
{
    if (stream) {
        stream->eof = 0;
        stream->error = 0;
    }
}

int setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    if (!stream)
        return -1;
    if (mode != _IONBF && mode != _IOLBF && mode != _IOFBF)
        return -1;

    fflush(stream);

    if (stream->buf_owned && stream->buf && stream->buf != stream->unbuf) {
        free(stream->buf);
        stream->buf_owned = 0;
    }

    stream->mode = mode;
    if (mode == _IONBF || !buf || size == 0) {
        stream->buf = stream->unbuf;
        stream->buf_size = 1;
        stream->buf_owned = 0;
    } else {
        stream->buf = buf;
        stream->buf_size = size;
        stream->buf_owned = 0;
    }
    stream->rpos = 0;
    stream->rend = 0;
    stream->wpos = 0;
    return 0;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    char buf[1024];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(buf, sizeof(buf), fmt, ap_copy);
    va_end(ap_copy);

    if (needed < 0)
        return -1;

    if ((size_t)needed < sizeof(buf)) {
        size_t written = fwrite(buf, 1, (size_t)needed, stream);
        return (written == (size_t)needed) ? needed : -1;
    }

    char *dyn = (char *)malloc((size_t)needed + 1);
    if (!dyn)
        return -1;

    vsnprintf(dyn, (size_t)needed + 1, fmt, ap);
    size_t written = fwrite(dyn, 1, (size_t)needed, stream);
    free(dyn);
    return (written == (size_t)needed) ? needed : -1;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int vdprintf(int fd, const char *fmt, va_list ap)
{
    char buf[1024];
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(buf, sizeof(buf), fmt, ap_copy);
    va_end(ap_copy);

    if (needed < 0)
        return -1;

    if ((size_t)needed < sizeof(buf)) {
        return (int)write(fd, buf, (size_t)needed);
    }

    char *dyn = (char *)malloc((size_t)needed + 1);
    if (!dyn)
        return -1;

    vsnprintf(dyn, (size_t)needed + 1, fmt, ap);
    int written = (int)write(fd, dyn, (size_t)needed);
    free(dyn);
    return written;
}

int dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vdprintf(fd, fmt, ap);
    va_end(ap);
    return rc;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}

void perror(const char *s)
{
    const char *err = strerror(errno);
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, err);
    else
        fprintf(stderr, "%s\n", err);
}

int ungetc(int c, FILE *stream)
{
    if (c == EOF || !stream)
        return EOF;

    if (stream->rpos > 0) {
        stream->rpos--;
        stream->buf[stream->rpos] = (char)c;
        stream->eof = 0;
        return (unsigned char)c;
    }

    if (stream->rend < stream->buf_size) {
        memmove(stream->buf + 1, stream->buf, stream->rend);
        stream->rend++;
        stream->buf[0] = (char)c;
        stream->eof = 0;
        return (unsigned char)c;
    }

    return EOF;
}

int remove(const char *pathname)
{
    return unlink(pathname);
}

int fileno(FILE *stream)
{
    if (!stream) {
        errno = EBADF;
        return -1;
    }
    return stream->fd;
}

static unsigned int g_tmpnam_counter = 0;
char *tmpnam(char *s)
{
    static char buf[32];
    char *out = s ? s : buf;
    snprintf(out, 32, "/tmp/t%u.tmp", ++g_tmpnam_counter);
    return out;
}

FILE *tmpfile(void)
{
    char name[32];
    tmpnam(name);
    FILE *fp = fopen(name, "w+");
    if (fp) {
        unlink(name);
    }
    return fp;
}

FILE *popen(const char *command, const char *type)
{
    if (!command || !type) {
        errno = EINVAL;
        return NULL;
    }

    if (type[0] == 'r') {
        char tmp[32];
        tmpnam(tmp);
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s > %s", command, tmp);
        system(cmd);
        FILE *fp = fopen(tmp, "r");
        if (fp) {
            unlink(tmp);
        }
        return fp;
    } else if (type[0] == 'w') {
        char tmp[32];
        tmpnam(tmp);
        FILE *fp = fopen(tmp, "w");
        return fp;
    }

    errno = EINVAL;
    return NULL;
}

int pclose(FILE *stream)
{
    return fclose(stream);
}

