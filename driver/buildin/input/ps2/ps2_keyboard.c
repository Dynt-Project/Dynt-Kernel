//
// Created by epaxgaming on 31.07.26.
//
// explained in the header file
//

#include "ps2_keyboard.h"
#include "ps2.h"
#include "../../../stacks/input/keyboard_stack.h"
#include "inter/isr.h"
#include "inter/pic.h"

// scancode set 1 special bytes
#define PS2_KBD_EXTENDED 0xE0
#define PS2_KBD_RELEASED 0xF0
#define PS2_KBD_ACK 0xFA
#define PS2_KBD_SELFTEST 0xAA

// scancode set command (0xF0) + set 1
#define PS2_KBD_CMD_SET_SCANCODE 0xF0
#define PS2_KBD_SCANCODE_SET_1 0x01

static bool ps2_extended;
static bool ps2_released;


// irq1 handler, every key press / release arrives here as a scancode
static void ps2_keyboard_irq(registers_t *regs)
{
    (void)regs;

    uint8_t byte = inb(PS2_DATA);

    if (byte == PS2_KBD_EXTENDED)
    {
        ps2_extended = true;
        return;
    }

    if (byte == PS2_KBD_RELEASED)
    {
        ps2_released = true;
        return;
    }

    // acks and selftest results are not keys
    if (byte == PS2_KBD_ACK || byte == PS2_KBD_SELFTEST || byte == 0x00)
    {
        ps2_extended = false;
        ps2_released = false;
        return;
    }

    uint16_t keycode = byte;

    if (ps2_extended)
        keycode |= 0x100;

    keyboard_report_key(keycode, !ps2_released);

    ps2_extended = false;
    ps2_released = false;
}


bool ps2_keyboard_init()
{
    keyboard_stack_init();

    ps2_flush_input();

    // make sure the keyboard uses scancode set 1
    ps2_send_data(PS2_KBD_CMD_SET_SCANCODE);
    uint8_t ack1 = ps2_read_data();

    ps2_send_data(PS2_KBD_SCANCODE_SET_1);
    uint8_t ack2 = ps2_read_data();

    if (ack1 != PS2_KBD_ACK || ack2 != PS2_KBD_ACK)
        return false;

    irq_install_handler(1, ps2_keyboard_irq);
    pic_clear_mask(1);

    return true;
}
