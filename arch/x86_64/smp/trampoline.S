# SMP application-processor trampoline.
#
# After a STARTUP (SIPI) IPI the APs begin executing in 16-bit real mode
# at 0x000VV000 with paging disabled — they cannot just jump into the
# 64-bit kernel entry (boot.S) like GRUB did for the BSP.  smp_start_aps()
# copies this blob to TRAMP_BASE (0x8000, below the 1 MiB kernel image,
# inside the pmm-reserved low region) and fills the trampoline_info /
# tramp_cpu_table blocks with per-cpu data; each AP then walks the same
# 16 -> 32 -> 64 bit path as boot.S and lands in smp_ap_entry().
#
# The blob is linked as part of the kernel (at 1 MiB+) but EXECUTES from
# TRAMP_BASE, so every label below is reached through TRAMP(), which maps
# a label to its runtime address = TRAMP_BASE + in-blob offset.  All
# accesses are absolute / segment-fixed, never rip-relative, so the copy
# is position-independent.
#
# The per-cpu info must NOT be read from one shared block late: APs can
# start asynchronously while the BSP is already preparing the next one.
# Each AP therefore identifies itself by its LAPIC id and looks its stack
# up in tramp_cpu_table (one 8-byte slot per cpu, keyed by lapic id).
# trampoline_info holds only cpu-independent data (cr3, C entry, idtr).

#define TRAMP_BASE 0x8000
#define TRAMP(label) (TRAMP_BASE + (label - trampoline_entry))

    .section .text

    .global _trampoline_start
_trampoline_start:

    .code16
    .global trampoline_entry
trampoline_entry:
    cli
    cld

    # a small stack inside the reserved region (0x9000)
    xorw %ax, %ax
    movw %ax, %ss
    movw $0x9000, %sp

    # flat segment 0, so the absolute TRAMP() offsets address correctly
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es

    # load the temporary GDT and switch to 32-bit protected mode
    lgdtl TRAMP(trampoline_gdtr)
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmp $0x08, $TRAMP(trampoline_32)

    .code32
trampoline_32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    # kernel_cr3 (the identity map covers the trampoline, so execution
    # continues at the same flat address once paging is on)
    movl TRAMP(trampoline_info) + 8, %eax
    movl %eax, %cr3

    # PAE
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    # long mode enable
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    # paging
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    # --- find this AP's slot in tramp_cpu_table by LAPIC id ---
    # (the LAPIC MMIO region is mapped in kernel_cr3, paging_map_lapic)
    movl $0xFEE00020, %esi
    movl (%esi), %eax
    shrl $24, %eax                 # LAPIC_ID register: id in bits 31-24
    xorl %ecx, %ecx                # slot index
1:
    movl TRAMP(tramp_cpu_table)+4(,%ecx,8), %edx
    cmpl %eax, %edx
    je 2f
    incl %ecx
    cmpl $16, %ecx
    jb 1b
    # unknown lapic id: park forever
3:
    cli
    hlt
    jmp 3b
2:
    # %ecx = cpu index; carry it into the 64-bit phase

    ljmp $0x18, $TRAMP(trampoline_64)

    .code64
trampoline_64:
    # per-cpu kernel stack (physical, identity-mapped -> 32-bit fits)
    movl TRAMP(tramp_cpu_table)(,%ecx,8), %eax
    movq %rax, %rsp

    # pass the cpu index to the C entry
    movl %ecx, %edi

    # into the C entry
    movl TRAMP(trampoline_info) + 0x10, %eax
    jmp *%rax

    # ---- temporary GDT (absolute base via TRAMP) ----
    .org 0x100
    .align 8
trampoline_gdt:
    .quad 0x0000000000000000           # 0x00 null
    .quad 0x00CF9A000000FFFF           # 0x08 32-bit flat code
    .quad 0x00CF92000000FFFF           # 0x10 32-bit flat data
    .quad 0x00209A0000000000           # 0x18 64-bit code
    .quad 0x0000000000000000
    .quad 0x0000000000000000
    .quad 0x0000000000000000
trampoline_gdtr:
    .word trampoline_gdtr - trampoline_gdt - 1
    .long TRAMP(trampoline_gdt)

    # ---- shared (cpu-independent) info, filled by smp_start_aps() ----
    .org 0x200
trampoline_info:
    .long 0x5452414D                    # 0x00 magic "TRAM"
    .long 0                            # 0x04 reserved
    .quad 0                           # 0x08 kernel_cr3
    .quad 0                           # 0x10 ap_entry_64
    .quad 0                           # 0x18 reserved
    .quad 0                           # 0x20 reserved
    .quad 0                           # 0x28 reserved
    .word 0                           # 0x30 reserved
    .org 0x240

    # ---- per-cpu slot table, one 8-byte slot per cpu ----
    # slot: +0 stack_top (low 32 bits), +4 lapic id
tramp_cpu_table:
    .rept 16
    .quad 0
    .endr
    .org 0x2C0

    .global _trampoline_end
_trampoline_end:

    .section .note.GNU-stack,"",@progbits
