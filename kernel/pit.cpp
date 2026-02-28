#include "../include/pit.h"
#include "../include/idt.h"
#include "../include/io.h"

static volatile uint32_t _ticks = 0;
static uint32_t _hz = 100;

static void timer_irq(Registers*) {
    _ticks++;
}

void pit_init(uint32_t hz) {
    _hz = hz;
    uint32_t divisor = 1193182 / hz;

    outb(0x43, 0x36);                    // channel 0, lo/hi, mode 3, binary
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)(divisor >> 8));

    irq_install_handler(0, timer_irq);
}

uint32_t pit_ticks()      { return _ticks; }
uint32_t pit_uptime_ms()  { return _ticks * (1000 / _hz); }
uint32_t pit_uptime_s()   { return _ticks / _hz; }

void pit_sleep(uint32_t ms) {
    uint32_t target = _ticks + (ms * _hz / 1000) + 1;
    while (_ticks < target)
        __asm__ volatile("hlt");
}
