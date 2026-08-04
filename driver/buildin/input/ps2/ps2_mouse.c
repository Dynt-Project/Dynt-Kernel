//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "ps2_mouse.h"
#include "ps2.h"
#include "../../../stacks/input/mouse_stack.h"
#include "inter/isr.h"
#include "inter/pic.h"

// controller commands used while setting the mouse up
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_READ_CMD_BYTE 0x20
#define PS2_CMD_WRITE_CMD_BYTE 0x60
#define PS2_CMD_WRITE_AUX 0xD4

// device commands, sent through the 0xD4 command
#define PS2_MOUSE_CMD_SET_DEFAULTS 0xF6
#define PS2_MOUSE_CMD_ENABLE_REPORT 0xF4
#define PS2_MOUSE_ACK 0xFA

static uint8_t packet[3];
static uint8_t cycle;
static uint8_t prev_buttons;


// irq12 handler, collects 3 bytes then reports one full packet
static void ps2_mouse_irq(registers_t *regs)
{
    (void)regs;

    packet[cycle] = inb(PS2_DATA);
    cycle = (uint8_t)((cycle + 1) % 3);

    if (cycle != 0)
        return;

    // byte0 of a real packet always has bit 3 set, if not we lost
    // sync -> shift the stream by one byte and wait for the next one
    if (!(packet[0] & 0x08))
    {
        packet[0] = packet[1];
        packet[1] = packet[2];
        cycle = 2;
        return;
    }

    uint8_t buttons = packet[0] & 0x07;

    // the mouse reports y as positive-up, the screen is positive-down
    int32_t dx = (int32_t)(int8_t)packet[1];
    int32_t dy = -(int32_t)(int8_t)packet[2];

    mouse_report_motion(dx, dy);

    // report every button that changed state
    for (uint8_t b = 0; b < 3; b++)
    {
        bool now = ((buttons >> b) & 1) != 0;
        bool was = ((prev_buttons >> b) & 1) != 0;

        if (now != was)
            mouse_report_button(b, now);
    }

    prev_buttons = buttons;
}


bool ps2_mouse_init()
{
    mouse_stack_init();

    ps2_flush_input();

    // enable the auxiliary device interface (irq12)
    ps2_send_command(PS2_CMD_ENABLE_AUX);

    // read the controller command byte
    ps2_send_command(PS2_CMD_READ_CMD_BYTE);
    uint8_t cmd = ps2_read_data();

    // bit 1 enables the mouse interrupt, bit 5 must stay clear so the
    // mouse device itself stays enabled
    cmd |= 0x02;
    cmd &= ~0x20;

    ps2_send_command(PS2_CMD_WRITE_CMD_BYTE);
    ps2_send_data(cmd);

    // reset the mouse to default settings (button 1, 3 byte packets)
    ps2_send_command(PS2_CMD_WRITE_AUX);
    ps2_send_data(PS2_MOUSE_CMD_SET_DEFAULTS);
    uint8_t ack1 = ps2_read_data();

    // start streaming data packets to us
    ps2_send_command(PS2_CMD_WRITE_AUX);
    ps2_send_data(PS2_MOUSE_CMD_ENABLE_REPORT);
    uint8_t ack2 = ps2_read_data();

    if (ack1 != PS2_MOUSE_ACK || ack2 != PS2_MOUSE_ACK)
        return false;

    cycle = 0;
    prev_buttons = 0;

    irq_install_handler(12, ps2_mouse_irq);
    pic_clear_mask(12);

    return true;
}
