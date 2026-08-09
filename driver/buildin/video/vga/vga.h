//
// Created by epaxgaming on 31.07.26.
//
// this file contains the vga text mode driver for the video stack
// it writes directly to the 0xB8000 text buffer (80x25) and registers
// itself in the video stack through vga_register()
//

#ifndef DYNT_KERNEL_VGA_H
#define DYNT_KERNEL_VGA_H

#include <stdint.h>
#include <stdbool.h>

// vga text mode standard colors
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_GREEN 2
#define VGA_COLOR_CYAN 3
#define VGA_COLOR_RED 4
#define VGA_COLOR_MAGENTA 5
#define VGA_COLOR_BROWN 6
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY 8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED 12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW 14
#define VGA_COLOR_WHITE 15


// registers the vga driver in the video stack, returns true on success
bool vga_register();

// virtual terminals: every VT has its own offscreen text buffer + cursor,
// so several bash shells can run on one screen. Ctrl+Alt+Fn switches.
#define VGA_VT_MAX 4

// writes one char to the given VT's buffer (used by the tty syscalls);
// if the VT is currently displayed the char is also pushed to the screen
void vga_vt_putc(uint8_t vt, char c);

// clears one VT's buffer
void vga_vt_clear(uint8_t vt);

// moves the cursor of one VT
void vga_vt_set_cursor(uint8_t vt, uint16_t x, uint16_t y);

// displays `vt` (copies its buffer to the VGA screen + hardware cursor)
void vga_vt_switch(uint8_t vt);

// true if `vt` is the one currently shown
bool vga_vt_displayed(uint8_t vt);

#endif // DYNT_KERNEL_VGA_H
