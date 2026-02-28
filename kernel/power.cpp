#include "../include/power.h"
#include "../include/io.h"
#include "../include/vga.h"

extern VGADriver vga; // From kernel.cpp

void system_shutdown() {
    vga.setColor(LIGHT_RED, BLACK);
    vga.println("Shutting down...");

    // 1. QEMU (newer versions, requires "-device isa-debug-exit" flag)
    outw(0x604, 0x2000);
    
    // 2. VirtualBox / QEMU alternative
    outw(0x4004, 0x3400);
    
    // 3. Bochs / Older QEMU
    outw(0xB004, 0x2000);

    // If it reaches here, we are either on real hardware or the emulator 
    // doesn't support the magic ports. We'll just halt the CPU safely.
    vga.println("It is now safe to turn off your computer.");
    while (true) {
        __asm__ volatile ("cli; hlt");
    }
}

void system_reboot() {
    vga.setColor(LIGHT_RED, BLACK);
    vga.println("Rebooting...");

    // Wait for the 8042 keyboard controller to be ready
    uint8_t temp = 0x02;
    while (temp & 0x02) {
        temp = inb(0x64);
    }
    
    // Send the reset command to the keyboard controller
    outb(0x64, 0xFE);

    // Halt in case the reset takes a moment
    while (true) {
        __asm__ volatile ("cli; hlt");
    }
}