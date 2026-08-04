// Preemptive round-robin scheduler for user processes.
//
// The timer IRQ fires on the per-CPU interrupt stack.  In user mode the
// frame carries the whole user context (GP registers + iretq frame), so
// switching is: save the frame into the current pcb, pick the next ready
// process, load its cr3 and copy its saved context back into the frame
// before iretq.  This preserves every register across a preempt.

#include "scheduler.h"

#include "../arch/x86_64/cpu/control_regs.h"
#include "../arch/x86_64/cpu/cpu.h"
#include "../arch/x86_64/cpu/msr.h"
#include "../arch/x86_64/syscall/usermode.h"
#include "../mem/lib/memory.h"

#define SCHED_QUANTUM 5  // ticks before yielding (100 Hz -> 50 ms)

static process_t *runqueue;
static uint32_t proc_count;
static uint64_t uptime_ticks;

uint64_t scheduler_ticks(void)
{
    return uptime_ticks;
}

void scheduler_init(void)
{
    runqueue = 0;
    proc_count = 0;
}

void scheduler_enqueue(process_t *proc)
{
    if (!proc)
        return;

    proc->state = PROC_READY;
    proc->next = runqueue;
    runqueue = proc;
    proc_count++;
}

static void unlink(process_t *proc)
{
    process_t **link = &runqueue;

    while (*link)
    {
        if (*link == proc)
        {
            *link = proc->next;
            proc_count--;
            return;
        }
        link = &(*link)->next;
    }
}

static process_t *pick_next(process_t *skip)
{
    if (!runqueue)
        return 0;

    process_t *start = runqueue;
    process_t *p = start;

    do
    {
        if (p->state == PROC_READY && p != skip)
            return p;
        p = p->next ? p->next : runqueue;
    } while (p != start);

    return 0;
}

void scheduler_timer_tick(registers_t *regs)
{
    uptime_ticks++;

    // only preempt user mode; a syscall or kernel handler must be atomic
    if (regs->cs != 0x23)
        return;

    process_t *cur = process_current();

    if (cur && cur->state == PROC_RUNNING)
    {
        cur->ticks++;
        cur->ctx = *regs;

        if (cur->ticks < SCHED_QUANTUM)
            return;
    }

    process_t *next = pick_next(cur);

    if (!next || next == cur)
        return;

    if (cur)
    {
        cur->ticks = 0;
        cur->state = PROC_READY;
    }

    next->ticks = 0;
    next->state = PROC_RUNNING;
    process_set_current(next);

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);

    *regs = next->ctx;
}

[[noreturn]] void scheduler_exit_current(void)
{
    process_t *cur = process_current();

    if (cur)
    {
        unlink(cur);
        process_set_current(0);
        process_destroy(cur);
    }

    process_t *next = pick_next(0);

    if (!next)
    {
        cli();
        for (;;)
        {
            hlt();
        }
    }

    next->ticks = 0;
    next->state = PROC_RUNNING;
    process_set_current(next);

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);
    usermode_resume_full(&next->ctx);
}

void scheduler_list(sched_list_cb cb, void *user)
{
    if (!cb)
        return;

    for (process_t *p = runqueue; p; p = p->next)
    {
        const char *state = "ready";

        if (p->state == PROC_RUNNING)
            state = "running";
        else if (p->state == PROC_ZOMBIE)
            state = "zombie";

        cb(p->pid, p->name, state, user);
    }
}

uint32_t scheduler_process_count(void)
{
    return proc_count;
}

process_t *scheduler_current(void)
{
    return process_current();
}
