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

    .preinit_array : ALIGN(4K)
    {
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;
    }

    .init_array : ALIGN(4K)
    {
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;
    }

    .fini_array : ALIGN(4K)
    {
        __fini_array_start = .;
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array))
        __fini_array_end = .;
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4K);
    }
}
