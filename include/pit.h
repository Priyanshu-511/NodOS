#pragma once
#include <stdint.h>

void     pit_init(uint32_t hz);     // Initialize PIT to given frequency (Hz)
uint32_t pit_ticks();               // raw tick count
uint32_t pit_uptime_ms();           // milliseconds since boot
uint32_t pit_uptime_s();            // seconds since boot
void     pit_sleep(uint32_t ms);    // Sleep for given milliseconds (busy wait)
