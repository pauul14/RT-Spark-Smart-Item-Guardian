/*
 * mini_printf.h - small reentrant snprintf (no heap, no locale, no newlib stdio).
 * Supports: %% %c %s %d %i %u %x %X, flags '-' '0' '+', width, precision,
 * length l / ll / z, and %f / %.Nf (N <= 6) for float/double arguments.
 */
#ifndef SIG_MINI_PRINTF_H
#define SIG_MINI_PRINTF_H

#include <stdarg.h>
#include <stddef.h>

int sig_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int sig_snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif
