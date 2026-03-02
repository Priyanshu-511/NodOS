#pragma once
#include <stdint.h>

// Basic x86 port I/O helpers (used by drivers)

static inline void outb(uint16_t port, uint8_t val) {   // write 8-bit
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {              // read 8-bit
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {  // write 16-bit
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {             // read 16-bit
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait() {                          // small I/O delay
    outb(0x80, 0);
}