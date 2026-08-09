#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

#include <stdint.h>
#include "../process/process.h"
#include "../arch/x86_64/inter/isr.h"
#include "../arch/x86_64/cpu/percpu.h"

#ifdef __cplusplus
extern "C" {
#endif

void scheduler_init(void);

// adds a ready process to the least-loaded cpu's runqueue
void scheduler_enqueue(process_t *proc);

// adds a ready process to a specific cpu's runqueue (SMP bring-up)
void scheduler_enqueue_on(process_t *proc, uint32_t cpu);

// index of the currently least-loaded cpu
uint32_t scheduler_least_loaded_cpu(void);

// timer handler (PIT on the BSP, LAPIC timer on the APs): preemptive
// round-robin on the LOCAL cpu.  syscalls (cs == 0x08) are never
// preempted; an idle cpu (no current process) picks up freshly enqueued
// work from its own queue.
void scheduler_timer_tick(registers_t *regs);

// called from SYS_EXIT: kills the current process and resumes the next.
// if the process has a parent it becomes a zombie (waitable) instead of
// being destroyed
[[noreturn]] void scheduler_exit_current(int64_t status);

// requests termination of the process with the given pid (SYS_KILL):
// sets the terminate flag, the process dies at the next switch/blocking
// syscall boundary and turns into a zombie for its parent
bool scheduler_terminate(uint64_t pid);

// true if the calling process has been asked to die; blocking syscall
// loops call this so a killed process wakes up immediately
bool scheduler_terminate_requested(void);

// parks the calling cpu (idle loop); the local timer wakes it up and
// starts any process enqueued on its runqueue
[[noreturn]] void scheduler_idle_cpu(void);

typedef void (*sched_list_cb)(uint64_t pid, const char *name,
                              const char *state, uint32_t cpu, void *user);

// walks every cpu's run queue, calling cb for each live process
void scheduler_list(sched_list_cb cb, void *user);

uint32_t scheduler_process_count(void);
process_t *scheduler_current(void);

// monotonic tick counter, incremented on every timer irq (100 Hz)
uint64_t scheduler_ticks(void);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_SCHEDULER_H
