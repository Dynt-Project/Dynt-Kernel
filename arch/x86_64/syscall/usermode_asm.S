# Written by [@saphhic](https://github.com/saphhic)  
# Date: 26 July 2026

.section .text
.global enter_usermode

enter_usermode:
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    
    pushq $0x1B
    pushq %rsi

    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax

    pushq $0x23
    pushq %rdi

    iretq

# void usermode_resume(uint64_t rip, uint64_t rsp, uint64_t rflags)
# Builds a ring3 frame and iretq's into it. Used by SYS_EXEC to jump
# into a freshly loaded program and by SYS_EXIT to resume the saved
# parent context. Never returns.
.global usermode_resume
usermode_resume:
    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs

    pushq $0x1B
    pushq %rsi

    orq $0x200, %rdx
    pushq %rdx

    pushq $0x23
    pushq %rdi

    iretq

.section .note.GNU-stack,"",@progbits
