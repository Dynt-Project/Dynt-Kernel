// Written by [@saphhic](https://github.com/saphhic)  
// Date: 30 July 2026

// Structure for the bootloader to pass information to the kernel, 
// this is used in the bootloader and the kernel

#pragma once
#include <stdint.h>

typedef uint16_t UI16;
typedef uint32_t UI32;
typedef uint64_t UI64;

struct krbootinfo {
    
    void* kernel;
    UI32 kernel_size;

    void* memory_map;
    UI64 memory_map_entries;

    void* framebuffer;
    UI32 framebuffer_width;
    UI32 framebuffer_height;
    UI16 framebuffer_bpp;
    UI32 framebuffer_pitch;
    
    void* rsdp;
    void* smbios;
};
