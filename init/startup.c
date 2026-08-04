//
// Created by epaxgaming on 31.07.26.
//
// modified from the original startup
//
// this is the entry for the os (the startup function)
// this is what initiates the kernel and sets up the architecture
// every time a new driver is added to the kernel or function
// you need to add its initialization function here,
// this is the first function that runs after the kernel is loaded into memory
// and before the main loop starts

#include "../arch/x86_64/inter/gdt.h"
#include "../arch/x86_64/inter/idt.h"
#include "../arch/x86_64/inter/pic.h"
#include "../arch/x86_64/cpu/percpu.h"
#include "../arch/x86_64/syscall/syscall.h"
#include "../arch/x86_64/cpu/cpu.h"
#include "../arch/x86_64/boot/common/bootinf.h"
#include "../driver/stacks/video/video_stack.h"
#include "../driver/buildin/video/vga/vga.h"
#include "../driver/buildin/input/ps2/ps2_keyboard.h"
#include "../driver/buildin/input/ps2/ps2_mouse.h"
#include "debug.h"
#include "startup.h"

#include <stdint.h>

alignas(16) static uint8_t kernel_stack[16384];

void startup() {
    // debug init also sets up serial, the video stack and the vga driver
    debug_init();

    debug_printf("[boot] Dynt-Kernel starting up\n");
    debug_printf("[boot] video driver: %ux%u\n",
                 video_get_width(),
                 video_get_height());
    debug_printf("[boot] memory map entries: %u\n",
                 (unsigned)g_bootinfo.memory_map_entries);

    uintptr_t kernel_stack_top = (uintptr_t)&kernel_stack[sizeof(kernel_stack)];

    // this sets up X86_64 Architecture
    gdt_init();
    debug_printf("[boot] gdt ok\n");

    tss_set_kernel_stack((uint64_t)kernel_stack_top);
    idt_init();
    debug_printf("[boot] idt ok\n");

    pic_remap(0x20, 0x28);
    debug_printf("[boot] pic ok\n");

    // ps/2 input drivers: keyboard on irq1, mouse on irq12
    bool ps2_kbd = ps2_keyboard_init();
    bool ps2_mse = ps2_mouse_init();

    debug_printf("[boot] ps/2 keyboard: %s\n", ps2_kbd ? "ok" : "fail");
    debug_printf("[boot] ps/2 mouse: %s\n", ps2_mse ? "ok" : "fail");

    percpu_init((uint64_t)kernel_stack_top);
    syscall_init();
    debug_printf("[boot] syscalls ok\n");

    sti();
    debug_printf("[boot] interrupts enabled, kernel ready\n");

}
