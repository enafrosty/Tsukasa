#include "kutils.h"

void k_memset(void *dest, int val, size_t len) {
    unsigned char *ptr = dest;
    while (len--) {
        *ptr++ = (unsigned char)val;
    }
}

void k_memcpy(void *dest, const void *src, size_t len) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (len--) {
        *d++ = *s++;
    }
}

size_t k_strlen(const char *str) {
    const char *s = str;
    while (*s) ++s;
    return (size_t)(s - str);
}

int k_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int k_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

void k_strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++) != '\0') {
    }
}

int k_atoi(const char *str) {
    int value = 0;
    int sign = 1;

    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    if (*str == '+' || *str == '-') {
        if (*str == '-') sign = -1;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        str++;
    }
    return value * sign;
}

void k_itoa(int n, char *buf) {
    char tmp[16];
    int i = 0;
    int is_negative = 0;

    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    if (n < 0) {
        is_negative = 1;
        n = -n;
    }
    while (n > 0) {
        tmp[i++] = (char)((n % 10) + '0');
        n /= 10;
    }
    if (is_negative) {
        tmp[i++] = '-';
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

void k_itoa_hex(uint64_t n, char *buf) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[17];
    int i = 0;

    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (n > 0) {
        tmp[i++] = hex[n & 0xF];
        n >>= 4;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

void k_delay(int iterations) {
    while (iterations-- > 0) {
        __asm__ volatile ("nop");
    }
}

void k_sleep(int ms) {
    (void)ms;
}

void k_reboot(void) {
    __asm__ volatile ("cli\n"
                      "hlt\n");
}

void k_shutdown(void) {
    __asm__ volatile ("cli\n"
                      "hlt\n");
}

void k_beep(int freq, int ms) {
    (void)freq;
    (void)ms;
}

void k_beep_process(void) {
}

char *k_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            ++h; ++n;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

/* glibc compatibility stubs for ctype locale support */
/* These are called by some libc code; provide minimal stubs */
unsigned char **__ctype_b_loc(void) {
    static unsigned char *ctype_b = NULL;
    if (!ctype_b) {
        ctype_b = (unsigned char *)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
                                  "\x20\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10\x10"
                                  "\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x10\x10\x10\x10\x10\x10"
                                  "\x10\x41\x41\x41\x41\x41\x41\x01\x01\x01\x01\x01\x01\x01\x01\x01"
                                  "\x01\x01\x01\x01\x01\x01\x01\x01\x01\x01\x10\x10\x10\x10\x10\x10"
                                  "\x10\x42\x42\x42\x42\x42\x42\x02\x02\x02\x02\x02\x02\x02\x02\x02"
                                  "\x02\x02\x02\x02\x02\x02\x02\x02\x02\x02\x10\x10\x10\x10\x10\0";
    }
    return &ctype_b;
}

int *__ctype_tolower_loc(void) {
    static int tolower_table[256];
    static int initialized = 0;
    if (!initialized) {
        int i;
        for (i = 0; i < 256; i++) {
            if (i >= 'A' && i <= 'Z') {
                tolower_table[i] = i + 32;
            } else {
                tolower_table[i] = i;
            }
        }
        initialized = 1;
    }
    return tolower_table;
}

int *__ctype_toupper_loc(void) {
    static int toupper_table[256];
    static int initialized = 0;
    if (!initialized) {
        int i;
        for (i = 0; i < 256; i++) {
            if (i >= 'a' && i <= 'z') {
                toupper_table[i] = i - 32;
            } else {
                toupper_table[i] = i;
            }
        }
        initialized = 1;
    }
    return toupper_table;
}
