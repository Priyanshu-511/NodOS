#pragma once
#include <stdint.h>
#include <stddef.h>

// Linear framebuffer driver (1024x768x32, double buffered)
static const uint32_t FB_WIDTH  = 1024;
static const uint32_t FB_HEIGHT = 768;
static const uint32_t FB_BPP    = 32;

// Pack RGB into 0x00RRGGBB
inline uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Basic UI color palette
namespace Color {
    static const uint32_t Black       = 0x000000;
    static const uint32_t White       = 0xFFFFFF;
    static const uint32_t Red         = 0xFF4444;
    static const uint32_t Green       = 0x44FF44;
    static const uint32_t Blue        = 0x4488FF;
    static const uint32_t DarkBlue    = 0x1A2B4A;
    static const uint32_t NavyBlue    = 0x0D1B2A;
    static const uint32_t LightBlue   = 0x87CEEB;
    static const uint32_t Grey        = 0x888888;
    static const uint32_t LightGrey   = 0xC0C0C0;
    static const uint32_t DarkGrey    = 0x333333;
    static const uint32_t MidGrey     = 0x555555;
    static const uint32_t Yellow      = 0xFFDD44;
    static const uint32_t Orange      = 0xFF8800;
    static const uint32_t Cyan        = 0x44DDFF;
    static const uint32_t Magenta     = 0xFF44FF;
    static const uint32_t Transparent = 0xFF000000; // sentinel

    // Desktop / UI chrome
    static const uint32_t Desktop     = 0x1E3A5F;
    static const uint32_t Taskbar     = 0x0F1F35;
    static const uint32_t WinTitle    = 0x2A5298;
    static const uint32_t WinTitleFoc = 0x3D6FCC;
    static const uint32_t WinBorder   = 0x4A7FBF;
    static const uint32_t WinBody     = 0x0E1E30;
    static const uint32_t BtnClose    = 0xCC3333;
    static const uint32_t BtnMin      = 0xCCAA00;
    static const uint32_t BtnMax      = 0x33AA33;
    static const uint32_t TextNormal  = 0xE0E8FF;
    static const uint32_t TextDim     = 0x7090B0;
    static const uint32_t Highlight   = 0x4A90D9;
    static const uint32_t TermBg      = 0x0A0F1A;
    static const uint32_t TermFg      = 0x00FF88;
    static const uint32_t TermCursor  = 0x00FF88;
}

// Multiboot framebuffer info (filled by kernel_main from mb_info)
struct FBInfo {
    uint32_t addr;
    uint32_t pitch;   // bytes per row
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
};

// API

// Must be called once from kernel_main after parsing multiboot tags.
// fb  = framebuffer info from multiboot
bool fb_init(const FBInfo& fb);

// Is the framebuffer available?
bool fb_available();

// Swap back-buffer → VRAM (full blit)
void fb_swap();

// Partial blit: only the dirty rectangle (x,y,w,h) in screen coords
void fb_swap_rect(int x, int y, int w, int h);

// Low-level pixel write to back-buffer (no bounds check for speed).
// Not inline here — defined once in fb.cpp to avoid ODR issues.
void fb_put(int x, int y, uint32_t color);

// Safe pixel write (with bounds check)
void fb_set(int x, int y, uint32_t color);

// Read pixel from back-buffer
uint32_t fb_get(int x, int y);

// Basic drawing
void fb_clear(uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);        // outline
void fb_draw_rect_thick(int x, int y, int w, int h, uint32_t color, int thickness);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_draw_circle(int cx, int cy, int r, uint32_t color);
void fb_fill_circle(int cx, int cy, int r, uint32_t color);

// Blending (alpha 0=transparent,255=opaque — very lightweight, no FP)
void fb_fill_rect_blend(int x, int y, int w, int h, uint32_t color, uint8_t alpha);

// Gradient fill (top-color → bot-color, vertical)
void fb_fill_gradient(int x, int y, int w, int h,
                      uint32_t top_color, uint32_t bot_color);

// Text rendering (built-in 8×8 bitmap font)
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg = Color::Transparent);
void fb_draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg = Color::Transparent);
int  fb_text_width(const char* s);   // pixels

// Scrolling region (used by terminal)
void fb_scroll_up(int x, int y, int w, int h, int pixels, uint32_t bg);

// Back-buffer pointer (for direct access by window manager)
uint32_t* fb_backbuffer();
uint32_t  fb_pitch_px();   // pitch in pixels (== FB_WIDTH when stride-less)