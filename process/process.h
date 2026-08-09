#ifndef PROCESS_PROCESS_H
#define PROCESS_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "../arch/x86_64/inter/isr.h"
#include "../arch/x86_64/syscall/syscall.h"

#define PROCESS_NAME_MAX 32

// user stack sits at a fixed high virtual address in every process
#define PROCESS_USER_STACK_SIZE (256ULL * 1024ULL)
#define PROCESS_USER_STACK_TOP 0x4000000000ULL

// anonymous user mappings (mlibc malloc arena etc.) grow up from here,
// well below the user stack and above the PIE base range
#define PROCESS_MMAP_START 0x1000000000ULL

// open file table: Linux-style file descriptors. fds 0-2 are the tty,
// regular files start at fd 3.
#define PROCESS_MAX_FDS 16
#define PROCESS_PATH_MAX 128

typedef struct proc_file
{
    char path[PROCESS_PATH_MAX];
    uint64_t offset;
    bool open;
} proc_file_t;

typedef enum process_state
{
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,
} process_state_t;

typedef struct process
{
    uint64_t pid;
    char name[PROCESS_NAME_MAX];
    process_state_t state;
    uint32_t cpu;          // which cpu's runqueue this process lives on
    uint64_t cr3;          // page table root (address space)
    uint64_t ticks;        // ticks consumed on the current slice
    uint64_t fs_base;      // FS segment base (mlibc TCB), restored on switch
    uint64_t mmap_cursor;  // bump cursor for anonymous user mappings
    proc_file_t files[PROCESS_MAX_FDS];

    // per-process kernel stack: syscalls and IRQs run on this stack, so a
    // process blocked inside a syscall can be switched out and resumed
    // without clobbering another process's stack frames
    uint64_t kernel_stack_top;
    uint64_t saved_kernel_rsp;  // nonzero while blocked in a syscall
    uint64_t user_rsp;          // gs:16 saved for the blocked syscall

    // fork/waitpid/zombie tracking
    int32_t exit_status;
    bool waiting;          // blocked in SYS_WAITPID
    struct process *parent;
    struct process *children;
    struct process *next_sibling;

    // virtual terminal this process reads/writes (multi-terminal support)
    uint8_t vt;
    bool terminate;        // set by SYS_KILL, honored on next switch/loop
    bool blocked;          // inside a blocking syscall (hlt loop); the
                           // timer may preempt a blocked kernel-mode process

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

// replaces the caller's image with the program at `path` (execve):
// frees the old address space, maps the new image and builds a user
// stack with argv/envp. argv/envp are user pointers read through the
// current cr3. returns false on any error (process unchanged... on
// partial failure the old image may already be gone - not used that way)
bool process_execve(process_t *proc, const char *path, char *const argv[],
                    char *const envp[]);

// fork clone: copies the address space, file table and user context.
// returns the child pcb (enqueued nowhere yet) or 0
process_t *process_fork(const syscall_regs_t *regs);

// finds a zombie child (pid == 0 means any), 0 if none
process_t *process_find_zombie_child(process_t *parent, uint64_t pid);

// removes `child` from the parent's children list and destroys it
void process_reap_child(process_t *parent, process_t *child);

// attaches `child` to `parent`'s children list (used at fork time)
void process_link_child(process_t *parent, process_t *child);

// sets the initial ring3 registers (full context, so a preempted
// process can be restored from ctx even on its very first run)
void process_setup(process_t *proc, uint64_t entry, uint64_t stack_top);

// frees the address space and pcb
void process_destroy(process_t *proc);

process_t *process_current(void);
void process_set_current(process_t *proc);

uint64_t process_next_pid(void);

// reads a C string from `cr3`'s address space into a kernel-allocated
// buffer (up to 255 bytes); returns 0 if any byte is unmapped or the
// string does not terminate
char *read_user_cstr(uint64_t cr3, uint64_t uaddr);

#ifdef __cplusplus
}
#endif

#endif // PROCESS_PROCESS_H
