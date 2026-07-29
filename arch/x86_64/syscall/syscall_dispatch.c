// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

//   Deliberately minimal — two syscalls, just enough to prove the whole
//   SYSCALL/SYSRET path works end to end. Both are placeholders for real
//   subsystems that don't exist yet.

//   you can add more SYSCALLS when ever one is needed.
//   NOTE: keep in mind the the Debug messages print in COM1 serial.

#include "syscall.h"
#include "../io/serial.h"
#include "../cpu/cpu.h"

void syscall_dispatch(syscall_regs_t *regs) {
    switch (regs->rax) {

        case SYS_WRITE: {
            const char *str = (const char *)regs->rdi;
            serial_write(str);
            regs->rax = 0;
            break;
        
        }

        case SYS_EXIT:
           serial_write("\n[syscall] SYS_EXIT, System Halting\n");
           cli();
           for (;;) {
               hlt();
           }
           break;

        default:
            serial_write("\n[syscall] UNKNOW, unknown syscall number.\n");
            regs->rax = (uint64_t)-1;
            break;
    }
}
