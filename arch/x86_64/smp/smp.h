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

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_SMP_H
