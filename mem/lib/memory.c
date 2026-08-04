#include "memory.h"

void *k_memset(void *dst, int value, size_t size)
{
    uint8_t *d = (uint8_t *)dst;

    for (size_t i = 0; i < size; i++)
        d[i] = (uint8_t)value;

    return dst;
}

void *k_memcpy(void *dst, const void *src, size_t size)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    for (size_t i = 0; i < size; i++)
        d[i] = s[i];

    return dst;
}

int k_memcmp(const void *a, const void *b, size_t size)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;

    for (size_t i = 0; i < size; i++)
    {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }

    return 0;
}

size_t k_strlen(const char *str)
{
    size_t len = 0;

    if (!str)
        return 0;

    while (str[len])
        len++;

    return len;
}

int k_strncmp(const char *a, const char *b, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];

        if (ca != cb || ca == 0 || cb == 0)
            return (int)ca - (int)cb;
    }

    return 0;
}

void k_strncpy(char *dst, const char *src, size_t size)
{
    if (size == 0)
        return;

    size_t i = 0;

    for (; i + 1 < size && src && src[i]; i++)
        dst[i] = src[i];

    for (; i < size; i++)
        dst[i] = 0;
}
