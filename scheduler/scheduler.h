#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

#include <stdint.h>
#include "../process/process.h"
#include "../arch/x86_64/inter/isr.h"

#ifdef __cplusplus
extern "C" {
#endif

void scheduler_init(void);

// adds a ready process to the run queue
void scheduler_enqueue(process_t *proc);

// irq0 timer handler: preemptive round-robin between user processes.
// syscalls (cs == 0x08) are never preempted
void scheduler_timer_tick(registers_t *regs);

// called from SYS_EXIT: kills the current process and resumes the next
[[noreturn]] void scheduler_exit_current(void);

typedef void (*sched_list_cb)(uint64_t pid, const char *name,
                              const char *state, void *user);

// walks the run queue, calling cb for every live process
void scheduler_list(sched_list_cb cb, void *user);

uint32_t scheduler_process_count(void);
process_t *scheduler_current(void);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_SCHEDULER_H
