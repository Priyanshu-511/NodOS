#include "../include/vga.h"
#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/pmm.h"
#include "../include/heap.h"
#include "../include/pit.h"
#include "../include/keyboard.h"
#include "../include/ata.h"
#include "../include/vfs.h"
#include "../include/process.h"
#include "../include/shell.h"
#include "../include/nodev.h"
#include "../include/kstring.h"

VGADriver vga;

extern "C" uint32_t kernel_end;

static void ok(const char* msg) {
    vga.setColor(LIGHT_GREEN, BLACK); vga.print("  [ OK ] ");
    vga.setColor(LIGHT_GREY,  BLACK); vga.println(msg);
}

static void print_banner() {
    vga.setColor(BLUE, BLACK);
    vga.println("      _   _           _  ___  ____");
    vga.setColor(LIGHT_BLUE, BLACK);
    vga.println("     | \\ | | ___   __| |/ _ \\/ ___|");
    vga.setColor(CYAN, BLACK);
    vga.println("     |  \\| |/ _ \\ / _` | | | \\___ \\");
    vga.setColor(LIGHT_CYAN, BLACK);
    vga.println("     | |\\  | (_) | (_| | |_| |___) |");
    vga.setColor(WHITE, BLACK);
    vga.println("     |_| \\_|\\___/ \\__,_|\\___/|____/");
    vga.println("");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  +");
    for(int i = 0; i < 66; i++) vga.putChar('-');
    vga.println("+");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  |   ");
    vga.setColor(YELLOW, BLACK);
    vga.print("NodOS v3.0");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_GREEN, BLACK);
    vga.print("x86 32-bit");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_MAGENTA, BLACK);
    vga.print("NodeV Engine");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.setColor(LIGHT_RED, BLACK);
    vga.printUInt(pmm_get_total_ram_mb());
    vga.print(" MB RAM");
    vga.setColor(DARK_GREY, BLACK);
    vga.print("  //  ");
    vga.print("\n     ");
    vga.setColor(BROWN, BLACK);
    uint32_t disk_mb = vfs_get_disk_size_mb();
    uint32_t disk_gb = disk_mb / 1024;
    vga.putChar('0' + disk_gb);
    vga.print(" GB Disk");
    vga.setColor(DARK_GREY, BLACK);
    vga.println("   |");
    vga.print("  +");
    for(int i = 0; i < 66; i++) vga.putChar('-');
    vga.println("+");
    vga.println("");
    vga.setColor(LIGHT_GREY, BLACK);
    
}

extern "C" void kernel_main(uint32_t magic, uint32_t /*mb_info*/) {
    vga.init();
    print_banner();

    gdt_init();
    ok("GDT     (5 descriptors)");

    idt_init();
    ok("IDT     (32 exceptions + 16 IRQs, PIC remapped)");

    uint32_t heap_start = ((uint32_t)&kernel_end + 0xFFF) & ~0xFFFu;
    pmm_init(heap_start);
    ok("PMM     (bitmap allocator, 4 KB pages)");

    heap_init(heap_start, 8 * 1024 * 1024);   // 4 MB heap
    ok("Heap    (4 MB kmalloc arena)");

    pit_init(100);
    ok("PIT     (100 Hz, IRQ 0)");

    keyboard_init();
    ok("PS/2 KB (IRQ 1)");

    __asm__ volatile("sti");
    ok("IRQs    enabled");

    process_init();
    ok("Sched   (round-robin, max 16 tasks)");

    ata_init();
    vfs_init();
    ok("VFS     (Persistent Disk Mounted, 64 entries)");

    shell_init();
    ok("Shell   (fs + NodeV scripting)");

    if (magic != 0x2BADB002) {
        vga.setColor(LIGHT_RED, BLACK); vga.println("  [WARN] Unexpected multiboot magic");
        vga.setColor(LIGHT_GREY, BLACK);
    }

    vga.println("");
    vga.setColor(YELLOW, BLACK); vga.println("  Boot complete!");
    vga.setColor(LIGHT_GREY, BLACK); vga.println("");

    shell_run();
}
