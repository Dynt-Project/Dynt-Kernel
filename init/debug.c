//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "debug.h"

#include "../arch/x86_64/io/serial.h"
#include "../driver/stacks/video/video_stack.h"
#include "../driver/buildin/video/vga/vga.h"

#include <stdarg.h>
#include <stdint.h>


// inits the debug output chain
void debug_init()
{
    serial_init();

    video_stack_init();
    vga_register();

    video_clear();
}


// puts one char, serial first so the early boot is never lost
void debug_putc(char c)
{
    serial_write_char(c);

    if (video_has_driver())
        video_putc(c);
}


// puts a string
void debug_puts(const char *str)
{
    while (*str)
        debug_putc(*str++);
}


// helper that prints an unsigned number in the given base
static void debug_put_u64(uint64_t value,
                          unsigned base,
                          bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF"
                               : "0123456789abcdef";

    char buf[64];
    int i = 0;

    if (value == 0)
    {
        debug_putc('0');
        return;
    }

    while (value)
    {
        buf[i++] = digits[value % base];
        value /= base;
    }

    while (i)
        debug_putc(buf[--i]);
}


// the real printf implementation
static void debug_vprintf(const char *fmt,
                          va_list args)
{
    while (*fmt)
    {
        if (*fmt != '%')
        {
            debug_putc(*fmt);
            fmt++;
            continue;
        }

        fmt++;

        if (*fmt == '\0')
        {
            debug_putc('%');
            break;
        }

        switch (*fmt)
        {
            case 'c':
                debug_putc((char)va_arg(args, int));
                break;

            case 's':
            {
                const char *s = va_arg(args, const char *);
                debug_puts(s ? s : "(null)");
                break;
            }

            case 'd':
            {
                int v = va_arg(args, int);

                if (v < 0)
                {
                    debug_putc('-');
                    v = -v;
                }

                debug_put_u64((uint64_t)(unsigned)v, 10, false);
                break;
            }

            case 'u':
                debug_put_u64(va_arg(args, unsigned), 10, false);
                break;

            case 'x':
                debug_put_u64(va_arg(args, unsigned), 16, false);
                break;

            case 'X':
                debug_put_u64(va_arg(args, unsigned), 16, true);
                break;

            case 'p':
                debug_puts("0x");
                debug_put_u64((uint64_t)va_arg(args, void *), 16, false);
                break;

            default:
                debug_putc('%');
                debug_putc(*fmt);
                break;
        }

        fmt++;
    }
}


// formatted debug output
void debug_printf(const char *fmt,
                  ...)
{
    va_list args;

    va_start(args, fmt);
    debug_vprintf(fmt, args);
    va_end(args);
}
