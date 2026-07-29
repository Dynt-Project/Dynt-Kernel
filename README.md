# x86_64 Architecture Support

Architecture support for the **x86_64** (64-bit) platform.

Written by [@saphhic](https://github.com/saphhic)  
Date: 29 July 2026

## Overview

This directory contains the x86_64 port of the kernel.  
It is designed for **UEFI** systems and includes support for the **Limine** bootloader as well as Multiboot.

## Features

----- Added July 26th ------
- I/O ports
- CPU Instructions
- Control Registers
- Model Specific Registers
- CPU Discovery
- Memory Ordering
- RFLAGS
- Interrupt Control
- Syscall handler
- Syscall Functions
----------------------------

## License

This Directory is under GNU General Public License v3.0 Released on 2007.
The license is as follows:

-----------------------------------------------------------------------
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
-----------------------------------------------------------------------

To see the full license check the [LICENSE](LICENSE) file.

## Note [IMPORTANT]

In the boot folder when bootloader is added a boot.asm file is needed, and in the mm folder goes functions such as paging or vmm, keep this in mind. when adding something to x86_64.

The x86 is written in C, i (saphhic) am just used to write files in C++ so all of the C files CAN be changed to C++ files whitout breaking anything, this is because none of these scripts contains C++ memory maneging functions.

The Assembler code is writen in GAS sintax, in the future if you reading this, plan to make a linker script or a Makefile for the whole kernel or OS keep that in mind.

## Contact

If you encounter any issues whit my code feel free to change it, fix it or directly contact me,
i would aprecieate a lot the feedback!

--------------------------------------------------------
- Email: <thbichosecundario@gmail.com>
- Github: [@saphhic](https://github.com/saphhic)
- Project Repository: (https://github.com/Dynt-Project)
--------------------------------------------------------
