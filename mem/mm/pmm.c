// Physical Memory Manager
//
// Bitmap allocator built from the multiboot2 memory map.  Every 4 KiB
// frame gets one bit: 1 = free, 0 = reserved/in-use.  The bitmap itself
// lives in static BSS so it needs no memory before it is ready.

#include "pmm.h"

#include "../../arch/x86_64/boot/common/bootinf.h"
#include "../../arch/x86_64/cpu/spinlock.h"
#include "../lib/memory.h"

#define PMM_BITMAP_BYTES (PMM_MAX_PHYS / PMM_FRAME_SIZE / 8)  // 2 MiB
#define PMM_MAX_FRAMES (PMM_BITMAP_BYTES * 8)

// multiboot2 mmap entry. MUST be 24 bytes (addr, len, type, reserved)
// so iterating the map with our own stride lines up with grub's
// entry_size — a 20-byte struct here makes every entry past the first
// read garbage, which empties the whole free-frame pool.
struct __attribute__((packed)) pmm_mmap_entry
{
    uint64_t addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

extern uint8_t kernel_end[];

static uint8_t bitmap[PMM_BITMAP_BYTES];
static uint64_t free_frames;
static uint64_t total_frames;
static uint64_t total_memory_bytes;

// the bitmap + hint are shared across cpus (concurrent exec/fork on every
// core allocates frames), so allocation and freeing must be serialized
static spinlock_t pmm_lock;

static inline bool frame_is_free(uint64_t idx)
{
    return (bitmap[idx >> 3] >> (idx & 7)) & 1;
}

static inline void frame_mark_free(uint64_t idx)
{
    bitmap[idx >> 3] |= (1u << (idx & 7));
}

static inline void frame_mark_used(uint64_t idx)
{
    bitmap[idx >> 3] &= ~(1u << (idx & 7));
}

static uint64_t frame_index(uintptr_t phys)
{
    return (uint64_t)(phys >> 12);
}

// marks [start, end) (byte addresses) as reserved
static void reserve_range(uintptr_t start, uintptr_t end)
{
    if (end <= start)
        return;

    uint64_t f0 = start >> 12;
    uint64_t f1 = (end + PMM_FRAME_SIZE - 1) >> 12;

    if (f0 >= PMM_MAX_FRAMES)
        return;
    if (f1 > PMM_MAX_FRAMES)
        f1 = PMM_MAX_FRAMES;

    for (uint64_t f = f0; f < f1; f++)
    {
        if (frame_is_free(f))
            free_frames--;
        frame_mark_used(f);
    }
}

void pmm_init(void)
{
    const struct pmm_mmap_entry *entries =
        (const struct pmm_mmap_entry *)g_bootinfo.memory_map;
    uint64_t entry_count = g_bootinfo.memory_map_entries;

    // bitmap is BSS = all zeroed = all reserved
    free_frames = 0;
    total_frames = 0;
    total_memory_bytes = 0;

    // 1) mark all usable RAM free
    for (uint64_t i = 0; i < entry_count; i++)
    {
        uint64_t start = entries[i].addr;
        uint64_t end = start + entries[i].length;

        if (start >= PMM_MAX_PHYS)
            continue;
        if (end > PMM_MAX_PHYS)
            end = PMM_MAX_PHYS;

        if (entries[i].type == 1)  // type 1 = usable RAM
        {
            uint64_t f0 = start >> 12;
            uint64_t f1 = (end + PMM_FRAME_SIZE - 1) >> 12;

            for (uint64_t f = f0; f < f1; f++)
            {
                if (f < PMM_MAX_FRAMES && !frame_is_free(f))
                {
                    frame_mark_free(f);
                    free_frames++;
                }
            }

            total_memory_bytes += end - start;
        }
    }

    // 2) reserve everything the kernel and firmware already occupy
    reserve_range(0, (uintptr_t)kernel_end);                    // kernel image + boot tables
    reserve_range((uintptr_t)bitmap, (uintptr_t)bitmap + sizeof(bitmap));  // ourselves

    for (uint64_t i = 0; i < entry_count; i++)
    {
        if (entries[i].type != 1)  // firmware, MMIO, holes
            reserve_range((uintptr_t)entries[i].addr,
                          (uintptr_t)(entries[i].addr + entries[i].length));
    }

    // 3) framebuffer handed out by grub (may sit inside a reserved entry)
    if (g_bootinfo.framebuffer)
    {
        reserve_range((uintptr_t)g_bootinfo.framebuffer,
                      (uintptr_t)g_bootinfo.framebuffer +
                      (uint64_t)g_bootinfo.framebuffer_pitch *
                      (uint64_t)g_bootinfo.framebuffer_height);
    }

    total_frames = (g_bootinfo.memory_map_entries && total_memory_bytes)
                       ? (total_memory_bytes / PMM_FRAME_SIZE)
                       : 0;
}

uintptr_t pmm_alloc_frame(void)
{
    static uint64_t hint;
    uint64_t flags = spinlock_acquire_irq(&pmm_lock);

    for (uint64_t f = hint; f < PMM_MAX_FRAMES; f++)
    {
        if (frame_is_free(f))
        {
            frame_mark_used(f);
            free_frames--;
            hint = f + 1;
            spinlock_release_irq(&pmm_lock, flags);
            return (uintptr_t)(f << 12);
        }
    }

    for (uint64_t f = 0; f < hint; f++)
    {
        if (frame_is_free(f))
        {
            frame_mark_used(f);
            free_frames--;
            hint = f + 1;
            spinlock_release_irq(&pmm_lock, flags);
            return (uintptr_t)(f << 12);
        }
    }

    spinlock_release_irq(&pmm_lock, flags);
    return 0;
}

uintptr_t pmm_alloc_contig(uint64_t count)
{
    if (count == 0)
        return 0;

    uint64_t flags = spinlock_acquire_irq(&pmm_lock);
    uint64_t run = 0;
    uint64_t start = 0;

    for (uint64_t f = 0; f < PMM_MAX_FRAMES; f++)
    {
        if (frame_is_free(f))
        {
            if (run == 0)
                start = f;
            run++;
            if (run == count)
            {
                for (uint64_t i = 0; i < count; i++)
                {
                    frame_mark_used(start + i);
                    free_frames--;
                }
                spinlock_release_irq(&pmm_lock, flags);
                return (uintptr_t)(start << 12);
            }
        }
        else
        {
            run = 0;
        }
    }

    spinlock_release_irq(&pmm_lock, flags);
    return 0;
}

void pmm_free_frame(uintptr_t phys)
{
    uint64_t flags = spinlock_acquire_irq(&pmm_lock);
    uint64_t f = frame_index(phys);

    if (f < PMM_MAX_FRAMES && !frame_is_free(f))
    {
        frame_mark_free(f);
        free_frames++;
    }

    spinlock_release_irq(&pmm_lock, flags);
}

void pmm_free_range(uintptr_t phys, uint64_t count)
{
    for (uint64_t i = 0; i < count; i++)
        pmm_free_frame(phys + i * PMM_FRAME_SIZE);
}

uint64_t pmm_total_frames(void)
{
    return total_frames;
}

uint64_t pmm_free_frames(void)
{
    return free_frames;
}

uint64_t pmm_total_memory(void)
{
    return total_memory_bytes;
}

void pmm_zero_page(uintptr_t phys)
{
    if (phys)
        k_memset((void *)phys, 0, PMM_FRAME_SIZE);
}
