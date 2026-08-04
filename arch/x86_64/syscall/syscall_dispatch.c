// Written by [@saphhic](https://github.com/saphhic)
// Date: 26 July 2026

//   Userspace-facing syscall table.  SYS_WRITE/READ drive the terminal,
//   SYS_READ_FILE/LIST_DIR reach the VFS, SYS_EXEC spawns a new process
//   and SYS_EXIT terminates the caller.  Everything is non-blocking so
//   the scheduler can preempt user processes freely.

#include "syscall.h"

#include "../io/serial.h"
#include "../cpu/cpu.h"
#include "../cpu/msr.h"

#include "driver/stacks/input/keyboard_stack.h"
#include "driver/stacks/video/video_stack.h"
#include "fs/vfs.h"
#include "exec/elf.h"
#include "mem/mm/kheap.h"
#include "mem/mm/paging.h"
#include "mem/mm/pmm.h"
#include "process/process.h"
#include "scheduler/scheduler.h"
#include "init/debug.h"

// user memory lives at PAGING_USER_BASE and up; the user stack tops out
// at PROCESS_USER_STACK_TOP (see process/process.h)
#define USER_MIN PAGING_USER_BASE
#define USER_MAX PROCESS_USER_STACK_TOP

static bool user_ptr_ok(const void *ptr, uint64_t len)
{
    uint64_t p = (uint64_t)ptr;
    return p >= USER_MIN && p < USER_MAX && len <= 0x1000000000ULL &&
           p + len < USER_MAX;
}

// writes to serial always, VGA understands the ANSI sequences BusyBox
// sends for clear (\x1b[2J) and cursor home (\x1b[H); returns bytes written
static uint64_t term_write(const char *buf, uint64_t len)
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

    return len;
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

static void sys_sleep(uint64_t ms)
{
    // spin with hlt; a timer irq wakes the cpu each tick, and the timer
    // handler refuses to preempt kernel mode, so this is a clean wait
    uint64_t now = 0;
    uint64_t target = ms / 10;  // ticks at 100 Hz

    sti();
    while (now < target)
    {
        hlt();
        now++;
    }
    cli();
}

typedef struct
{
    char *buf;
    uint32_t size;
    uint32_t used;
} ps_buf_t;

static void ps_append(uint64_t pid, const char *name, const char *state,
                      void *user)
{
    ps_buf_t *pb = (ps_buf_t *)user;
    char line[64];
    int n = 0;

    uint64_t p = pid;
    do
    {
        line[n++] = (char)('0' + p % 10);
        p /= 10;
    } while (p && n < 8);
    // reverse pid digits
    for (int i = 0, j = n - 1; i < j; i++, j--)
    {
        char t = line[i];
        line[i] = line[j];
        line[j] = t;
    }

    line[n++] = ' ';

    for (const char *s = name; *s && n < 40; s++)
        line[n++] = *s;

    while (n < 40)
        line[n++] = ' ';

    for (const char *s = state; *s && n < 56; s++)
        line[n++] = *s;

    line[n++] = '\n';

    for (int i = 0; i < n && pb->used < pb->size - 1; i++)
        pb->buf[pb->used++] = line[i];
}

static int32_t sys_ps(char *buf, uint64_t len)
{
    if (!user_ptr_ok(buf, len))
        return -1;

    ps_buf_t pb;
    pb.buf = buf;
    pb.size = (uint32_t)(len > 0 ? len - 1 : 0);
    pb.used = 0;

    scheduler_list(ps_append, &pb);

    buf[pb.used] = 0;
    return (int32_t)pb.used;
}

// anonymous user mapping for mlibc's malloc arena. the user asks for
// `len` bytes and gets a page-aligned region below the stack
static uint64_t sys_mmap(uint64_t len, uint64_t flags)
{
    (void)flags;
    if (len == 0)
        len = 4096;
    len = (len + 4095) & ~4095ULL;
    if (len > 0x40000000ULL)
        return (uint64_t)-1;

    process_t *cur = process_current();
    if (!cur)
        return (uint64_t)-1;

    uint64_t base = cur->mmap_cursor;
    if (base + len > PROCESS_USER_STACK_TOP)
        return (uint64_t)-1;

    const uint64_t map_flags = PAGING_FLAG_PRESENT | PAGING_FLAG_WRITABLE |
                               PAGING_FLAG_USER;

    for (uint64_t va = base; va < base + len; va += 4096)
    {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys)
        {
            for (uint64_t v2 = base; v2 < va; v2 += 4096)
            {
                pmm_free_frame(paging_translate(cur->cr3, v2));
                paging_unmap(cur->cr3, v2);
            }
            return (uint64_t)-1;
        }
        pmm_zero_page(phys);
        paging_map(cur->cr3, va, phys, map_flags);
    }

    cur->mmap_cursor = base + len;
    return base;
}

static int64_t sys_munmap(uint64_t addr, uint64_t len)
{
    len = (len + 4095) & ~4095ULL;
    if (len == 0 || addr < USER_MIN || addr + len >= PROCESS_USER_STACK_TOP)
        return -1;

    process_t *cur = process_current();
    if (!cur)
        return -1;

    for (uint64_t va = addr; va < addr + len; va += 4096)
    {
        uintptr_t phys = paging_translate(cur->cr3, va);
        if (phys)
            pmm_free_frame(phys);
        paging_unmap(cur->cr3, va);
    }
    return 0;
}

void syscall_dispatch(syscall_regs_t *regs) {
    switch (regs->rax) {

        case SYS_WRITE:
            // Linux ABI: write(fd, buf, count) -> rdi=fd, rsi=buf, rdx=count
            if (!user_ptr_ok((void *)regs->rsi, regs->rdx))
            {
                regs->rax = (uint64_t)-1;
                break;
            }
            if (regs->rdi <= 2)
                regs->rax = term_write((const char *)regs->rsi, regs->rdx);
            else
                regs->rax = (uint64_t)vfs_write_fd(process_current(),
                                                   (int32_t)regs->rdi,
                                                   (const void *)regs->rsi,
                                                   regs->rdx);
            break;

        case SYS_READ: {
            // Linux ABI: read(fd, buf, count) -> rdi=fd, rsi=buf, rdx=count
            int64_t fd = (int64_t)regs->rdi;
            void *buf = (void *)regs->rsi;
            uint64_t len = regs->rdx;

            if (len == 0)
            {
                regs->rax = 0;
                break;
            }
            if (!user_ptr_ok(buf, len))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            if (fd == 0)
            {
                // canonical tty read, blocking so a shell/fgets can wait
                // for a line; keyboard IRQs and serial bytes both wake hlt
                sti();
                do
                {
                    tty_drain_serial();
                    if (tty_line_ready())
                        break;
                    hlt();
                } while (true);
                cli();
                regs->rax = (uint64_t)tty_getline((char *)buf, (int)len);
            }
            else if (fd >= 3)
                regs->rax = (uint64_t)vfs_read_fd(process_current(),
                                                  (int32_t)fd, buf, len);
            else
                regs->rax = (uint64_t)-1;  // EBADF
            break;
        }

        case SYS_OPEN: {
            const char *path = (const char *)regs->rdi;
            uint32_t flags = (uint32_t)regs->rsi;

            if (!user_ptr_ok(path, 1))
            {
                regs->rax = (uint64_t)-1;
                break;
            }
            regs->rax = (uint64_t)vfs_open_fd(process_current(), path, flags);
            break;
        }

        case SYS_CLOSE:
            regs->rax = (uint64_t)vfs_close_fd(process_current(),
                                               (int32_t)regs->rdi);
            break;

        case SYS_SEEK:
            regs->rax = (uint64_t)vfs_seek_fd(process_current(),
                                              (int32_t)regs->rdi,
                                              (int64_t)regs->rsi,
                                              (uint32_t)regs->rdx);
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
            const char *path = (const char *)regs->rdi;

            if (!user_ptr_ok(path, 1))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            process_t *child = process_create("user");
            if (!child || !process_load_elf(child, path))
            {
                if (child)
                    process_destroy(child);
                regs->rax = (uint64_t)-1;
                break;
            }

            scheduler_enqueue(child);
            debug_printf("[syscall] spawned pid %lu running %s on cpu %u\n",
                         child->pid, path, (unsigned)child->cpu);
            regs->rax = child->pid;
            break;
        }

        case SYS_GETPID: {
            process_t *cur = process_current();
            regs->rax = cur ? cur->pid : 1;
            break;
        }

        case SYS_SLEEP:
            sys_sleep(regs->rdi);
            regs->rax = 0;
            break;

        case SYS_PS:
            regs->rax = sys_ps((char *)regs->rdi, regs->rsi);
            break;

        case SYS_MMAP:
            regs->rax = sys_mmap(regs->rdi, regs->rsi);
            break;

        case SYS_MUNMAP:
            regs->rax = sys_munmap(regs->rdi, regs->rsi);
            break;

        case SYS_GETTICKS:
            regs->rax = scheduler_ticks();
            break;

        case SYS_SETFSBASE: {
            process_t *cur = process_current();
            if (cur)
            {
                cur->fs_base = regs->rdi;
                wrmsr(MSR_IA32_FS_BASE, regs->rdi);
            }
            regs->rax = 0;
            break;
        }

        case SYS_EXIT:
            scheduler_exit_current();
            break;

        default:
            serial_write("\n[syscall] UNKNOW, unknown syscall number.\n");
            regs->rax = (uint64_t)-1;
            break;
    }
}
