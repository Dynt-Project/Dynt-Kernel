// Written by [@saphhic](https://github.com/saphhic)  
// Date: 26 July 2026

//   The only way into ring3: building a fake interrupt-return frame by hand
//   and iretq into it. There's no other instruction that lets you drop
//   privilege level on x86_64 (SYSRET requires already being in an
//   established syscall context, which doesn't apply the first time).

#ifndef ARCH_X86_64_USERMODE_H
#define ARCH_X86_64_USERMODE_H

#include <stdint.h>

#include "../inter/isr.h"

#ifdef __cplusplus
extern "C" {
#endif

[[noreturn]] void enter_usermode(uint64_t entry, uint64_t user_stack);

/* iretq into an explicit ring3 frame (used by exec/exit to resume a
   previously saved user context).  Never returns. */
[[noreturn]] void usermode_resume(uint64_t rip, uint64_t rsp, uint64_t rflags);

/* restores a full saved process context (GP registers + iretq frame)
   and enters ring3.  Used by the scheduler on process switch. */
[[noreturn]] void usermode_resume_full(const registers_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_USERMODE_H