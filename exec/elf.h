#ifndef EXEC_ELF_H
#define EXEC_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct elf_image
{
    uint64_t entry;
    uint64_t low_address;
    uint64_t high_address;
    uint64_t segment_count;
} elf_image_t;

#ifdef __cplusplus
extern "C" {
#endif

bool elf64_validate(const void *image, size_t size);
bool elf64_load_image(const void *image, size_t size, elf_image_t *out);
[[noreturn]] void elf64_enter_ring3(const elf_image_t *image, uint64_t user_stack);

#ifdef __cplusplus
}
#endif

#endif // EXEC_ELF_H
