#ifndef MEM_MM_PMM_H
#define MEM_MM_PMM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// maximum physical address the bitmap covers (64 GiB)
#define PMM_MAX_PHYS (64ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_FRAME_SIZE 4096ULL

// parses the multiboot memory map and builds the frame bitmap.
// must run before kheap_init() so the heap can grow from real pages.
void pmm_init(void);

// allocates one physical frame, returns its physical address or 0
uintptr_t pmm_alloc_frame(void);

// allocates a physically contiguous run of frames, returns start or 0
uintptr_t pmm_alloc_contig(uint64_t count);

// frees one frame
void pmm_free_frame(uintptr_t phys);

// frees a run of frames
void pmm_free_range(uintptr_t phys, uint64_t count);

// stats
uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);

// total usable RAM in bytes
uint64_t pmm_total_memory(void);

// zeroes a whole physical page
void pmm_zero_page(uintptr_t phys);

#ifdef __cplusplus
}
#endif

#endif // MEM_MM_PMM_H
