#include "kheap.h"

#include "../lib/memory.h"

#define EARLY_HEAP_SIZE (16ULL * 1024ULL * 1024ULL)

extern uint8_t kernel_end[];

static uintptr_t heap_current;
static uintptr_t heap_end;
static uintptr_t heap_start;

static uintptr_t align_up(uintptr_t value, size_t alignment)
{
    if (alignment == 0)
        alignment = sizeof(uintptr_t);

    uintptr_t mask = (uintptr_t)alignment - 1;
    return (value + mask) & ~mask;
}

void kheap_init(void)
{
    heap_start = align_up((uintptr_t)kernel_end, 4096);
    heap_current = heap_start;
    heap_end = heap_start + EARLY_HEAP_SIZE;
}

void *kheap_alloc(size_t size, size_t alignment)
{
    uintptr_t ptr = align_up(heap_current, alignment);
    uintptr_t next = ptr + size;

    if (size == 0 || next < ptr || next > heap_end)
        return 0;

    heap_current = next;
    k_memset((void *)ptr, 0, size);
    return (void *)ptr;
}

uint64_t kheap_bytes_used(void)
{
    return heap_current >= heap_start ? heap_current - heap_start : 0;
}

void kheap_free(void *ptr)
{
    (void)ptr;
}
