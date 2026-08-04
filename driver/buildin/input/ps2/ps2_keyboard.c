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

// scancode set 1: break codes are 0x80 | make code
#define PS2_KBD_SET1_BREAK 0x80

// scancode set command (0xF0) + set 2
// NOTE: the i8042 controller's AT->XT translation (on by default, also in
// QEMU) already converts set 2 scancodes to set 1. Forcing set 1 here would
// make the controller translate the set 1 codes AGAIN (translate_table) and
// corrupt them (e.g. k -> 0x05). So we keep the keyboard in its default set 2.
#define PS2_KBD_CMD_SET_SCANCODE 0xF0
#define PS2_KBD_SCANCODE_SET_2 0x02

static bool ps2_extended;
static bool ps2_released;


// irq1 handler, every key press / release arrives here as a scancode
static void ps2_keyboard_irq(registers_t *regs)
{
    (void)regs;

    uint8_t byte = inb(PS2_DATA);

    // late ack / typematic-overrun bytes after init are not keys.
    // NOTE: 0xAA is deliberately NOT filtered here - it is the set 1
    // BREAK code of left shift (0x2A | 0x80), so treating it as a
    // selftest result would stick shift forever.
    if (byte == PS2_KBD_ACK || byte == 0x00)
    {
        ps2_extended = false;
        ps2_released = false;
        return;
    }

    if (byte == PS2_KBD_EXTENDED)
    {
        ps2_extended = true;
        return;
    }

    // set 2 style release prefix, some keyboards send E0 F0 xx for
    // extended breaks even in scancode set 1
    if (byte == PS2_KBD_RELEASED)
    {
        ps2_released = true;
        return;
    }

    // scancode set 1 break code: 0x80 | make code
    if (byte & PS2_KBD_SET1_BREAK)
    {
        ps2_released = true;
        byte &= ~PS2_KBD_SET1_BREAK;
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

    // make sure the keyboard uses scancode set 2 (controller translates
    // it to set 1 for us)
    ps2_send_data(PS2_KBD_CMD_SET_SCANCODE);
    uint8_t ack1 = ps2_read_data();

    ps2_send_data(PS2_KBD_SCANCODE_SET_2);
    uint8_t ack2 = ps2_read_data();

    if (ack1 != PS2_KBD_ACK || ack2 != PS2_KBD_ACK)
        return false;

    irq_install_handler(1, ps2_keyboard_irq);
    pic_clear_mask(1);

    return true;
}
