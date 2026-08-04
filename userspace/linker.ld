/*
 * Userspace linker script — PIE layout. The kernel ELF loader picks a
 * free base (1-1.5 GiB, per-process) and applies the R_X86_64_RELATIVE
 * relocations itself, so no userspace dynamic loader is needed.
 */

ENTRY(_start)

SECTIONS
{
    . = SIZEOF_HEADERS;

    .text : ALIGN(4K)
    {
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data.rel.ro : ALIGN(4K)
    {
        *(.data.rel.ro*)
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
