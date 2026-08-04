//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "vga.h"
#include "../../../stacks/video/video_stack.h"
#include "io/io.h"

// vga text buffer, 80 columns * 25 rows, every cell is 2 bytes:
// low byte = ascii char, high byte = attribute (bg << 4 | fg)
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

// cursor is controlled through the 0x3D4 / 0x3D5 ports
#define VGA_CRTC 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW 0x0F

static uint16_t cursor_x;
static uint16_t cursor_y;

static uint8_t fg_color;
static uint8_t bg_color;

static void vga_clear();
static void vga_puts(const char *str);


// builds a vga text cell from char and color
static inline uint16_t vga_cell(char c,
                                uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}


// moves the hardware cursor
static void vga_update_cursor()
{
    uint16_t pos = cursor_y * VGA_COLS + cursor_x;

    outb(VGA_CRTC, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));

    outb(VGA_CRTC, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
}


// scrolls the screen up by one row
static void vga_scroll()
{
    for (uint16_t y = 1; y < VGA_ROWS; y++)
    {
        for (uint16_t x = 0; x < VGA_COLS; x++)
            VGA_MEMORY[(y - 1) * VGA_COLS + x] = VGA_MEMORY[y * VGA_COLS + x];
    }

    for (uint16_t x = 0; x < VGA_COLS; x++)
        VGA_MEMORY[(VGA_ROWS - 1) * VGA_COLS + x] = vga_cell(' ', (uint8_t)(bg_color << 4 | fg_color));
}


// inits the vga driver
static void vga_init()
{
    fg_color = VGA_COLOR_LIGHT_GREY;
    bg_color = VGA_COLOR_BLACK;

    cursor_x = 0;
    cursor_y = 0;

    vga_clear();
}


// draws one char at the current cursor
static void vga_putc(char c)
{
    switch (c)
    {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;

        case '\r':
            cursor_x = 0;
            break;

        case '\t':
            cursor_x = (uint16_t)((cursor_x + 8) & ~7);
            break;

        case '\b':
            if (cursor_x > 0)
            {
                cursor_x--;
                VGA_MEMORY[cursor_y * VGA_COLS + cursor_x] = vga_cell(' ', (uint8_t)(bg_color << 4 | fg_color));
            }
            break;

        default:
            VGA_MEMORY[cursor_y * VGA_COLS + cursor_x] = vga_cell(c, (uint8_t)(bg_color << 4 | fg_color));
            cursor_x++;
            break;
    }

    if (cursor_x >= VGA_COLS)
    {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= VGA_ROWS)
    {
        vga_scroll();
        cursor_y = VGA_ROWS - 1;
    }

    vga_update_cursor();
}


// draws one char at a fixed cell, does not move the cursor
static void vga_putc_at(uint16_t x,
                        uint16_t y,
                        char c)
{
    if (x >= VGA_COLS || y >= VGA_ROWS)
        return;

    VGA_MEMORY[y * VGA_COLS + x] = vga_cell(c, (uint8_t)(bg_color << 4 | fg_color));
}


// draws a whole string
static void vga_puts(const char *str)
{
    while (*str)
        vga_putc(*str++);
}


// clears the whole screen
static void vga_clear()
{
    for (uint16_t i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEMORY[i] = vga_cell(' ', (uint8_t)(bg_color << 4 | fg_color));

    cursor_x = 0;
    cursor_y = 0;

    vga_update_cursor();
}


// sets the foreground color
static void vga_set_fg_color(uint8_t color)
{
    fg_color = color;
}


// sets the background color
static void vga_set_bg_color(uint8_t color)
{
    bg_color = color;
}


// moves the cursor
static void vga_set_cursor(uint16_t x,
                           uint16_t y)
{
    cursor_x = x;
    cursor_y = y;

    vga_update_cursor();
}


// registers the vga driver in the video stack
bool vga_register()
{
    static video_driver_t vga_driver;

    vga_driver.init = vga_init;
    vga_driver.putc = vga_putc;
    vga_driver.puts = vga_puts;
    vga_driver.clear = vga_clear;

    vga_driver.set_fg_color = vga_set_fg_color;
    vga_driver.set_bg_color = vga_set_bg_color;
    vga_driver.set_cursor = vga_set_cursor;
    vga_driver.putc_at = vga_putc_at;

    vga_driver.width = VGA_COLS;
    vga_driver.height = VGA_ROWS;

    for (int i = 0; i < VIDEO_MAX_DRIVER_NAME; i++)
        vga_driver.name[i] = 0;
    for (int i = 0; "VGA Text Driver"[i] && i < VIDEO_MAX_DRIVER_NAME - 1; i++)
        vga_driver.name[i] = "VGA Text Driver"[i];

    return video_stack_register(&vga_driver);
}
