/*
 * Project Tsukasa — Standard Input and Output Header
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

#ifndef _TSUKASA_STDIO_H
#define _TSUKASA_STDIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define BUFSIZ 1024
#define EOF    (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

enum {
    _IONBF, /* Unbuffered */
    _IOLBF, /* Line buffered */
    _IOFBF  /* Fully buffered */
};

typedef struct FILE {
    int fd;
    int flags;
    int mode;           /* _IONBF, _IOLBF, _IOFBF */
    int error;
    int eof;
    char *buf;
    size_t buf_size;
    size_t rpos;
    size_t rend;
    size_t wpos;
    int buf_owned;
    char unbuf[1];      /* Small buffer for _IONBF */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *pathname, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *pathname, const char *mode, FILE *stream);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

int ungetc(int c, FILE *stream);

int getchar(void);
int putchar(int c);
int puts(const char *s);

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int dprintf(int fd, const char *fmt, ...);
int vdprintf(int fd, const char *fmt, va_list ap);
int sprintf(char *out, const char *fmt, ...);
int snprintf(char *out, size_t size, const char *fmt, ...);
int vsnprintf(char *out, size_t size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int vsscanf(const char *str, const char *fmt, va_list ap);

int rename(const char *oldpath, const char *newpath);
int remove(const char *pathname);
int fileno(FILE *stream);
FILE *tmpfile(void);
char *tmpnam(char *s);
void perror(const char *s);

FILE *popen(const char *command, const char *type);
int pclose(FILE *stream);

#define getc(f)         fgetc(f)
#define putc(c, f)      fputc((c), (f))
#define getc_unlocked(f) fgetc(f)
#define flockfile(f)    ((void)(f))
#define funlockfile(f)  ((void)(f))
#define fseeko(f, o, w) fseek((f), (long)(o), (w))
#define ftello(f)       ftell(f)

#endif /* _TSUKASA_STDIO_H */

