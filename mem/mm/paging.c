// VMM paging layer
//
// The kernel keeps its identity-mapped boot tables as its own address
// space.  Every user process gets a private page table tree: the kernel
// identity half is cloned in as supervisor pages (so ring3 code can
// never touch kernel memory) and the user half starts empty above
// PAGING_USER_BASE.  Exec fills that half with the program image.

#include "paging.h"

#include "pmm.h"
#include "../lib/memory.h"

#include "../../arch/x86_64/cpu/control_regs.h"

#define PAGING_PAGE 0x1000ULL
#define PAGING_HUGE 0x200000ULL

#define PML4_IDX(a) (((a) >> 39) & 0x1FF)
#define PDPT_IDX(a) (((a) >> 30) & 0x1FF)
#define PD_IDX(a) (((a) >> 21) & 0x1FF)
#define PT_IDX(a) (((a) >> 12) & 0x1FF)

static uint64_t kernel_cr3;

void paging_init(void)
{
    kernel_cr3 = read_cr3();
}

uint64_t paging_kernel_cr3(void)
{
    return kernel_cr3;
}

static void invlpg(uint64_t vaddr)
{
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

uint64_t paging_new_address_space(void)
{
    uint64_t pml4 = pmm_alloc_frame();
    uint64_t pdpt = pmm_alloc_frame();

    if (!pml4 || !pdpt)
    {
        if (pml4)
            pmm_free_frame(pml4);
        if (pdpt)
            pmm_free_frame(pdpt);
        return 0;
    }

    pmm_zero_page(pml4);
    pmm_zero_page(pdpt);

    const uint64_t *kroot = (const uint64_t *)kernel_cr3;

    // clone kernel entries 1..511 (kernel never uses them today, but a
    // future higher-half kernel would live here)
    for (int i = 1; i < 512; i++)
        ((uint64_t *)pml4)[i] = kroot[i];

    // kernel identity half: clone the kernel's pdpt[0] (0-1 GiB huge
    // pages) but as SUPERVISOR so ring3 cannot reach kernel memory
    const uint64_t *kpdpt = (const uint64_t *)(kroot[0] & ~0xFFFULL);
    ((uint64_t *)pdpt)[0] = (kpdpt[0] & ~0x04ULL) | 0x01ULL;

    ((uint64_t *)pml4)[0] = pdpt | 0x07;  // present | writable | user

    return pml4;
}

static void free_user_tree(uint64_t cr3)
{
    uint64_t *pml4 = (uint64_t *)cr3;
    uint64_t *pdpt = (uint64_t *)(pml4[0] & ~0xFFFULL);

    for (int i = 1; i < 512; i++)
    {
        if (!(pdpt[i] & 1))
            continue;

        uint64_t *pd = (uint64_t *)(pdpt[i] & ~0xFFFULL);

        for (int j = 0; j < 512; j++)
        {
            if (!(pd[j] & 1))
                continue;

            if (pd[j] & PAGING_FLAG_LARGE)
            {
                // free the backing frames of the 2 MiB huge page
                uint64_t base = pd[j] & ~0x1FFFFFULL;
                for (int k = 0; k < 512; k++)
                    pmm_free_frame(base + k * PAGING_PAGE);
            }
            else
            {
                uint64_t *pt = (uint64_t *)(pd[j] & ~0xFFFULL);

                for (int k = 0; k < 512; k++)
                {
                    if (pt[k] & 1)
                        pmm_free_frame(pt[k] & ~0xFFFULL);
                }

                pmm_free_frame((uintptr_t)pt);
            }
        }

        pmm_free_frame((uintptr_t)pd);
    }

    pmm_free_frame((uintptr_t)pdpt);
    pmm_free_frame(cr3);
}

void paging_free_address_space(uint64_t cr3)
{
    if (!cr3 || cr3 == kernel_cr3)
        return;

    free_user_tree(cr3);
}

bool paging_clone_user_space(uint64_t src_cr3, uint64_t dst_cr3)
{
    if (!src_cr3 || !dst_cr3)
        return false;

    uint64_t *spml4 = (uint64_t *)src_cr3;
    uint64_t *dpml4 = (uint64_t *)dst_cr3;

    for (int a = 0; a < 512; a++)
    {
        if (!(spml4[a] & 1))
            continue;

        uint64_t *spdpt = (uint64_t *)(spml4[a] & ~0xFFFULL);

        for (int b = 0; b < 512; b++)
        {
            if (!(spdpt[b] & 1))
                continue;

            uint64_t *spd = (uint64_t *)(spdpt[b] & ~0xFFFULL);

            for (int c = 0; c < 512; c++)
            {
                if (!(spd[c] & 1))
                    continue;

                if (!(spd[c] & PAGING_FLAG_USER))
                    continue;

                uint64_t base = ((uint64_t)a << 39) | ((uint64_t)b << 30) |
                                ((uint64_t)c << 21);

                if (spd[c] & PAGING_FLAG_LARGE)
                {
                    uint64_t srcp = spd[c] & ~0x1FFFFFULL;

                    for (int k = 0; k < 512; k++)
                    {
                        uint64_t frame = pmm_alloc_frame();
                        if (!frame)
                            return false;
                        pmm_zero_page(frame);
                        k_memcpy((void *)frame, (void *)(srcp + k * PAGING_PAGE),
                                 PAGING_PAGE);
                        if (!paging_map(dst_cr3, base + k * PAGING_PAGE, frame,
                                        PAGING_FLAG_USER | PAGING_FLAG_WRITABLE))
                            return false;
                    }
                    continue;
                }

                uint64_t *spt = (uint64_t *)(spd[c] & ~0xFFFULL);

                for (int k = 0; k < 512; k++)
                {
                    if (!(spt[k] & 1))
                        continue;
                    if (!(spt[k] & PAGING_FLAG_USER))
                        continue;

                    uint64_t vaddr = base + k * PAGING_PAGE;
                    uint64_t srcp = spt[k] & ~0xFFFULL;
                    uint64_t frame = pmm_alloc_frame();

                    if (!frame)
                        return false;

                    pmm_zero_page(frame);
                    k_memcpy((void *)frame, (void *)srcp, PAGING_PAGE);

                    if (!paging_map(dst_cr3, vaddr, frame,
                                    PAGING_FLAG_USER | PAGING_FLAG_WRITABLE))
                        return false;
                }
            }
        }
    }

    (void)dpml4;
    return true;
}

bool paging_map(uint64_t cr3, uint64_t vaddr, uint64_t phys, uint64_t flags)
{
    if (vaddr < PAGING_USER_BASE || (vaddr & 0xFFF) || (phys & 0xFFF))
        return false;

    uint64_t *pml4 = (uint64_t *)cr3;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (pml4[PML4_IDX(vaddr)] & 1)
        pdpt = (uint64_t *)(pml4[PML4_IDX(vaddr)] & ~0xFFFULL);
    else
    {
        pdpt = (uint64_t *)pmm_alloc_frame();
        if (!pdpt)
            return false;
        pmm_zero_page((uintptr_t)pdpt);
        pml4[PML4_IDX(vaddr)] = (uint64_t)pdpt | 0x07;
    }

    if (pdpt[PDPT_IDX(vaddr)] & 1)
        pd = (uint64_t *)(pdpt[PDPT_IDX(vaddr)] & ~0xFFFULL);
    else
    {
        pd = (uint64_t *)pmm_alloc_frame();
        if (!pd)
            return false;
        pmm_zero_page((uintptr_t)pd);
        pdpt[PDPT_IDX(vaddr)] = (uint64_t)pd | 0x07;
    }

    if ((pd[PD_IDX(vaddr)] & 1) && !(pd[PD_IDX(vaddr)] & PAGING_FLAG_LARGE))
    {
        pt = (uint64_t *)(pd[PD_IDX(vaddr)] & ~0xFFFULL);
        if (pt[PT_IDX(vaddr)] & 1)
            return false;  // already mapped
    }
    else if (pd[PD_IDX(vaddr)] & 1)
    {
        return false;  // huge page in the way
    }
    else
    {
        pt = (uint64_t *)pmm_alloc_frame();
        if (!pt)
            return false;
        pmm_zero_page((uintptr_t)pt);
        pd[PD_IDX(vaddr)] = (uint64_t)pt | 0x07;
    }

    pt[PT_IDX(vaddr)] = (phys & ~0xFFFULL) | flags | PAGING_FLAG_PRESENT;
    invlpg(vaddr);
    return true;
}

bool paging_map_large(uint64_t cr3, uint64_t vaddr, uint64_t phys,
                      uint64_t flags)
{
    if (vaddr < PAGING_USER_BASE || (vaddr & (PAGING_HUGE - 1)) ||
        (phys & (PAGING_HUGE - 1)))
        return false;

    uint64_t *pml4 = (uint64_t *)cr3;
    uint64_t *pdpt;
    uint64_t *pd;

    if (pml4[PML4_IDX(vaddr)] & 1)
        pdpt = (uint64_t *)(pml4[PML4_IDX(vaddr)] & ~0xFFFULL);
    else
    {
        pdpt = (uint64_t *)pmm_alloc_frame();
        if (!pdpt)
            return false;
        pmm_zero_page((uintptr_t)pdpt);
        pml4[PML4_IDX(vaddr)] = (uint64_t)pdpt | 0x07;
    }

    if (pdpt[PDPT_IDX(vaddr)] & 1)
        pd = (uint64_t *)(pdpt[PDPT_IDX(vaddr)] & ~0xFFFULL);
    else
    {
        pd = (uint64_t *)pmm_alloc_frame();
        if (!pd)
            return false;
        pmm_zero_page((uintptr_t)pd);
        pdpt[PDPT_IDX(vaddr)] = (uint64_t)pd | 0x07;
    }

    if (pd[PD_IDX(vaddr)] & 1)
        return false;

    pd[PD_IDX(vaddr)] = (phys & ~0x1FFFFFULL) | flags | PAGING_FLAG_LARGE |
                        PAGING_FLAG_PRESENT;
    invlpg(vaddr);
    return true;
}

void paging_unmap(uint64_t cr3, uint64_t vaddr)
{
    if (vaddr < PAGING_USER_BASE)
        return;

    uint64_t *pml4 = (uint64_t *)cr3;

    if (!(pml4[PML4_IDX(vaddr)] & 1))
        return;

    uint64_t *pdpt = (uint64_t *)(pml4[PML4_IDX(vaddr)] & ~0xFFFULL);

    if (!(pdpt[PDPT_IDX(vaddr)] & 1))
        return;

    uint64_t *pd = (uint64_t *)(pdpt[PDPT_IDX(vaddr)] & ~0xFFFULL);

    if (!(pd[PD_IDX(vaddr)] & 1))
        return;

    if (pd[PD_IDX(vaddr)] & PAGING_FLAG_LARGE)
    {
        uint64_t base = pd[PD_IDX(vaddr)] & ~0x1FFFFFULL;
        for (int k = 0; k < 512; k++)
            pmm_free_frame(base + k * PAGING_PAGE);
        pd[PD_IDX(vaddr)] = 0;
        invlpg(vaddr);
        return;
    }

    uint64_t *pt = (uint64_t *)(pd[PD_IDX(vaddr)] & ~0xFFFULL);

    if (pt[PT_IDX(vaddr)] & 1)
    {
        pmm_free_frame(pt[PT_IDX(vaddr)] & ~0xFFFULL);
        pt[PT_IDX(vaddr)] = 0;
        invlpg(vaddr);
    }
}

uint64_t paging_translate(uint64_t cr3, uint64_t vaddr)
{
    uint64_t *pml4 = (uint64_t *)cr3;

    if (!(pml4[PML4_IDX(vaddr)] & 1))
        return 0;

    uint64_t *pdpt = (uint64_t *)(pml4[PML4_IDX(vaddr)] & ~0xFFFULL);

    if (!(pdpt[PDPT_IDX(vaddr)] & 1))
        return 0;

    uint64_t *pd = (uint64_t *)(pdpt[PDPT_IDX(vaddr)] & ~0xFFFULL);

    if (!(pd[PD_IDX(vaddr)] & 1))
        return 0;

    if (pd[PD_IDX(vaddr)] & PAGING_FLAG_LARGE)
        return (pd[PD_IDX(vaddr)] & ~0x1FFFFFULL) + (vaddr & (PAGING_HUGE - 1));

    uint64_t *pt = (uint64_t *)(pd[PD_IDX(vaddr)] & ~0xFFFULL);

    if (!(pt[PT_IDX(vaddr)] & 1))
        return 0;

    return (pt[PT_IDX(vaddr)] & ~0xFFFULL) + (vaddr & 0xFFF);
}
