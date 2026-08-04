// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

// Global Descriptor Table functions
//  check the functions in [gdt.h](<DyntKernel/arch/x86_64/gdt.h>)

#include "gdt.h"

#define GDT_ENTRY_COUNT 7

static gdt_entry_t gdt[GDT_ENTRY_COUNT];
static gdt_ptr_t gdtr;
static tss_t tss;

extern "C" void gdt_flush(uint64_t gdtr_addr);
extern "C" void tss_flush(void);

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity) {
    gdt[index].base_low = (uint8_t)((base & 0xFFFF));
    gdt[index].base_mid = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[index].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= (granularity & 0xF0);
    gdt[index].access = access;
}

static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    gdt_tss_entry_t *desc = (gdt_tss_entry_t *)&gdt[index];
    desc->limit_low = (uint16_t)(limit & 0xFFFF);
    desc->base_low = (uint16_t)(base & 0xFFFF);
    desc->base_mid = (uint8_t)((base >> 16) & 0xFF);
    desc->access = 0x89;
    desc->granularity = (uint8_t)((limit >> 16) & 0x0F);
    desc->base_high = (uint8_t)((base >> 24) & 0xFF);
    desc->base_upper = (uint32_t)(base >> 32);
    desc->reserved = 0;
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;

    gdt_set_entry(0, 0, 0, 0, 0);
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);
    /* user data first (0x18), user code right after (0x20): sysretq
       computes CS = STAR[63:48]+16 and SS = STAR[63:48]+8, so the user
       code selector must be exactly 8 above the user data selector. */
    gdt_set_entry(3, 0, 0xFFFFF, 0xF2, 0xC0);
    gdt_set_entry(4, 0, 0xFFFFF, 0xFA, 0xA0);

    for (uint8_t *p = (uint8_t *)&tss; p < (uint8_t *)&tss + sizeof(tss); p++) 
        *p = 0;
    tss.iomap_base = sizeof(tss_t);

    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss_t) - 1);

    gdt_flush((uint64_t)&gdtr);
    tss_flush();
}
