//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "mouse_stack.h"

static bool button_state[MOUSE_MAX_BUTTONS];

static int32_t mouse_x;
static int32_t mouse_y;

static int32_t delta_x;
static int32_t delta_y;

static mouse_event_t queue[MOUSE_QUEUE_SIZE];

static uint32_t head;
static uint32_t tail;


// inits mouse stack
void mouse_stack_init()
{
    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++)
        button_state[i] = false;

    mouse_x = 0;
    mouse_y = 0;

    delta_x = 0;
    delta_y = 0;

    head = tail = 0;
}


// reports mouse movement
void mouse_report_motion(int32_t dx, int32_t dy)
{
    mouse_x += dx;
    mouse_y += dy;

    delta_x = dx;
    delta_y = dy;

    mouse_event_t e;

    e.x = mouse_x;
    e.y = mouse_y;

    e.dx = dx;
    e.dy = dy;

    e.wheel = 0;
    e.buttons = 0;

    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++)
    {
        if (button_state[i])
            e.buttons |= (1 << i);
    }

    uint32_t next = (head + 1) % MOUSE_QUEUE_SIZE;

    if (next != tail)
    {
        queue[head] = e;
        head = next;
    }
}


// reports button state
void mouse_report_button(uint8_t button, bool pressed)
{
    if (button >= MOUSE_MAX_BUTTONS)
        return;

    button_state[button] = pressed;

    mouse_event_t e;

    e.x = mouse_x;
    e.y = mouse_y;

    e.dx = 0;
    e.dy = 0;

    e.wheel = 0;
    e.buttons = 0;

    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++)
    {
        if (button_state[i])
            e.buttons |= (1 << i);
    }

    uint32_t next = (head + 1) % MOUSE_QUEUE_SIZE;

    if (next != tail)
    {
        queue[head] = e;
        head = next;
    }
}


// reports mouse wheel
void mouse_report_wheel(int8_t wheel)
{
    mouse_event_t e;

    e.x = mouse_x;
    e.y = mouse_y;

    e.dx = 0;
    e.dy = 0;

    e.wheel = wheel;
    e.buttons = 0;

    for (int i = 0; i < MOUSE_MAX_BUTTONS; i++)
    {
        if (button_state[i])
            e.buttons |= (1 << i);
    }

    uint32_t next = (head + 1) % MOUSE_QUEUE_SIZE;

    if (next != tail)
    {
        queue[head] = e;
        head = next;
    }
}


// checks if button is pressed
bool mouse_is_pressed(uint8_t button)
{
    if (button >= MOUSE_MAX_BUTTONS)
        return false;

    return button_state[button];
}


// gets current mouse x
int32_t mouse_get_x()
{
    return mouse_x;
}


// gets current mouse y
int32_t mouse_get_y()
{
    return mouse_y;
}


// sets mouse position
void mouse_set_position(int32_t x, int32_t y)
{
    mouse_x = x;
    mouse_y = y;
}


// gets last delta x
int32_t mouse_get_delta_x()
{
    return delta_x;
}


// gets last delta y
int32_t mouse_get_delta_y()
{
    return delta_y;
}


// polls next event
bool mouse_poll_event(mouse_event_t *event)
{
    if (head == tail)
        return false;

    *event = queue[tail];

    tail = (tail + 1) % MOUSE_QUEUE_SIZE;

    return true;
}