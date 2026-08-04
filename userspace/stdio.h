/*
 * Userspace libc — stdio functions built on syscalls
 */

#ifndef USR_STDIO_H
#define USR_STDIO_H

#include "syscall.h"
#include "string.h"
#include <stdint.h>

static inline long write(const char *str, unsigned long len)
{
    if (len == 0)
        len = strlen(str);
    return syscall2(SYS_WRITE, (long)str, (long)len);
}

static inline long read(char *buf, unsigned long len)
{
    return syscall2(SYS_READ, (long)buf, (long)len);
}

static inline void puts(const char *s)
{
    write(s, strlen(s));
    write("\n", 1);
}

static inline void putchar(char c)
{
    write(&c, 1);
}

static void print_unsigned(unsigned long val, int base, int upper)
{
    char buf[32];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0)
    {
        putchar('0');
        return;
    }

    while (val && i < 32)
    {
        buf[i++] = digits[val % base];
        val /= base;
    }

    while (i > 0)
        putchar(buf[--i]);
}

static inline void printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt)
    {
        if (*fmt != '%')
        {
            putchar(*fmt);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
            case 'c':
                putchar((char)__builtin_va_arg(args, int));
                break;
            case 's':
            {
                const char *s = __builtin_va_arg(args, const char *);
                if (s)
                    write(s, strlen(s));
                else
                    write("(null)", 6);
                break;
            }
            case 'd':
            {
                int v = __builtin_va_arg(args, int);
                if (v < 0)
                {
                    putchar('-');
                    v = -v;
                }
                print_unsigned((unsigned long)v, 10, 0);
                break;
            }
            case 'u':
                print_unsigned((unsigned long)__builtin_va_arg(args, unsigned), 10, 0);
                break;
            case 'x':
                print_unsigned((unsigned long)__builtin_va_arg(args, unsigned), 16, 0);
                break;
            case 'X':
                print_unsigned((unsigned long)__builtin_va_arg(args, unsigned), 16, 1);
                break;
            case 'p':
                write("0x", 2);
                print_unsigned((unsigned long)__builtin_va_arg(args, void *), 16, 0);
                break;
            case '%':
                putchar('%');
                break;
            default:
                putchar('%');
                putchar(*fmt);
                break;
        }
        fmt++;
    }

    __builtin_va_end(args);
}

#endif /* USR_STDIO_H */