#include "string.h"

#include "../mem/lib/memory.h"

void *memset(void *dst, int value, size_t size)
{
    return k_memset(dst, value, size);
}

void *memcpy(void *dst, const void *src, size_t size)
{
    return k_memcpy(dst, src, size);
}

int memcmp(const void *a, const void *b, size_t size)
{
    return k_memcmp(a, b, size);
}

size_t strlen(const char *str)
{
    return k_strlen(str);
}

int strncmp(const char *a, const char *b, size_t size)
{
    return k_strncmp(a, b, size);
}

char *strncpy(char *dst, const char *src, size_t size)
{
    k_strncpy(dst, src, size);
    return dst;
}
