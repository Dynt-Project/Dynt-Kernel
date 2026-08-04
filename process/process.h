#ifndef PROCESS_PROCESS_H
#define PROCESS_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "../arch/x86_64/inter/isr.h"

#define PROCESS_NAME_MAX 32

// user stack sits at a fixed high virtual address in every process
#define PROCESS_USER_STACK_SIZE (256ULL * 1024ULL)
#define PROCESS_USER_STACK_TOP 0x4000000000ULL

// anonymous user mappings (mlibc malloc arena etc.) grow up from here,
// well below the user stack and above the PIE base range
#define PROCESS_MMAP_START 0x1000000000ULL

typedef enum process_state
{
    PROC_READY,
    PROC_RUNNING,
    PROC_ZOMBIE,
} process_state_t;

typedef struct process
{
    uint64_t pid;
    char name[PROCESS_NAME_MAX];
    process_state_t state;
    uint64_t cr3;          // page table root (address space)
    uint64_t ticks;        // ticks consumed on the current slice
    uint64_t fs_base;      // FS segment base (mlibc TCB), restored on switch
    uint64_t mmap_cursor;  // bump cursor for anonymous user mappings
    // full saved user context: general registers + the iretq frame
    // (rip/cs/rflags/user_rsp/ss). the scheduler copies the interrupt
    // frame here on preempt and back into the frame on switch-in
    registers_t ctx;
    struct process *next;
} process_t;

#ifdef __cplusplus
extern "C" {
#endif

void process_init(void);

// allocates a pcb + fresh address space
process_t *process_create(const char *name);

// loads a program from the FAT32/VFS filesystem into the process,
// choosing a random PIE base and mapping a user stack. returns false on
// any error (leaves the process reusable)
bool process_load_elf(process_t *proc, const char *path);

// sets the initial ring3 registers (full context, so a preempted
// process can be restored from ctx even on its very first run)
void process_setup(process_t *proc, uint64_t entry, uint64_t stack_top);

// frees the address space and pcb
void process_destroy(process_t *proc);

process_t *process_current(void);
void process_set_current(process_t *proc);

uint64_t process_next_pid(void);

#ifdef __cplusplus
}
#endif

#endif // PROCESS_PROCESS_H
