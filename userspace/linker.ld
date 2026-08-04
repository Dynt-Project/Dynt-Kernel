/*
 * Userspace linker script — places all sections at 0x2000000 (32 MiB)
 * which is beyond the kernel heap and within the identity-mapped 1 GiB.
 * The kernel ELF loader copies PT_LOAD segments directly to their vaddr.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x2000000;

    .text : ALIGN(4K)
    {
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4K);
    }
}