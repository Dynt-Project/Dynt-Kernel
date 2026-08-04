#include "elf.h"

#include "../arch/x86_64/syscall/usermode.h"
#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"

#define EI_NIDENT 16
#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct elf64_ehdr
{
    uint8_t ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} elf64_ehdr_t;

typedef struct elf64_phdr
{
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} elf64_phdr_t;

static bool range_ok(size_t size, uint64_t off, uint64_t len)
{
    return off <= size && len <= size - off;
}

bool elf64_validate(const void *image, size_t size)
{
    if (!image || size < sizeof(elf64_ehdr_t))
        return false;

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;

    if (eh->ident[0] != ELFMAG0 || eh->ident[1] != ELFMAG1 ||
        eh->ident[2] != ELFMAG2 || eh->ident[3] != ELFMAG3)
        return false;

    if (eh->ident[4] != ELFCLASS64 || eh->ident[5] != ELFDATA2LSB)
        return false;

    if ((eh->type != ET_EXEC && eh->type != ET_DYN) || eh->machine != EM_X86_64)
        return false;

    if (eh->phentsize != sizeof(elf64_phdr_t) || eh->phnum == 0)
        return false;

    return range_ok(size, eh->phoff, (uint64_t)eh->phnum * eh->phentsize);
}

bool elf64_load_image(const void *image, size_t size, elf_image_t *out)
{
    if (!elf64_validate(image, size) || !out)
        return false;

    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    const elf64_phdr_t *ph = (const elf64_phdr_t *)(bytes + eh->phoff);

    k_memset(out, 0, sizeof(*out));
    out->entry = eh->entry;
    out->low_address = UINT64_MAX;

    for (uint16_t i = 0; i < eh->phnum; i++)
    {
        if (ph[i].type != PT_LOAD)
            continue;

        if (ph[i].memsz < ph[i].filesz || !range_ok(size, ph[i].offset, ph[i].filesz))
            return false;

        /* Identity-mapped kernel: vaddr == physical address, so write the
           segment straight to its virtual address.  Refuse anything that
           would stomp the kernel or the early heap; userspace lives at
           0x2000000 (32 MiB) and up (see userspace/linker.ld). */
        uint64_t vaddr = ph[i].vaddr;

        if (vaddr < 0x2000000ULL)
            return false;

        void *segment = (void *)(uintptr_t)vaddr;

        k_memcpy(segment, bytes + ph[i].offset, (size_t)ph[i].filesz);

        if (ph[i].memsz > ph[i].filesz)
            k_memset((uint8_t *)segment + ph[i].filesz, 0,
                     (size_t)(ph[i].memsz - ph[i].filesz));

        uint64_t base = vaddr;
        uint64_t end = vaddr + ph[i].memsz;

        if (base < out->low_address)
            out->low_address = base;
        if (end > out->high_address)
            out->high_address = end;

        out->segment_count++;
        (void)PF_X;
        (void)PF_W;
        (void)PF_R;
    }

    return out->segment_count != 0;
}

[[noreturn]] void elf64_enter_ring3(const elf_image_t *image, uint64_t user_stack)
{
    enter_usermode(image->entry, user_stack);
}
