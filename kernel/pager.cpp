#include "../include/pager.h"
#include "../include/vga.h"
#include "../include/keyboard.h"

bool pager_enabled = false;
static int lines_printed = 0;

void pager_enable() {
    lines_printed = 0;
    pager_enabled = true;
}

void pager_disable() {
    pager_enabled = false;
    lines_printed = 0;
}

void pager_check() {
    if (!pager_enabled) return;

    lines_printed++;
    
    // Pause every 22 lines
    if (lines_printed >= 22) {
        // Turn off pager temporarily so the --More-- text doesn't trigger itself!
        pager_enabled = false; 
        
        vga.setColor(LIGHT_GREY, BLACK);
        vga.print("--More--");
        
        char c = keyboard_getchar(); // Wait for key
        
        vga.print("\r        \r"); // Clear the --More-- text
        
        lines_printed = 0;
        pager_enabled = true; // Turn it back on
        
        // Optional: If you want 'q' to abort, you would need to throw an 
        // interrupt or set a global "abort_command" flag here, since we 
        // are now deep inside the VGA driver.
    }
}