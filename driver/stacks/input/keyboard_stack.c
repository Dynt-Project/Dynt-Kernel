//
// Created by epaxgaming on 31.07.26.
//
//explained in the header file

#include "keyboard_stack.h"

static bool key_state[KEYBOARD_MAX_KEYS];

static bool shift;
static bool ctrl;
static bool alt;

static keyboard_event_t queue[KEYBOARD_QUE_SIZE];

static uint32_t head;
static uint32_t tail;


//inits keyboard stack
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

// translates a scancode set 1 keycode into an ascii char
// only normal (non extended) keycodes have an ascii representation
char hid_key_to_ascii(uint16_t keycode,bool shift)
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