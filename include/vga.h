#pragma once
#include "pager.h"
#include <stdint.h>
#include <stddef.h>

// VGA text-mode driver (80x25, memory at 0xB8000)

enum VGAColor : uint8_t {
    BLACK = 0, BLUE, GREEN, CYAN, RED, MAGENTA,
    BROWN, LIGHT_GREY, DARK_GREY, LIGHT_BLUE,
    LIGHT_GREEN, LIGHT_CYAN, LIGHT_RED,
    LIGHT_MAGENTA, YELLOW, WHITE
};

class VGADriver {
public:
    static const size_t WIDTH  = 80;
    static const size_t HEIGHT = 25;

    // No constructor — global C++ constructors are NOT called in freestanding
    // kernels. Call init() explicitly from kernel_main instead.
    void init() {
        buffer = (uint16_t*)0xB8000;
        row = 0; col = 0;
        color = makeColor(LIGHT_GREY, BLACK);
        clear();
    }

    void clear() {
        for (size_t i = 0; i < WIDTH * HEIGHT; i++)
            buffer[i] = makeEntry(' ', color);
        row = 0; col = 0;
    }

    void setColor(VGAColor fg, VGAColor bg) {
        color = makeColor(fg, bg);
    }

    void putChar(char c) {
        if (c == '\n') { newline(); pager_check(); return; }
        if (c == '\r') { col = 0; return; }
        if (c == '\b') {
            if (col > 0) { col--; buffer[row * WIDTH + col] = makeEntry(' ', color); }
            return;
        }
        buffer[row * WIDTH + col] = makeEntry(c, color);
        if (++col == WIDTH) newline();
    }

    void print(const char* s) { for (size_t i = 0; s[i]; i++) putChar(s[i]); }
    void println(const char* s) { print(s); putChar('\n'); }

    void printInt(int n) {
        if (n < 0) { putChar('-'); printUInt((uint32_t)(-n)); }
        else printUInt((uint32_t)n);
    }

    void printUInt(uint32_t n, int base = 10) {
        if (n == 0) { putChar('0'); return; }
        char buf[32]; int i = 0;
        while (n) { int d = n % base; buf[i++] = (d < 10) ? '0'+d : 'a'+d-10; n /= base; }
        while (i--) putChar(buf[i]);
    }

    void printHex(uint32_t n) {
        print("0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nib = (n >> i) & 0xF;
            putChar(nib < 10 ? '0'+nib : 'A'+nib-10);
        }
    }

    size_t getRow() const { return row; }
    size_t getCol() const { return col; }
    // Write a single cell directly (used by vi editor to bypass scroll tracking)
    void write_cell(size_t r, size_t c, char ch, VGAColor fg, VGAColor bg) {
        if (r < HEIGHT && c < WIDTH)
            buffer[r * WIDTH + c] = makeEntry(ch, makeColor(fg, bg));
    }
    // Move the VGA logical cursor (used after full-screen redraws)
    void set_cursor(size_t r, size_t c) { 
        row = r; 
        col = c; 

        uint16_t pos = r * WIDTH + c;

        // Send the Low Byte of the position to VGA port 0x3D4/0x3D5
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)0x3D4));
        asm volatile("outb %0, %1" : : "a"((uint8_t)(pos & 0xFF)), "Nd"((uint16_t)0x3D5));

        // Send the High Byte of the position
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x0E), "Nd"((uint16_t)0x3D4));
        asm volatile("outb %0, %1" : : "a"((uint8_t)((pos >> 8) & 0xFF)), "Nd"((uint16_t)0x3D5));
    }

    void disable_cursor() {
        // Select Cursor Start Register (0x0A)
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x0A), "Nd"((uint16_t)0x3D4));
        // Write 0x20 (Bit 5 set to 1) to disable the cursor
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x20), "Nd"((uint16_t)0x3D5));
    }

    void enable_cursor(uint8_t cursor_start = 14, uint8_t cursor_end = 15) {
        // Set Cursor Start scanline
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x0A), "Nd"((uint16_t)0x3D4));
        asm volatile("outb %0, %1" : : "a"((uint8_t)(cursor_start & 0x1F)), "Nd"((uint16_t)0x3D5));

        // Set Cursor End scanline
        asm volatile("outb %0, %1" : : "a"((uint8_t)0x0B), "Nd"((uint16_t)0x3D4));
        asm volatile("outb %0, %1" : : "a"((uint8_t)(cursor_end & 0x1F)), "Nd"((uint16_t)0x3D5));
    }

private:
    volatile uint16_t* buffer;
    size_t row, col;
    uint8_t color;

    static uint8_t  makeColor(VGAColor fg, VGAColor bg) { return fg | (bg << 4); }
    static uint16_t makeEntry(char c, uint8_t col)      { return (uint16_t)c | ((uint16_t)col << 8); }

    void newline() { col = 0; if (++row == HEIGHT) scroll(); }

    void scroll() {
        for (size_t y = 1; y < HEIGHT; y++)
            for (size_t x = 0; x < WIDTH; x++)
                buffer[(y-1)*WIDTH+x] = buffer[y*WIDTH+x];
        for (size_t x = 0; x < WIDTH; x++)
            buffer[(HEIGHT-1)*WIDTH+x] = makeEntry(' ', color);
        row = HEIGHT - 1;
    }
};


// Global VGA instance — defined in kernel.cpp, used everywhere via this extern.
extern VGADriver vga;