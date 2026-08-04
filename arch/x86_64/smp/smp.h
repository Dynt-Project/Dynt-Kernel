#ifndef ARCH_X86_64_SMP_H
#define ARCH_X86_64_SMP_H

#include <stdbool.h>
#include <stdint.h>

#define SMP_MAX_BOOT_CPUS 256

typedef struct smp_cpu
{
    uint8_t processor_id;
    uint8_t lapic_id;
    bool enabled;
    bool bootstrap;
} smp_cpu_t;

#ifdef __cplusplus
extern "C" {
#endif

void smp_init(void);
uint32_t smp_cpu_count(void);
const smp_cpu_t *smp_cpu_at(uint32_t index);
uintptr_t smp_lapic_base(void);

// boot of the application processors: copies the trampoline to low
// memory, allocates per-cpu stacks/GDT/TSS and INIT-SIPIs every enabled
// non-bootstrap cpu.  `lapic_timer_count` is the BSP-calibrated LAPIC
// timer period the APs program as their own 100 Hz clock.
void smp_start_aps(uint32_t lapic_timer_count);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_SMP_H
