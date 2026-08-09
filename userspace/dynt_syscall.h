#pragma once

// Raw Dynt kernel syscall helpers for apps that need calls mlibc does not
// wrap (e.g. listing a directory or binding a process to a terminal).
// The ABI matches arch/x86_64/syscall/ (Linux-style: rdi/rsi/rdx/r10/r8/r9).
// Errors come back as negative errno values (kernel convention).

#define DYNT_SYS_LIST_DIR  4
#define DYNT_SYS_PS        8
#define DYNT_SYS_VTSET     20

static inline long dynt_syscall0(long sc)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(sc)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline long dynt_syscall1(long sc, long a1)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(sc), "D"(a1)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline long dynt_syscall2(long sc, long a1, long a2)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(sc), "D"(a1), "S"(a2)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline long dynt_syscall3(long sc, long a1, long a2, long a3)
{
    long ret;
    asm volatile("syscall" : "=a"(ret) : "a"(sc), "D"(a1), "S"(a2), "d"(a3)
                 : "rcx", "r11", "memory");
    return ret;
}
