#ifndef EXEC_ELF_H
#define EXEC_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ELF_SEGMENT_MAX 8

typedef struct elf_segment
{
    uint64_t vaddr;
    uint64_t offset;
    uint64_t filesz;
    uint64_t memsz;
} elf_segment_t;

typedef struct elf_dynamic
{
    bool present;
    uint64_t rela_offset;  // file offset of the DT_RELA table
    uint64_t rela_size;    // DT_RELASZ: bytes of relocations
    uint64_t rela_ent;     // DT_RELAENT: size of one entry
    uint64_t rela_count;   // DT_RELACOUNT: leading RELATIVE relocs
    uint64_t symtab_offset; // file offset of DT_SYMTAB
    uint64_t strtab_offset; // file offset of DT_STRTAB
    uint64_t sym_ent;      // DT_SYMENT: size of one symbol
} elf_dynamic_t;

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

// fills out->entry with the entry point (before PIE base is applied),
// sets *out_pie when the image is a position independent executable,
// and stores every PT_LOAD segment into segs
bool elf64_parse_segments(const void *image, size_t size,
                          elf_segment_t *segs, int max_segs,
                          int *out_count, uint64_t *out_entry,
                          bool *out_pie);

// finds the PT_DYNAMIC program header and exposes the relocation table
bool elf64_parse_dynamic(const void *image, size_t size,
                         elf_dynamic_t *out);

// translates a virtual address inside the image to its file offset
bool elf64_vaddr_to_offset(const void *image, size_t size,
                           uint64_t vaddr, uint64_t *out_offset);

bool elf64_load_image(const void *image, size_t size, elf_image_t *out);
[[noreturn]] void elf64_enter_ring3(const elf_image_t *image, uint64_t user_stack);

#ifdef __cplusplus
}
#endif

#endif // EXEC_ELF_H
