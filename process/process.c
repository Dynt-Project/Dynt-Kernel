// Process management: pcbs, address spaces, PIE loading.

#include "process.h"

#include "../exec/elf.h"
#include "../fs/vfs.h"
#include "../mem/mm/paging.h"
#include "../mem/mm/pmm.h"
#include "../mem/mm/kheap.h"
#include "../mem/lib/memory.h"
#include "../init/debug.h"

#define PIE_BASE_MIN PAGING_USER_BASE          // 1 GiB
#define PIE_BASE_RANGE (512ULL * 1024 * 1024)  // 512 MiB of bases

static process_t *current;
static uint64_t next_pid = 1;

void process_init(void)
{
    current = 0;
    next_pid = 1;
}

uint64_t process_next_pid(void)
{
    return next_pid;
}

process_t *process_current(void)
{
    return current;
}

void process_set_current(process_t *proc)
{
    current = proc;
}

// weak pseudo-random PIE base, deterministic per pid
static uint64_t pie_base(process_t *proc)
{
    uint64_t x = proc->pid * 0x9E3779B97F4A7C15ULL + 0x1234567ULL;
    uint64_t off = x % PIE_BASE_RANGE;

    return PIE_BASE_MIN + (off & ~0x1FFFFFULL);
}

process_t *process_create(const char *name)
{
    process_t *proc = (process_t *)kheap_alloc(sizeof(process_t), 16);

    if (!proc)
        return 0;

    k_memset(proc, 0, sizeof(*proc));
    proc->pid = next_pid++;
    proc->state = PROC_READY;
    proc->ctx.cs = 0x23;   // ring3 code segment
    proc->ctx.ss = 0x1B;   // ring3 data segment
    proc->ctx.rflags = 0x202;

    if (name)
        k_strncpy(proc->name, name, sizeof(proc->name));
    else
        k_strncpy(proc->name, "user", sizeof(proc->name));

    proc->cr3 = paging_new_address_space();
    if (!proc->cr3)
    {
        kheap_free(proc);
        return 0;
    }

    return proc;
}

// maps a (possibly partial) segment from the image into the process
static bool map_segment(process_t *proc, const uint8_t *image,
                        const elf_segment_t *seg, uint64_t base)
{
    uint64_t vaddr = base + seg->vaddr;
    uint64_t start_page = vaddr & ~0xFFFULL;
    uint64_t end_page = ((vaddr + seg->memsz) + 0xFFF) & ~0xFFFULL;

    for (uint64_t va = start_page; va < end_page; va += 0x1000)
    {
        uintptr_t frame = pmm_alloc_frame();

        if (!frame)
            return false;

        pmm_zero_page(frame);

        if (!paging_map(proc->cr3, va, frame,
                        PAGING_FLAG_USER | PAGING_FLAG_WRITABLE))
        {
            pmm_free_frame(frame);
            return false;
        }

        uint64_t lo = vaddr > va ? vaddr : va;
        uint64_t hi = (vaddr + seg->filesz) < (va + 0x1000)
                          ? (vaddr + seg->filesz)
                          : (va + 0x1000);

        if (hi > lo)
        {
            uint64_t src = seg->offset + (lo - vaddr);
            k_memcpy((void *)(frame + (lo - va)), image + src, hi - lo);
        }
    }

    return true;
}

// maps a zeroed user stack at [STACK_TOP - size, STACK_TOP)
static bool map_user_stack(process_t *proc)
{
    uint64_t base = PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_SIZE;

    for (uint64_t va = base; va < PROCESS_USER_STACK_TOP; va += 0x1000)
    {
        uintptr_t frame = pmm_alloc_frame();

        if (!frame)
            return false;

        pmm_zero_page(frame);

        if (!paging_map(proc->cr3, va, frame,
                        PAGING_FLAG_USER | PAGING_FLAG_WRITABLE))
        {
            pmm_free_frame(frame);
            return false;
        }
    }

    return true;
}

// relocation types
#define R_X86_64_64 1
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_RELATIVE 8
#define R_X86_64_JUMP_SLOT 7

typedef struct elf64_rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} elf64_rela_t;

// applies base-relative relocations from PT_DYNAMIC so a PIE (or a
// future .so) works without a userspace loader: every RELATIVE slot
// gets base + addend written straight into the process's memory
static bool apply_relocations(process_t *proc, const uint8_t *image,
                              size_t size, uint64_t base)
{
    elf_dynamic_t dyn;

    if (!elf64_parse_dynamic(image, size, &dyn) || !dyn.present)
        return true;  // statically linked, nothing to do

    if (dyn.rela_ent < sizeof(elf64_rela_t))
        return true;

    uint64_t n = dyn.rela_size / dyn.rela_ent;

    for (uint64_t i = 0; i < n; i++)
    {
        const elf64_rela_t *r = (const elf64_rela_t *)(image + dyn.rela_offset +
                                                       i * dyn.rela_ent);

        uint64_t type = r->r_info & 0xFFFFFFFFULL;

        if (type != R_X86_64_RELATIVE &&
            type != R_X86_64_GLOB_DAT &&
            type != R_X86_64_64)
            continue;

        uint64_t vaddr = base + r->r_offset;
        uint64_t value = base + (uint64_t)r->r_addend;

        uint64_t phys = paging_translate(proc->cr3, vaddr);

        if (!phys)
            return false;

        *(uint64_t *)phys = value;
    }

    return true;
}

bool process_load_elf(process_t *proc, const char *path)
{
    uint8_t *elf_buf = (uint8_t *)kheap_alloc(1024 * 1024, 16);

    if (!elf_buf)
        return false;

    int32_t size = vfs_read_file(path, elf_buf, 1024 * 1024);
    if (size <= 0)
    {
        kheap_free(elf_buf);
        return false;
    }

    elf_segment_t segs[ELF_SEGMENT_MAX];
    int seg_count;
    uint64_t entry;
    bool pie;

    if (!elf64_parse_segments(elf_buf, (size_t)size, segs, ELF_SEGMENT_MAX,
                              &seg_count, &entry, &pie))
    {
        kheap_free(elf_buf);
        return false;
    }

    uint64_t base = pie ? pie_base(proc) : 0;

    for (int i = 0; i < seg_count; i++)
    {
        if (!map_segment(proc, elf_buf, &segs[i], base))
        {
            kheap_free(elf_buf);
            return false;
        }
    }

    if (!apply_relocations(proc, elf_buf, (size_t)size, base))
    {
        kheap_free(elf_buf);
        return false;
    }

    if (!map_user_stack(proc))
    {
        kheap_free(elf_buf);
        return false;
    }

    kheap_free(elf_buf);

    process_setup(proc, base + entry, PROCESS_USER_STACK_TOP);

    debug_printf("[proc] %s pid=%lu base=0x%lx entry=0x%lx cr3=0x%lx\n",
                 proc->name, proc->pid, base, base + entry, proc->cr3);
    return true;
}

void process_setup(process_t *proc, uint64_t entry, uint64_t stack_top)
{
    proc->ctx.rip = entry;
    proc->ctx.user_rsp = stack_top;
    proc->ctx.rflags = 0x202;
}

void process_destroy(process_t *proc)
{
    if (!proc)
        return;

    paging_free_address_space(proc->cr3);
    proc->state = PROC_ZOMBIE;
    kheap_free(proc);
}
