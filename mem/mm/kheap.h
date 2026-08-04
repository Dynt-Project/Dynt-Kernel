#ifndef MEM_MM_KHEAP_H
#define MEM_MM_KHEAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void kheap_init(void);
void *kheap_alloc(size_t size, size_t alignment);
void kheap_free(void *ptr);
uint64_t kheap_bytes_used(void);

#ifdef __cplusplus
}
#endif

#endif // MEM_MM_KHEAP_H
