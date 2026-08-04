// Written by [@saphhic](https://github.com/saphhic)
// Date: 26 July 2026

//   Userspace-facing syscall table.  Syscall 0-1 (exit/write) prove the
//   SYSCALL/SYSRET path, 2-6 give the ring3 BusyBox a working stdin,
//   filesystem and exec.  Debug messages still go to COM1 serial.

#include "syscall.h"
#include "usermode.h"

#include "../io/serial.h"
#include "../cpu/cpu.h"
#include "../cpu/percpu.h"

#include "../../../driver/stacks/input/keyboard_stack.h"
#include "../../../driver/stacks/video/video_stack.h"
#include "../../../fs/vfs.h"
#include "../../../exec/elf.h"
#include "../../../mem/mm/kheap.h"
#include "../../../init/debug.h"

// user memory lives at 0x2000000 and up (userspace/linker.ld)
#define USER_MIN 0x2000000ULL

// saved parent context, filled by SYS_EXEC so SYS_EXIT can resume it
static bool exec_saved;
static uint64_t exec_rip;
static uint64_t exec_rsp;
static uint64_t exec_rflags;

static bool user_ptr_ok(const void *ptr, uint64_t len)
{
    uint64_t p = (uint64_t)ptr;
    return p >= USER_MIN && p <= USER_MIN + 0x4000000ULL && len <= 0x4000000ULL;
}

// writes to serial always, VGA understands the ANSI sequences BusyBox
// sends for clear (\x1b[2J) and cursor home (\x1b[H)
static void term_write(const char *buf, uint64_t len)
{
    enum { T_NORM, T_ESC, T_CSI } state = T_NORM;

    for (uint64_t i = 0; i < len; i++)
    {
        char c = buf[i];
        serial_write_char(c);

        if (!video_has_driver())
            continue;

        switch (state)
        {
            case T_NORM:
                if (c == 0x1b)
                    state = T_ESC;
                else
                    video_putc(c);
                break;

            case T_ESC:
                if (c == '[')
                    state = T_CSI;
                else
                {
                    video_putc(0x1b);
                    video_putc(c);
                    state = T_NORM;
                }
                break;

            case T_CSI:
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
                {
                    if (c == 'J')
                        video_clear();
                    else if (c == 'H' || c == 'f')
                        video_set_cursor(0, 0);
                    state = T_NORM;
                }
                break;
        }
    }
}

// blocking line read with echo; returns length (newline stripped) or -1
static int32_t sys_read_line(char *buf, uint64_t len)
{
    if (!user_ptr_ok(buf, len) || len == 0)
        return -1;

    int32_t n = 0;

    for (;;)
    {
        keyboard_event_t ev;

        // wait for the next event with interrupts on; IRQ frames go to
        // the TSS rsp0 stack, our syscall frame lives on the dedicated
        // syscall stack, so nothing collides
        sti();
        while (!keyboard_poll_event(&ev))
        {
            hlt();
        }
        cli();

        if (!ev.pressed)
            continue;

        if (ev.keycode == KEY_ENTER)
        {
            if (n > 0)
                debug_putc('\n');
            buf[n] = 0;
            return n;
        }

        if (ev.keycode == KEY_BACKSPACE)
        {
            if (n > 0)
            {
                n--;
                debug_puts("\b \b");
            }
            continue;
        }

        if (ev.ascii && n < (int32_t)len - 1)
        {
            buf[n++] = ev.ascii;
            debug_putc(ev.ascii);
        }
    }
}

typedef struct
{
    char *buf;
    uint32_t size;
    uint32_t used;
} list_buf_t;

static void list_append(const char *name, uint32_t size, bool is_dir, void *user)
{
    list_buf_t *lb = (list_buf_t *)user;
    char line[96];
    int n = 0;

    for (const char *p = name; *p && n < 64; p++)
        line[n++] = *p;

    if (is_dir)
        line[n++] = '/';

    while (n < 48)
        line[n++] = ' ';

    char tmp[16];
    int t = 0;
    do
    {
        tmp[t++] = (char)('0' + size % 10);
        size /= 10;
    } while (size && t < 16);

    while (t)
        line[n++] = tmp[--t];

    line[n++] = '\n';

    for (int i = 0; i < n && lb->used < lb->size - 1; i++)
        lb->buf[lb->used++] = line[i];
}

static int32_t sys_list_dir(const char *path, char *buf, uint64_t len)
{
    if (!user_ptr_ok(path, 1) || !user_ptr_ok(buf, len))
        return -1;

    list_buf_t lb;
    lb.buf = buf;
    lb.size = (uint32_t)(len > 0 ? len - 1 : 0);
    lb.used = 0;

    vfs_list_dir(path, list_append, &lb);

    buf[lb.used] = 0;
    return (int32_t)lb.used;
}

static int32_t sys_read_file(const char *path, void *buf, uint64_t len)
{
    if (!user_ptr_ok(path, 1) || !user_ptr_ok(buf, len))
        return -1;

    return vfs_read_file(path, buf, (uint32_t)len);
}

// jumps into a freshly loaded ELF, never returns
static void sys_exec(const char *path)
{
    if (!user_ptr_ok(path, 1))
        return;

    uint8_t *elf_buf = (uint8_t *)kheap_alloc(1024 * 1024, 16);
    if (!elf_buf)
        return;

    int32_t size = vfs_read_file(path, elf_buf, 1024 * 1024);
    if (size <= 0)
    {
        kheap_free(elf_buf);
        return;
    }

    elf_image_t img;
    if (!elf64_load_image(elf_buf, (size_t)size, &img))
    {
        kheap_free(elf_buf);
        return;
    }

    uint64_t user_stack = (uint64_t)kheap_alloc(65536, 16);
    if (!user_stack)
    {
        kheap_free(elf_buf);
        return;
    }
    user_stack += 65536 - 16;

    kheap_free(elf_buf);

    debug_printf("[syscall] exec %s: entry=0x%p\n", path, img.entry);

    usermode_resume(img.entry, user_stack, 0x202);
}

static void sys_exit(void)
{
    if (exec_saved)
    {
        exec_saved = false;
        debug_printf("[syscall] resuming parent\n");
        // the parent's SYS_EXEC must see a defined return value
        __asm__ volatile("xor %rax, %rax");
        usermode_resume(exec_rip, exec_rsp, exec_rflags);
    }

    serial_write("\n[syscall] SYS_EXIT, System Halting\n");
    cli();
    for (;;)
    {
        hlt();
    }
}

void syscall_dispatch(syscall_regs_t *regs) {
    switch (regs->rax) {

        case SYS_WRITE:
            term_write((const char *)regs->rdi, regs->rsi);
            regs->rax = 0;
            break;

        case SYS_READ:
            regs->rax = sys_read_line((char *)regs->rdi, regs->rsi);
            break;

        case SYS_READ_FILE:
            regs->rax = sys_read_file((const char *)regs->rdi,
                                      (void *)regs->rsi, regs->rdx);
            break;

        case SYS_LIST_DIR:
            regs->rax = sys_list_dir((const char *)regs->rdi,
                                     (char *)regs->rsi, regs->rdx);
            break;

        case SYS_EXEC: {
            // save the calling context so SYS_EXIT can come back
            exec_saved = true;
            exec_rip = regs->rcx;
            exec_rsp = g_percpu0.user_rsp;
            exec_rflags = regs->r11;

            sys_exec((const char *)regs->rdi);

            // sys_exec failed, restore the saved context for the parent
            exec_saved = false;
            regs->rax = (uint64_t)-1;
            break;
        }

        case SYS_GETPID:
            regs->rax = 1;
            break;

        case SYS_EXIT:
            sys_exit();
            break;

        default:
            serial_write("\n[syscall] UNKNOW, unknown syscall number.\n");
            regs->rax = (uint64_t)-1;
            break;
    }
}
