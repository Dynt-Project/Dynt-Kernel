// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

//   The real (SYSCALL/SYSRET) mechanism, not an int 0x80 gate. Field
//   order in syscall_regs_t MUST match the push order in syscall_entry.asm
//   exactly — same contract as registers_t/isr.asm.

#ifndef ARCH_X86_64_SYSCALL_H
#define ARCH_X86_64_SYSCALL_H

#include <stdint.h>

struct __attribute__((packed)) syscall_regs_t {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_READ_FILE 3
#define SYS_LIST_DIR  4
#define SYS_EXEC      5
#define SYS_GETPID    6
#define SYS_SLEEP     7
#define SYS_PS        8
#define SYS_MMAP      9
#define SYS_MUNMAP    10
#define SYS_OPEN      11
#define SYS_CLOSE     12
#define SYS_SEEK      13
#define SYS_GETTICKS  14
#define SYS_SETFSBASE 15
#define SYS_FORK      16
#define SYS_EXECVE    17
#define SYS_WAITPID   18
#define SYS_KILL      19
#define SYS_VTSET     20
#define SYS_TCGETATTR 21
#define SYS_TCSETATTR 22
#define SYS_TTYWINSIZE 23
#define SYS_MKDIR     24
#define SYS_REMOVE    25

#ifdef __cplusplus
extern "C" {
#endif

void syscall_init(void);
extern void syscall_entry(void);

void syscall_dispatch(syscall_regs_t *regs);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_SYSCALL_H