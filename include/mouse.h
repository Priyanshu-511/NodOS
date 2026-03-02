#pragma once
#include <stdint.h>

//  PS/2 mouse driver, IRQ 12
struct MouseState {
    int  x, y;          // current position (clamped to screen)
    int  dx, dy;        // delta since last read
    bool left;          // button states
    bool right;
    bool middle;
    bool left_clicked;  // true for exactly one frame after release
    bool right_clicked;
};

void       mouse_init();
MouseState mouse_get();          // snapshot of current state
void       mouse_clear_clicks(); // clear clicked flags (call after processing)

// Draw / erase the software cursor at current position into the back-buffer
void mouse_draw_cursor();
void mouse_erase_cursor();       // restores saved background pixels