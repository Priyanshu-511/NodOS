#pragma once
#include <stdint.h>

// GDT entry (segment descriptor)
struct GDTEntry {
    uint16_t limit_low;   // lower 16 bits of segment limit
    uint16_t base_low;    // lower 16 bits of base address
    uint8_t  base_mid;    // middle 8 bits of base
    uint8_t  access;      // access flags
    uint8_t  gran;        // granularity + high limit bits
    uint8_t  base_high;   // high 8 bits of base
} __attribute__((packed));

// GDT pointer for lgdt
struct GDTPtr {
    uint16_t limit;       // size of GDT - 1
    uint32_t base;        // address of first entry
} __attribute__((packed));

void gdt_init();          // setup and load GDT