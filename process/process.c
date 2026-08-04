// Process management: pcbs, address spaces, PIE loading.

#include "process.h"

#include "../exec/elf.h"
#include "../fs/vfs.h"
#include "../mem/mm/paging.h"
#include "../mem/mm/pmm.h"
#include "../mem/mm/kheap.h"
#include "../mem/lib/memory.h"
#include "../arch/x86_64/cpu/msr.h"
#include "../arch/x86_64/cpu/percpu.h"
#include "../init/debug.h"

#define PIE_BASE_MIN PAGING_USER_BASE          // 1 GiB
#define PIE_BASE_RANGE (512ULL * 1024 * 1024)  // 512 MiB of bases
#define PROCESS_KSTACK_SIZE (16 * 1024)

static uint64_t next_pid = 1;

void process_init(void)
{
    next_pid = 1;
}

uint64_t process_next_pid(void)
{
    return next_pid;
}

// the "current" process is per-cpu (SMP): each core tracks the process
// it is running in its own percpu struct
process_t *process_current(void)
{
    return (process_t *)percpu_current()->current;
}

void process_set_current(process_t *proc)
{
    percpu_current()->current = proc;
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
    proc->mmap_cursor = PROCESS_MMAP_START;

    // fds 0-2 are the tty (stdin/stdout/stderr), like Linux
    for (int i = 0; i < 3 && i < PROCESS_MAX_FDS; i++)
    {
        k_strncpy(proc->files[i].path, "tty", sizeof(proc->files[i].path));
        proc->files[i].offset = 0;
        proc->files[i].open = true;
    }

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

    // per-process kernel stack (syscalls + IRQs); blocking syscalls can
    // be switched out and resumed without corrupting another process
    proc->kernel_stack_top =
        (uint64_t)kheap_alloc(PROCESS_KSTACK_SIZE, 16) + PROCESS_KSTACK_SIZE;
    if (!proc->kernel_stack_top)
    {
        paging_free_address_space(proc->cr3);
        kheap_free(proc);
        return 0;
    }

    return proc;
}

// maps a (possibly partial) segment from the image into the process
static bool map_segment(uint64_t cr3, const uint8_t *image,
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

        if (!paging_map(cr3, va, frame,
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
static bool map_user_stack(uint64_t cr3)
{
    uint64_t base = PROCESS_USER_STACK_TOP - PROCESS_USER_STACK_SIZE;

    for (uint64_t va = base; va < PROCESS_USER_STACK_TOP; va += 0x1000)
    {
        uintptr_t frame = pmm_alloc_frame();

        if (!frame)
            return false;

        pmm_zero_page(frame);

        if (!paging_map(cr3, va, frame,
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
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_IRELATIVE 0x24
#define R_X86_64_IRELATIV 0x25

typedef struct elf64_rela
{
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} elf64_rela_t;

typedef struct elf64_sym
{
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} elf64_sym_t;

// resolves a symbol referenced by a relocation: returns its base-relative
// virtual address and whether it is defined (SHN_UNDEF means not defined)
static uint64_t resolve_symbol(const uint8_t *image, size_t size,
                               const elf_dynamic_t *dyn, uint32_t sym_index,
                               uint64_t base, bool *defined)
{
    *defined = false;

    if (!dyn->symtab_offset || dyn->sym_ent < sizeof(elf64_sym_t))
        return 0;

    uint64_t off = dyn->symtab_offset + (uint64_t)sym_index * dyn->sym_ent;

    if (off + sizeof(elf64_sym_t) > size)
        return 0;

    const elf64_sym_t *sym = (const elf64_sym_t *)(image + off);

    if (sym->st_shndx == 0)  // SHN_UNDEF: weakly undefined in a static link
        return 0;

    *defined = true;
    return base + sym->st_value;
}

// applies base-relative relocations from PT_DYNAMIC so a PIE (or a
// future .so) works without a userspace loader: every RELATIVE slot
// gets base + addend written straight into the process's memory
static bool apply_relocations(uint64_t cr3, const uint8_t *image,
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
        uint64_t value = 0;

        switch (type)
        {
            case R_X86_64_RELATIVE:
                value = base + (uint64_t)r->r_addend;
                break;

            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
            {
                bool defined;
                value = resolve_symbol(image, size, &dyn,
                                       (uint32_t)(r->r_info >> 32), base,
                                       &defined);
                if (!defined)
                    continue;  // leave weakly-undefined slots untouched
                break;
            }

            case R_X86_64_64:
            {
                bool defined;
                uint64_t sym = resolve_symbol(image, size, &dyn,
                                              (uint32_t)(r->r_info >> 32), base,
                                              &defined);
                value = sym + (uint64_t)r->r_addend;
                (void)defined;
                break;
            }

            case R_X86_64_IRELATIVE:
            case R_X86_64_IRELATIV:
                // IFUNC slots need a running resolver; the Dynt build is
                // compiled with -fno-ifunc so none should appear
                continue;

            default:
                continue;
        }

        uint64_t vaddr = base + r->r_offset;
        uint64_t phys = paging_translate(cr3, vaddr);

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
        if (!map_segment(proc->cr3, elf_buf, &segs[i], base))
        {
            kheap_free(elf_buf);
            return false;
        }
    }

    if (!apply_relocations(proc->cr3, elf_buf, (size_t)size, base))
    {
        kheap_free(elf_buf);
        return false;
    }

    if (!map_user_stack(proc->cr3))
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

// builds a System V x86-64 initial user stack so mlibc's crt1 can read
// argc/argv/envp/auxv. layout at %rsp on entry:
//   argc | argv[0] | NULL | envp[0]=NULL | auxv(AT_NULL,0)
void process_setup(process_t *proc, uint64_t entry, uint64_t stack_top)
{
    uint64_t vaddr = stack_top;

    // argv[0] string lives deep inside the mapped stack region
    uint64_t str_vaddr = stack_top - 0x2000;
    char *str_k = (char *)paging_translate(proc->cr3, str_vaddr);
    if (str_k)
        k_memcpy(str_k, proc->name, k_strlen(proc->name) + 1);

    vaddr -= 16;  // auxv: AT_NULL pair (type = 0, value = 0)
    uint64_t *aux = (uint64_t *)paging_translate(proc->cr3, vaddr);
    if (aux)
    {
        aux[0] = 0;
        aux[1] = 0;
    }

    vaddr -= 8;  // envp terminator
    uint64_t *envp = (uint64_t *)paging_translate(proc->cr3, vaddr);
    if (envp)
        *envp = 0;

    vaddr -= 8;  // argv terminator
    uint64_t *argv_null = (uint64_t *)paging_translate(proc->cr3, vaddr);
    if (argv_null)
        *argv_null = 0;

    vaddr -= 8;  // argv[0] pointer
    uint64_t *argv = (uint64_t *)paging_translate(proc->cr3, vaddr);
    if (argv)
        *argv = str_vaddr;

    vaddr -= 8;  // argc
    uint64_t *argc = (uint64_t *)paging_translate(proc->cr3, vaddr);
    if (argc)
        *argc = 1;

    proc->ctx.rip = entry;
    proc->ctx.user_rsp = vaddr;
    proc->ctx.rflags = 0x202;
}

void process_destroy(process_t *proc)
{
    if (!proc)
        return;

    if (proc->kernel_stack_top)
        kheap_free((void *)(proc->kernel_stack_top - PROCESS_KSTACK_SIZE));

    paging_free_address_space(proc->cr3);
    kheap_free(proc);
}

// reads a C string from the given address space (current process), up to
// a reasonable cap. returns a kernel-allocated copy, 0 on unmapped/fault
static char *read_user_cstr(uint64_t cr3, uint64_t uaddr)
{
    char *out = (char *)kheap_alloc(256, 1);

    if (!out)
        return 0;

    for (size_t i = 0; i < 255; i++)
    {
        uint64_t phys = paging_translate(cr3, uaddr + i);

        if (!phys)
        {
            kheap_free(out);
            return 0;
        }

        char c = *(volatile char *)phys;
        out[i] = c;

        if (c == '\0')
            return out;
    }

    kheap_free(out);
    return 0;
}

// builds a fresh System V initial stack with argc/argv/envp and returns
// the %rsp to enter the program with. on entry %rsp points to argc;
// argv[] strings and envp[] strings live just below the stack top.
static uint64_t build_exec_stack(uint64_t cr3, char *const argv[], int argc,
                                 char *const envp[], int envc, int *out_argc)
{
    uint64_t sp = PROCESS_USER_STACK_TOP;

    // strings, starting just below the top of the mapped stack
    uint64_t arena = sp;

    for (int i = 0; i < envc; i++)
    {
        uint64_t len = k_strlen(envp[i]) + 1;
        arena -= len;
        arena &= ~0xFULL;
        uint64_t phys = paging_translate(cr3, arena);
        if (!phys)
            return 0;
        k_memcpy((void *)phys, envp[i], len);
    }

    for (int i = 0; i < argc; i++)
    {
        uint64_t len = k_strlen(argv[i]) + 1;
        arena -= len;
        arena &= ~0xFULL;
        uint64_t phys = paging_translate(cr3, arena);
        if (!phys)
            return 0;
        k_memcpy((void *)phys, argv[i], len);
    }

    sp -= 16;  // auxv: AT_NULL pair
    uint64_t *aux = (uint64_t *)paging_translate(cr3, sp);
    if (!aux)
        return 0;
    aux[0] = 0;
    aux[1] = 0;

    sp -= 8;  // envp NULL terminator
    uint64_t *en = (uint64_t *)paging_translate(cr3, sp);
    if (!en)
        return 0;
    *en = 0;

    sp -= 8 * envc;  // envp array
    uint64_t env_base = sp;
    for (int i = 0; i < envc; i++)
    {
        uint64_t *e = (uint64_t *)paging_translate(cr3, env_base + i * 8);
        if (!e)
            return 0;
        *e = 0;  // filled below
    }

    sp -= 8;  // argv NULL terminator
    uint64_t *an = (uint64_t *)paging_translate(cr3, sp);
    if (!an)
        return 0;
    *an = 0;

    sp -= 8 * argc;  // argv array
    uint64_t arg_base = sp;
    for (int i = 0; i < argc; i++)
    {
        uint64_t *a = (uint64_t *)paging_translate(cr3, arg_base + i * 8);
        if (!a)
            return 0;
        *a = 0;  // filled below
    }

    sp -= 8;  // argc
    uint64_t *ac = (uint64_t *)paging_translate(cr3, sp);
    if (!ac)
        return 0;
    *ac = (uint64_t)argc;

    // walk the arena again to fill argv/envp pointers in order
    uint64_t cur = arena;
    for (int i = 0; i < argc; i++)
    {
        uint64_t len = k_strlen(argv[i]) + 1;
        uint64_t *a = (uint64_t *)paging_translate(cr3, arg_base + i * 8);
        if (!a)
            return 0;
        *a = cur;
        cur += len;
    }

    for (int i = 0; i < envc; i++)
    {
        uint64_t len = k_strlen(envp[i]) + 1;
        uint64_t *e = (uint64_t *)paging_translate(cr3, env_base + i * 8);
        if (!e)
            return 0;
        *e = cur;
        cur += len;
    }

    *out_argc = argc;
    return sp;
}

bool process_execve(process_t *proc, const char *path, char *const argv[],
                    char *const envp[])
{
    // snapshot argc/argc from the user arrays (kernel-side, still under
    // the old address space, so they're only valid if argv/envp point at
    // the old process - fine for syscall usage)
    int argc = 0;
    while (argv && argv[argc])
        argc++;
    int envc = 0;
    while (envp && envp[envc])
        envc++;

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

    // fresh address space for the new image (keep the old one until the
    // new image is fully mapped, so a failure leaves the process intact)
    uint64_t new_cr3 = paging_new_address_space();
    if (!new_cr3)
    {
        kheap_free(elf_buf);
        return false;
    }

    uint64_t base = pie ? pie_base(proc) : 0;

    for (int i = 0; i < seg_count; i++)
    {
        if (!map_segment(new_cr3, elf_buf, &segs[i], base))
        {
            paging_free_address_space(new_cr3);
            kheap_free(elf_buf);
            return false;
        }
    }

    if (!apply_relocations(new_cr3, elf_buf, (size_t)size, base))
    {
        paging_free_address_space(new_cr3);
        kheap_free(elf_buf);
        return false;
    }

    if (!map_user_stack(new_cr3))
    {
        paging_free_address_space(new_cr3);
        kheap_free(elf_buf);
        return false;
    }

    kheap_free(elf_buf);

    // build the initial stack (argc/argv/envp) in the new address space
    int built_argc = 0;
    uint64_t rsp = build_exec_stack(new_cr3, argv, argc, envp, envc,
                                    &built_argc);
    if (!rsp)
    {
        paging_free_address_space(new_cr3);
        return false;
    }

    paging_free_address_space(proc->cr3);
    proc->cr3 = new_cr3;
    proc->mmap_cursor = PROCESS_MMAP_START;

    // reset signal/state bits, fs_base (crt1 re-setups the TCB)
    proc->fs_base = 0;
    proc->state = PROC_READY;
    proc->ctx.rip = base + entry;
    proc->ctx.user_rsp = rsp;
    proc->ctx.rflags = 0x202;

    debug_printf("[proc] %s exec pid=%lu entry=0x%lx rsp=0x%lx cr3=0x%lx\n",
                 proc->name, proc->pid, base + entry, rsp, proc->cr3);
    return true;
}

process_t *process_fork(void)
{
    process_t *parent = process_current();
    if (!parent)
        return 0;

    process_t *child = process_create(parent->name);
    if (!child)
        return 0;

    if (!paging_clone_user_space(parent->cr3, child->cr3))
    {
        process_destroy(child);
        return 0;
    }

    // file table clone
    for (int i = 0; i < PROCESS_MAX_FDS; i++)
        child->files[i] = parent->files[i];

    // user context clone: the child appears to return from the fork
    // syscall with value 0, same registers and same stack
    child->ctx = parent->ctx;
    child->ctx.rax = 0;

    // child is enqueued separately (scheduler_enqueue) by the caller
    return child;
}

process_t *process_find_zombie_child(process_t *parent, uint64_t pid)
{
    process_t *c = parent ? parent->children : 0;

    while (c)
    {
        if (c->state == PROC_ZOMBIE && (pid == 0 || c->pid == pid))
            return c;
        c = c->next_sibling;
    }

    return 0;
}

void process_reap_child(process_t *parent, process_t *child)
{
    if (!parent || !child)
        return;

    process_t **pp = &parent->children;
    while (*pp && *pp != child)
        pp = &(*pp)->next_sibling;

    if (*pp)
        *pp = child->next_sibling;

    process_destroy(child);
}

// attaches `child` to `parent`'s children list (used at fork time)
void process_link_child(process_t *parent, process_t *child)
{
    child->parent = parent;
    child->next_sibling = parent->children;
    parent->children = child;
}
