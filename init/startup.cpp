// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

//  this is the entery for the os (the startup function)
//  this is what iniciats the kernel and sets up the architecture
//  every time a new driver is added to the kernel or function
//  you need to add its inicialization function here, 
//  this is the first function that runs after the kernel is loaded into memory 
//  and before the main loop starts

#include "../arch/x86_64/inter/gdt.h"
#include "../arch/x86_64/inter/idt.h"
#include "../arch/x86_64/inter/pic.h"
#include "../arch/x86_64/cpu/percpu.h"
#include "../arch/x86_64/syscall/syscall.h"
#include "../arch/x86_64/cpu/cpu.h"
#include "startup.h"

#include <stdint.h>

alignas(16) static uint8_t kernel_stack[16384];

extern "C" void startup() {
    uintptr_t kernel_stack_top = (uintptr_t)&kernel_stack[sizeof(kernel_stack)];

    // this sets up X86_64 Architecture
    gdt_init();
    tss_set_kernel_stack((uint64_t)kernel_stack_top);
    idt_init();
    pic_remap(0x20, 0x28);
    percpu_init((uint64_t)kernel_stack_top);
    syscall_init();
    sti();
}
