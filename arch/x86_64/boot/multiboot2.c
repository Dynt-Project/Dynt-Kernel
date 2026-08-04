//
// Created by epaxgaming on 31.07.26.
//
// this file parses the multiboot2 information structure that grub passes
// in ebx (see boot.S) into the kernel wide krbootinfo struct.
// kernel_entry() is the first C function that runs after the jump to
// long mode, it fills g_bootinfo and then starts the kernel via main().

#include "common/bootinf.h"

#include <stdint.h>

// multiboot2 magic value that grub puts into eax before entering the kernel
#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

// multiboot2 tag types (only the ones we care about right now)
#define MB2_TAG_END 0
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_TAG_MMAP 6

// the kernel wide boot info struct, filled by kernel_entry()
krbootinfo g_bootinfo;

// defined in main.c
void main();

struct mb2_tag_header
{
    uint32_t type;
    uint32_t size;
};

struct __attribute__((packed)) mb2_mmap_tag
{
    struct mb2_tag_header header;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct __attribute__((packed)) mb2_mmap_entry
{
    uint64_t addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;  // multiboot2 mmap entries are 24 bytes
};

struct __attribute__((packed)) mb2_framebuffer_tag
{
    struct mb2_tag_header header;
    uint64_t framebuffer_addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t reserved;
};

extern "C" void kernel_entry(uint32_t magic,
                             uint32_t info_addr)
{
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || info_addr == 0)
    {
        // grub is required by the spec to hand over these, if we are here
        // something else booted us -> halt without touching any hardware
        __asm__ volatile("cli; hlt");
        for (;;)
        {
        }
    }

    struct mb2_tag_header *tag = (struct mb2_tag_header *)(uintptr_t)(info_addr + 8);

    while (tag->type != MB2_TAG_END)
    {
        switch (tag->type)
        {
            case MB2_TAG_MMAP:
            {
                struct mb2_mmap_tag *mmap = (struct mb2_mmap_tag *)tag;
                g_bootinfo.memory_map = (void *)((uintptr_t)mmap + sizeof(struct mb2_mmap_tag));
                g_bootinfo.memory_map_entries =
                    (mmap->header.size - sizeof(struct mb2_mmap_tag)) / mmap->entry_size;
                break;
            }

            case MB2_TAG_FRAMEBUFFER:
            {
                struct mb2_framebuffer_tag *fb = (struct mb2_framebuffer_tag *)tag;
                g_bootinfo.framebuffer = (void *)(uintptr_t)fb->framebuffer_addr;
                g_bootinfo.framebuffer_pitch = fb->pitch;
                g_bootinfo.framebuffer_width = fb->width;
                g_bootinfo.framebuffer_height = fb->height;
                g_bootinfo.framebuffer_bpp = fb->bpp;
                break;
            }
        }

        tag = (struct mb2_tag_header *)((uintptr_t)tag + ((tag->size + 7) & ~7));
    }

    main();
}
