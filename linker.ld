/*
 * Created by epaxgaming on 31.07.26.
 *
 * this linker script places the kernel at 1 MiB (the classic grub/multiboot
 * load point) with identity mapped virtual addresses, so the multiboot2
 * header ends up at the very start of the ELF where grub scans for it.
 */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : ALIGN(4K)
    {
        KEEP(*(.multiboot))
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
