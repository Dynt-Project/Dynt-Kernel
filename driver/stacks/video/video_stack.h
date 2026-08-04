//
// Created by epaxgaming on 31.07.26.
//
// this file contains a stack like api for managing multiple video drivers at once
// every video driver registers itself here and reports its draw functions
// the rest of the kernel only interacts with this stack
//

#ifndef DYNT_KERNEL_VIDEO_STACK_H
#define DYNT_KERNEL_VIDEO_STACK_H

// including some basic headers
#include <stdint.h>
#include <stdbool.h>

// defines
#define VIDEO_MAX_DRIVERS 8
#define VIDEO_MAX_DRIVER_NAME 32

typedef struct video_driver video_driver_t;

// every video driver has to fill this struct and register itself
struct video_driver
{
    char name[VIDEO_MAX_DRIVER_NAME];

    // called once when the driver is registered
    void (*init)(void);

    // draws one char at the current cursor
    void (*putc)(char c);

    // draws a whole string
    void (*puts)(const char *str);

    // clears the whole screen
    void (*clear)(void);

    // changes the foreground / background color
    void (*set_fg_color)(uint8_t color);

    void (*set_bg_color)(uint8_t color);

    // moves the hardware cursor
    void (*set_cursor)(uint16_t x,
                       uint16_t y);

    // draws one char at a fixed cell, does not move the cursor
    // (used by the window manager for pixel-perfect text layouts)
    void (*putc_at)(uint16_t x,
                    uint16_t y,
                    char c);

    // size of the screen
    uint16_t width;
    uint16_t height;
};


// init function, has to be called before anything else
void video_stack_init();


// registers a video driver, returns true on success
bool video_stack_register(video_driver_t *driver);


// draws one char through the active driver
void video_putc(char c);


// draws a string through the active driver
void video_puts(const char *str);


// clears the screen
void video_clear();


// sets the foreground color
void video_set_fg_color(uint8_t color);


// sets the background color
void video_set_bg_color(uint8_t color);


// moves the cursor
void video_set_cursor(uint16_t x,
                      uint16_t y);


// draws one char at a fixed cell without moving the cursor
void video_putc_at(uint16_t x,
                   uint16_t y,
                   char c);


// returns the screen size of the active driver
uint16_t video_get_width();

uint16_t video_get_height();


// returns true if at least one driver is registered
bool video_has_driver();


// returns the built-in 8x8 bitmap font, 128 chars * 8 bytes
// the vga text driver uses the hardware font, this one is meant for
// the framebuffer console that will come later
const uint8_t *video_get_font8x8();

#endif // DYNT_KERNEL_VIDEO_STACK_H
