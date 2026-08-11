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

typedef struct
{
    char line[TTY_LINE_MAX];
    int len;
    bool ready;
    bool sigint;
    uint8_t raw[TTY_RAW_MAX];
    uint32_t raw_head;
    uint32_t raw_tail;
    dynt_termios_t termios;
} vt_tty_t;

static vt_tty_t vt_tty[TTY_VT_MAX];
static uint8_t active_vt;

static void tty_push_raw(uint8_t vt, uint8_t byte);

// inits keyboard stack
void keyboard_stack_init()
{
    for(int i=0;i<KEYBOARD_MAX_KEYS;i++)
        key_state[i]=false;

    head=tail=0;

    for (int v = 0; v < TTY_VT_MAX; v++)
    {
        // default termios: canonical line mode with echo and signals,
        // VMIN=1 / VTIME=0 like a classic tty
        vt_tty[v].termios.c_iflag = 0;
        vt_tty[v].termios.c_oflag = 0;
        vt_tty[v].termios.c_cflag = 0;
        vt_tty[v].termios.c_lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON |
                                    TTY_LFLAG_ECHO;
        vt_tty[v].termios.c_line = 0;
        for (int i = 0; i < 32; i++)
            vt_tty[v].termios.c_cc[i] = 0;
        vt_tty[v].termios.c_cc[TTY_CC_VMIN] = 1;
        vt_tty[v].termios.c_cc[TTY_CC_VTIME] = 0;
        vt_tty[v].len = 0;
        vt_tty[v].line[0] = 0;
        vt_tty[v].ready = false;
        vt_tty[v].sigint = false;
        vt_tty[v].raw_head = 0;
        vt_tty[v].raw_tail = 0;
    }
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

// translates a raw-mode keypress into bytes for the VT's raw queue:
// printable chars, tabs/enter/backspace, Ctrl+letter control chars and
// the standard escape sequences for the arrow/navigation keys (what
// VT100 applications like the kilo editor expect)
static void keyboard_raw_push(uint8_t vt, uint16_t key, bool shift, bool ctrl)
{
    switch (key)
    {
        case KEY_UP: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, 'A'); return;
        case KEY_DOWN: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, 'B'); return;
        case KEY_RIGHT: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, 'C'); return;
        case KEY_LEFT: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, 'D'); return;
        case KEY_HOME: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, '1'); tty_push_raw(vt, '~'); return;
        case KEY_END: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, '4'); tty_push_raw(vt, '~'); return;
        case KEY_PAGE_UP: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, '5'); tty_push_raw(vt, '~'); return;
        case KEY_PAGE_DOWN: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, '6'); tty_push_raw(vt, '~'); return;
        case KEY_DELETE: tty_push_raw(vt, 0x1b); tty_push_raw(vt, '['); tty_push_raw(vt, '3'); tty_push_raw(vt, '~'); return;
        case KEY_ESC: tty_push_raw(vt, 0x1b); return;
        case KEY_TAB: tty_push_raw(vt, '\t'); return;
        case KEY_ENTER: tty_push_raw(vt, '\n'); return;
        case KEY_BACKSPACE: tty_push_raw(vt, 0x7F); return;
    }

    if (ctrl)
    {
        // Ctrl+letter -> control character; the ASCII maps a letter to
        // its position in the alphabet (Ctrl+C = 0x03, Ctrl+D = 0x04 ...)
        char c = hid_key_to_ascii(key, false);

        if (c >= 'a' && c <= 'z')
        {
            tty_push_raw(vt, (uint8_t)(c & 0x1F));
            return;
        }
    }

    char ascii = hid_key_to_ascii(key, shift);

    if (ascii >= 0x20)
        tty_push_raw(vt, (uint8_t)ascii);
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

    // Ctrl+Alt+F1..Fn: switch the active virtual terminal
    if (ctrl && alt && key >= KEY_F1 && key <= KEY_F6)
    {
        tty_vt_switch((uint8_t)(key - KEY_F1));
        return;
    }

    uint8_t vt = tty_active_vt();

    if (tty_raw_mode(vt))
    {
        // raw mode: only key presses produce bytes, Ctrl+C is delivered
        // as 0x03 instead of a signal, and special keys become escape
        // sequences. echo back printable chars if ECHO is set.
        if (!pressed)
            return;

        if ((vt_tty[vt].termios.c_lflag & TTY_LFLAG_ECHO) &&
            key != KEY_UP && key != KEY_DOWN && key != KEY_LEFT &&
            key != KEY_RIGHT && key != KEY_HOME && key != KEY_END &&
            key != KEY_PAGE_UP && key != KEY_PAGE_DOWN &&
            key != KEY_DELETE)
        {
            char ascii = hid_key_to_ascii(key, shift);

            if (ascii >= 0x20)
            {
                debug_putc(ascii);
                vga_vt_putc(vt, ascii);
            }
        }

        keyboard_raw_push(vt, key, shift, ctrl);
        return;
    }

    // canonical line input: only key presses produce characters
    if (!pressed)
        return;

    // Ctrl+C: interrupt the active terminal (used by bash to stop
    // the foreground program), unless ISIG is turned off
    if (ctrl && key == KEY_C && (vt_tty[vt].termios.c_lflag & TTY_LFLAG_ISIG))
    {
        tty_sigint_trigger(vt);
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

static void tty_push_raw(uint8_t vt, uint8_t byte)
{
    vt_tty_t *t = &vt_tty[vt];
    uint32_t next = (t->raw_head + 1) % TTY_RAW_MAX;

    if (next == t->raw_tail)
        return;  // full, drop

    t->raw[t->raw_head] = byte;
    t->raw_head = next;
}

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

void tty_set_mode(uint8_t vt, const dynt_termios_t *t)
{
    if (vt >= TTY_VT_MAX || !t)
        return;

    vt_tty_t *s = &vt_tty[vt];
    s->termios = *t;

    // tcsetattr discards pending input: reset the canonical line and
    // the raw queue so stale bytes never leak into the new mode
    s->len = 0;
    s->line[0] = 0;
    s->ready = false;
    s->raw_head = 0;
    s->raw_tail = 0;
}

void tty_get_mode(uint8_t vt, dynt_termios_t *out)
{
    if (vt >= TTY_VT_MAX || !out)
        return;

    *out = vt_tty[vt].termios;
}

bool tty_raw_mode(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return false;

    return (vt_tty[vt].termios.c_lflag & TTY_LFLAG_ICANON) == 0;
}

uint32_t tty_raw_available(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return 0;

    return (vt_tty[vt].raw_head - vt_tty[vt].raw_tail) % TTY_RAW_MAX;
}

uint32_t tty_read_raw(uint8_t vt, void *buf, uint32_t len)
{
    if (vt >= TTY_VT_MAX || !buf)
        return 0;

    vt_tty_t *t = &vt_tty[vt];
    uint8_t *dst = (uint8_t *)buf;
    uint32_t n = 0;

    while (n < len && t->raw_tail != t->raw_head)
    {
        dst[n++] = t->raw[t->raw_tail];
        t->raw_tail = (t->raw_tail + 1) % TTY_RAW_MAX;
    }

    return n;
}

uint8_t tty_get_vmin(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return 0;

    return vt_tty[vt].termios.c_cc[TTY_CC_VMIN];
}

uint8_t tty_get_vtime(uint8_t vt)
{
    if (vt >= TTY_VT_MAX)
        return 0;

    return vt_tty[vt].termios.c_cc[TTY_CC_VTIME];
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

    if (c >= 0x01 && t->len < TTY_LINE_MAX - 1)
    {
        t->line[t->len++] = c;
        if (c >= 0x20)
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

// drains polled COM1 input into the active VT's line buffer so
// a headless serial console works as stdin without a display/PS2
void tty_drain_serial(void)
{
    uint8_t vt = tty_active_vt();

    while (serial_received())
    {
        char c = serial_read_char();

        if (tty_raw_mode(vt))
            tty_push_raw(vt, (uint8_t)c);
        else
            tty_input_char(c);
    }
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