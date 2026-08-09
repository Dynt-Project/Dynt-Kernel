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
#define VGA_CELLS (VGA_COLS * VGA_ROWS)

// cursor is controlled through the 0x3D4 / 0x3D5 ports
#define VGA_CRTC 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_CURSOR_HIGH 0x0E
#define VGA_CURSOR_LOW 0x0F

// every virtual terminal keeps its own text buffer + cursor, so multiple
// shells can run on one physical screen. the video stack's plain putc
// calls always target the displayed VT (kernel boot logs stay visible);
// userspace tty writes go through vga_vt_putc() to their own VT.
static uint16_t vt_buf[VGA_VT_MAX][VGA_CELLS];
static uint16_t vt_x[VGA_VT_MAX];
static uint16_t vt_y[VGA_VT_MAX];
static uint8_t vt_fg[VGA_VT_MAX];
static uint8_t vt_bg[VGA_VT_MAX];
static uint8_t displayed_vt;

static void vga_clear();
static void vga_puts(const char *str);


// builds a vga text cell from char and color
static inline uint16_t vga_cell(char c,
                                uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}


// copies the displayed VT's shadow buffer onto the real screen
static void vga_flush()
{
    uint8_t v = displayed_vt;

    for (int i = 0; i < VGA_CELLS; i++)
        VGA_MEMORY[i] = vt_buf[v][i];
}


// moves the hardware cursor
static void vga_update_cursor()
{
    uint8_t v = displayed_vt;
    uint16_t pos = vt_y[v] * VGA_COLS + vt_x[v];

    outb(VGA_CRTC, VGA_CURSOR_HIGH);
    outb(VGA_CRTC_DATA, (uint8_t)(pos >> 8));

    outb(VGA_CRTC, VGA_CURSOR_LOW);
    outb(VGA_CRTC_DATA, (uint8_t)(pos & 0xFF));
}


// scrolls one VT's buffer up by one row
static void vt_scroll(uint8_t v)
{
    for (uint16_t y = 1; y < VGA_ROWS; y++)
    {
        for (uint16_t x = 0; x < VGA_COLS; x++)
            vt_buf[v][(y - 1) * VGA_COLS + x] = vt_buf[v][y * VGA_COLS + x];
    }

    for (uint16_t x = 0; x < VGA_COLS; x++)
        vt_buf[v][(VGA_ROWS - 1) * VGA_COLS + x] =
            vga_cell(' ', (uint8_t)(vt_bg[v] << 4 | vt_fg[v]));
}


// clears one VT's buffer (and the screen if it is displayed)
static void vt_clear_impl(uint8_t v)
{
    for (int i = 0; i < VGA_CELLS; i++)
        vt_buf[v][i] = vga_cell(' ', (uint8_t)(vt_bg[v] << 4 | vt_fg[v]));

    vt_x[v] = 0;
    vt_y[v] = 0;

    if (v == displayed_vt)
    {
        vga_flush();
        vga_update_cursor();
    }
}


// draws one char into a VT's buffer at its cursor, handling control chars
static void vt_putc_impl(uint8_t v, char c)
{
    switch (c)
    {
        case '\n':
            vt_x[v] = 0;
            vt_y[v]++;
            break;

        case '\r':
            vt_x[v] = 0;
            break;

        case '\t':
            vt_x[v] = (uint16_t)((vt_x[v] + 8) & ~7);
            break;

        case '\b':
            if (vt_x[v] > 0)
            {
                vt_x[v]--;
                vt_buf[v][vt_y[v] * VGA_COLS + vt_x[v]] =
                    vga_cell(' ', (uint8_t)(vt_bg[v] << 4 | vt_fg[v]));
            }
            break;

        default:
            vt_buf[v][vt_y[v] * VGA_COLS + vt_x[v]] =
                vga_cell(c, (uint8_t)(vt_bg[v] << 4 | vt_fg[v]));
            vt_x[v]++;
            break;
    }

    if (vt_x[v] >= VGA_COLS)
    {
        vt_x[v] = 0;
        vt_y[v]++;
    }

    if (vt_y[v] >= VGA_ROWS)
    {
        vt_scroll(v);
        vt_y[v] = VGA_ROWS - 1;
    }

    if (v == displayed_vt)
    {
        vga_flush();
        vga_update_cursor();
    }
}


// inits the vga driver
static void vga_init()
{
    displayed_vt = 0;

    for (int v = 0; v < VGA_VT_MAX; v++)
    {
        vt_fg[v] = VGA_COLOR_LIGHT_GREY;
        vt_bg[v] = VGA_COLOR_BLACK;
        vt_x[v] = 0;
        vt_y[v] = 0;
        vt_clear_impl((uint8_t)v);
    }

    vga_flush();
}


// draws one char at the current cursor of the displayed VT
static void vga_putc(char c)
{
    vt_putc_impl(displayed_vt, c);
}


// draws one char at a fixed cell, does not move the cursor
static void vga_putc_at(uint16_t x,
                        uint16_t y,
                        char c)
{
    uint8_t v = displayed_vt;

    if (x >= VGA_COLS || y >= VGA_ROWS)
        return;

    vt_buf[v][y * VGA_COLS + x] =
        vga_cell(c, (uint8_t)(vt_bg[v] << 4 | vt_fg[v]));

    if (v == displayed_vt)
        VGA_MEMORY[y * VGA_COLS + x] = vt_buf[v][y * VGA_COLS + x];
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
    vt_clear_impl(displayed_vt);
}


// sets the foreground color
static void vga_set_fg_color(uint8_t color)
{
    vt_fg[displayed_vt] = color;
}


// sets the background color
static void vga_set_bg_color(uint8_t color)
{
    vt_bg[displayed_vt] = color;
}


// moves the cursor
static void vga_set_cursor(uint16_t x,
                           uint16_t y)
{
    vt_x[displayed_vt] = x;
    vt_y[displayed_vt] = y;

    vga_update_cursor();
}


/* ---- virtual terminal API (used by the tty syscalls) ---- */

void vga_vt_putc(uint8_t vt, char c)
{
    if (vt >= VGA_VT_MAX)
        vt = 0;

    vt_putc_impl(vt, c);
}

void vga_vt_clear(uint8_t vt)
{
    if (vt >= VGA_VT_MAX)
        vt = 0;

    vt_clear_impl(vt);
}

void vga_vt_set_cursor(uint8_t vt, uint16_t x, uint16_t y)
{
    if (vt >= VGA_VT_MAX)
        vt = 0;

    vt_x[vt] = x;
    vt_y[vt] = y;

    if (vt == displayed_vt)
        vga_update_cursor();
}

void vga_vt_switch(uint8_t vt)
{
    if (vt >= VGA_VT_MAX)
        return;

    displayed_vt = vt;
    vga_flush();
    vga_update_cursor();
}

bool vga_vt_displayed(uint8_t vt)
{
    return vt == displayed_vt;
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
