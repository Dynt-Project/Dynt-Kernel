//
// Created by epaxgaming on 31.07.26.
//
// this file contains the multiboot2 header and the first code that runs
// when grub hands control to the kernel.
// grub enters this stub in 32-bit protected mode with paging disabled
// (multiboot2 spec) so this file has to do three things:
//   1. build identity paging (1 GiB with 2 MiB pages)
//   2. enable PAE + long mode + paging
//   3. far jump into 64-bit code and hand over to kernel_entry() in C
// the temporary gdt below keeps the 0x08/0x10 selectors so the later
// gdt_flush() in gdt.c can reload its own tables without any surprises.

.section .multiboot
.code32

// =================== multiboot2 header ===================
.align 8
mb2_header_start:
    .long 0xE85250D6                    // magic for multiboot2
    .long 0                             // architecture: i386 (protected mode)
    .long mb2_header_end - mb2_header_start
    .long 0x100000000 - (0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))

    // end tag (type 0, size 8)
    .align 8
    .word 0
    .word 0
    .long 8
mb2_header_end:

// =================== temporary gdt ===================
.section .rodata
.align 8
boot_gdt:
    .quad 0x0000000000000000            // null descriptor
    .quad 0x00AF9A000000FFFF            // 0x08 kernel code (long mode)
    .quad 0x00AF92000000FFFF            // 0x10 kernel data
boot_gdtr:
    .word boot_gdtr - boot_gdt - 1
    .long boot_gdt

// =================== serial debug ===================
// raw COM1 output so we can see boot progress even before serial.c runs

#define COM1 0x3F8

serial_init:
    movw $COM1 + 1, %dx
    movb $0x00, %al
    outb %al, %dx
    movw $COM1 + 3, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $COM1, %dx
    movb $0x03, %al
    outb %al, %dx
    movw $COM1 + 1, %dx
    movb $0x00, %al
    outb %al, %dx
    movw $COM1 + 3, %dx
    movb $0x03, %al
    outb %al, %dx
    movw $COM1 + 2, %dx
    movb $0xC7, %al
    outb %al, %dx
    movw $COM1 + 4, %dx
    movb $0x0B, %al
    outb %al, %dx
    ret

serial_putc:
    pushl %eax
    movw $COM1 + 5, %dx
1:
    inb %dx, %al
    testb $0x20, %al
    jz 1b
    popl %eax
    movw $COM1, %dx
    outb %al, %dx
    ret

serial_puts:
    lodsb
    testb %al, %al
    jz 1f
    call serial_putc
    jmp serial_puts
1:
    ret

// =================== 32-bit entry ===================
.section .text
.global _start
_start:
    cli
    cld

    // save what the bootloader passed us first (multiboot2 spec:
    // eax = magic, ebx = info address); the serial calls below clobber eax
    movl %eax, boot_magic
    movl %ebx, boot_info_addr

    // bring up COM1 so the boot stages can be watched on the serial port
    call serial_init
    movl $bstr_enter, %esi
    call serial_puts

    // zero the paging area: 3 pages (pml4 + pdpt + pd)
    movl $boot_pml4, %edi
    movl $3072, %ecx
    xorl %eax, %eax
    rep stosl

    // pd[0..511] identity maps 0 - 1 GiB as 2 MiB pages
    // (present, rw, user, ps) — bit 2 = user, so ring3 can run code
    movl $boot_pd, %edi
    movl $0x87, %eax
    xorl %ebx, %ebx                     // high dword of every entry = 0
    movl $512, %ecx
1:
    movl %eax, (%edi)
    movl %ebx, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    // pdpt[0] -> boot_pd (present, rw, user)
    movl $boot_pdpt, %edi
    movl $boot_pd, %eax
    orl $0x07, %eax
    movl %eax, (%edi)
    movl %ebx, 4(%edi)                  // high dword = 0

    // pml4[0] -> boot_pdpt (present, rw, user)
    movl $boot_pml4, %edi
    movl $boot_pdpt, %eax
    orl $0x07, %eax
    movl %eax, (%edi)
    movl %ebx, 4(%edi)                  // high dword = 0

    // enable PAE (cr4 bit 5)
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    // load the pml4 table
    movl $boot_pml4, %eax
    movl %eax, %cr3

    // enable long mode (efer bit 8 = LME)
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    // enable paging + protected mode (cr0 bits 31 and 0)
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    movl $bstr_paging, %esi
    call serial_puts

    // load the temporary gdt and far jump into 64-bit code
    lgdt boot_gdtr
    ljmp $0x08, $start64

// =================== 64-bit entry ===================
.code64
start64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    // make sure sse/fpu can be used by the C code
    movq %cr0, %rax
    andq $-5, %rax                      // clear EM (cr0 bit 2)
    orq $0x2, %rax                      // set MP  (cr0 bit 1)
    movq %rax, %cr0
    movq %cr4, %rax
    orq $0x600, %rax                    // OSFXSR | OSXMMEXCPT
    movq %rax, %cr4

    // set up the bootstrap stack and call the C entry
    movq $boot_stack_top, %rsp
    lea bstr_entry(%rip), %rsi
    call serial_puts
    movl boot_magic(%rip), %edi
    movl boot_info_addr(%rip), %esi
    call kernel_entry

.hang:
    cli
    hlt
    jmp .hang

// =================== boot messages ===================
.section .rodata
bstr_enter:
    .asciz "[boot] _start entered\r\n"
bstr_paging:
    .asciz "[boot] long mode + paging ok\r\n"
bstr_entry:
    .asciz "[boot] calling kernel_entry\r\n"

// =================== data ===================
.section .bss
.align 4096
boot_magic:
    .skip 4
boot_info_addr:
    .skip 4
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096
.align 4096
boot_stack:
    .skip 16384
boot_stack_top:

.section .note.GNU-stack,"",@progbits
