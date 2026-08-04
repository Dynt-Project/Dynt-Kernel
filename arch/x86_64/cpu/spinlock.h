// Written by [@saphhic](https://github.com/saphhic)
// Date: 04 August 2026

//   Tiny ticket-free spinlock plus interrupt-safe variants.  SMP makes
//   the scheduler queues shared state, so every queue operation takes a
//   lock.  The interrupt-safe acquire masks local IRQs while holding the
//   lock so a timer tick on the SAME cpu can't re-enter the lock and
//   spin forever against itself.

#ifndef ARCH_X86_64_SPINLOCK_H
#define ARCH_X86_64_SPINLOCK_H

#include <stdint.h>

#include "cpu.h"

typedef struct
{
    volatile uint32_t lock;
} spinlock_t;

static inline void spinlock_init(spinlock_t *s)
{
    s->lock = 0;
}

static inline void spinlock_acquire(spinlock_t *s)
{
    while (__atomic_test_and_set(&s->lock, __ATOMIC_ACQUIRE))
        pause_cpu();
}

static inline void spinlock_release(spinlock_t *s)
{
    __atomic_clear(&s->lock, __ATOMIC_RELEASE);
}

static inline uint64_t cpu_save_flags(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static inline void cpu_restore_flags(uint64_t flags)
{
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "memory");
}

// acquire with local IRQs masked; restores them on release
static inline uint64_t spinlock_acquire_irq(spinlock_t *s)
{
    uint64_t flags = cpu_save_flags();
    cli();
    spinlock_acquire(s);
    return flags;
}

static inline void spinlock_release_irq(spinlock_t *s, uint64_t flags)
{
    spinlock_release(s);
    cpu_restore_flags(flags);
}

#endif // ARCH_X86_64_SPINLOCK_H
