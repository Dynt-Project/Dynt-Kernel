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
#include "../arch/x86_64/io/io.h"
#include "../arch/x86_64/cpu/spinlock.h"
#include "../arch/x86_64/smp/smp.h"
#include "../arch/x86_64/smp/lapic.h"
#include "../arch/x86_64/inter/pic.h"
#include "../arch/x86_64/syscall/usermode.h"
#include "../init/debug.h"
#include "../mem/lib/memory.h"

#define SCHED_QUANTUM 5  // ticks before yielding (100 Hz -> 50 ms)

// each busy tick adds this to the (decaying) load signal
#define LOAD_UNIT (1ULL << 12)

// The syscall stub pair of swapgs assumes the classic convention: user
// mode runs with GS_BASE=0 and KERNEL_GS_BASE=percpu, the syscall entry
// swapgs exchanges them (GS_BASE=percpu), the exit swapgs restores them.
// Everything else (timer IRQ, blocked resume, switch to a fresh process)
// never swaps, so a resume that changes the mode must re-apply the
// convention of its target or the next syscall reads its kernel stack
// pointer from linear address 0x8.  These helpers put the GS MSRs back
// into the state the target expects (percpu_current() detects either).
static inline void swapgs(void) { __asm__ volatile ("swapgs" ::: "memory"); }

static inline bool gs_base_is_percpu(void)
{
    uint64_t gs = rdmsr(MSR_IA32_GS_BASE);
    return gs >= (uint64_t)&g_percpu[0] &&
           gs < (uint64_t)&g_percpu[PERCPU_MAX_CPUS];
}

static inline void gs_to_user(void)
{
    if (gs_base_is_percpu())
        swapgs();
}

static inline void gs_to_kernel(void)
{
    if (!gs_base_is_percpu())
        swapgs();
}

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

// caller must hold sched_lock. prefer the cpu with the fewest ready
// processes so freshly forked children spread over the (idle) APs; a
// tie-breaks on the decaying load signal.
static uint32_t least_loaded_locked(void)
{
    uint32_t n = cpu_count();
    uint32_t best = 0;

    for (uint32_t i = 1; i < n; i++)
    {
        if (rq[i].count < rq[best].count ||
            (rq[i].count == rq[best].count && rq[i].load < rq[best].load))
            best = i;
    }

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

// caller must hold sched_lock
static process_t *find_pid_locked(uint64_t pid)
{
    for (uint32_t cpu = 0; cpu < PERCPU_MAX_CPUS; cpu++)
        for (process_t *p = rq[cpu].head; p; p = p->next)
            if (p->pid == pid)
                return p;
    return 0;
}

// removes `cur` from its runqueue. if it has a parent it is kept alive
// as a zombie (exit_status recorded) so the parent's waitpid can reap
// it; orphans are destroyed outright. caller must hold sched_lock.
static void kill_cur_locked(process_t *cur, uint32_t cpu)
{
    if (!cur)
        return;

    unlink(cpu, cur);
    percpu_current()->current = 0;

    if (cur->parent)
    {
        cur->state = PROC_ZOMBIE;
        cur->terminate = false;
    }
    else
    {
        process_destroy(cur);
    }
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
    bool saved_ctx = false;

    if (cur && cur->state == PROC_RUNNING)
    {
        rq[cpu].busy_ticks++;
        rq[cpu].load = (rq[cpu].load >> 1) + LOAD_UNIT;

        cur->ticks++;
        cur->ctx = *regs;
        saved_ctx = true;

        // a blocked process parks in a kernel-mode hlt loop on its own
        // kernel stack; the interrupt frame it got preempted on holds no
        // rsp/ss (kernel->kernel iretq does not pop them), so capture
        // the live stack pointer for the resume path.
        if (cur->blocked)
            cur->saved_kernel_rsp = (uint64_t)&regs->user_rsp;

        if (cur->terminate)
        {
            // SYS_KILL target: die now (only when the frame is user
            // mode, otherwise we are inside a kernel syscall handler)
            if (regs->cs == 0x23)
            {
                kill_cur_locked(cur, cpu);
                cur = 0;
            }
            else
            {
                spinlock_release(&sched_lock);
                return;
            }
        }
        else if (cur->ticks < SCHED_QUANTUM && !cur->blocked)
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
    // only preempt user mode (cs == 0x23) or a process parked in a
    // blocking syscall (blocked, waiting in a hlt loop on its own kernel
    // stack, so its context can be suspended and resumed safely).  An
    // idle cpu has no current process and its frame is a kernel (hlt)
    // frame, which we may replace with a user frame to start freshly
    // enqueued work.
    if (regs->cs != 0x23 && cur && !cur->blocked)
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
    percpu_current()->kernel_rsp = next->kernel_stack_top;

#if SCHEDULER_DEBUG
    debug_printf("[sw] cpu %u %lu->%lu irip=0x%lx ics=0x%lx sav=%d cblk=%d nst=0x%x nblk=%d\n",
                 cpu, cur ? cur->pid : 0, next->pid, regs->rip, regs->cs,
                 saved_ctx, cur ? cur->blocked : 0,
                 next->state, next->blocked);
#endif

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);

    // a blocked process must be resumed back into its kernel-mode hlt
    // loop with the interrupted stack pointer restored (iretq to a
    // kernel target would leave RSP on the preempted cpu's stack).
    if (next->blocked)
    {
        uint64_t krsp = next->saved_kernel_rsp;
#if SCHEDULER_DEBUG
        debug_printf("[wake] cpu %u resume pid %lu krsp=0x%lx ctx.rip=0x%lx cs=0x%lx fl=0x%lx ss=0x%lx\n",
                     cpu, next->pid, krsp, next->ctx.rip, next->ctx.cs,
                     next->ctx.rflags, next->ctx.ss);
#endif
        // this resume never returns, so the caller's EOI (pic_send_eoi
        // in irq_handler / lapic_eoi in lapic_handler) would never run
        // and the clock on this cpu would stop; issue both EOIs here
        // (the BSP clocks on the PIT, the APs on the LAPIC timer, the
        // other write is a harmless no-op)
        lapic_eoi();
        pic_send_eoi(0);
        spinlock_release(&sched_lock);
        gs_to_kernel();
        kernel_resume(&next->ctx, krsp);
    }

    gs_to_user();
#if SCHEDULER_DEBUG
    debug_printf("[res] cpu %u pid %lu rip=0x%lx rsp=0x%lx cs=0x%lx\n", cpu,
                 next->pid, next->ctx.rip, next->ctx.user_rsp, next->ctx.cs);
#endif
    *regs = next->ctx;
    spinlock_release(&sched_lock);
}

[[noreturn]] void scheduler_exit_current(int64_t status)
{
    uint32_t cpu = percpu_current()->cpu_index;

    // SYS_EXIT runs with IRQs enabled; mask them so our own timer cannot
    // touch the queue while we tear the process down.
    uint64_t flags = spinlock_acquire_irq(&sched_lock);

    process_t *cur = (process_t *)percpu_current()->current;

    if (cur)
    {
        // `status` is already in POSIX wait format (see scheduler_exit
        // and scheduler_terminate); waitpid just hands it to the parent
        cur->exit_status = (int32_t)status;
        kill_cur_locked(cur, cpu);
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
    percpu_current()->kernel_rsp = next->kernel_stack_top;

    spinlock_release_irq(&sched_lock, flags);

    write_cr3(next->cr3);
    wrmsr(MSR_IA32_FS_BASE, next->fs_base);

    if (next->blocked)
    {
#if SCHEDULER_DEBUG
        debug_printf("[wake] exit-path pid %lu krsp=0x%lx ctx.rip=0x%lx cs=0x%lx fl=0x%lx ss=0x%lx\n",
                     next->pid, next->saved_kernel_rsp, next->ctx.rip,
                     next->ctx.cs, next->ctx.rflags, next->ctx.ss);
#endif
        gs_to_kernel();
        kernel_resume(&next->ctx, next->saved_kernel_rsp);
    }
    gs_to_user();
#if SCHEDULER_DEBUG
    debug_printf("[res] cpu %u pid %lu rip=0x%lx\n", percpu_current()->cpu_index,
                 next->pid, next->ctx.rip);
#endif
    usermode_resume_full(&next->ctx);
}

bool scheduler_terminate(uint64_t pid)
{
    uint64_t flags = spinlock_acquire_irq(&sched_lock);
    process_t *p = find_pid_locked(pid);

    if (p && p->state != PROC_ZOMBIE)
    {
        p->terminate = true;
        p->exit_status = 2;  // SIGINT semantics for the wait status
        spinlock_release_irq(&sched_lock, flags);
        return true;
    }

    spinlock_release_irq(&sched_lock, flags);
    return false;
}

bool scheduler_terminate_requested(void)
{
    process_t *cur = process_current();

    return cur && cur->terminate;
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

            cb(p->pid, p->name, state, cpu, user);
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
