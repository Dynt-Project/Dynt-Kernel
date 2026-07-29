// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

#include "syscall.h"
#include "../cpu/msr.h"
#include "../inter/gdt.h"

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

#define EFER_SCE (1ULL << 0)

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    uint64_t star = ((uint64_t)(GDT_SEL_UDATA - 8) << 48) | ((uint64_t)GDT_SEL_KCODE << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, (1ULL << 9));
}