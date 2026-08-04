#ifndef MEM_MM_PAGING_H
#define MEM_MM_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// flags for paging_map / paging_map_large
#define PAGING_FLAG_PRESENT 0x001
#define PAGING_FLAG_WRITABLE 0x002
#define PAGING_FLAG_USER 0x004
#define PAGING_FLAG_LARGE 0x080
#define PAGING_FLAG_NOEXEC (1ULL << 63)

// the kernel is identity mapped in low memory; user mappings live at
// 1 GiB and up so they never collide with the kernel's address space
#define PAGING_USER_BASE 0x40000000ULL

// uses the current cr3 as the kernel root (boot tables)
void paging_init(void);

uint64_t paging_kernel_cr3(void);

// maps the local APIC MMIO region (supervisor huge page) in the kernel
// root; every paging_new_address_space() clones the mapping afterwards
void paging_map_lapic(uint64_t phys);

// creates a fresh address space: kernel identity (supervisor) + empty
// user half. returns the new pml4 physical address or 0
uint64_t paging_new_address_space(void);

// frees a whole user address space (table pages AND all mapped frames)
void paging_free_address_space(uint64_t cr3);

// copies every user mapping from src_cr3 into dst_cr3 (new frames,
// page contents copied). used by fork(). returns false on OOM.
bool paging_clone_user_space(uint64_t src_cr3, uint64_t dst_cr3);

// maps a single 4 KiB page at vaddr >= PAGING_USER_BASE
bool paging_map(uint64_t cr3, uint64_t vaddr, uint64_t phys, uint64_t flags);

// maps a 2 MiB huge page (vaddr and phys must be 2 MiB aligned)
bool paging_map_large(uint64_t cr3, uint64_t vaddr, uint64_t phys,
                      uint64_t flags);

// unmaps and frees the backing frames of a 4 KiB or 2 MiB mapping
void paging_unmap(uint64_t cr3, uint64_t vaddr);

// translates a virtual address to physical, 0 if not mapped
uint64_t paging_translate(uint64_t cr3, uint64_t vaddr);

#ifdef __cplusplus
}
#endif

#endif // MEM_MM_PAGING_H
