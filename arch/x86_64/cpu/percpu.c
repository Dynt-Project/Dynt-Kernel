// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

// program for [percpu.h](<DyntKernel/arch/x86_64/percpu.h>)

#include "percpu.h"
#include "msr.h"

#define MSR_GS_BASE 0xC0000101
#define MSR_KERNEL_GS_BASE 0xC0000102

percpu_t g_percpu0;

void percpu_init(uint64_t kernel_stack_top) {
    g_percpu0.self = (uint64_t)&g_percpu0;
    g_percpu0.kernel_rsp = kernel_stack_top;
    g_percpu0.user_rsp = 0;

    wrmsr(MSR_GS_BASE, (uint64_t)&g_percpu0);
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)&g_percpu0);
}