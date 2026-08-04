// Written by [@saphhic](https://github.com/saphhic)
// Date: 26 July 2026

// program for [percpu.h](<DyntKernel/arch/x86_64/percpu.h>)

#include "percpu.h"
#include "msr.h"

#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

percpu_t g_percpu[PERCPU_MAX_CPUS];

void percpu_init(uint64_t kernel_stack_top) {
    for (uint32_t i = 0; i < PERCPU_MAX_CPUS; i++) {
        g_percpu[i].self = (uint64_t)&g_percpu[i];
        g_percpu[i].cpu_index = i;
        g_percpu[i].kernel_rsp = kernel_stack_top;
        g_percpu[i].syscall_stack_top = kernel_stack_top;
        g_percpu[i].tss_rsp0 = kernel_stack_top;
        g_percpu[i].current = 0;
    }

    g_percpu[0].lapic_id = 0;

    wrmsr(MSR_GS_BASE, (uint64_t)&g_percpu[0]);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&g_percpu[0]);
}

// AP bring-up: initializes the cpu's percpu slot and installs it in both
// GS MSRs, so percpu_current()/percpu_index() work on this cpu from here on
void percpu_init_cpu(uint32_t index, uint8_t lapic_id,
                     uint64_t interrupt_stack_top,
                     uint64_t syscall_stack_top)
{
    if (index >= PERCPU_MAX_CPUS)
        return;

    percpu_t *p = &g_percpu[index];

    p->self = (uint64_t)p;
    p->cpu_index = index;
    p->lapic_id = lapic_id;
    p->kernel_rsp = syscall_stack_top;
    p->syscall_stack_top = syscall_stack_top;
    p->tss_rsp0 = interrupt_stack_top;
    p->current = 0;

    wrmsr(MSR_GS_BASE, (uint64_t)p);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)p);
}

// In kernel mode the percpu struct is reachable through exactly one of the
// two GS MSRs:
//   - interrupt from user mode: no swapgs happened, so KERNEL_GS_BASE
//     still holds the percpu pointer (GS_BASE is the user's value)
//   - syscall handler / interrupt during a syscall: the entry stub did
//     swapgs, so GS_BASE holds the percpu pointer (KERNEL_GS_BASE is the
//     user's saved value)
// Pick whichever of the two points inside the g_percpu array.
percpu_t *percpu_current(void)
{
    uint64_t gs = rdmsr(MSR_GS_BASE);

    if (gs >= (uint64_t)&g_percpu[0] &&
        gs < (uint64_t)&g_percpu[PERCPU_MAX_CPUS])
        return (percpu_t *)gs;

    return (percpu_t *)rdmsr(MSR_KERNEL_GS_BASE);
}

uint32_t percpu_index(void)
{
    return percpu_current()->cpu_index;
}
