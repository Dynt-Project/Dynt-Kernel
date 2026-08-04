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
#include "../arch/x86_64/syscall/usermode.h"
#include "../arch/x86_64/cpu/cpu.h"
#include "../arch/x86_64/cpu/control_regs.h"
#include "../arch/x86_64/boot/common/bootinf.h"
#include "../mem/mm/kheap.h"
#include "../mem/mm/pmm.h"
#include "../mem/mm/paging.h"
#include "../driver/stacks/video/video_stack.h"
#include "../driver/stacks/storage/block.h"
#include "../driver/stacks/storage/partition.h"
#include "../driver/buildin/video/vga/vga.h"
#include "../driver/buildin/input/ps2/ps2_keyboard.h"
#include "../driver/buildin/input/ps2/ps2_mouse.h"
#include "../driver/buildin/timer/pit.h"
#include "../driver/buildin/storage/ide/ide.h"
#include "../driver/buildin/storage/ahci/ahci.h"
#include "../fs/vfs.h"
#include "../fs/fat32.h"
#include "../fs/ext2.h"
#include "../scheduler/scheduler.h"
#include "../arch/x86_64/smp/smp.h"
#include "../process/process.h"
#include "debug.h"
#include "startup.h"

#include <stdint.h>

alignas(16) static uint8_t kernel_stack[16384];
alignas(16) static uint8_t syscall_stack[16384];

static void launch_userspace(void)
{
    // create + load the init process from the FAT32 partition
    process_t *init = process_create("init");

    if (!init)
    {
        debug_printf("[boot] no pcb for init\n");
        return;
    }

    if (!process_load_elf(init, "/init"))
    {
        debug_printf("[boot] no /init on disk, staying in kernel\n");
        process_destroy(init);
        return;
    }

    scheduler_enqueue(init);
    debug_printf("[boot] entering ring3 userspace (pid=%lu)\n", init->pid);

    init->ticks = 0;
    init->state = PROC_RUNNING;
    process_set_current(init);

    write_cr3(init->cr3);
    debug_printf("[boot] ctx rip=0x%lx cs=0x%lx rflags=0x%lx rsp=0x%lx ss=0x%lx\n",
                 init->ctx.rip, init->ctx.cs, init->ctx.rflags,
                 init->ctx.user_rsp, init->ctx.ss);
    usermode_resume_full(&init->ctx);
}

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
    uintptr_t syscall_stack_top = (uintptr_t)&syscall_stack[sizeof(syscall_stack)];

    // this sets up X86_64 Architecture
    gdt_init();
    debug_printf("[boot] gdt ok\n");

    tss_set_kernel_stack((uint64_t)kernel_stack_top);
    idt_init();
    debug_printf("[boot] idt ok\n");

    smp_init();
    debug_printf("[boot] smp cpus: %u\n", (unsigned)smp_cpu_count());

    pic_remap(0x20, 0x28);
    debug_printf("[boot] pic ok\n");

    // ps/2 input drivers: keyboard on irq1, mouse on irq12
    bool ps2_kbd = ps2_keyboard_init();
    bool ps2_mse = ps2_mouse_init();

    debug_printf("[boot] ps/2 keyboard: %s\n", ps2_kbd ? "ok" : "fail");
    debug_printf("[boot] ps/2 mouse: %s\n", ps2_mse ? "ok" : "fail");

    pmm_init();
    paging_init();
    kheap_init();
    debug_printf("[boot] early heap ok (%u MiB usable, %u MiB free)\n",
                 (unsigned)(pmm_total_memory() >> 20),
                 (unsigned)((pmm_free_frames() * 4) >> 10));

    scheduler_init();
    debug_printf("[boot] bore scheduler ok\n");

    block_stack_init();
    vfs_init();
    fat32_register();
    ext2_register();
    debug_printf("[boot] storage stacks ok\n");

    uint32_t ahci_drives = ahci_init();
    debug_printf("[boot] ahci sata ports: %u\n", (unsigned)ahci_drives);

    uint32_t ide_drives = ide_init();
    debug_printf("[boot] ide/ata drives: %u\n", (unsigned)ide_drives);

    uint32_t partitions = partition_scan_all();
    debug_printf("[boot] partitions: %u\n", (unsigned)partitions);

    uint32_t mounts = vfs_mount_all();
    debug_printf("[boot] vfs mounts: %u\n", (unsigned)mounts);

    // syscall handlers run on their own stack so that an IRQ frame
    // (pushed at TSS rsp0) can never clobber the syscall register save
    percpu_init((uint64_t)syscall_stack_top);
    syscall_init();
    debug_printf("[boot] syscalls ok\n");

    // 100 Hz PIT timer -> preemptive scheduling
    pit_init(100);
    debug_printf("[boot] pit timer ok\n");

    sti();
    debug_printf("[boot] interrupts enabled, kernel ready\n");

    /* Launch userspace init from the FAT32 partition */
    launch_userspace();
}