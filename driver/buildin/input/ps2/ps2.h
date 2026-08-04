//
// Created by epaxgaming on 31.07.26.
//
// this file contains the shared low level helpers for talking to the
// ps/2 controller (port 0x60 / 0x64). both the keyboard and the mouse
// driver use them, they are small enough to stay inline in this header.
//

#ifndef DYNT_KERNEL_INPUT_PS2_H
#define DYNT_KERNEL_INPUT_PS2_H

#include <stdint.h>
#include "io/io.h"
#include "io/ports.h"

// waits until the controller is ready to accept the next byte
static inline void ps2_wait_input_buffer_empty(void)
{
    for (uint32_t i = 0; i < 100000; i++)
    {
        if (!(inb(PS2_STATUS) & 0x02))
            return;
        io_wait();
    }
}

// waits until the controller has data for us to read
static inline void ps2_wait_output_buffer_full(void)
{
    for (uint32_t i = 0; i < 100000; i++)
    {
        if (inb(PS2_STATUS) & 0x01)
            return;
        io_wait();
    }
}

// sends a controller command byte (0x64)
static inline void ps2_send_command(uint8_t cmd)
{
    ps2_wait_input_buffer_empty();
    outb(PS2_COMMAND, cmd);
    ps2_wait_input_buffer_empty();
}

// sends a data byte to a device (0x60)
static inline void ps2_send_data(uint8_t data)
{
    ps2_wait_input_buffer_empty();
    outb(PS2_DATA, data);
    ps2_wait_input_buffer_empty();
}

// reads a byte that a device sent us
static inline uint8_t ps2_read_data(void)
{
    ps2_wait_output_buffer_full();
    return inb(PS2_DATA);
}

// throws away everything the controller still has buffered
static inline void ps2_flush_input(void)
{
    for (uint32_t i = 0; i < 100; i++)
    {
        if (inb(PS2_STATUS) & 0x01)
            inb(PS2_DATA);
        else
            break;
    }
}

#endif // DYNT_KERNEL_INPUT_PS2_H
