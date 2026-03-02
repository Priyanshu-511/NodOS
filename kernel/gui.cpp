#include "../include/gui.h"
#include "../include/fb.h"
#include "../include/mouse.h"
#include "../include/wm.h"
#include "../include/desktop.h"
#include "../include/gui_terminal.h"
#include "../include/keyboard.h"
#include "../include/pit.h"
#include "../include/kstring.h"
#include "../include/shell.h"
#include "../include/splash.h"


//  Multiboot v1 info parsing (flags + framebuffer fields)

struct MBInfo {
    uint32_t flags;          // 0
    uint32_t mem_lower;      // 4
    uint32_t mem_upper;      // 8
    uint32_t boot_device;    // 12
    uint32_t cmdline;        // 16
    uint32_t mods_count;     // 20
    uint32_t mods_addr;      // 24
    uint32_t syms[4];        // 28
    uint32_t mmap_length;    // 44
    uint32_t mmap_addr;      // 48
    uint32_t drives_length;  // 52
    uint32_t drives_addr;    // 56
    uint32_t config_table;   // 60
    uint32_t boot_loader;    // 64
    uint32_t apm_table;      // 68

    // VBE info (flags bit 11)
    uint32_t vbe_control_info;  // 72
    uint32_t vbe_mode_info;     // 76
    uint16_t vbe_mode;          // 80
    uint16_t vbe_interface_seg; // 82
    uint16_t vbe_interface_off; // 84
    uint16_t vbe_interface_len; // 86

    // Framebuffer (flags bit 12)
    uint64_t fb_addr;           // 88  <--- Correct offset!
    uint32_t fb_pitch;          // 96
    uint32_t fb_width;          // 100
    uint32_t fb_height;         // 104
    uint8_t  fb_bpp;            // 108
    uint8_t  fb_type;           // 109
    uint8_t  color_info[6];     // 110
} __attribute__((packed)); // <--- Ensure no compiler padding ruins the offsets

static bool s_gui_running = false;

void gui_init(uint32_t mb_info_addr) {
    if (!mb_info_addr) return;
    MBInfo* mb = (MBInfo*)mb_info_addr;

    // Bit 12 = framebuffer info present
    if (!(mb->flags & (1 << 12))) return;

    FBInfo fbi;
    fbi.addr   = (uint32_t)(mb->fb_addr & 0xFFFFFFFF);
    fbi.pitch  = mb->fb_pitch;
    fbi.width  = mb->fb_width;
    fbi.height = mb->fb_height;
    fbi.bpp    = mb->fb_bpp;

    if (!fb_init(fbi)) return;

    // Init subsystems
    mouse_init();
    wm_init();
    desktop_init();

    s_gui_running = true;

    // Initial screen clear
    fb_clear(Color::Desktop);
    fb_swap();
}

bool gui_running() { return s_gui_running; }


//  Main event loop


void gui_run() {
    if (!s_gui_running) return;

    splash_show();

    // Open terminal on startup
    // gui_terminal_open();

    uint32_t last_frame = pit_uptime_ms();
    const uint32_t FRAME_MS = 16;   // ~60 fps cap

    bool prev_left  = false;

    while (true) {
        //  Keyboard 
        while (keyboard_available()) {
            char key = keyboard_getchar();
            // Alt+Tab: next window focus (basic)
            // For now pass all keys to the focused window
            wm_handle_key(key);
        }

        //  Mouse 
        MouseState ms = mouse_get();
        bool clicked  = ms.left_clicked;
        bool pressing = ms.left;

        // Close Start menu on click outside
        if (clicked && startmenu_visible())
            startmenu_handle_mouse(ms.x, ms.y, clicked);

        // Try WM first (window chrome / drag)
        bool consumed = wm_handle_mouse(ms.x, ms.y, pressing, ms.right,
                                        pressing && !prev_left,
                                        ms.right_clicked);

        if (!consumed) {
            int bar_y = (int)FB_HEIGHT - TASKBAR_HEIGHT;
            if (ms.y >= bar_y) {
                // Taskbar area
                taskbar_handle_mouse(ms.x, ms.y, clicked);
            } else {
                // Desktop
                desktop_handle_mouse(ms.x, ms.y, clicked);
            }
        }

        mouse_clear_clicks();
        prev_left = pressing;

        //  Frame timing (skip render if not time yet) 
        uint32_t now = pit_uptime_ms();
        if (now - last_frame < FRAME_MS) {
            __asm__ volatile("hlt");  // wait for next IRQ (timer / kbd / mouse)
            continue;
        }
        last_frame = now;

        //  Render 
        mouse_erase_cursor();

        // Draw desktop background + icons
        desktop_draw();

        // Draw all windows (back to front)
        wm_render_all();

        // Draw taskbar on top
        taskbar_draw();

        // Start menu (very top)
        if (startmenu_visible()) startmenu_draw();

        // Software mouse cursor (always on top)
        mouse_draw_cursor();

        // Blit back-buffer → VRAM
        fb_swap();
    }
}


//  gui_run_textmode()
//  Boots into a fullscreen terminal — no desktop, no taskbar, no mouse.
//  Used when the kernel is launched with the 'nogui' cmdline flag.
//  The framebuffer is still required (GRUB leaves the display in gfx mode),
//  so gui_init() must be called before this.

void gui_run_textmode() {
    if (!s_gui_running) return;

    // Black out the entire screen
    fb_clear(0x000000);
    fb_swap();

    // Open terminal window sized to fill the whole screen
    // (no titlebar chrome needed — pass borderless-style coords)
    const int MARGIN = 0;
    int wid = wm_create("NodOS Terminal",
                        MARGIN, MARGIN,
                        (int)FB_WIDTH  - MARGIN * 2,
                        (int)FB_HEIGHT - MARGIN * 2);

    wm_set_callbacks(wid,
                     gui_terminal_draw,
                     gui_terminal_key,
                     gui_terminal_close,
                     nullptr, nullptr);

    // Route all shell output through the GUI terminal
    shell_set_gui_output(true);

    uint32_t last_frame = pit_uptime_ms();
    const uint32_t FRAME_MS = 16;   // ~60 fps

    while (true) {
        // Keyboard → terminal only (no mouse, no desktop)
        while (keyboard_available()) {
            char key = keyboard_getchar();
            wm_handle_key(key);
        }

        // Frame cap
        uint32_t now = pit_uptime_ms();
        if (now - last_frame < FRAME_MS) {
            __asm__ volatile("hlt");
            continue;
        }
        last_frame = now;

        // Render: just the terminal window, no desktop/taskbar/cursor
        fb_clear(0x000000);
        wm_render_all();
        fb_swap();
    }
}