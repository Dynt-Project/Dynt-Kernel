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
#define PT_DYNAMIC 2
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_RELACOUNT 0x6FFFFFF9
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct elf64_dyn
{
    int64_t tag;
    uint64_t value;
} elf64_dyn_t;

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

bool elf64_parse_segments(const void *image, size_t size,
                          elf_segment_t *segs, int max_segs,
                          int *out_count, uint64_t *out_entry,
                          bool *out_pie)
{
    if (!elf64_validate(image, size))
        return false;

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_phdr_t *ph = (const elf64_phdr_t *)(bytes + eh->phoff);

    int count = 0;

    for (uint16_t i = 0; i < eh->phnum; i++)
    {
        if (ph[i].type != PT_LOAD)
            continue;

        if (ph[i].memsz < ph[i].filesz ||
            !range_ok(size, ph[i].offset, ph[i].filesz))
            return false;

        if (count >= max_segs)
            return false;

        segs[count].vaddr = ph[i].vaddr;
        segs[count].offset = ph[i].offset;
        segs[count].filesz = ph[i].filesz;
        segs[count].memsz = ph[i].memsz;
        count++;
    }

    if (count == 0)
        return false;

    if (out_count)
        *out_count = count;
    if (out_entry)
        *out_entry = eh->entry;
    if (out_pie)
        *out_pie = eh->type == ET_DYN;

    return true;
}

bool elf64_parse_dynamic(const void *image, size_t size, elf_dynamic_t *out)
{
    if (!elf64_validate(image, size) || !out)
        return false;

    k_memset(out, 0, sizeof(*out));

    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    const elf64_phdr_t *ph = (const elf64_phdr_t *)(bytes + eh->phoff);

    for (uint16_t i = 0; i < eh->phnum; i++)
    {
        if (ph[i].type != PT_DYNAMIC)
            continue;

        if (!range_ok(size, ph[i].offset, ph[i].memsz))
            return false;

        out->present = true;

        const elf64_dyn_t *dyn = (const elf64_dyn_t *)(bytes + ph[i].offset);
        uint64_t count = ph[i].memsz / sizeof(elf64_dyn_t);

        for (uint64_t d = 0; d < count; d++)
        {
            switch (dyn[d].tag)
            {
                case DT_RELA:
                {
                    uint64_t off;
                    if (elf64_vaddr_to_offset(image, size, dyn[d].value,
                                              &off))
                        out->rela_offset = off;
                    break;
                }

                case DT_RELASZ:
                    out->rela_size = dyn[d].value;
                    break;

                case DT_RELAENT:
                    out->rela_ent = dyn[d].value;
                    break;

                case DT_RELACOUNT:
                    out->rela_count = dyn[d].value;
                    break;

                default:
                    break;
            }
        }

        return out->rela_size != 0 && out->rela_ent != 0;
    }

    return false;
}

bool elf64_vaddr_to_offset(const void *image, size_t size,
                           uint64_t vaddr, uint64_t *out_offset)
{
    if (!image || !out_offset)
        return false;

    const uint8_t *bytes = (const uint8_t *)image;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)image;
    const elf64_phdr_t *ph = (const elf64_phdr_t *)(bytes + eh->phoff);

    for (uint16_t i = 0; i < eh->phnum; i++)
    {
        if (ph[i].type != PT_LOAD)
            continue;

        uint64_t seg_start = ph[i].vaddr;
        uint64_t seg_end = seg_start + ph[i].memsz;

        if (vaddr >= seg_start && vaddr < seg_end)
        {
            *out_offset = ph[i].offset + (vaddr - seg_start);
            return true;
        }
    }

    return false;
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
