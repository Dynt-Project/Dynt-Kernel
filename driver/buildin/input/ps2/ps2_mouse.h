//
// Created by epaxgaming on 31.07.26.
//
// this file contains the ps/2 mouse driver, it hooks the irq12 handler
// and decodes the standard 3 byte ps/2 mouse packets into deltas and
// button states which are reported to the mouse stack.
// the packet bytes are: byte0 = flags+buttons, byte1 = dx (signed),
// byte2 = dy (signed). bit 3 of byte0 is always 1 and is used to
// re-sync if the stream ever gets misaligned.
//

#ifndef DYNT_KERNEL_INPUT_PS2_MOUSE_H
#define DYNT_KERNEL_INPUT_PS2_MOUSE_H

#include <stdbool.h>

// inits the ps/2 mouse, installs the irq12 handler and returns true
// if the mouse answered the setup commands correctly
bool ps2_mouse_init();

#endif // DYNT_KERNEL_INPUT_PS2_MOUSE_H
