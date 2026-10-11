/* Minimal freestanding libc subset for the calculator build. */
#include <stddef.h>
#include <stdarg.h>

void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    if (((size_t)p & 3) == 0 && n >= 16) {
        unsigned int v = (unsigned char)c * 0x01010101u;
        unsigned int *q = (unsigned int *)p;
        while (n >= 4) { *q++ = v; n -= 4; }
        p = (unsigned char *)q;
    }
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if ((((size_t)a | (size_t)b) & 3) == 0) {
        unsigned int *x = (unsigned int *)a; const unsigned int *y = (const unsigned int *)b;
        while (n >= 4) { *x++ = *y++; n -= 4; }
        a = (unsigned char *)x; b = (const unsigned char *)y;
    }
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d; const unsigned char *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t o = 0;
#define PUT(c) do { if (o + 1 < size) buf[o] = (c); o++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int zero = 0, width = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        char tmp[16]; int n = 0;
        if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x') {
            unsigned int v; int neg = 0;
            if (*fmt == 'd') { int s = va_arg(ap, int); if (s < 0) { neg = 1; v = (unsigned)-s; } else v = (unsigned)s; }
            else v = va_arg(ap, unsigned int);
            unsigned base = *fmt == 'x' ? 16 : 10;
            do { tmp[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            if (neg) tmp[n++] = '-';
            while (n < width) tmp[n++] = zero ? '0' : ' ';
            while (n) PUT(tmp[--n]);
        } else if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            while (*s) PUT(*s++);
        } else if (*fmt == 'c') {
            PUT((char)va_arg(ap, int));
        } else if (*fmt == '%') PUT('%');
        else if (!*fmt) break;
    }
    if (size) buf[o < size ? o : size - 1] = 0;
    return (int)o;
#undef PUT
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}
