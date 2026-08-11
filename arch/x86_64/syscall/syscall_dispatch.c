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
#include "../cpu/control_regs.h"
#include "../cpu/percpu.h"

#include "driver/stacks/input/keyboard_stack.h"
#include "driver/stacks/video/video_stack.h"
#include "driver/buildin/video/vga/vga.h"
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

// writes to serial always, VGA understands the ANSI sequences Bash/Kilo
// send (clear \x1b[2J, cursor home \x1b[H, cursor positioning, erase
// line/screen, SGR colors and reverse video, hide/show cursor); returns
// bytes written. output goes to the given virtual terminal's buffer
// (each VT runs its own shell), serial is shared by all VTs.

// SGR foreground color -> VGA palette. VGA has 16 fixed colors.
static uint8_t sgr_to_vga(int sgr)
{
    switch (sgr)
    {
        case 30: return 0;   /* black */
        case 31: return 4;   /* red */
        case 32: return 2;   /* green */
        case 33: return 14;  /* yellow */
        case 34: return 1;   /* blue */
        case 35: return 5;   /* magenta */
        case 36: return 3;   /* cyan */
        case 37: return 7;   /* white */
        case 90: return 8;   /* bright black */
        case 91: return 12;  /* bright red */
        case 92: return 10;  /* bright green */
        case 93: return 14;  /* bright yellow */
        case 94: return 9;   /* bright blue */
        case 95: return 13;  /* bright magenta */
        case 96: return 11;  /* bright cyan */
        case 97: return 15;  /* bright white */
        default: return 0xFF;
    }
}

static void csi_apply(uint8_t vt, bool priv, const int *params, int nparams,
                      char final)
{
    int p0 = nparams > 0 && params[0] >= 0 ? params[0] : 0;
    int p1 = nparams > 1 && params[1] >= 0 ? params[1] : 0;

    if (priv)
    {
        // \x1b[?25l / \x1b[?25h: hide/show the hardware cursor
        if (p0 == 25 && (final == 'l' || final == 'h'))
            vga_vt_set_cursor_visible(vt, final == 'h');
        return;
    }

    switch (final)
    {
        case 'H':
        case 'f':
        {
            // cursor position (1-based), default 1;1
            uint16_t row = (p0 > 0) ? (uint16_t)(p0 - 1) : 0;
            uint16_t col = (p1 > 0) ? (uint16_t)(p1 - 1) : 0;
            vga_vt_set_cursor(vt, col, row);
            break;
        }

        case 'A':
            vga_vt_move_cursor(vt, 0, (int16_t)-(p0 > 0 ? p0 : 1));
            break;

        case 'B':
            vga_vt_move_cursor(vt, 0, (int16_t)(p0 > 0 ? p0 : 1));
            break;

        case 'C':
            vga_vt_move_cursor(vt, (int16_t)(p0 > 0 ? p0 : 1), 0);
            break;

        case 'D':
            vga_vt_move_cursor(vt, (int16_t)-(p0 > 0 ? p0 : 1), 0);
            break;

        case 'J':
            if (p0 == 2)
                vga_vt_clear(vt);
            else
                vga_vt_clear_to_escreen(vt);
            break;

        case 'K':
            if (p0 == 2)
                vga_vt_clear_line(vt);
            else
                vga_vt_clear_to_eol(vt);
            break;

        case 'm':
            // SGR; 0 = reset, 7 = reverse video, 30..97 = foreground
            for (int i = 0; i < nparams; i++)
            {
                int p = params[i];
                if (p < 0)
                    p = 0;
                if (p == 0)
                {
                    vga_vt_set_reverse(vt, false);
                    vga_vt_set_fg_color(vt, 7);
                    vga_vt_set_bg_color(vt, 0);
                }
                else if (p == 7)
                    vga_vt_set_reverse(vt, true);
                else if (p == 39)
                    vga_vt_set_fg_color(vt, 7);
                else
                {
                    uint8_t vgac = sgr_to_vga(p);
                    if (vgac != 0xFF)
                        vga_vt_set_fg_color(vt, vgac);
                }
            }
            break;
    }
}

static uint64_t term_write(uint8_t vt, const char *buf, uint64_t len)
{
    enum { T_NORM, T_ESC, T_CSI } state = T_NORM;
    int params[4];
    int nparams = 0;
    bool priv = false;

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
                    vga_vt_putc(vt, c);
                break;

            case T_ESC:
                if (c == '[')
                {
                    state = T_CSI;
                    nparams = 0;
                    priv = false;
                    for (int j = 0; j < 4; j++)
                        params[j] = -1;
                }
                else
                {
                    vga_vt_putc(vt, 0x1b);
                    vga_vt_putc(vt, c);
                    state = T_NORM;
                }
                break;

            case T_CSI:
                if (c == '?')
                {
                    priv = true;
                    break;
                }
                if (c >= '0' && c <= '9')
                {
                    if (nparams < 4)
                    {
                        if (params[nparams] < 0)
                            params[nparams] = 0;
                        params[nparams] = params[nparams] * 10 + (c - '0');
                    }
                    break;
                }
                if (c == ';')
                {
                    if (nparams < 3)
                        nparams++;
                    break;
                }
                // any other byte finishes the sequence (ESC is handled
                // below so a fresh sequence can nest)
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == 0x1b)
                {
                    if (c != 0x1b)
                        csi_apply(vt, priv, params, nparams + 1, c);
                    state = T_NORM;
                    if (c == 0x1b)
                        i--;
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
    // park in a hlt loop; the timer preempts blocked processes (so other
    // processes keep running), then this one resumes and re-checks the
    // deadline. a SYS_KILL against this process aborts the sleep early.
    process_t *cur = process_current();
    uint64_t start = scheduler_ticks();
    uint64_t ticks = ms / 10;  // 100 Hz timer
    uint64_t deadline = start + ticks;

    if (cur)
        cur->blocked = true;
    sti();
    while (scheduler_ticks() < deadline)
    {
        if (scheduler_terminate_requested())
            scheduler_exit_current(process_current()->exit_status);
        hlt();
    }
    cli();
    if (cur)
        cur->blocked = false;
}

typedef struct
{
    char *buf;
    uint32_t size;
    uint32_t used;
} ps_buf_t;

static void ps_append(uint64_t pid, const char *name, const char *state,
                      uint32_t cpu, void *user)
{
    ps_buf_t *pb = (ps_buf_t *)user;
    char line[72];
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

    while (n < 8)
        line[n++] = ' ';

    uint32_t c = cpu;
    do
    {
        line[n++] = (char)('0' + c % 10);
        c /= 10;
    } while (c && n < 16);
    for (int i = 8, j = n - 1; i < j; i++, j--)
    {
        char t = line[i];
        line[i] = line[j];
        line[j] = t;
    }

    while (n < 16)
        line[n++] = ' ';

    for (const char *s = state; *s && n < 28; s++)
        line[n++] = *s;

    while (n < 28)
        line[n++] = ' ';

    for (const char *s = name; *s && n < 60; s++)
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
            {
                process_t *cur = process_current();
                uint8_t vt = cur ? cur->vt : 0;
                regs->rax = term_write(vt, (const char *)regs->rsi,
                                       regs->rdx);
            }
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
                // tty read, blocking so a shell/fgets (canonical) or the
                // kilo editor (raw) can wait for input on THIS process's
                // virtual terminal. keyboard IRQs, serial bytes and timer
                // ticks all wake hlt.
                process_t *cur = process_current();
                uint8_t vt = cur ? cur->vt : 0;
                bool raw = tty_raw_mode(vt);

                if (cur)
                    cur->blocked = true;
                sti();
#if SCHEDULER_DEBUG
                debug_printf("[read] pid %lu vt%u krsp=0x%lx\n", cur->pid, vt,
                             percpu_current()->kernel_rsp);
#endif
                if (raw)
                {
                    // raw mode: return bytes as they arrive. VMIN = how
                    // many bytes to wait for (0 = any), VTIME = timeout
                    // in 0.1s units (0 = wait forever).
                    uint8_t vmin = tty_get_vmin(vt);
                    uint8_t vtime = tty_get_vtime(vt);
                    uint64_t deadline = 0;

                    if (vtime > 0)
                        deadline = scheduler_ticks() + (uint64_t)vtime * 10;

                    for (;;)
                    {
                        tty_drain_serial();

                        uint32_t avail = tty_raw_available(vt);

                        if (avail > 0 && (vmin == 0 || avail >= vmin))
                        {
                            regs->rax = (uint64_t)tty_read_raw(vt, buf,
                                                               (uint32_t)len);
                            break;
                        }

                        if (vtime > 0 && scheduler_ticks() >= deadline)
                        {
                            regs->rax = 0;
                            break;
                        }

                        if (scheduler_terminate_requested())
                            scheduler_exit_current(process_current()->exit_status);

                        hlt();
                    }
                }
                else
                {
                    for (;;)
                    {
                        tty_drain_serial();

                        if (tty_line_ready(vt))
                        {
                            regs->rax = (uint64_t)tty_getline(vt, (char *)buf,
                                                              (int)len);
                            break;
                        }

                        if (tty_sigint_consume(vt))
                        {
                            // Ctrl+C: return -EINTR, a shell reacts to it
                            regs->rax = (uint64_t)-4;
                            break;
                        }

                        if (scheduler_terminate_requested())
                            scheduler_exit_current(process_current()->exit_status);

                        hlt();
                    }
                }
                cli();
                if (cur)
                    cur->blocked = false;
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

        case SYS_FORK: {
            process_t *parent = process_current();
            process_t *child = process_fork(regs);

            if (!child)
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            scheduler_enqueue(child);
#if SCHEDULER_DEBUG
            debug_printf("[syscall] fork: pid %lu -> pid %lu (cpu %u)\n",
                         parent ? parent->pid : 0, child->pid,
                         (unsigned)child->cpu);
#endif
            regs->rax = child->pid;
            break;
        }

        case SYS_EXECVE: {
            const char *path = (const char *)regs->rdi;
            char *const *argv = (char *const *)regs->rsi;
            char *const *envp = (char *const *)regs->rdx;

            process_t *cur = process_current();
            if (!cur)
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            // execve builds a fresh address space and frees the old one,
            // so the user path buffer must be copied out first
            char *path_buf = read_user_cstr(cur->cr3, (uint64_t)path);
            if (!path_buf)
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            if (!process_execve(cur, path_buf, argv, envp))
            {
                kheap_free(path_buf);
                regs->rax = (uint64_t)-1;
                break;
            }

            // process_execve replaced the image and the saved context
            // (rip/rsp); execve returns via the regular sysret path but
            // with the new rip/rsp, so it never returns to the caller.
            // the new image is mapped in a fresh cr3 - switch to it and
            // reset fs (crt1 re-establishes the TCB on entry).
            write_cr3(cur->cr3);
            wrmsr(MSR_IA32_FS_BASE, 0);
            percpu_current()->kernel_rsp = cur->kernel_stack_top;
            regs->rcx = cur->ctx.rip;
            percpu_current()->user_rsp = cur->ctx.user_rsp;
            regs->rax = 0;
#if SCHEDULER_DEBUG
            debug_printf("[syscall] execve pid %lu -> %s\n", cur->pid,
                         path_buf);
#endif
            kheap_free(path_buf);
            break;
        }

        case SYS_WAITPID: {
            process_t *cur = process_current();

            if (!cur)
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            int64_t want = (int64_t)regs->rdi;   // 0 or negative = any child
            int32_t *status = (int32_t *)regs->rsi;
            uint64_t vt = cur->vt;

            cur->blocked = true;
            sti();
            for (;;)
            {
                process_t *z = process_find_zombie_child(cur, want <= 0 ? 0 : want);

                if (z)
                {
                    uint64_t zpid = z->pid;
                    int32_t st = z->exit_status;

                    if (status && user_ptr_ok(status, 4))
                        *status = st;

                    process_reap_child(cur, z);
                    regs->rax = zpid;
                    break;
                }

                if (tty_sigint_consume(vt))
                {
                    // Ctrl+C while waiting: tell the shell, it kills
                    // the foreground program itself
                    regs->rax = (uint64_t)-4;
                    break;
                }

                if (scheduler_terminate_requested())
                    scheduler_exit_current(process_current()->exit_status);

                hlt();
            }
            cli();
            cur->blocked = false;
            break;
        }

        case SYS_KILL:
            scheduler_terminate(regs->rdi);
            regs->rax = 0;
            break;

        case SYS_VTSET: {
            process_t *cur = process_current();

            if (cur)
                cur->vt = (uint8_t)(regs->rdi & 0xFF);

            regs->rax = 0;
            break;
        }

        case SYS_TCGETATTR: {
            process_t *cur = process_current();
            uint8_t vt = cur ? cur->vt : 0;
            dynt_termios_t *out = (dynt_termios_t *)regs->rdi;

            if (!user_ptr_ok(out, sizeof(dynt_termios_t)))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            tty_get_mode(vt, out);
            regs->rax = 0;
            break;
        }

        case SYS_TCSETATTR: {
            process_t *cur = process_current();
            uint8_t vt = cur ? cur->vt : 0;
            dynt_termios_t *in = (dynt_termios_t *)regs->rdi;

            if (!user_ptr_ok(in, sizeof(dynt_termios_t)))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            tty_set_mode(vt, in);
            regs->rax = 0;
            break;
        }

        case SYS_TTYWINSIZE: {
            // returns the virtual terminal geometry as rows;cols, 16-bit
            // each, which mlibc maps into struct winsize for tcgetwinsize()
            uint32_t *out = (uint32_t *)regs->rdi;

            if (!user_ptr_ok(out, 4))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            uint32_t packed = ((uint32_t)vga_vt_rows() << 16) |
                              vga_vt_cols();
            *out = packed;
            regs->rax = 0;
            break;
        }

        case SYS_MKDIR: {
            const char *path = (const char *)regs->rdi;

            if (!user_ptr_ok(path, 1))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            regs->rax = (uint64_t)vfs_mkdir(path);
            break;
        }

        case SYS_REMOVE: {
            const char *path = (const char *)regs->rdi;

            if (!user_ptr_ok(path, 1))
            {
                regs->rax = (uint64_t)-1;
                break;
            }

            regs->rax = (uint64_t)vfs_remove(path);
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

        case SYS_EXIT: {
            process_t *ecur = process_current();
#if SCHEDULER_DEBUG
            debug_printf("[exit] pid %lu code=0x%lx\n",
                         ecur ? ecur->pid : 0, regs->rdi);
#endif
            // encode the exit code POSIX-style so waitpid can decode it
            scheduler_exit_current((regs->rdi & 0xFF) << 8);
            break;
        }

        default:
            serial_write("\n[syscall] UNKNOW, unknown syscall number.\n");
            regs->rax = (uint64_t)-1;
            break;
    }
}
