//
// Created by epaxgaming on 31.07.26.
//
// this file contains a stack like api for managing multiple mouse devices at once
// every mouse driver registers itself here and reports movement/buttons
// the rest of the kernel only interacts with this stack
//

#ifndef DYNT_KERNEL_MOUSE_STACK_H
#define DYNT_KERNEL_MOUSE_STACK_H

// including some basic headers
#include <stdint.h>
#include <stdbool.h>

// defines
#define MOUSE_MAX_BUTTONS 8
#define MOUSE_QUEUE_SIZE 256

typedef struct
{
    int32_t x;
    int32_t y;

    int32_t dx;
    int32_t dy;

    int8_t wheel;

    uint8_t buttons;

} mouse_event_t;


// init function
void mouse_stack_init();


// reports a mouse movement
void mouse_report_motion(int32_t dx,
                         int32_t dy);


// reports a mouse button change
void mouse_report_button(uint8_t button,
                         bool pressed);


// reports a mouse wheel movement
void mouse_report_wheel(int8_t delta);


// returns true if a button is currently pressed
bool mouse_is_pressed(uint8_t button);


// returns the current cursor position
int32_t mouse_get_x();

int32_t mouse_get_y();


// sets the cursor position
void mouse_set_position(int32_t x,
                        int32_t y);


// returns the current movement since the last report
int32_t mouse_get_delta_x();

int32_t mouse_get_delta_y();


// polls the next mouse event
bool mouse_poll_event(mouse_event_t *event);

#endif // DYNT_KERNEL_MOUSE_STACK_H