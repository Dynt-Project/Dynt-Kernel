// PIT (8254) driver: periodic irq0 for the scheduler.

#include "pit.h"

#include "../../../arch/x86_64/io/ports.h"
#include "../../../arch/x86_64/io/io.h"
#include "../../../arch/x86_64/inter/isr.h"
#include "../../../arch/x86_64/inter/pic.h"
#include "../../../scheduler/scheduler.h"

#define PIT_FREQUENCY 1193182

static void pit_irq(registers_t *regs)
{
    scheduler_timer_tick(regs);
}

void pit_init(uint32_t hz)
{
    if (hz == 0)
        hz = 100;

    uint32_t divisor = PIT_FREQUENCY / hz;

    // channel 0, access lobyte/hibyte, square wave mode
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_install_handler(0, pit_irq);
    pic_clear_mask(0);
}
