#ifndef ARCH_X86_64_SMP_LAPIC_H
#define ARCH_X86_64_SMP_LAPIC_H

#include <stdint.h>

// free interrupt vector for the local APIC timer (PIC IRQs use 32-47)
#define LAPIC_TIMER_VECTOR 48

#ifdef __cplusplus
extern "C" {
#endif

// maps the local APIC MMIO region and software-enables the BSP's LAPIC
void lapic_init(void);

// writes the LAPIC EOI register (the local APIC is not part of the 8259
// PIC, so its interrupts need their own end-of-interrupt)
void lapic_eoi(void);

// programs the calling cpu's LAPIC timer to fire ~100 Hz, `count_per_tick`
// ticks per period (from lapic_calibrate()); unmasked + periodic
void lapic_timer_init(uint32_t count_per_tick);

// calibrates the LAPIC timer against the PIT (100 Hz) on the BSP: runs
// the timer for one second and returns the count for a 10 ms period
uint32_t lapic_calibrate(void);

// sends an INIT IPI to a specific local APIC id
void lapic_send_init_ipi(uint8_t lapic_id);

// sends a STARTUP (SIPI) IPI: the target starts executing at 0xVV000
void lapic_send_sipi(uint8_t lapic_id, uint8_t vector);

// reads/writes LAPIC MMIO registers (offset from the LAPIC base)
uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif // ARCH_X86_64_SMP_LAPIC_H
