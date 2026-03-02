#pragma once
#include <stdint.h>
#include "fb.h"

//  NodOS Window Manager
//  Supports: create/destroy windows, drag (title bar), focus, minimize,
//            maximize / restore, close.  All rendering to the framebuffer
//            back-buffer; caller calls fb_swap() when ready.

static const int WM_MAX_WINDOWS  = 16;
static const int WM_TITLE_HEIGHT = 26;   // pixels — title bar
static const int WM_BORDER       = 2;    // px — border around client area
static const int WM_BTN_SIZE     = 14;   // close/min/max buttons

// Window flags
enum WMFlags {
    WM_RESIZABLE   = 1 << 0,
    WM_NO_CLOSE    = 1 << 1,
    WM_NO_MINIMIZE = 1 << 2,
    WM_NO_MAXIMIZE = 1 << 3,
    WM_MODAL       = 1 << 4,
};

// Callback types
typedef void (*WMDrawCB)(int wid, int cx, int cy, int cw, int ch, void* userdata);
typedef void (*WMKeysCB)(int wid, char key, void* userdata);
typedef void (*WMCloseCB)(int wid, void* userdata);
typedef void (*WMMouseCB)(int wid, int cx, int cy, bool left, bool right, void* userdata);

struct Window {
    int      id;
    char     title[64];
    int      x, y, w, h;         // outer frame (incl. chrome)
    int      saved_x, saved_y;   // pre-maximize position
    int      saved_w, saved_h;
    bool     focused;
    bool     minimized;
    bool     maximized;
    bool     visible;
    bool     used;
    uint32_t flags;

    WMDrawCB  on_draw;
    WMKeysCB  on_key;
    WMCloseCB on_close;
    WMMouseCB on_mouse;
    void*    userdata;
};

// Lifecycle 
void wm_init();

// Create a window; returns id ≥ 0 or -1 on failure
int  wm_create(const char* title, int x, int y, int w, int h,
               uint32_t flags = 0);

// Attach callbacks 
void wm_set_callbacks(int wid,
                      WMDrawCB  draw,
                      WMKeysCB  key  = nullptr,
                      WMCloseCB cls  = nullptr,
                      WMMouseCB mouse = nullptr,
                      void*     ud   = nullptr);

void wm_destroy(int wid);
void wm_show(int wid);
void wm_hide(int wid);

// Window control functions (check flags before calling these in your event handlers!)
void wm_focus(int wid);
void wm_minimize(int wid);
void wm_maximize(int wid);
void wm_restore(int wid);
void wm_move(int wid, int x, int y);
void wm_resize(int wid, int w, int h);
void wm_set_title(int wid, const char* title);

// Input dispatch
// Call from your main loop; returns true if event was consumed
bool wm_handle_mouse(int mx, int my, bool left, bool right,
                     bool left_down, bool right_down);
void wm_handle_key(char key);

// Rendering 
// Redraws all visible windows (back-to-front).  Desktop + taskbar drawn first.
void wm_render_all();

// Request a single window redraw
void wm_invalidate(int wid);

// Accessors 
Window* wm_get(int wid);

// Client-area origin for a window (excludes chrome)
void wm_client_rect(int wid, int* cx, int* cy, int* cw, int* ch);

// Iterate all used windows (for taskbar etc.)
// Fills out[] with pointers, returns count. Max WM_MAX_WINDOWS entries.
int wm_get_all(Window** out, int max_count);

// Current focused window id, or -1
int wm_focused_id();

// Bring window to front of Z-order
void wm_raise(int wid);