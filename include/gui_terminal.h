#pragma once
#include <stdint.h>

//  Graphical terminal window backed by the NodOS shell
static const int GTERM_COLS       = 100;
static const int GTERM_ROWS       = 38;
static const int GTERM_CHAR_W     = 8;
static const int GTERM_CHAR_H     = 13;
static const int GTERM_SCROLL_MAX = 500;  // lines of scrollback

// Open (or re-focus) the terminal window
void gui_terminal_open();

// Called by the window manager when the terminal window gets a key event
void gui_terminal_key(int wid, char key, void* ud);

// Called by WM to repaint the terminal client area
void gui_terminal_draw(int wid, int cx, int cy, int cw, int ch, void* ud);

// Called by WM on close button
void gui_terminal_close(int wid, void* ud);

// Print a string into the terminal (called by shell output redirect)
void gui_terminal_print(const char* s);

// Flush pending shell output to the terminal buffer
void gui_terminal_flush();
void gui_terminal_clear();