// Written by [@saphhic](https://github.com/saphhic)
// Date: 26 July 2026

//   SYSCALL does NOT switch stacks the way an interrupt/TSS.rsp0 does —
//   it leaves RSP exactly as the user program had it. Running kernel code
//   on a user-controlled stack is not something we want to do even for
//   one instruction, so well: keep a small per-CPU
//   struct, point GS_BASE/KERNEL_GS_BASE MSRs at it, and use `swapgs` +
//   %gs-relative loads in the syscall entry stub to get onto a
//   kernel stack.
//
//   SMP: every CPU (BSP + APs) gets its own percpu_t.  The syscall stub
//   only relies on the first three fields (self at 0, kernel_rsp at 8,
//   user_rsp at 16) — the offsets must not move, so new fields go after
//   them.

#ifndef ARCH_X86_64_PERCPU_H
#define ARCH_X86_64_PERCPU_H

#include <stdint.h>

struct __attribute__((packed)) percpu_t {
    uint64_t self;         // 0x00: points to this struct
    uint64_t kernel_rsp;   // 0x08: syscall kernel stack (swapgs + gs:8)
    uint64_t user_rsp;     // 0x10: saved user rsp at syscall entry
    uint32_t cpu_index;    // 0x18: which cpu this is
    uint32_t lapic_id;     // 0x1c: local APIC id
    void *current;         // 0x20: currently running process on this cpu
    uint64_t tss_rsp0;     // 0x28: interrupt stack (TSS rsp0) for this cpu
    uint64_t syscall_stack_top;  // 0x30: this cpu's syscall stack
};

#ifdef __cplusplus
extern "C" {
#endif

// how many per-cpu slots we reserve (must match the cpu count cap)
#define PERCPU_MAX_CPUS 16

extern percpu_t g_percpu[PERCPU_MAX_CPUS];

// sets up the BSP (cpu 0)
void percpu_init(uint64_t kernel_stack_top);

// fills the slot of an application processor and points this cpu's GS
// MSRs at it; called from the AP boot path
void percpu_init_cpu(uint32_t index, uint8_t lapic_id,
                     uint64_t interrupt_stack_top,
                     uint64_t syscall_stack_top);

// returns the percpu struct of the currently executing cpu
percpu_t *percpu_current(void);

// index of the currently executing cpu
uint32_t percpu_index(void);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_PERCPU_H
