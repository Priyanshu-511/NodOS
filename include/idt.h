#pragma once
#include <stdint.h>

// Saved register state on the kernel stack when an interrupt fires.
// Order must match exactly what cpu.asm isr_common/irq_common pushes.
struct Registers {
    uint32_t ds;                                        // pushed last before call
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha (edi = lowest)
    uint32_t int_no, err_code;                          // pushed by stub
    uint32_t eip, cs, eflags;                           // pushed by CPU
};

typedef void (*ISRHandler)(Registers*);
typedef void (*IRQHandler)(Registers*);

struct IDTEntry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct IDTPtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void idt_init();
void irq_install_handler(int irq, IRQHandler h);
void irq_uninstall_handler(int irq);
void isr_install_handler(int isr, ISRHandler h);
