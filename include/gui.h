#pragma once
#include <stdint.h>

// GUI core: init + main loop

// Init GUI (uses multiboot info for framebuffer)
void gui_init(uint32_t mb_info_addr);

// True if framebuffer GUI is active
bool gui_running();

// Main GUI loop (~60 FPS), handles input + redraw
void gui_run();

// Framebuffer text-mode fallback (no desktop/taskbar)
void gui_run_textmode();