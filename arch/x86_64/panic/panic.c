// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

// program for [panic.h](<DyntKernel/arch/x86_64/panic.h>)

#include "panic.h"
#include "../inter/isr.h"
#include "../io/serial.h"
#include "../cpu/cpu.h"
#include "../cpu/percpu.h"
#include "../cpu/control_regs.h"
#include "../../mem/mm/paging.h"
#include "../../process/process.h"

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[19];

    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = '\0';

    for (int i = 0; i < 16; i++) {
        buf[17 - i] = digits[value & 0xF];
        value >>= 4;
    }

    serial_write(buf);
}

static inline uint64_t read_raw_rsp(void)
{
    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

[[noreturn]] void panic(const char *message, registers_t *regs) {
    cli();

    serial_write("\nKERNEL PANIC!: \n");
    serial_write("Oh noo, seems like DyntKernel experienced a crash.\n");
    serial_write("restart is needed. ERROR:\n");
    serial_write(message);
    serial_write("\n");
    
    if (regs) {
        serial_write("cpu=");
        print_hex64(percpu_current()->cpu_index);
        process_t *pc = process_current();
        if (pc)
        {
            serial_write("  pid=");
            print_hex64(pc->pid);
        }
        serial_write("  vector="); print_hex64(regs->int_no);
        serial_write("  err="); print_hex64(regs->err_code);
        serial_write("\n");

        if (regs->int_no == 14) {
            serial_write("cr2="); print_hex64(read_cr2());
            serial_write("\n");
        }

        serial_write("rip="); print_hex64(regs->rip);
        serial_write("  cs="); print_hex64(regs->cs);
        serial_write("  rflags="); print_hex64(regs->rflags);
        serial_write("\n");

        serial_write("rax="); print_hex64(regs->rax);
        serial_write("  rbx="); print_hex64(regs->rbx);
        serial_write("  rcx="); print_hex64(regs->rcx);
        serial_write("  rdx="); print_hex64(regs->rdx);
        serial_write("\n");

        serial_write("rsi="); print_hex64(regs->rsi);
        serial_write("  rdi="); print_hex64(regs->rdi);
        serial_write("  rbp="); print_hex64(regs->rbp);
        serial_write("  rsp="); print_hex64(regs->user_rsp);
        serial_write("\n");

        serial_write("ss="); print_hex64(regs->ss);
        serial_write("  cr3="); print_hex64(read_cr3());
        serial_write("  rawrsp="); print_hex64(read_raw_rsp());
        serial_write("\n");

        serial_write("stack: ");
        const uint64_t *sp = (const uint64_t *)regs->user_rsp;
        for (int i = 0; i < 64; i++) {
            if (!paging_translate(read_cr3(), regs->user_rsp + i * 8))
            {
                serial_write("(unmapped)");
                break;
            }
            print_hex64(sp[i]);
            serial_write(" ");
        }
        serial_write("\n");

        serial_write("instr: ");
        const uint8_t *ip = (const uint8_t *)regs->rip;
        for (int i = 0; i < 16; i++) {
            if (!paging_translate(read_cr3(), regs->rip + i))
            {
                serial_write("(unmapped)");
                break;
            }
            static const char digits[] = "0123456789ABCDEF";
            char b[3];
            b[0] = digits[ip[i] >> 4];
            b[1] = digits[ip[i] & 0xF];
            b[2] = '\0';
            serial_write(b);
        }
        serial_write("\n");
    }

    serial_write("system halted. \n");

    for (;;) {
        hlt();
    }
}
