#include "mini_printf.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct { char *buf; size_t size; size_t len; } out_t;

static void put(out_t *o, char c)
{
    if (o->len + 1 < o->size) o->buf[o->len] = c;
    o->len++;
}

static void put_padded(out_t *o, const char *s, int n, int width, bool left, char pad, char sign)
{
    int total = n + (sign ? 1 : 0);
    if (!left && pad == '0' && sign) { put(o, sign); sign = 0; }
    if (!left) for (int i = total; i < width; i++) put(o, pad);
    if (sign) put(o, sign);
    for (int i = 0; i < n; i++) put(o, s[i]);
    if (left) for (int i = total; i < width; i++) put(o, ' ');
}

static int utoa_rev(unsigned long long v, unsigned base, bool upper, char *tmp)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { tmp[n++] = dig[v % base]; v /= base; } while (v);
    return n;
}

static void put_uint(out_t *o, unsigned long long v, unsigned base, bool upper,
                     int width, bool left, char pad, char sign)
{
    char tmp[24], s[24];
    int n = utoa_rev(v, base, upper, tmp);
    for (int i = 0; i < n; i++) s[i] = tmp[n - 1 - i];
    put_padded(o, s, n, width, left, pad, sign);
}

static void put_float(out_t *o, double v, int prec, int width, bool left, char pad, bool plus)
{
    char s[40];
    int n = 0;
    char sign = 0;
    if (v != v) { put_padded(o, "nan", 3, width, left, ' ', 0); return; }
    if (v < 0) { sign = '-'; v = -v; } else if (plus) { sign = '+'; }
    if (v > 4.0e9) { put_padded(o, "inf", 3, width, left, ' ', sign); return; }
    if (prec > 6) prec = 6;
    static const uint32_t p10[] = {1, 10, 100, 1000, 10000, 100000, 1000000};
    uint32_t scale = p10[prec];
    double scaled = v * (double)scale + 0.5;
    unsigned long long whole = (unsigned long long)scaled;
    unsigned long long ip = whole / scale;
    uint32_t fp = (uint32_t)(whole % scale);
    char tmp[24];
    int k = utoa_rev(ip, 10, false, tmp);
    for (int i = 0; i < k; i++) s[n++] = tmp[k - 1 - i];
    if (prec > 0) {
        s[n++] = '.';
        for (int i = prec - 1; i >= 0; i--) { s[n + i] = (char)('0' + fp % 10); fp /= 10; }
        n += prec;
    }
    put_padded(o, s, n, width, left, pad, sign);
}

int sig_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    out_t o = { buf, size, 0 };
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { put(&o, *p); continue; }
        p++;
        bool left = false, plus = false;
        char pad = ' ';
        for (;; p++) {
            if (*p == '-') left = true;
            else if (*p == '0') pad = '0';
            else if (*p == '+') plus = true;
            else break;
        }
        int width = 0;
        if (*p == '*') { width = va_arg(ap, int); p++; }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        int prec = -1;
        if (*p == '.') {
            p++; prec = 0;
            while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        int lng = 0;
        if (*p == 'z') { lng = 1; p++; }
        while (*p == 'l') { lng++; p++; }
        if (left) pad = ' ';
        switch (*p) {
        case '%': put(&o, '%'); break;
        case 'c': { char c = (char)va_arg(ap, int); put_padded(&o, &c, 1, width, left, ' ', 0); break; }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int n = 0;
            while (s[n] && (prec < 0 || n < prec)) n++;
            put_padded(&o, s, n, width, left, ' ', 0);
            break;
        }
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : (lng == 1 ? (long long)va_arg(ap, long) : va_arg(ap, int));
            char sign = v < 0 ? '-' : (plus ? '+' : 0);
            unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1u : (unsigned long long)v;
            put_uint(&o, u, 10, false, width, left, pad, sign);
            break;
        }
        case 'u': case 'x': case 'X': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long)
                                 : (lng == 1 ? (unsigned long long)va_arg(ap, unsigned long) : va_arg(ap, unsigned));
            put_uint(&o, v, *p == 'u' ? 10 : 16, *p == 'X', width, left, pad, 0);
            break;
        }
        case 'f': put_float(&o, va_arg(ap, double), prec < 0 ? 6 : prec, width, left, pad, plus); break;
        case '\0': p--; break;
        default: put(&o, '%'); put(&o, *p); break;
        }
    }
    if (size) buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int sig_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = sig_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
