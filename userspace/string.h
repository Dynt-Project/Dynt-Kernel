/*
 * Userspace libc — string functions (self-contained, no kernel deps)
 */

#ifndef USR_STRING_H
#define USR_STRING_H

#include <stdint.h>
#include <stddef.h>

static inline void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)v;
    return dst;
}

static inline void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++)
    {
        if (pa[i] != pb[i])
            return pa[i] - pb[i];
    }
    return 0;
}

static inline size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (a[i] == 0)
            return 0;
    }
    return 0;
}

static inline char *strcpy(char *dst, const char *src)
{
    size_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

static inline char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    while (i < n && src[i]) { dst[i] = src[i]; i++; }
    while (i < n) { dst[i++] = 0; }
    return dst;
}

static inline char *strcat(char *dst, const char *src)
{
    size_t dl = strlen(dst);
    size_t i = 0;
    while (src[i]) { dst[dl + i] = src[i]; i++; }
    dst[dl + i] = 0;
    return dst;
}

static inline char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : 0;
}

#endif /* USR_STRING_H */