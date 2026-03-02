#pragma once

// GUI text editor window (vi-style) for NodOS

// Opens a WM window with a vi-like modal editor.
// Reads/writes files via VFS — does NOT call keyboard_getchar()
// so it is safe to open from the GUI event loop.
//
// Modes
//   NORMAL  — hjkl / arrow movement, x, dd, o, O, A, G, gg, w, b
//   INSERT  — type to insert, Esc → NORMAL
//   COMMAND — entered with ':', supports :w :q :q! :wq :x :w <file>


// Multiple files can be open simultaneously (each call to gui_vi_open
// creates a new window unless that filename is already open).

void gui_vi_open(const char* filename);