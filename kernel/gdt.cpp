#include "../include/gdt.h"

static GDTEntry gdt[5];
static GDTPtr   gdt_ptr;

extern "C" void gdt_flush(uint32_t);

static void set_gate(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].base_high = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access    = access;
}

void gdt_init() {
    gdt_ptr.limit = sizeof(GDTEntry) * 5 - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    set_gate(0, 0, 0,          0x00, 0x00); // null descriptor
    set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // kernel code  (ring 0)
    set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // kernel data  (ring 0)
    set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // user code    (ring 3)
    set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // user data    (ring 3)

    gdt_flush((uint32_t)&gdt_ptr);
}
