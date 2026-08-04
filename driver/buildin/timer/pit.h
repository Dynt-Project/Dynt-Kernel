#ifndef DRIVER_BUILDIN_TIMER_PIT_H
#define DRIVER_BUILDIN_TIMER_PIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// arms the PIT channel 0 to fire `hz` times per second and installs the
// scheduler preemption handler on irq0
void pit_init(uint32_t hz);

#ifdef __cplusplus
}
#endif

#endif // DRIVER_BUILDIN_TIMER_PIT_H
