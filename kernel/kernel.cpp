// NodOS kernel entry point (GUI Edition)
// Boot flow:
//   1. Hardware init (GDT, IDT, PMM, heap, PIT, KB, IRQs, proc, ATA, VFS)
//   2. Detect framebuffer in multiboot info
//   3a. If framebuffer available → launch GUI (gui_run — never returns)
//   3b. If not             → fall back to VGA text shell

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
#include "../include/power.h"
#include "../include/gui.h"
#include "../include/gui_terminal.h"
#include "../include/settings_config.h"

//  Global VGA instance (used by VGA text fallback + boot messages) 
VGADriver vga;

extern "C" uint32_t kernel_end;

//  Boot-time OK logger 
static void ok(const char* msg) {
    vga.setColor(LIGHT_GREEN, BLACK); vga.print("  [ OK ] ");
    vga.setColor(LIGHT_GREY,  BLACK); vga.println(msg);
}

static void warn(const char* msg) {
    vga.setColor(YELLOW, BLACK); vga.print("  [WARN] ");
    vga.setColor(LIGHT_GREY, BLACK); vga.println(msg);
}

//  Boot banner (VGA text, shown briefly before GUI takes over) 
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
    vga.print("  +-"); for(int i=0;i<64;i++) vga.putChar('-'); vga.println("-+");
    vga.print("  |   ");
    vga.setColor(YELLOW, BLACK);       vga.print("NodOS v4.0 GUI");
    vga.setColor(DARK_GREY, BLACK);    vga.print("  //  ");
    vga.setColor(LIGHT_GREEN, BLACK);  vga.print("x86 32-bit");
    vga.setColor(DARK_GREY, BLACK);    vga.print("  //  ");
    vga.setColor(LIGHT_MAGENTA,BLACK); vga.print("1024x768x32");
    vga.setColor(DARK_GREY, BLACK);    vga.print("  //  ");
    vga.setColor(LIGHT_RED, BLACK);    vga.printUInt(pmm_get_total_ram_mb());
    vga.print(" MB RAM");
    vga.setColor(DARK_GREY, BLACK);    vga.println("   |");
    vga.print("  +-"); for(int i=0;i<64;i++) vga.putChar('-'); vga.println("-+");
    vga.println("");
    vga.setColor(LIGHT_GREY, BLACK);
}



//  kernel_main



//  Multiboot cmdline helper

struct MBCmdline {
    uint32_t flags;
    uint32_t mem_lower, mem_upper, boot_device;
    uint32_t cmdline;   // physical addr of null-terminated string (flags bit 2)
};

static bool cmdline_has_flag(const char* hay, const char* needle) {
    if (!hay || !needle) return false;
    int nlen = (int)k_strlen(needle);
    const char* p = hay;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        int wlen = 0;
        while (p[wlen] && p[wlen] != ' ') wlen++;
        if (wlen == nlen && k_strncmp(p, needle, nlen) == 0) return true;
        p += wlen;
    }
    return false;
}

extern "C" void kernel_main(uint32_t magic, uint32_t mb_info) {
    //  VGA text init (used for boot log) 
    vga.init();

    //  Read GRUB cmdline — detect 'nogui' flag 
    bool force_text = false;
    {
        MBCmdline* mb = (MBCmdline*)(uintptr_t)mb_info;
        if (mb && (mb->flags & (1 << 2)) && mb->cmdline)
            force_text = cmdline_has_flag((const char*)(uintptr_t)mb->cmdline, "nogui");
    }

    //  Memory 
    uint32_t heap_start = ((uint32_t)&kernel_end + 0xFFF) & ~0xFFFu;

    pmm_init(heap_start);
    // Use larger heap for GUI back-buffer (1024*768*4 = 3MB just for that)
    heap_init(heap_start, 16 * 1024 * 1024);   // 16 MB heap

    //  Now we can print the banner 
    print_banner();

    //  Core hardware 
    gdt_init();
    ok("GDT     (5 descriptors)");

    idt_init();
    ok("IDT     (32 exceptions + 16 IRQs, PIC remapped)");

    ok("PMM     (bitmap allocator, 4 KB pages)");
    ok("Heap    (16 MB kmalloc arena)");

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
    ok("VFS     (Persistent Disk Mounted)");

    shell_init();
    ok("Shell   ready");

    settings_init();
    ok("Settings loaded (/etc/settings.cfg)");

    if (magic != 0x2BADB002)
        warn("Unexpected multiboot magic (non-fatal)");

    //  GUI init 
    vga.println("");
    vga.setColor(CYAN, BLACK);
    if (force_text)
        vga.println("  Text mode boot (nogui) — initialising framebuffer...");
    else
        vga.println("  Initialising framebuffer GUI...");
    vga.setColor(LIGHT_GREY, BLACK);

    // Always init the framebuffer: GRUB leaves the display in gfx mode
    // regardless of which menu entry was chosen, so VGA text writes are
    // invisible without it.
    gui_init(mb_info);

    if (gui_running()) {
        ok("Framebuffer ready (1024x768x32bpp)");
        if (!force_text) ok("Mouse   (PS/2 IRQ 12)");
        vga.println("");
        vga.setColor(YELLOW, BLACK);
        if (force_text)
            vga.println("  Launching fullscreen terminal (text mode)...");
        else
            vga.println("  Launching graphical environment...");
        vga.setColor(LIGHT_GREY, BLACK);

        if (force_text)
            gui_run_textmode();   // fullscreen terminal, no desktop — never returns
        else
            gui_run();            // full desktop GUI — never returns
    } else {
        warn("Framebuffer unavailable — falling back to VGA text shell");
        vga.println("");
        vga.setColor(YELLOW, BLACK); vga.println("  Boot complete (text mode)");
        vga.setColor(LIGHT_GREY, BLACK); vga.println("");
        shell_run();
    }
}