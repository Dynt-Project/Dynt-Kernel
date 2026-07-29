# Written by [@saphhic](https://github.com/saphhic)  
# Date: 26 July 2026

.section .text
.global enter_usermode

enter_usermode:
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    
    pushq $0x23
    pushq %rsi

    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax

    pushq $0x1B
    pushq %rdi

    iretq

.section .note.GNU-stack,"",@progbits
