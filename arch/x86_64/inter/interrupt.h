// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

// eseier functions to use interrupts like sti or cli

#ifndef ARCH_X86_64_INTERRUPT_H
#define ARCH_X86_64_INTERRUPT_H

static inline void enable_interrupts(void) {
    __asm__ volatile("sti");
}

static inline void disable_interrupts(void) {
    __asm__ volatile("cli");
}

#endif // ARCH_X86_64_INTERRUPT_H