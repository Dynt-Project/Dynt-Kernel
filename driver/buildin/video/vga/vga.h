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

#endif // DYNT_KERNEL_VGA_H
