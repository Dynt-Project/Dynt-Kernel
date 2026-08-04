#ifndef MEM_LIB_MEMORY_H
#define MEM_LIB_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *k_memset(void *dst, int value, size_t size);
void *k_memcpy(void *dst, const void *src, size_t size);
int k_memcmp(const void *a, const void *b, size_t size);
size_t k_strlen(const char *str);
int k_strncmp(const char *a, const char *b, size_t size);
void k_strncpy(char *dst, const char *src, size_t size);

static inline uint16_t k_le16(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t k_le32(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline uint64_t k_le64(const void *ptr)
{
    const uint8_t *p = (const uint8_t *)ptr;
    return (uint64_t)k_le32(p) | ((uint64_t)k_le32(p + 4) << 32);
}

#ifdef __cplusplus
}
#endif

#endif // MEM_LIB_MEMORY_H
