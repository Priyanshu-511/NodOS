#include "../include/mouse.h"
#include "../include/idt.h"
#include "../include/io.h"
#include "../include/fb.h"
#include "../include/kstring.h"

//  PS/2 controller ports 
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static inline void ps2_wait_write() {
    uint32_t timeout = 100000;
    while (timeout-- && (inb(PS2_STATUS) & 0x02));
}
static inline void ps2_wait_read() {
    uint32_t timeout = 100000;
    while (timeout-- && !(inb(PS2_STATUS) & 0x01));
}
static inline void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_write(); outb(PS2_CMD, cmd);
}
static inline void ps2_write_data(uint8_t data) {
    ps2_wait_write(); outb(PS2_DATA, data);
}
static inline uint8_t ps2_read_data() {
    ps2_wait_read(); return inb(PS2_DATA);
}

//  State 
static volatile uint8_t  s_packet[3];
static volatile int      s_phase  = 0;

static volatile int      s_mx = (int)FB_WIDTH / 2;
static volatile int      s_my = (int)FB_HEIGHT / 2;
static volatile int      s_dx = 0, s_dy = 0;
static volatile bool     s_btn_left   = false;
static volatile bool     s_btn_right  = false;
static volatile bool     s_btn_mid    = false;
static volatile bool     s_left_click  = false;
static volatile bool     s_right_click = false;
static volatile bool     s_prev_left   = false;

// Cursor save/restore buffer (12×18 px)
static const int CURSOR_W = 12, CURSOR_H = 18;
static uint32_t s_cursor_save[CURSOR_W * CURSOR_H];
static int      s_cursor_drawn_x = -1, s_cursor_drawn_y = -1;

//  IRQ handler 
static void mouse_irq(Registers*) {
    uint8_t byte = inb(PS2_DATA);

    if (s_phase == 0 && !(byte & 0x08)) return; // sync check

    s_packet[s_phase] = byte;
    s_phase++;

    if (s_phase < 3) return;
    s_phase = 0;

    // Decode packet
    uint8_t flags = s_packet[0];
    int dx =  (int)(int8_t)s_packet[1];
    int dy = -(int)(int8_t)s_packet[2];  // Y is inverted

    // Overflow guard
    if (flags & 0x40) dx = 0;
    if (flags & 0x80) dy = 0;

    s_dx += dx; s_dy += dy;
    s_mx  = s_mx + dx;
    s_my  = s_my + dy;

    // Clamp
    if (s_mx < 0) s_mx = 0;
    if (s_my < 0) s_my = 0;
    if (s_mx >= (int)FB_WIDTH)  s_mx = (int)FB_WIDTH  - 1;
    if (s_my >= (int)FB_HEIGHT) s_my = (int)FB_HEIGHT - 1;

    bool now_left = !!(flags & 0x01);
    bool now_right= !!(flags & 0x02);
    bool now_mid  = !!(flags & 0x04);

    // Click = transition high→low
    if (s_prev_left && !now_left)   s_left_click  = true;
    if (s_btn_right && !now_right)  s_right_click = true;
    s_prev_left  = now_left;
    s_btn_left   = now_left;
    s_btn_right  = now_right;
    s_btn_mid    = now_mid;
}

//  Init 
void mouse_init() {
    // Enable auxiliary (mouse) device
    ps2_write_cmd(0xA8);

    // Enable IRQ12 (set bit 1 in config byte)
    ps2_write_cmd(0x20);
    uint8_t cfg = ps2_read_data();
    cfg |= 0x02;   // enable mouse interrupt
    cfg &= ~0x20;  // enable mouse clock
    ps2_write_cmd(0x60);
    ps2_write_data(cfg);

    // Send reset to mouse, then enable streaming
    ps2_write_cmd(0xD4); ps2_write_data(0xFF);
    ps2_read_data(); ps2_read_data(); ps2_read_data(); // ACK + self-test + device-id

    ps2_write_cmd(0xD4); ps2_write_data(0xF6); // set defaults
    ps2_read_data();

    ps2_write_cmd(0xD4); ps2_write_data(0xF4); // enable streaming
    ps2_read_data();

    irq_install_handler(12, mouse_irq);
}

//  Public API 
MouseState mouse_get() {
    MouseState st;
    st.x = s_mx; st.y = s_my;
    st.dx = s_dx; st.dy = s_dy;
    s_dx = 0; s_dy = 0;
    st.left   = s_btn_left;
    st.right  = s_btn_right;
    st.middle = s_btn_mid;
    st.left_clicked  = s_left_click;
    st.right_clicked = s_right_click;
    return st;
}

void mouse_clear_clicks() {
    s_left_click  = false;
    s_right_click = false;
}

//  Software cursor (arrow) 
// Simple 12×18 arrow bitmap (1 = foreground, 2 = outline, 0 = transparent)
static const uint8_t CURSOR_MASK[CURSOR_H][CURSOR_W] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,1,0,0,0,0},
};

void mouse_erase_cursor() {
    if (s_cursor_drawn_x < 0) return;
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++) {
            int px = s_cursor_drawn_x + col;
            int py = s_cursor_drawn_y + row;
            if ((uint32_t)px < FB_WIDTH && (uint32_t)py < FB_HEIGHT)
                fb_put(px, py, s_cursor_save[row * CURSOR_W + col]);
        }
    s_cursor_drawn_x = -1;
}

void mouse_draw_cursor() {
    int cx = s_mx, cy = s_my;
    // Save background
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++) {
            int px = cx + col, py = cy + row;
            s_cursor_save[row * CURSOR_W + col] =
                ((uint32_t)px < FB_WIDTH && (uint32_t)py < FB_HEIGHT)
                ? fb_get(px, py) : 0;
        }
    // Draw cursor
    for (int row = 0; row < CURSOR_H; row++)
        for (int col = 0; col < CURSOR_W; col++) {
            uint8_t v = CURSOR_MASK[row][col];
            if (!v) continue;
            int px = cx + col, py = cy + row;
            if ((uint32_t)px < FB_WIDTH && (uint32_t)py < FB_HEIGHT)
                fb_put(px, py, v == 1 ? Color::White : Color::Black);
        }
    s_cursor_drawn_x = cx;
    s_cursor_drawn_y = cy;
}