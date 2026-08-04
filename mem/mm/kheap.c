// Dynamic kernel heap
//
// First-fit free-list allocator with splitting and coalescing.  The
// region grows on demand by pulling physically contiguous chunks from
// the physical memory manager (pmm), so it is no longer a fixed-size
// bump allocator.

#include "kheap.h"

#include "pmm.h"
#include "../lib/memory.h"

#define HEAP_MAGIC 0x484541504B44414EULL  // "NEDPKHEAP"
#define HEAP_HEADER 48
#define HEAP_MIN_FREE (HEAP_HEADER + 16)
#define HEAP_GROW_CHUNK (4ULL * 1024 * 1024)

typedef struct heap_block
{
    uint64_t magic;
    uint64_t size;    // total span of this block, header included
    uint64_t pad;     // alignment padding between header and payload
    uint64_t flags;   // bit 0 = free
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *heap_head;
static uint64_t allocated_bytes;

static inline bool block_free(const heap_block_t *b)
{
    return (b->flags & 1) != 0;
}

static inline void block_set_free(heap_block_t *b, bool free)
{
    if (free)
        b->flags |= 1;
    else
        b->flags &= ~1ULL;
}

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
    if (alignment == 0)
        alignment = 16;

    uintptr_t mask = (uintptr_t)alignment - 1;
    return (value + mask) & ~mask;
}

// inserts a free block at the correct address-sorted position
static void insert_free_block(heap_block_t *block)
{
    block_set_free(block, true);
    block->magic = HEAP_MAGIC;

    heap_block_t *cur = heap_head;

    if (!cur || (uintptr_t)block < (uintptr_t)cur)
    {
        block->next = cur;
        block->prev = 0;
        if (cur)
            cur->prev = block;
        heap_head = block;
        return;
    }

    while (cur->next && (uintptr_t)cur->next < (uintptr_t)block)
        cur = cur->next;

    block->next = cur->next;
    block->prev = cur;
    if (cur->next)
        cur->next->prev = block;
    cur->next = block;
}

// adds a raw memory region to the heap as one free block
static void add_region(void *base, size_t size)
{
    if (size < HEAP_MIN_FREE)
        return;

    heap_block_t *block = (heap_block_t *)base;
    block->size = size - HEAP_HEADER;
    block->pad = 0;
    insert_free_block(block);
}

static bool grow_heap(void)
{
    size_t chunk = HEAP_GROW_CHUNK;

    while (chunk >= 64 * 1024)
    {
        uintptr_t mem = pmm_alloc_contig(chunk / 4096);

        if (mem)
        {
            add_region((void *)mem, chunk);
            return true;
        }

        chunk /= 2;
    }

    return false;
}

static void unlink(heap_block_t *block)
{
    if (block->prev)
        block->prev->next = block->next;
    else
        heap_head = block->next;

    if (block->next)
        block->next->prev = block->prev;
}

static void coalesce(heap_block_t *block)
{
    // merge with next if adjacent
    while (block->next &&
           block_free(block->next) &&
           (uintptr_t)block->next == (uintptr_t)block + block->size)
    {
        heap_block_t *next = block->next;
        block->size += next->size;
        unlink(next);
    }

    // merge with prev if adjacent
    if (block->prev &&
        block_free(block->prev) &&
        (uintptr_t)block == (uintptr_t)block->prev + block->prev->size)
    {
        heap_block_t *prev = block->prev;
        prev->size += block->size;
        unlink(block);
    }
}

void kheap_init(void)
{
    heap_head = 0;
    allocated_bytes = 0;

    // grab a first chunk so allocations work immediately
    grow_heap();
}

void *kheap_alloc(size_t size, size_t alignment)
{
    if (size == 0)
        return 0;

    if (alignment == 0)
        alignment = 16;

    size = (size + 15) & ~(size_t)15;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        for (heap_block_t *block = heap_head; block; block = block->next)
        {
            if (!block_free(block))
                continue;

            uintptr_t payload = (uintptr_t)block + HEAP_HEADER;
            uintptr_t aligned = align_up(payload, alignment);
            size_t pad = (size_t)(aligned - payload);

            if (block->size < HEAP_HEADER + pad + size)
                continue;

            // split the tail into a new free block if it can hold one
            size_t used = HEAP_HEADER + pad + size;
            size_t remaining = block->size - used;

            if (remaining >= HEAP_MIN_FREE)
            {
                heap_block_t *tail = (heap_block_t *)((uintptr_t)block + used);
                tail->size = remaining - HEAP_HEADER;
                tail->pad = 0;
                tail->magic = HEAP_MAGIC;
                block_set_free(tail, true);
                tail->next = block->next;
                tail->prev = block;
                if (block->next)
                    block->next->prev = tail;
                block->next = tail;
                block->size = used;
            }

            block->pad = pad;
            block_set_free(block, false);
            allocated_bytes += used;

            void *result = (void *)aligned;
            k_memset(result, 0, size);
            return result;
        }

        if (!grow_heap())
            break;
    }

    return 0;
}

void kheap_free(void *ptr)
{
    if (!ptr)
        return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - HEAP_HEADER);
    block = (heap_block_t *)((uint8_t *)block - block->pad);

    if (block->magic != HEAP_MAGIC)
        return;

    if (allocated_bytes >= block->size)
        allocated_bytes -= block->size;

    block_set_free(block, true);
    coalesce(block);
}

uint64_t kheap_bytes_used(void)
{
    return allocated_bytes;
}
