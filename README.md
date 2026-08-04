# Dynt Kernel

Dynt Kernel is an experimental x86_64 kernel project.

## Status

- Architecture: x86_64
- Boot protocol: Multiboot2
- Bootloader for testing: GRUB
- Language: C/C++ with GAS assembly
- License: GNU General Public License v3.0

## Features

- Multiboot2 boot header and GRUB test boot
- 32-bit boot stub with transition to long mode
- Basic paging setup for early boot
- GDT and IDT setup
- Interrupt and exception handling
- PIC support
- Serial output
- VGA text output
- PS/2 keyboard and mouse code
- SMP CPU discovery and local APIC enable path
- BORE-inspired scheduler core
- Ring 3 ELF64 loader foundation
- Minimal libc string/memory layer
- IDE/ATA PIO storage driver
- AHCI/SATA PCI detection
- MBR and GPT partition discovery
- VFS mount registry
- FAT32 and ext2 filesystem detection
- Basic syscall structure

## Build

Build the kernel ELF:

```sh
make kernel
```

Build a bootable GRUB ISO:

```sh
make iso
```

Run it in QEMU:

```sh
make run
```

The QEMU run target creates and attaches `build/fat32.img` as a FAT32 IDE disk.

## Layout

- `arch/x86_64/` - architecture-specific boot, CPU, interrupt, and syscall code
- `boot/grub/` - GRUB test configuration
- `driver/` - built-in and stack-level driver code
- `init/` - early kernel startup code
- `main.c` - main kernel entry after architecture startup
- `linker.ld` - kernel linker script

## Contributors

See [CONTRIBUTER.md](CONTRIBUTER.md).

## License

This project is licensed under the GNU General Public License v3.0.
See [LICENSE](LICENSE) for the full license text.
