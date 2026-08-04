// Preemptive round-robin scheduler with one runqueue per cpu.
//
// The timer IRQ fires on the per-CPU interrupt stack.  In user mode the
// frame carries the whole user context (GP registers + iretq frame), so
// switching is: save the frame into the current pcb, pick the next ready
// process, load its cr3 and copy its saved context back into the frame
// before iretq.  This preserves every register across a preempt.
//
// SMP: every cpu has its own queue and its own current process.  A global
// spinlock guards all queue state, because a timer IRQ on cpu A can race
// with an enqueue done by a syscall on cpu B.  A timer tick only ever
// preempts the LOCAL cpu.  SYS_EXEC enqueues onto the least-loaded cpu,
// so freshly spawned programs spread across the cores.

#include "scheduler.h"

#include "../arch/x86_64/cpu/control_regs.h"
#include "../arch/x86_64/cpu/cpu.h"
#include "../arch/x86_64/cpu/msr.h"
#include "../arch/x86_64/cpu/percpu.h"
#include "../arch/x86_64/cpu/spinlock.h"
#include "../arch/x86_64/smp/smp.h"
#include "../arch/x86_64/syscall/usermode.h"
#include "../mem/lib/memory.h"

#define SCHED_QUANTUM 5  // ticks before yielding (100 Hz -> 50 ms)

// each busy tick adds this to the (decaying) load signal
#define LOAD_UNIT (1ULL << 12)

typedef struct cpu_rq
{
    process_t *head;  // ready processes on this cpu
    uint32_t count;
    uint64_t load;  // decaying busy signal -> least-loaded selection
    uint64_t busy_ticks;
    uint64_t idle_ticks;
} cpu_rq_t;

static cpu_rq_t rq[PERCPU_MAX_CPUS];
static spinlock_t sched_lock;
static uint64_t uptime_ticks;

uint64_t scheduler_ticks(void)
{
    return uptime_ticks;
}

void scheduler_init(void)
{
    spinlock_init(&sched_lock);
    uptime_ticks = 0;

    for (uint32_t i = 0; i < PERCPU_MAX_CPUS; i++)
    {
        rq[i].head = 0;
        rq[i].count = 0;
        rq[i].load = 0;
        rq[i].busy_ticks = 0;
        rq[i].idle_ticks = 0;
    }
}

static uint32_t cpu_count(void)
{
    uint32_t n = smp_cpu_count();

    return n ? n : 1;
}

static void push_front(uint32_t cpu, process_t *proc)
{
    proc->next = rq[cpu].head;
    rq[cpu].head = proc;
    rq[cpu].count++;
}

// caller must hold sched_lock
static uint32_t least_loaded_locked(void)
{
    uint32_t n = cpu_count();
    uint32_t best = 0;

    for (uint32_t i = 1; i < n; i++)
        if (rq[i].load < rq[best].load)
            best = i;

    return best;
}

void scheduler_enqueue_on(process_t *proc, uint32_t cpu)
{
    if (!proc)
        return;

    if (cpu >= PERCPU_MAX_CPUS)
        cpu = 0;

    uint64_t flags = spinlock_acquire_irq(&sched_lock);
    proc->state = PROC_READY;
    proc->cpu = cpu;
    proc->ticks = 0;
    push_front(cpu, proc);
    spinlock_release_irq(&sched_lock, flags);
}

uint32_t scheduler_least_loaded_cpu(void)
{
    uint64_t flags = spinlock_acquire_irq(&sched_lock);
    uint32_t cpu = least_loaded_locked();
    spinlock_release_irq(&sched_lock, flags);

    return cpu;
}

void scheduler_enqueue(process_t *proc)
{
    if (!proc)
        return;

    uint64_t flags = spinlock_acquire_irq(&sched_lock);
    proc->state = PROC_READY;
    proc->cpu = least_loaded_locked();
    proc->ticks = 0;
    push_front(proc->cpu, proc);
    spinlock_release_irq(&sched_lock, flags);
}

static void unlink(uint32_t cpu, process_t *proc)
{
    process_t **link = &rq[cpu].head;

    while (*link)
    {
        if (*link == proc)
        {
            *link = proc->next;
            rq[cpu].count--;
            return;
        }
        link = &(*link)->next;
    }
}

static process_t *pick_next(uint32_t cpu, process_t *skip)
{
    process_t *start = rq[cpu].head;
    process_t *p = start;

    if (!start)
        return 0;

    do
    {
        if (p->state == PROC_READY && p != skip)
            return p;
        p = p->next ? p->next : start;
    } while (p != start);

    return 0;
}

void scheduler_timer_tick(registers_t *regs)
{
    uint32_t cpu = percpu_current()->cpu_index;

    // IRQs are already masked inside an interrupt handler, so the plain
    // (non-IRQ-safe) spinlock is fine; cross-cpu enqueues take the same
    // lock with their local IRQs masked.
    spinlock_acquire(&sched_lock);
    uptime_ticks++;

    process_t *cur = (process_t *)percpu_current()->current;

    if (cur && cur->state == PROC_RUNNING)
    {
        rq[cpu].busy_ticks++;
        rq[cpu].load = (rq[cpu].load >> 1) + LOAD_UNIT;

        cur->ticks++;
        cur->ctx = *regs;

        if (cur->ticks < SCHED_QUANTUM)
        {
            spinlock_release(&sched_lock);
            return;
        }
    }
    else
    {
        rq[cpu].idle_ticks++;
        rq[cpu].load >>= 1;
    }

    // A syscall or kernel handler of a running process must stay atomic;
    // only preempt user mode (cs == 0x23).  An idle cpu has no current
    // process and its frame is a kernel (hlt) frame, which we may replace
    // with a user frame to start freshly enqueued work.
    if (regs->cs != 0x23 && cur)
    {
        spinlock_release(&sched_lock);
        return;
    }

    process_t *next = pick_next(cpu, cur);

    if (!next || next == cur)
    {
        spinlock_release(&sched_lock);
        return;
    }

    if (cur)
    {
        cur->ticks = 0;
        cur->state = PROC_READY;
    }

    next->ticks = 0;
    next->state = PROC_RUNNING;
    percpu_current()->current = next;

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);

    *regs = next->ctx;
    spinlock_release(&sched_lock);
}

[[noreturn]] void scheduler_exit_current(void)
{
    uint32_t cpu = percpu_current()->cpu_index;

    // SYS_EXIT runs with IRQs enabled; mask them so our own timer cannot
    // touch the queue while we tear the process down.
    uint64_t flags = spinlock_acquire_irq(&sched_lock);

    process_t *cur = (process_t *)percpu_current()->current;

    if (cur)
    {
        unlink(cpu, cur);
        percpu_current()->current = 0;
        process_destroy(cur);
    }

    process_t *next = pick_next(cpu, 0);

    if (!next)
    {
        spinlock_release_irq(&sched_lock, flags);
        scheduler_idle_cpu();
    }

    next->ticks = 0;
    next->state = PROC_RUNNING;
    percpu_current()->current = next;

    spinlock_release_irq(&sched_lock, flags);

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);
    usermode_resume_full(&next->ctx);
}

[[noreturn]] void scheduler_idle_cpu(void)
{
    for (;;)
    {
        sti();
        hlt();
    }
}

void scheduler_list(sched_list_cb cb, void *user)
{
    if (!cb)
        return;

    uint64_t flags = spinlock_acquire_irq(&sched_lock);

    for (uint32_t cpu = 0; cpu < PERCPU_MAX_CPUS; cpu++)
    {
        for (process_t *p = rq[cpu].head; p; p = p->next)
        {
            const char *state = "ready";

            if (p->state == PROC_RUNNING)
                state = "running";
            else if (p->state == PROC_ZOMBIE)
                state = "zombie";

            cb(p->pid, p->name, state, user);
        }
    }

    spinlock_release_irq(&sched_lock, flags);
}

uint32_t scheduler_process_count(void)
{
    uint32_t total = 0;

    uint64_t flags = spinlock_acquire_irq(&sched_lock);
    for (uint32_t i = 0; i < PERCPU_MAX_CPUS; i++)
        total += rq[i].count;
    spinlock_release_irq(&sched_lock, flags);

    return total;
}

process_t *scheduler_current(void)
{
    return process_current();
}
