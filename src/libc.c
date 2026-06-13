#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mm.h>

void putchar(char c)
{
    if (c == '\n')
        usart_putc('\r');
    usart_putc(c);
}

void vprintf(const char *fmt, va_list arg)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            putchar(*fmt);
            continue;
        }
        fmt++;
        switch (*fmt) {
        case '\0':
            goto DONE;
        case 's': {
            const char *s = va_arg(arg, const char *);
            while (*s)
                putchar(*s++);
            break;
        }
        case 'c': {
            int c = va_arg(arg, int);
            putchar(c);
            break;
        }
        case 'i':
        case 'd': {
            int x = va_arg(arg, int);
            int i = 0;
            if (x < 0) {
                putchar('-');
                x = -x;
            }
            print_uint(x);
            break;
        }
        case 'u':
            unsigned int y = va_arg(arg, unsigned int);
            print_uint(y);
            break;
        case 'p':
        case 'x':
            unsigned int z = va_arg(arg, unsigned int);
            for (int i = 7; i >= 0; i--) {
                putchar("0123456789abcdef"[(z >> (i << 2)) & 0xf]);
            }
            break;
        }
    }
DONE:
}

void printf(const char *fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    vprintf(fmt, arg);
    va_end(arg);
}

int snprintf(char *dst, unsigned long size, const char *fmt, ...)
{
    va_list arg;
    va_start(arg, fmt);
    int n = vsnprintf(dst, size, fmt, arg);
    va_end(arg);
    return n;
}

int vsnprintf(char *dst, unsigned long size, const char *fmt, va_list arg)
{
    unsigned long cnt = 0;
    for (; *fmt; fmt++) {
        if (cnt == size)
            goto DONE;
        if (*fmt != '%') {
            dst[cnt++] = *fmt;
            continue;
        }
        fmt++;

        /* Width / precision: %[0][width][.prec]conv. "%.2d" and "%02d" both
         * mean "at least N digits, zero-padded" for integers here. Ignored
         * for %s/%c/%x. */
        int zeropad = 0, width = 0, prec = -1;
        if (*fmt == '0') {
            zeropad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }

        switch (*fmt) {
        case '\0':
            goto DONE;
        case 's': {
            const char *s = va_arg(arg, const char *);
            while (*s && cnt < size)
                dst[cnt++] = *s++;

            if (cnt == size)
                goto DONE;
            break;
        }
        case 'c': {
            int c = va_arg(arg, int);
            dst[cnt++] = c;
            break;
        }
        case 'i':
        case 'd':
        case 'u': {
            unsigned int x;
            if (*fmt == 'u') {
                x = va_arg(arg, unsigned int);
            } else {
                int sx = va_arg(arg, int);
                if (sx < 0) {
                    if (cnt < size)
                        dst[cnt++] = '-';
                    sx = -sx;
                }
                x = (unsigned int) sx;
            }
            char buf[12];
            int i = 0;
            do {
                buf[i++] = (x % 10) + '0';
                x /= 10;
            } while (x > 0);
            int mindigits = prec >= 0 ? prec : (zeropad ? width : 0);
            while (i < mindigits && i < (int) sizeof(buf))
                buf[i++] = '0';
            while (i && cnt < size) {
                dst[cnt++] = buf[--i];
            }
            break;
        }
        case 'p':
        case 'x':
            unsigned int z = va_arg(arg, unsigned int);
            for (int i = 7; i >= 0 && cnt < size; i--)
                dst[cnt++] = "0123456789abcdef"[(z >> (i << 2)) & 0xf];
            break;
        }
    }
    if (!(*fmt) && cnt < size) {
        dst[cnt++] = *fmt;
    }
DONE:
    dst[size - 1] = 0;
}


int sscanf(const char *str, const char *format, ...)
{
    printf("sscanf not implemented");
    while (1)
        ;
    return 0;
}

// fopen/fclose/fread/fwrite/remove are implemented over littlefs in lfs_flash.c
// (the VFS layer). fseek/ftell/fflush stay here (unused by the save path).
// When SD_CARD==1 the FatFs VFS (vfs_fatfs.c) provides real fseek/ftell instead.
#if SD_CARD == 0
int fseek(FILE *stream, long offset, int whence)
{
    printf("fseek not implemented");
    while (1)
        ;
    return -1;
}

long ftell(FILE *stream)
{
    printf("ftell not implemented");
    while (1)
        ;
    return -1;
}
#endif // SD_CARD == 0

__attribute__((weak)) int fflush(FILE *stream)
{
    return 0;
}

int rename(const char *oldpath, const char *newpath)
{
    printf("rename not implemented");
    while (1)
        ;
    return -1;
}

int mkdir(const char *pathname, size_t mode)
{
    printf("mkdir not implemented");
    while (1)
        ;
    return -1;
}



int puts(const char *s)
{
    printf("%s\n", s);
    return 0;
}


void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    // block->size carries the in-use bit (bit 0); mask it off, or the usable
    // size is reported one byte too large -> in-place grows overflow by a byte
    // and the copy reads one byte past the old block.
    size_t blksz = ((block_t *) (ptr - sizeof(block_t)))->size & ~(size_t) 1;
    size_t usable = blksz - sizeof(block_t);
    if (size <= usable)
        return ptr;

    void *data = malloc(size);
    if (!data)
        return NULL;
    memcpy(data, ptr, usable);
    free(ptr);
    return data;
}


void exit(int status)
{
    printf("EXIT called with status %d", status);
    while (1)
        ;
}

int system(const char *command)
{
    printf("Do not call system\n");
    while (1)
        ;
    return -1;
}

int *__errno(void)
{
    printf("__errno not implemented");
    while (1)
        ;
    return 0;
}


int *__errno_location(void)
{
    printf("__errno_location not implemented");
    while (1)
        ;
    return 0;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *) s;
    while (n--)
        *p++ = (unsigned char) c;
    return s;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *) dst;
    const char *s = (const char *) src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    char *d = (char *) dst;
    const char *s = (const char *) src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

unsigned long strlen(const char *s)
{
    unsigned long len = 0;
    while (*s++)
        len++;
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;
    while (*dest++ = *src++)
        ;
    return tmp;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *tmp = dst;
    while (n--) {
        if (!(*dst = *src))
            break;
        dst++;
        src++;
    }
    return tmp;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}

int strncmp(const char *s1, const char *s2, unsigned long n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *) s1 - *(const unsigned char *) s2;
}

int strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && (tolower(*s1) == tolower(*s2))) {
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *) s1) - tolower(*(unsigned char *) s2);
}

int strncasecmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0)
        return 0;
    while ((n--) && tolower(*s1) == tolower(*s2)) {
        if (!n || !(*s1) || !(*s2))
            break;
        s1++;
        s2++;
    }
    return tolower(*(unsigned char *) s1) - tolower(*(unsigned char *) s2);
}

char *strchr(const char *s, int c)
{
    printf("strchr not implemented");
    while (1)
        ;
    return 0;
    while (*s != (char) c) {
        if (!*s++)
            return 0;
    }
    return (char *) s;
}

char *strrchr(const char *s, int c)
{
    printf("strrchr not implemented");
    while (1)
        ;
    return 0;
    const char *last = 0;
    do {
        if (*s == (char) c)
            last = s;
    } while (*s++);
    return (char *) last;
}

char *strstr(const char *haystack, const char *needle)
{
    printf("strstr not implemented");
    while (1)
        ;
    return 0;
    size_t n = strlen(needle);
    while (*haystack) {
        if (!memcmp(haystack, needle, n))
            return (char *) haystack;
        haystack++;
    }
    return 0;
}

char *strdup(const char *s)
{
    printf("strdup not implemented");
    while (1)
        ;
    return 0;
    size_t len = strlen(s) + 1;
    void *new = malloc(len);
    if (new == 0)
        return 0;
    return (char *) memcpy(new, s, len);
}

int atoi(const char *s)
{
    int sign = 1;
    int res = 0;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (*s) {
        res = res * 10 + *s - '0';
        s++;
    }
    return sign * res;
}

double atof(const char *ptr)
{
    if (*ptr)
        printf("atof not implemented");
    while (1)
        ;
    return 0;
}

int abs(int j)
{
    return (j < 0) ? -j : j;
}

double fabs(double x)
{
    return __builtin_fabs(x);
}

int toupper(int c)
{
    if (c < 'a' || c > 'z')
        return c;
    return c & ~32;
}

int tolower(int c)
{
    if (c < 'A' || c > 'Z')
        return c;
    return c | 32;
}

int isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
            c == '\r');
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *x = s1;
    const unsigned char *y = s2;
    for (; n; n--, x++, y++) {
        if (*x != *y)
            return (int)*x - (int)*y;   /* compare the FIRST differing byte */
    }
    return 0;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t n = 0;
    for (; s[n]; n++) {
        for (const char *r = reject; *r; r++)
            if (s[n] == *r)
                return n;
    }
    return n;
}

size_t strspn(const char *s, const char *accept)
{
    size_t n = 0;
    for (; s[n]; n++) {
        const char *a = accept;
        while (*a && *a != s[n])
            a++;
        if (!*a)
            return n;
    }
    return n;
}
