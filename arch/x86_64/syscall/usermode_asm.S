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

# void usermode_resume_full(const registers_t *ctx)
# Restores every register (GP regs + the iretq frame) from a saved
# process context. Used by the scheduler to resume a process that was
# previously preempted, so its general-purpose registers survive a
# context switch intact. Never returns.
#
# registers_t layout (offsets in bytes):
#   rax=0 rbx=8 rcx=16 rdx=24 rsi=32 rdi=40 rbp=48
#   r8=56 r9=64 r10=72 r11=80 r12=88 r13=96 r14=104 r15=112
#   (int_no=120, err_code=128 skipped)
#   rip=136 cs=144 rflags=152 user_rsp=160 ss=168
.global usermode_resume_full
usermode_resume_full:
    mov %rdi, %r11

    mov $0x1B, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs

    mov 0(%r11), %rax
    mov 8(%r11), %rbx
    mov 16(%r11), %rcx
    mov 24(%r11), %rdx
    mov 32(%r11), %rsi
    mov 40(%r11), %rdi
    mov 48(%r11), %rbp
    mov 56(%r11), %r8
    mov 64(%r11), %r9
    mov 72(%r11), %r10
    mov 88(%r11), %r12
    mov 96(%r11), %r13
    mov 104(%r11), %r14
    mov 112(%r11), %r15
    # r11 (offset 80) stays as the frame pointer

    pushq 168(%r11)
    pushq 160(%r11)
    pushq 152(%r11)
    pushq 144(%r11)
    pushq 136(%r11)

    iretq

# void kernel_resume(const registers_t *ctx, uint64_t rsp)
# Restores every register from a saved KERNEL-mode context and iretq's
# back into it with RSP = rsp. Used to resume a process that was
# preempted while blocked inside a syscall (hlt-loop parked on its own
# kernel stack). iretq to a kernel target does not pop RSP/SS, so the
# caller hands us the stack pointer that was live at the interrupt.
.global kernel_resume
kernel_resume:
    mov %rdi, %r10      # ctx
    mov %rsi, %r9       # target kernel rsp

    mov 0(%r10), %rax
    mov 8(%r10), %rbx
    mov 16(%r10), %rcx
    mov 24(%r10), %rdx
    mov 32(%r10), %rsi
    mov 40(%r10), %rdi
    mov 48(%r10), %rbp
    mov 56(%r10), %r8

    mov %r9, %rsp       # resume the interrupted stack pointer
    # A 64-bit iretq pops a 5-qword frame (RIP, CS, RFLAGS, RSP, SS) and
    # validates the SS selector against the CS RPL even for a same-ring
    # (kernel->kernel) return, so supply a matching kernel SS (0x10,
    # RPL 0 == CS RPL 0) and the kernel RSP (krsp) instead of the stale
    # user-mode fields that would otherwise sit at 160/168.
    pushq $0x10         # ss
    pushq %r9           # rsp = krsp
    pushq 152(%r10)     # rflags
    pushq 144(%r10)     # cs
    pushq 136(%r10)     # rip

    mov 80(%r10), %r11
    mov 88(%r10), %r12
    mov 96(%r10), %r13
    mov 104(%r10), %r14
    mov 112(%r10), %r15
    mov 64(%r10), %r9
    mov 72(%r10), %r10

    iretq

.section .note.GNU-stack,"",@progbits
