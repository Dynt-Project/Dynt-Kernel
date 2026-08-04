//
// Created by epaxgaming on 31.07.26.
//
// this file contains the ps/2 keyboard driver, it hooks the irq1 handler
// and translates scancode set 1 bytes into make/break keycodes which are
// reported to the keyboard stack. extended scancodes (0xE0 prefix) get
// 0x100 added so the keyboard stack can tell left from right ctrl/alt and
// the arrow keys from the number block.
//

#ifndef DYNT_KERNEL_INPUT_PS2_KEYBOARD_H
#define DYNT_KERNEL_INPUT_PS2_KEYBOARD_H

#include <stdbool.h>

// inits the ps/2 keyboard, installs the irq1 handler and returns true
// if the keyboard answered the setup commands correctly
bool ps2_keyboard_init();

#endif // DYNT_KERNEL_INPUT_PS2_KEYBOARD_H
