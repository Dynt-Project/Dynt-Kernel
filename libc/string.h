#ifndef LIBC_STRING_H
#define LIBC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memset(void *dst, int value, size_t size);
void *memcpy(void *dst, const void *src, size_t size);
int memcmp(const void *a, const void *b, size_t size);
size_t strlen(const char *str);
int strncmp(const char *a, const char *b, size_t size);
char *strncpy(char *dst, const char *src, size_t size);

#ifdef __cplusplus
}
#endif

#endif // LIBC_STRING_H
