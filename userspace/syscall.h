/*
 * Userspace syscall wrappers — uses the SYSCALL instruction.
 * Syscall numbers must match arch/x86_64/syscall/syscall.h
 */

#ifndef USR_SYSCALL_H
#define USR_SYSCALL_H

#include <stdint.h>

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_READ_FILE 3
#define SYS_LIST_DIR  4
#define SYS_EXEC      5
#define SYS_GETPID    6
#define SYS_SLEEP     7
#define SYS_PS        8

static inline long syscall0(long n)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall1(long n, long a1)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall2(long n, long a1, long a2)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4)
{
    register long r10 __asm__("r10") = a4;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall6(long n, long a1, long a2, long a3, long a4,
                            long a5, long a6)
{
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8),
                        "r"(r9)
                      : "rcx", "r11", "memory");
    return ret;
}

#endif /* USR_SYSCALL_H */