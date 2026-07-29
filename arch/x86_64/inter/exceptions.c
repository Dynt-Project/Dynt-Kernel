// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

//   The C-side counterpart of isr.asm: isr_handler() runs for every CPU
//   exception (vectors 0-31), irq_handler() runs for every remapped
//   hardware IRQ (32-47). Keeping these here (rather than inline in
//   idt.cpp) means idt.cpp only ever deals with "how do I register a
//   vector", not "what do I do when it fires".

#include "isr.h"
#include "pic.h"
#include "../panic/panic.h"

static const char *exception_name(uint8_t vector) {
    static const char *names[32] = {
        "Divide-by-zero",              "Debug",
        "Non-Maskable Interrupt",      "Breakpoint",
        "Overflow",                    "Bound Range Exceeded",
        "Invalid Opcode",              "Device Not Available",
        "Double Fault",                "Coprocessor Segment Overrun",
        "Invalid TSS",                 "Segment Not Present",
        "Stack-Segment Fault",         "General Protection Fault",
        "Page Fault",                  "Reserved",
        "x87 Floating-Point Error",    "Alignment Check",
        "Machine Check",               "SIMD Floating-Point Exception",
        "Virtualization Exception",    "Control Protection Exception",
        "Reserved",                    "Reserved",
        "Reserved",                    "Reserved",
        "Reserved",                    "Reserved",
        "Hypervisor Injection",        "VMM Communication Exception",
        "Security Exception",          "Reserved",
    };

    if (vector < 32)
        return names[vector];
    
    return "Unknown exception";
}

extern "C" void isr_handler(registers_t *regs) {
    panic(exception_name(regs->int_no), regs);
}

static isr_handler_t irq_handlers[16] = { 0 };

void irq_install_handler(int irq_num, isr_handler_t handler) {
    if (irq_num < 0 || irq_num >= 16)
        return;

    irq_handlers[irq_num] = handler;
}

void irq_uninstall_handler(int irq_num) {
    if (irq_num < 0 || irq_num >= 16)
        return;

    irq_handlers[irq_num] = 0;
}

extern "C" void irq_handler(registers_t *regs) {
    int irq_num = (int)(regs->int_no - 32);

    if (irq_num >= 0 && irq_num < 16 && irq_handlers[irq_num])
        irq_handlers[irq_num](regs);

    pic_send_eoi((uint8_t)irq_num);
}
