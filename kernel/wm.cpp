// NodOS Window Manager
// Handles: Z-order, chrome rendering, drag, focus, minimize/maximize/close.

#include "../include/wm.h"
#include "../include/fb.h"
#include "../include/kstring.h"
#include "../include/desktop.h"   // for TASKBAR_HEIGHT


//  State

static Window   s_windows[WM_MAX_WINDOWS];
static int      s_zorder[WM_MAX_WINDOWS];  // front→back index
static int      s_wcount   = 0;
static int      s_next_id  = 1;
static int      s_focused  = -1;

// Drag state
static bool     s_dragging = false;
static int      s_drag_id  = -1;
static int      s_drag_ox  = 0;  // offset from window origin to mouse at grab
static int      s_drag_oy  = 0;
static bool     s_prev_left = false;


//  Helpers

static Window* find(int wid) {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (s_windows[i].used && s_windows[i].id == wid)
            return &s_windows[i];
    return nullptr;
}

static int find_slot() {
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (!s_windows[i].used) return i;
    return -1;
}

// Returns the window (by z-order top-first) hit by (mx,my), or -1
static int hit_test(int mx, int my) {
    for (int z = 0; z < s_wcount; z++) {
        Window* w = &s_windows[s_zorder[z]];
        if (!w->visible || !w->used || w->minimized) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return w->id;
    }
    return -1;
}

static void draw_chrome(Window* w) {
    bool focused = (w->id == s_focused);
    uint32_t title_col = focused ? Color::WinTitleFoc : Color::WinTitle;
    uint32_t border_col= focused ? Color::WinBorder   : Color::MidGrey;

    // Outer border
    fb_draw_rect_thick(w->x, w->y, w->w, w->h, border_col, WM_BORDER);

    // Title bar gradient
    fb_fill_gradient(w->x + WM_BORDER,
                     w->y + WM_BORDER,
                     w->w - 2*WM_BORDER,
                     WM_TITLE_HEIGHT,
                     title_col,
                     (title_col & 0xFEFEFE) >> 1); // darker bottom

    // Title text (centered vertically in title bar)
    int tx = w->x + WM_BORDER + 8;
    int ty = w->y + WM_BORDER + (WM_TITLE_HEIGHT - 8) / 2;
    fb_draw_text(tx, ty, w->title, Color::TextNormal);

    // Buttons: close, max, min (right-to-left)
    int by  = w->y + WM_BORDER + (WM_TITLE_HEIGHT - WM_BTN_SIZE) / 2;
    int bx  = w->x + w->w - WM_BORDER - WM_BTN_SIZE - 4;

    // Close (red X)
    if (!(w->flags & WM_NO_CLOSE)) {
        fb_fill_circle(bx + WM_BTN_SIZE/2, by + WM_BTN_SIZE/2,
                       WM_BTN_SIZE/2 - 1, Color::BtnClose);
        // X mark
        fb_draw_line(bx+3, by+3, bx+WM_BTN_SIZE-4, by+WM_BTN_SIZE-4, Color::White);
        fb_draw_line(bx+WM_BTN_SIZE-4, by+3, bx+3, by+WM_BTN_SIZE-4, Color::White);
        bx -= WM_BTN_SIZE + 4;
    }

    // Maximize (green square-in-square)
    if (!(w->flags & WM_NO_MAXIMIZE)) {
        fb_fill_circle(bx + WM_BTN_SIZE/2, by + WM_BTN_SIZE/2,
                       WM_BTN_SIZE/2 - 1, Color::BtnMax);
        // square symbol
        fb_draw_rect(bx+3, by+3, WM_BTN_SIZE-6, WM_BTN_SIZE-6, Color::White);
        bx -= WM_BTN_SIZE + 4;
    }

    // Minimize (yellow dash)
    if (!(w->flags & WM_NO_MINIMIZE)) {
        fb_fill_circle(bx + WM_BTN_SIZE/2, by + WM_BTN_SIZE/2,
                       WM_BTN_SIZE/2 - 1, Color::BtnMin);
        // dash symbol
        fb_fill_rect(bx+3, by + WM_BTN_SIZE/2 - 1, WM_BTN_SIZE-6, 2, Color::White);
    }

    // Client area background
    int cx, cy, cw, ch;
    wm_client_rect(w->id, &cx, &cy, &cw, &ch);
    fb_fill_rect(cx, cy, cw, ch, Color::WinBody);
}


//  Public API


void wm_init() {
    k_memset(s_windows, 0, sizeof(s_windows));
    s_wcount  = 0;
    s_next_id = 1;
    s_focused = -1;
}

int wm_create(const char* title, int x, int y, int w, int h, uint32_t flags) {
    int slot = find_slot();
    if (slot < 0) return -1;

    Window* win   = &s_windows[slot];
    win->id       = s_next_id++;
    win->x        = x; win->y = y;
    win->w        = w; win->h = h;
    win->flags    = flags;
    win->used     = true;
    win->visible  = true;
    win->minimized= false;
    win->maximized= false;
    win->on_draw  = nullptr;
    win->on_key   = nullptr;
    win->on_close = nullptr;
    win->on_mouse = nullptr;
    win->userdata = nullptr;
    k_strncpy(win->title, title, 63);

    // Push to front of Z-order
    for (int i = s_wcount; i > 0; i--) s_zorder[i] = s_zorder[i-1];
    s_zorder[0] = slot;
    s_wcount++;

    wm_focus(win->id);
    return win->id;
}

void wm_set_callbacks(int wid, WMDrawCB draw, WMKeysCB key,
                      WMCloseCB cls, WMMouseCB mouse, void* ud) {
    Window* w = find(wid);
    if (!w) return;
    w->on_draw  = draw;
    w->on_key   = key;
    w->on_close = cls;
    w->on_mouse = mouse;
    w->userdata = ud;
}

void wm_destroy(int wid) {
    Window* w = find(wid);
    if (!w) return;
    w->used = false;
    // Remove from Z-order
    int slot_to_remove = (int)(w - s_windows);
    int new_cnt = 0;
    int tmp[WM_MAX_WINDOWS];
    for (int i = 0; i < s_wcount; i++)
        if (s_zorder[i] != slot_to_remove) tmp[new_cnt++] = s_zorder[i];
    for (int i = 0; i < new_cnt; i++) s_zorder[i] = tmp[i];
    s_wcount = new_cnt;
    if (s_focused == wid) s_focused = (s_wcount > 0) ?
        s_windows[s_zorder[0]].id : -1;
}

void wm_show(int wid) {
    Window* w = find(wid);
    if (w) { w->visible = true; w->minimized = false; }
}

void wm_hide(int wid) {
    Window* w = find(wid);
    if (w) w->visible = false;
}

void wm_focus(int wid) {
    if (s_focused == wid) return;
    if (s_focused >= 0) {
        Window* old = find(s_focused);
        if (old) old->focused = false;
    }
    s_focused = wid;
    Window* w = find(wid);
    if (w) w->focused = true;
    wm_raise(wid);
}

void wm_raise(int wid) {
    Window* w = find(wid);
    if (!w) return;
    int slot = (int)(w - s_windows);
    // Find in Z order
    int pos = -1;
    for (int i = 0; i < s_wcount; i++)
        if (s_zorder[i] == slot) { pos = i; break; }
    if (pos <= 0) return;
    for (int i = pos; i > 0; i--) s_zorder[i] = s_zorder[i-1];
    s_zorder[0] = slot;
}

void wm_minimize(int wid) {
    Window* w = find(wid);
    if (!w) return;
    w->minimized = true;
    if (s_focused == wid) {
        s_focused = (s_wcount > 0) ? s_windows[s_zorder[1 < s_wcount ? 1 : 0]].id : -1;
    }
}

void wm_maximize(int wid) {
    Window* w = find(wid);
    if (!w || w->maximized) return;
    w->saved_x = w->x; w->saved_y = w->y;
    w->saved_w = w->w; w->saved_h = w->h;
    w->x = 0; w->y = 0;
    w->w = (int)FB_WIDTH;
    w->h = (int)FB_HEIGHT - TASKBAR_HEIGHT;   // TASKBAR_HEIGHT from desktop.h
    w->maximized = true;
}

void wm_restore(int wid) {
    Window* w = find(wid);
    if (!w) return;
    if (w->maximized) {
        w->x = w->saved_x; w->y = w->saved_y;
        w->w = w->saved_w; w->h = w->saved_h;
        w->maximized = false;
    }
    if (w->minimized) w->minimized = false;
}

void wm_move(int wid, int x, int y)   { Window* w = find(wid); if (w) { w->x=x; w->y=y; } }
void wm_resize(int wid, int nw, int h){ Window* w = find(wid); if (w) { w->w=nw; w->h=h; } }
void wm_set_title(int wid, const char* t) {
    Window* w = find(wid); if (w) k_strncpy(w->title, t, 63);
}

void wm_client_rect(int wid, int* cx, int* cy, int* cw, int* ch) {
    Window* w = find(wid);
    if (!w) { *cx=*cy=*cw=*ch=0; return; }
    *cx = w->x + WM_BORDER;
    *cy = w->y + WM_BORDER + WM_TITLE_HEIGHT;
    *cw = w->w - 2*WM_BORDER;
    *ch = w->h - 2*WM_BORDER - WM_TITLE_HEIGHT;
    if (*cw < 0) *cw = 0;
    if (*ch < 0) *ch = 0;
}

int wm_focused_id() { return s_focused; }
Window* wm_get(int wid) { return find(wid); }

int wm_get_all(Window** out, int max_count) {
    int n = 0;
    // Back-to-front order (z-order)
    for (int z = s_wcount - 1; z >= 0 && n < max_count; z--) {
        Window* w = &s_windows[s_zorder[z]];
        if (w->used) out[n++] = w;
    }
    return n;
}

void wm_invalidate(int wid) {
    Window* w = find(wid);
    if (!w || !w->visible || w->minimized) return;
    draw_chrome(w);
    if (w->on_draw) {
        int cx, cy, cw, ch;
        wm_client_rect(wid, &cx, &cy, &cw, &ch);
        w->on_draw(wid, cx, cy, cw, ch, w->userdata);
    }
}

//  Button hit test helpers 
// Returns: 0=none, 1=close, 2=max, 3=min
static int hit_chrome_btn(Window* w, int mx, int my) {
    int by  = w->y + WM_BORDER + (WM_TITLE_HEIGHT - WM_BTN_SIZE) / 2;
    int bx  = w->x + w->w - WM_BORDER - WM_BTN_SIZE - 4;

    // Close
    if (!(w->flags & WM_NO_CLOSE)) {
        if (mx >= bx && mx < bx + WM_BTN_SIZE &&
            my >= by && my < by + WM_BTN_SIZE) return 1;
        bx -= WM_BTN_SIZE + 4;
    }
    // Max
    if (!(w->flags & WM_NO_MAXIMIZE)) {
        if (mx >= bx && mx < bx + WM_BTN_SIZE &&
            my >= by && my < by + WM_BTN_SIZE) return 2;
        bx -= WM_BTN_SIZE + 4;
    }
    // Min
    if (!(w->flags & WM_NO_MINIMIZE)) {
        if (mx >= bx && mx < bx + WM_BTN_SIZE &&
            my >= by && my < by + WM_BTN_SIZE) return 3;
    }
    return 0;
}

//  Input handling 

bool wm_handle_mouse(int mx, int my, bool left, bool right,
                     bool left_down, bool right_down) {
    bool clicked = (!left) && s_prev_left;  // click = release
    s_prev_left = left;

    // Dragging
    if (s_dragging) {
        if (!left) {
            s_dragging = false;
        } else {
            Window* dw = find(s_drag_id);
            if (dw && !dw->maximized) {
                dw->x = mx - s_drag_ox;
                dw->y = my - s_drag_oy;
                // Keep title bar on screen
                if (dw->y < 0) dw->y = 0;
                if (dw->x < -dw->w + 40) dw->x = -dw->w + 40;
                if (dw->x > (int)FB_WIDTH - 40) dw->x = (int)FB_WIDTH - 40;
            }
        }
        return true;
    }

    // Hittest
    int wid = hit_test(mx, my);
    if (wid < 0) return false;

    Window* w = find(wid);
    if (!w) return false;

    // Focus on press
    if (left && s_focused != wid) {
        wm_focus(wid);
    }

    // Click on chrome
    if (my < w->y + WM_BORDER + WM_TITLE_HEIGHT) {
        if (clicked) {
            int btn = hit_chrome_btn(w, mx, my);
            if (btn == 1) {
                if (w->on_close) w->on_close(wid, w->userdata);
                else             wm_destroy(wid);
            } else if (btn == 2) {
                if (w->maximized) wm_restore(wid);
                else              wm_maximize(wid);
            } else if (btn == 3) {
                wm_minimize(wid);
            } else {
                // Double-click title = maximize/restore (simple: check if no btn)
                // (skip double-click for now)
            }
        }
        // Start drag on title bar press
        if (left && !s_dragging &&
            my >= w->y + WM_BORDER &&
            my < w->y + WM_BORDER + WM_TITLE_HEIGHT &&
            hit_chrome_btn(w, mx, my) == 0) {
            s_dragging = true;
            s_drag_id  = wid;
            s_drag_ox  = mx - w->x;
            s_drag_oy  = my - w->y;
        }
        return true;
    }

    // Pass mouse to client
    if (w->on_mouse) {
        int cx, cy, cw, ch;
        wm_client_rect(wid, &cx, &cy, &cw, &ch);
        w->on_mouse(wid, mx - cx, my - cy, left, right, w->userdata);
    }
    (void)left_down; (void)right_down;
    return true;
}

void wm_handle_key(char key) {
    if (s_focused < 0) return;
    Window* w = find(s_focused);
    if (w && w->on_key) w->on_key(w->id, key, w->userdata);
}

//  Render all 

void wm_render_all() {
    // Back-to-front (last Z index = bottom)
    for (int z = s_wcount - 1; z >= 0; z--) {
        Window* w = &s_windows[s_zorder[z]];
        if (!w->used || !w->visible || w->minimized) continue;
        draw_chrome(w);
        if (w->on_draw) {
            int cx, cy, cw, ch;
            wm_client_rect(w->id, &cx, &cy, &cw, &ch);
            w->on_draw(w->id, cx, cy, cw, ch, w->userdata);
        }
    }
}