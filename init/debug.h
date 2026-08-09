//
// Created by epaxgaming on 31.07.26.
//
// this file provides the "debug put" api, the one place every boot log
// goes through. every debug message is mirrored to the COM1 serial port
// (always) and to the video stack (once a video driver is registered)
//

#ifndef INIT_DEBUG_H
#define INIT_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

// context-switch / spawn / exec diagnostics ([sw], [res], [wake], [exit],
// [proc], [read], ...).  set to 1 to re-enable
#ifndef SCHEDULER_DEBUG
#define SCHEDULER_DEBUG 0
#endif

// inits serial, the video stack and the vga driver, call this first
void debug_init();

// puts one char into the debug output
void debug_putc(char c);

// puts a string into the debug output
void debug_puts(const char *str);

// formatted debug output, supports %c %s %d %u %x %X %p %%
void debug_printf(const char *fmt,
                  ...);

#ifdef __cplusplus
}
#endif

#endif // INIT_DEBUG_H
