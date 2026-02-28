#include "../include/idt.h"
#include "../include/io.h"
#include "../include/vga.h"
#include "../include/kstring.h"

static IDTEntry idt[256];
static IDTPtr   idt_ptr;
static IRQHandler irq_handlers[16];
static ISRHandler isr_handlers[32];

// Forward declarations of all ISR/IRQ stubs from cpu.asm
extern "C" {
    void isr0();  void isr1();  void isr2();  void isr3();
    void isr4();  void isr5();  void isr6();  void isr7();
    void isr8();  void isr9();  void isr10(); void isr11();
    void isr12(); void isr13(); void isr14(); void isr15();
    void isr16(); void isr17(); void isr18(); void isr19();
    void isr20(); void isr21(); void isr22(); void isr23();
    void isr24(); void isr25(); void isr26(); void isr27();
    void isr28(); void isr29(); void isr30(); void isr31();

    void irq0();  void irq1();  void irq2();  void irq3();
    void irq4();  void irq5();  void irq6();  void irq7();
    void irq8();  void irq9();  void irq10(); void irq11();
    void irq12(); void irq13(); void irq14(); void irq15();

    void idt_load(uint32_t);
}

static void set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = base & 0xFFFF;
    idt[n].base_hi = (base >> 16) & 0xFFFF;
    idt[n].sel     = sel;
    idt[n].zero    = 0;
    idt[n].flags   = flags;
}

static void pic_remap() {
    // Remap master PIC: IRQ 0-7  → INT 32-39
    // Remap slave  PIC: IRQ 8-15 → INT 40-47
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();   // master offset
    outb(0xA1, 0x28); io_wait();   // slave  offset
    outb(0x21, 0x04); io_wait();   // slave at IRQ2
    outb(0xA1, 0x02); io_wait();   // cascade identity
    outb(0x21, 0x01); io_wait();   // 8086 mode
    outb(0xA1, 0x01); io_wait();
    outb(0x21, 0x00);              // unmask all
    outb(0xA1, 0x00);
}

static const char* exception_names[] = {
    "Division by Zero",     "Debug",                "NMI",
    "Breakpoint",           "Overflow",             "Bound Range Exceeded",
    "Invalid Opcode",       "Device Not Available",  "Double Fault",
    "Coprocessor Overrun",  "Invalid TSS",          "Segment Not Present",
    "Stack-Segment Fault",  "General Protection",   "Page Fault",
    "Reserved",             "x87 FP Exception",     "Alignment Check",
    "Machine Check",        "SIMD FP Exception",     "Virtualization Exception",
    "Reserved","Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved",
    "Security Exception",   "Reserved"
};

extern "C" void isr_handler(Registers* r) {
    if (isr_handlers[r->int_no]) {
        isr_handlers[r->int_no](r);
        return;
    }
    vga.setColor(LIGHT_RED, BLACK);
    vga.println("\n[KERNEL PANIC]");
    vga.print("Exception: ");
    if (r->int_no < 32) vga.println(exception_names[r->int_no]);
        else vga.println("Unknown");
    vga.print("EIP: "); vga.printHex(r->eip); vga.print("  ERR: "); vga.printHex(r->err_code); vga.putChar('\n');
    __asm__ volatile("cli; hlt");
}

// Forward declaration from process.cpp
extern uint32_t scheduler_tick(uint32_t esp);

extern "C" uint32_t irq_handler(uint32_t esp) {
    Registers* r = (Registers*)esp;
    uint8_t    irq = r->int_no - 32;

    if (irq_handlers[irq])
        irq_handlers[irq](r);

    // Send EOI
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    // Let the scheduler switch tasks on every timer tick
    if (irq == 0) esp = scheduler_tick(esp);

    return esp;
}

void irq_install_handler(int irq, IRQHandler h)   { irq_handlers[irq] = h; }
void irq_uninstall_handler(int irq)                { irq_handlers[irq] = nullptr; }
void isr_install_handler(int n, ISRHandler h)      { isr_handlers[n] = h; }

void idt_init() {
    k_memset(idt, 0, sizeof(idt));
    k_memset(irq_handlers, 0, sizeof(irq_handlers));
    k_memset(isr_handlers, 0, sizeof(isr_handlers));

    pic_remap();

    // Exceptions
    set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    // IRQs
    set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    idt_ptr.limit = sizeof(IDTEntry) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt;
    idt_load((uint32_t)&idt_ptr);
}
