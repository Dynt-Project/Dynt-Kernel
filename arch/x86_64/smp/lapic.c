// Local APIC driver.
//
// Every CPU has a local APIC.  The BSP's is used to calibrate the APIC
// timer against the PIT and to send the INIT/SIPI IPIs that start the
// application processors, while each AP uses its own LAPIC timer as its
// 100 Hz scheduler clock (the legacy PIT only delivers IRQ0 to the BSP).
//
// The LAPIC MMIO region is mapped into the kernel and every process
// address space (see paging_map_lapic) because a timer interrupt can fire
// while a user page table is loaded.

#include "lapic.h"

#include "../cpu/cpu.h"
#include "../cpu/msr.h"
#include "../inter/isr.h"
#include "../../../mem/mm/paging.h"
#include "../../../scheduler/scheduler.h"
#include "../../../init/debug.h"

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_ENABLE 0x800

// register offsets from the LAPIC base
#define LAPIC_ID        0x20
#define LAPIC_VER       0x30
#define LAPIC_EOI       0xB0
#define LAPIC_SVR       0xF0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_INIT    0x380
#define LAPIC_TIMER_CURRENT 0x390
#define LAPIC_TIMER_DIV     0x3E0

#define LAPIC_SVR_ENABLE (1 << 8)

// LVT timer bits: 16 = mask, 17 = periodic mode, low 8 bits = vector
#define LVT_TIMER_MASKED   0x10000
#define LVT_TIMER_PERIODIC 0x20000

#define LAPIC_DIVIDE_1 0x0B  // divide config register: divide by 1

// ICR0 bits: 8-10 = delivery mode, 12 = delivery status, 14 = assert
#define LAPIC_DELIVERY_INIT    0x500
#define LAPIC_DELIVERY_STARTUP 0x600
#define LAPIC_INT_ASSERT       0x4000
#define LAPIC_INT_LEVELTRIG    0x8000

static uintptr_t lapic_mmio;

uint32_t lapic_read(uint32_t reg)
{
    return *(volatile uint32_t *)(lapic_mmio + reg);
}

void lapic_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(lapic_mmio + reg) = val;
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_EOI, 0);
}

void lapic_init(void)
{
    // authoritative LAPIC base is in the IA32_APIC_BASE MSR; map the
    // MMIO region so the timer and IPI code work under any cr3
    lapic_mmio = rdmsr(IA32_APIC_BASE_MSR) & 0xFFFFF000ULL;
    paging_map_lapic(lapic_mmio);

    // software-enable the local APIC (SVR bit 8), spurious vector 0xFF
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE | 0xFF);

    debug_printf("[boot] lapic id=0x%x base=0x%lx\n",
                 (unsigned)lapic_read(LAPIC_ID),
                 (unsigned long)lapic_mmio);
}

uint32_t lapic_calibrate(void)
{
    // masked one-shot with the maximum count: it just counts down while
    // we measure a full second of PIT ticks
    lapic_write(LAPIC_LVT_TIMER, LVT_TIMER_MASKED | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_DIV, LAPIC_DIVIDE_1);
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);

    uint64_t start = scheduler_ticks();
    while (scheduler_ticks() - start < 100)
        pause_cpu();

    uint64_t elapsed = (uint64_t)0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURRENT);

    // stop the calibration timer
    lapic_write(LAPIC_LVT_TIMER, LVT_TIMER_MASKED | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_TIMER_INIT, 0);

    uint32_t per_tick = (uint32_t)(elapsed / 100);

    debug_printf("[boot] lapic timer: %lu counts/s -> %u per 10ms\n",
                 (unsigned long)elapsed, (unsigned)per_tick);
    return per_tick;
}

void lapic_timer_init(uint32_t count_per_tick)
{
    // The INIT/SIPI sequence resets the local APIC to software-disabled
    // (SVR bit 8 clear); without this no interrupt ever reaches the CPU.
    // The BSP already did it in lapic_init(), but each AP must too.
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE | 0xFF);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_VECTOR | LVT_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_DIV, LAPIC_DIVIDE_1);
    lapic_write(LAPIC_TIMER_INIT, count_per_tick);
}

void lapic_send_init_ipi(uint8_t lapic_id)
{
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)lapic_id << 24);
    // INIT level assert
    lapic_write(LAPIC_ICR_LOW,
                LAPIC_DELIVERY_INIT | LAPIC_INT_ASSERT | LAPIC_INT_LEVELTRIG);

    while (lapic_read(LAPIC_ICR_LOW) & (1 << 12))
        pause_cpu();
}

void lapic_send_sipi(uint8_t lapic_id, uint8_t vector)
{
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)lapic_id << 24);
    lapic_write(LAPIC_ICR_LOW, LAPIC_DELIVERY_STARTUP | vector);

    while (lapic_read(LAPIC_ICR_LOW) & (1 << 12))
        pause_cpu();
}

// the LAPIC timer interrupts route through their own stub/EOI, not the
// legacy PIC handler
extern "C" void lapic_handler(registers_t *regs)
{
    scheduler_timer_tick(regs);
    lapic_eoi();
}
