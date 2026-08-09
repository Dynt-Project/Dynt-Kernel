//
// Created by epaxgaming on 31.07.26.
//
//explained in the header file

#include "keyboard_stack.h"

#include "../../init/debug.h"
#include "../../arch/x86_64/io/serial.h"
#include "../video/video_stack.h"
#include "../../buildin/video/vga/vga.h"

static bool key_state[KEYBOARD_MAX_KEYS];

static bool shift;
static bool ctrl;
static bool alt;

static keyboard_event_t queue[KEYBOARD_QUE_SIZE];

static uint32_t head;
static uint32_t tail;

// inits keyboard stack
void keyboard_stack_init()
{
    for(int i=0;i<KEYBOARD_MAX_KEYS;i++)
        key_state[i]=false;

    head=tail=0;
}


//checks if key is pressed
bool keyboard_is_pressed(uint16_t key)
{
    return key_state[key];
}


//checks if shift is pressed
bool keyboard_shift()
{
    return shift;
}

//checks if ctrl is pressed
bool keyboard_ctrl()
{
    return ctrl;
}

//checks if alt is pressed
bool keyboard_alt()
{
    return alt;
}

//method for drivers to report pressed keys
void keyboard_report_key(uint16_t key, bool pressed)
{
    key_state[key]=pressed;

    switch(key)
    {
        case KEY_LEFT_SHIFT:
        case KEY_RIGHT_SHIFT:
            shift=pressed;
            break;

        case KEY_LEFT_CTRL:
        case KEY_RIGHT_CTRL:
            ctrl=pressed;
            break;

        case KEY_LEFT_ALT:
        case KEY_RIGHT_ALT:
            alt=pressed;
            break;
    }

    keyboard_event_t e;

    e.keycode=key;
    e.pressed=pressed;
    e.ascii=hid_key_to_ascii(key,shift);

    uint32_t next=(head+1)%KEYBOARD_QUE_SIZE;

    if(next!=tail)
    {
        queue[head]=e;
        head=next;
    }

    // canonical line input: only key presses produce characters
    if (!pressed)
        return;

    // Ctrl+Alt+F1..Fn: switch the active virtual terminal
    if (ctrl && alt && key >= KEY_F1 && key <= KEY_F6)
    {
        tty_vt_switch((uint8_t)(key - KEY_F1));
        return;
    }

    // Ctrl+C: interrupt the active terminal (used by bash to stop
    // the foreground program)
    if (ctrl && key == KEY_C)
    {
        tty_sigint_trigger(tty_active_vt());
        return;
    }

    if (key == KEY_ENTER)
        tty_input_char('\n');
    else if (key == KEY_BACKSPACE)
        tty_input_char(0x08);
    else if (e.ascii)
        tty_input_char(e.ascii);
}

//returns the next queued char, 0 if the queue is empty
char keyboard_getchar()
{
    keyboard_event_t e;

    if(!keyboard_poll_event(&e))
        return 0;

    return e.ascii;
}

//event polling for driver
bool keyboard_poll_event(keyboard_event_t *e)
{
    if(head==tail)
        return false;

    *e=queue[tail];

    tail=(tail+1)%KEYBOARD_QUE_SIZE;

    return true;
}

/* ---- canonical line input, one buffer per virtual terminal ---- */

typedef struct
{
    char line[TTY_LINE_MAX];
    int len;
    bool ready;
    bool sigint;
} vt_tty_t;

static vt_tty_t vt_tty[TTY_VT_MAX];
static uint8_t active_vt;

void tty_vt_switch(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return;

    active_vt = vt;
    vga_vt_switch(vt);
}

uint8_t tty_active_vt(void)
{
    return active_vt;
}

void tty_input_char(char c)
{
    vt_tty_t *t = &vt_tty[active_vt];

    // a line is already finished but not consumed yet - don't extend it
    if (t->ready)
        return;

    if (c == '\n')
    {
        if (t->len < TTY_LINE_MAX - 1)
            t->line[t->len++] = '\n';
        t->line[t->len] = 0;
        t->ready = true;
        debug_putc('\n');
        return;
    }

    if (c == 0x08 || c == 0x7F)  // backspace
    {
        if (t->len > 0)
        {
            t->len--;
            t->line[t->len] = 0;
            debug_puts("\b \b");
        }
        return;
    }

    if (c >= 0x20 && t->len < TTY_LINE_MAX - 1)
    {
        t->line[t->len++] = c;
        debug_putc(c);
    }
}

void tty_sigint_trigger(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return;

    vt_tty_t *t = &vt_tty[vt];

    t->sigint = true;
    t->len = 0;
    t->line[0] = 0;
    t->ready = false;

    // echo ^C so the user sees the interrupt
    debug_puts("^C\n");
}

bool tty_sigint_consume(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return false;

    vt_tty_t *t = &vt_tty[vt];

    if (t->sigint)
    {
        t->sigint = false;
        return true;
    }

    return false;
}

int tty_getline(uint8_t vt, char *buf, int size)
{
    if (vt >= TTY_VT_MAX)
        return 0;

    vt_tty_t *t = &vt_tty[vt];

    if (!t->ready)
        return 0;

    int n = t->len;
    if (n > size - 1)
        n = size - 1;

    for (int i = 0; i < n; i++)
        buf[i] = t->line[i];

    buf[n] = 0;
    t->len = 0;
    t->ready = false;
    return n;
}

// true if a full line is waiting to be consumed
bool tty_line_ready(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return false;

    return vt_tty[vt].ready;
}

// drains polled COM1 input into the active VT's canonical line buffer so
// a headless serial console works as stdin without a display/PS2
void tty_drain_serial(void)
{
    while (serial_received())
        tty_input_char(serial_read_char());
}

// translates a scancode set 1 keycode into an ascii char
// only normal (non extended) keycodes have an ascii representation
char hid_key_to_ascii(uint16_t keycode, bool shift)
{
    static const char normal[0x3A] = {
        0,0,                       '1','2','3','4','5','6','7','8','9','0','-','=',
        0,0,
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',
        0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,0,0,' ',
    };

    static const char shifted[0x3A] = {
        0,0,                       '!','@','#','$','%','^','&','*','(',')','_','+',
        0,0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
        0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
        'Z','X','C','V','B','N','M','<','>','?',0,0,0,' ',
    };

    if(keycode>=0x3A)
        return 0;

    return shift ? shifted[keycode] : normal[keycode];
}