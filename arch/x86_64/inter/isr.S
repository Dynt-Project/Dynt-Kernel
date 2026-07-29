# Written by [@saphhic](https://github.com/saphhic)  
# Date: 26 July 2026

#   Every interrupt vector needs its OWN stub because the CPU only tells
#   you the vector number implicitly (by which stub it jumped to) — and
#   only SOME vectors get a hardware error code pushed automatically
#   (8, 10-14, 17, 21, 29, 30). To make int_no/err_code always sit at the
#   same stack offset for the C++ side, vectors with no hardware error
#   code push a dummy 0 first. Get this wrong for even one vector and
#   every register in isr_handler() after it is silently misaligned.

.section .text

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    pushq $0
    pushq $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    pushq $\num
    jmp isr_common_stub
.endm

.macro IRQ num, vector
.global irq\num
irq\num:
    pushq $0
    pushq $\vector
    jmp irq_common_stub
.endm

ISR_NOERRCODE 0  # DE
ISR_NOERRCODE 1  # DB
ISR_NOERRCODE 2  # NMI
ISR_NOERRCODE 3  # BP
ISR_NOERRCODE 4  # OF
ISR_NOERRCODE 5  # BR
ISR_NOERRCODE 6  # UD
ISR_NOERRCODE 7  # NM
ISR_ERRCODE 8    # DF
ISR_NOERRCODE 9  # CSO
ISR_ERRCODE 10   # TS
ISR_ERRCODE 11   # NP
ISR_ERRCODE 12   # SS
ISR_ERRCODE 13   # GP
ISR_ERRCODE 14   # PF
ISR_NOERRCODE 15 # Reserved
ISR_NOERRCODE 16 # MF
ISR_ERRCODE 17   # AC
ISR_NOERRCODE 18 # MC
ISR_NOERRCODE 19 # XM
ISR_NOERRCODE 20 # VE
ISR_ERRCODE 21   # CP
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28 # HV
ISR_ERRCODE 29   # VC
ISR_ERRCODE 30   # SX
ISR_NOERRCODE 31

IRQ 0, 32 # PIT timer
IRQ 1, 33 # Keyboard (PS/2)
IRQ 2, 34 # Cascade to slave PIC
IRQ 3, 35 # COM2/4
IRQ 4, 36 # COM1/3
IRQ 5, 37 # LPT2
IRQ 6, 38 # Floppy
IRQ 7, 39 # LPT1
IRQ 8, 40 # RTC
IRQ 9, 41 
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44 # mouse (PS/2)
IRQ 13, 45 # FPU
IRQ 14, 46 # Primary ATA
IRQ 15, 47 # Secondary ATA


.extern isr_handler
.extern irq_handler

isr_common_stub:
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

    mov %rsp, %rdi
    call isr_handler

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

    add $16, %rsp
    iretq

irq_common_stub:
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

    mov %rsp, %rdi
    call irq_handler

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

    add $16, %rsp
    iretq

.section .note.GNU-stack,"",@progbits
