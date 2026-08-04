//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "video_stack.h"
#include "../../buildin/video/font/font8x8.h"

static video_driver_t *drivers[VIDEO_MAX_DRIVERS];

static uint8_t driver_count;

static uint8_t active;


// inits video stack
void video_stack_init()
{
    driver_count = 0;
    active = 0;

    for (int i = 0; i < VIDEO_MAX_DRIVERS; i++)
        drivers[i] = 0;
}


// registers a video driver
bool video_stack_register(video_driver_t *driver)
{
    if (driver_count >= VIDEO_MAX_DRIVERS)
        return false;

    if (!driver->init)
        return false;

    drivers[driver_count] = driver;

    driver_count++;

    driver->init();

    return true;
}


// draws one char
void video_putc(char c)
{
    if (!video_has_driver())
        return;

    drivers[active]->putc(c);
}


// draws a string
void video_puts(const char *str)
{
    if (!video_has_driver())
        return;

    drivers[active]->puts(str);
}


// clears the screen
void video_clear()
{
    if (!video_has_driver())
        return;

    drivers[active]->clear();
}


// sets the foreground color
void video_set_fg_color(uint8_t color)
{
    if (!video_has_driver())
        return;

    drivers[active]->set_fg_color(color);
}


// sets the background color
void video_set_bg_color(uint8_t color)
{
    if (!video_has_driver())
        return;

    drivers[active]->set_bg_color(color);
}


// moves the cursor
void video_set_cursor(uint16_t x,
                      uint16_t y)
{
    if (!video_has_driver())
        return;

    drivers[active]->set_cursor(x, y);
}


// draws one char at a fixed cell without moving the cursor
void video_putc_at(uint16_t x,
                   uint16_t y,
                   char c)
{
    if (!video_has_driver())
        return;

    drivers[active]->putc_at(x, y, c);
}


// gets the screen width
uint16_t video_get_width()
{
    if (!video_has_driver())
        return 0;

    return drivers[active]->width;
}


// gets the screen height
uint16_t video_get_height()
{
    if (!video_has_driver())
        return 0;

    return drivers[active]->height;
}


// checks if a driver is registered
bool video_has_driver()
{
    return driver_count > 0 && drivers[active] != 0;
}


// returns the built in 8x8 font
const uint8_t *video_get_font8x8()
{
    return (const uint8_t *)font8x8_basic;
}
