# Written by [@saphhic](https://github.com/saphhic)  
# Date: 26 July 2026

# Field order of the pushes below MUST match `struct syscall_regs_t`
# in syscall.h. Also note: rcx (user return RIP) and r11 (user RFLAGS)
#  ride along in this same save/restore — syscall_dispatch() must not
#   touch them, since sysretq reads them back verbatim at the end.

.section .text
.global syscall_entry
.extern syscall_dispatch

syscall_entry:
    swapgs
    movq %rsp, %gs:16
    movq %gs:8, %rsp

    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax

    movq %rsp, %rdi
    callq syscall_dispatch

    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    movq %gs:16, %rsp
    swapgs
    sysretq

.section .note.GNU-stack,"",@progbits
