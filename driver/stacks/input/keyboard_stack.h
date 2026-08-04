//
// Created by epaxgaming on 31.07.26.
//
// this file contains a stack like api for managaing multiple keyboard devices at once
// the file is the included file then for getting input because here every keyboard input driver
// registers itself and sends their input in !
// for example by using a method like  bool "keyboard_is_pressed(uint8_t keycode);"

#ifndef DYNT_KERNEL_KEYBOARD_STACK_H
#define DYNT_KERNEL_KEYBOARD_STACK_H

//including asome basic headers
#include <stdint.h>
#include <stdbool.h>

//defines the maximum keys on keyboard
// keycodes are uint16_t so extended scancodes (0xE0 prefix) can be
// stored as 0x100 + scancode, e.g. the arrow keys are 0x148..0x150
#define KEYBOARD_MAX_KEYS 512
#define KEYBOARD_QUE_SIZE 256

// scancode set 1 make codes for the modifier keys
// extended scancodes (the ones prefixed with 0xE0) get 0x100 added
#define KEY_LEFT_SHIFT 0x2A
#define KEY_RIGHT_SHIFT 0x36
#define KEY_LEFT_CTRL 0x1D
#define KEY_RIGHT_CTRL 0x11D
#define KEY_LEFT_ALT 0x38
#define KEY_RIGHT_ALT 0x138

// the keys the window manager and the games use, scancode set 1
#define KEY_ESC 0x01
#define KEY_TAB 0x0F
#define KEY_Q 0x10
#define KEY_W 0x11
#define KEY_E 0x12
#define KEY_R 0x13
#define KEY_T 0x14
#define KEY_Y 0x15
#define KEY_U 0x16
#define KEY_I 0x17
#define KEY_O 0x18
#define KEY_P 0x19
#define KEY_A 0x1E
#define KEY_S 0x1F
#define KEY_D 0x20
#define KEY_F 0x21
#define KEY_G 0x22
#define KEY_H 0x23
#define KEY_J 0x24
#define KEY_K 0x25
#define KEY_L 0x26
#define KEY_ENTER 0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_SPACE 0x39
#define KEY_UP 0x148
#define KEY_LEFT 0x14B
#define KEY_RIGHT 0x14D
#define KEY_DOWN 0x150

typedef struct {
    uint16_t keycode;
    char ascii;
    bool pressed;
} keyboard_event_t;


//init function
void keyboard_stack_init();

void keyboard_report_key(uint16_t keycode,
                         bool pressed);

bool keyboard_is_pressed(uint16_t keycode);

bool keyboard_shift();

bool keyboard_ctrl();

bool keyboard_alt();

char keyboard_getchar();

// translates a scancode set 1 keycode to an ascii char, 0 if unmapped
char hid_key_to_ascii(uint16_t keycode,
                      bool shift);

bool keyboard_poll_event(keyboard_event_t *event);

// canonical line input: ps2_keyboard feeds keys in, userspace reads
// complete lines out. non-blocking on purpose so the scheduler can
// preempt a spinning userspace between calls.
#define TTY_LINE_MAX 256

// called by keyboard drivers for every pressed key
void tty_input_char(char c);

// returns a full line (newline stripped, NUL terminated) or 0 if none
// is ready yet
int tty_getline(char *buf, int size);

// true if a full line is waiting to be consumed
bool tty_line_ready(void);

// drains polled COM1 input into the canonical line buffer
void tty_drain_serial(void);

#endif //DYNT_KERNEL_KEYBOARD_STACK_H
