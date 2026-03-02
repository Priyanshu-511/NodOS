#include "../include/desktop.h"
#include "../include/fb.h"
#include "../include/wm.h"
#include "../include/pit.h"
#include "../include/kstring.h"
#include "../include/gui_terminal.h"
#include "../include/gui_filemanager.h"
#include "../include/pmm.h"
#include "../include/vfs.h"
#include "../include/power.h"
#include "../include/gui_settings.h"
#include "../include/settings_config.h"

//  Desktop icons
static void icon_terminal()    { gui_terminal_open();    }
static void icon_filemanager() { gui_filemanager_open(); }
static void icon_settings()    { gui_settings_open();    }
static void icon_about();

// Glyph indices (drawn by draw_icon_glyph)
enum Glyph { GLYPH_TERMINAL = 0, GLYPH_FOLDER = 1, GLYPH_INFO = 2, GLYPH_SETTINGS = 3 };

static DesktopIcon s_icons[] = {
    { "Terminal",     Color::Cyan,    GLYPH_TERMINAL,    icon_terminal,    0, 0, false },
    { "Files",        Color::Yellow,  GLYPH_FOLDER,      icon_filemanager, 0, 0, false },
    { "Settings",     Color::Orange,  GLYPH_SETTINGS,    icon_settings,    0, 0, false },
    { "About",  Color::Magenta, GLYPH_INFO,        icon_about,       0, 0, false },
};
static const int ICON_COUNT = 4;

// About window
static int s_about_wid = -1;

static void about_draw(int, int cx, int cy, int cw, int, void*) {
    fb_fill_gradient(cx, cy, cw, 120, Color::DarkBlue, Color::NavyBlue);
    fb_draw_text(cx + 8,  cy + 8,  "NodOS v4.0  —  Graphical Edition", Color::Cyan);
    fb_draw_text(cx + 8,  cy + 26, "x86 32-bit freestanding kernel",    Color::TextNormal);
    fb_draw_text(cx + 8,  cy + 44, "NodeV scripting engine",            Color::TextNormal);
    fb_draw_text(cx + 8,  cy + 62, "PS/2 keyboard + mouse",             Color::TextNormal);
    fb_draw_text(cx + 8,  cy + 80, "Framebuffer 1024x768 @ 32bpp",      Color::TextNormal);
    fb_draw_text(cx + 8,  cy + 98, "ATA PIO disk / custom VFS",         Color::TextNormal);

    char buf[64];
    k_strcpy(buf, "RAM: ");
    char tmp[16];
    k_itoa((int)pmm_get_total_ram_mb(), tmp, 10);
    k_strcat(buf, tmp);
    k_strcat(buf, " MB total, ");
    k_itoa((int)(pmm_free_pages() * 4 / 1024), tmp, 10);
    k_strcat(buf, tmp);
    k_strcat(buf, " MB free");
    fb_draw_text(cx + 8, cy + 120, buf, Color::Yellow);

    fb_draw_text(cx + 8, cy + 144, "Built with pure C++ + NASM, no STL",Color::TextDim);
}

static void about_close(int wid, void*) { wm_destroy(wid); s_about_wid = -1; }

static void icon_about() {
    if (s_about_wid >= 0) { wm_focus(s_about_wid); return; }
    s_about_wid = wm_create("About", 300, 200, 420, 220);
    wm_set_callbacks(s_about_wid, about_draw, nullptr, about_close);
}

//  Draw icon glyph inside ICON_SIZE × ICON_SIZE box at (ox,oy)
static void draw_icon_glyph(int ox, int oy, uint32_t glyph, uint32_t accent) {
    fb_fill_rect(ox, oy, ICON_SIZE, ICON_SIZE, Color::DarkBlue);
    fb_draw_rect(ox, oy, ICON_SIZE, ICON_SIZE, accent);

    if (glyph == GLYPH_TERMINAL) {
        // Monitor frame
        fb_draw_rect(ox+4,  oy+4,  40, 28, accent);
        fb_fill_rect(ox+5,  oy+5,  38, 26, Color::TermBg);
        // > prompt
        fb_draw_text(ox+8,  oy+11, ">_", Color::TermFg);
        // Stand
        fb_fill_rect(ox+18, oy+32, 12, 4,  accent);
        fb_fill_rect(ox+14, oy+36, 20, 3,  accent);
    } else if (glyph == GLYPH_FOLDER) {
        // Folder shape
        fb_fill_rect(ox+4,  oy+16, 40, 24, accent);
        fb_fill_rect(ox+4,  oy+12, 18, 6,  accent);
        fb_fill_rect(ox+5,  oy+17, 38, 22, 0x334466);
        // Lines
        fb_fill_rect(ox+8,  oy+21, 30, 2,  Color::LightBlue);
        fb_fill_rect(ox+8,  oy+26, 25, 2,  Color::LightBlue);
        fb_fill_rect(ox+8,  oy+31, 20, 2,  Color::LightBlue);
    } else if (glyph == GLYPH_INFO) {
        // Circle
        fb_draw_circle(ox+24, oy+24, 18, accent);
        // i
        fb_draw_text(ox+20, oy+12, "i", accent);
        fb_fill_rect(ox+22, oy+22, 5, 14, accent);
    } else if (glyph == GLYPH_SETTINGS) {
        // Gear: outer ring + inner circle + cross spokes
        fb_draw_circle(ox+24, oy+24, 16, accent);
        fb_fill_circle(ox+24, oy+24, 7,  accent);
        fb_fill_circle(ox+24, oy+24, 4,  Color::DarkBlue);
        // Teeth (8 small rects at cardinal+diagonal)
        fb_fill_rect(ox+21, oy+4,  6, 6, accent);   // top
        fb_fill_rect(ox+21, oy+34, 6, 6, accent);   // bottom
        fb_fill_rect(ox+4,  oy+21, 6, 6, accent);   // left
        fb_fill_rect(ox+34, oy+21, 6, 6, accent);   // right
    }
}

//  Desktop
void desktop_init() {
    // Position icons
    for (int i = 0; i < ICON_COUNT; i++) {
        s_icons[i].x = DESKTOP_ICONS_X;
        s_icons[i].y = DESKTOP_ICONS_Y + i * ICON_STRIDE_Y;
    }
}

void desktop_draw() {
    // Background gradient from settings
    fb_fill_gradient(0, 0, (int)FB_WIDTH, (int)FB_HEIGHT - TASKBAR_HEIGHT,
                     g_settings.wp_top, g_settings.wp_bottom);

    // Subtle grid dots
    for (int y = 24; y < (int)FB_HEIGHT - TASKBAR_HEIGHT; y += 32)
        for (int x = 24; x < (int)FB_WIDTH; x += 32)
            fb_set(x, y, 0x1E3A5F + 0x080808); // slightly lighter

    // Icons
    for (int i = 0; i < ICON_COUNT; i++) {
        DesktopIcon& ic = s_icons[i];
        uint32_t accent = ic.hovered ? Color::White : ic.color;

        // Shadow
        fb_fill_rect_blend(ic.x+4, ic.y+4, ICON_SIZE, ICON_SIZE+ICON_LABEL_H, 0x000000, 80);

        draw_icon_glyph(ic.x, ic.y, ic.glyph, accent);

        // Highlight if hovered
        if (ic.hovered) fb_draw_rect(ic.x, ic.y, ICON_SIZE, ICON_SIZE, Color::White);

        // Label background
        int lw = fb_text_width(ic.label);
        int lx = ic.x + (ICON_SIZE - lw) / 2;
        int ly = ic.y + ICON_SIZE + 2;
        if (ic.hovered)
            fb_fill_rect(lx - 2, ly - 1, lw + 4, 10, Color::Highlight);
        fb_draw_text(lx, ly, ic.label, Color::White);
    }
}

void desktop_handle_mouse(int mx, int my, bool clicked) {
    for (int i = 0; i < ICON_COUNT; i++) {
        DesktopIcon& ic = s_icons[i];
        bool over = (mx >= ic.x && mx < ic.x + ICON_SIZE &&
                     my >= ic.y && my < ic.y + ICON_SIZE);
        ic.hovered = over;
        if (over && clicked && ic.on_click) ic.on_click();
    }
}

//  Taskbar
static bool s_start_open = false;

// Start button rect
static const int SB_X = 4, SB_Y_OFF = 4, SB_W = 80, SB_H = 28;

void taskbar_draw() {
    int bar_y = (int)FB_HEIGHT - TASKBAR_HEIGHT;

    // Bar background
    fb_fill_gradient(0, bar_y, (int)FB_WIDTH, TASKBAR_HEIGHT,
                     Color::Taskbar, 0x0A1525);

    // Top separator line
    fb_fill_rect(0, bar_y, (int)FB_WIDTH, 1, Color::WinBorder);

    // Start button
    bool sb_hover = false; // updated in handle
    fb_fill_gradient(SB_X, bar_y + SB_Y_OFF, SB_W, SB_H,
                     s_start_open ? 0x204080 : 0x1A3560,
                     s_start_open ? 0x102040 : 0x0F1F35);
    fb_draw_rect(SB_X, bar_y + SB_Y_OFF, SB_W, SB_H, Color::WinBorder);
    fb_draw_text(SB_X + 10, bar_y + SB_Y_OFF + 10, "Start", Color::White);

    // Taskbar buttons for all windows via proper wm iterator
    Window* wins[WM_MAX_WINDOWS];
    int     win_count = wm_get_all(wins, WM_MAX_WINDOWS);
    int bx = SB_X + SB_W + 8;
    for (int i = 0; i < win_count && bx < (int)FB_WIDTH - 200; i++) {
        Window* w = wins[i];
        if (w->flags & WM_NO_MINIMIZE) continue;

        int bw = 120, bh = 24;
        int by = bar_y + (TASKBAR_HEIGHT - bh) / 2;
        bool focused   = (w->id == wm_focused_id());
        bool minimized = w->minimized;

        fb_fill_gradient(bx, by, bw, bh,
                         focused ? 0x2A5298 : 0x162040,
                         focused ? 0x1A3268 : 0x0C1428);
        fb_draw_rect(bx, by, bw, bh,
                     minimized ? Color::MidGrey : Color::WinBorder);

        char short_title[15];
        k_strncpy(short_title, w->title, 14);
        short_title[14] = '\0';
        fb_draw_text(bx + 6, by + 8, short_title,
                     minimized ? Color::TextDim : Color::TextNormal);
        bx += bw + 4;
    }

    // Clock (right side)
    uint32_t up = pit_uptime_s();
    uint32_t hh = (up / 3600) % 24;
    uint32_t mm = (up / 60)   % 60;
    uint32_t ss =  up         % 60;
    char clock[12];
    clock[0] = '0' + hh / 10; clock[1] = '0' + hh % 10;
    clock[2] = ':';
    clock[3] = '0' + mm / 10; clock[4] = '0' + mm % 10;
    clock[5] = ':';
    clock[6] = '0' + ss / 10; clock[7] = '0' + ss % 10;
    clock[8] = '\0';
    fb_draw_text((int)FB_WIDTH - 70, bar_y + 14, clock, Color::TextNormal);

    // System info
    char sysinfo[32];
    uint32_t free_mb = pmm_free_pages() * 4 / 1024;
    k_itoa((int)free_mb, sysinfo, 10);
    k_strcat(sysinfo, "MB");
    fb_draw_text((int)FB_WIDTH - 130, bar_y + 14, sysinfo, Color::TextDim);
}

void taskbar_handle_mouse(int mx, int my, bool clicked) {
    int bar_y = (int)FB_HEIGHT - TASKBAR_HEIGHT;
    if (my < bar_y) return;

    // Start button
    if (mx >= SB_X && mx < SB_X + SB_W &&
        my >= bar_y + SB_Y_OFF && my < bar_y + SB_Y_OFF + SB_H) {
        if (clicked) startmenu_toggle();
        return;
    }

    // Taskbar buttons — use proper iterator
    Window* wins[WM_MAX_WINDOWS];
    int     win_count = wm_get_all(wins, WM_MAX_WINDOWS);
    int bx = SB_X + SB_W + 8;
    for (int i = 0; i < win_count && bx < (int)FB_WIDTH - 200; i++) {
        Window* w = wins[i];
        if (w->flags & WM_NO_MINIMIZE) continue;
        int bw = 120, bh = 24;
        int by = bar_y + (TASKBAR_HEIGHT - bh) / 2;
        if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
            if (clicked) {
                if (w->minimized) {
                    wm_restore(w->id);
                    wm_focus(w->id);
                } else if (wm_focused_id() == w->id) {
                    wm_minimize(w->id);
                } else {
                    wm_focus(w->id);
                }
            }
            return;
        }
        bx += bw + 4;
    }
}

//  Start Menu
struct MenuItem {
    const char* label;
    uint32_t    icon_color;
    void (*action)();
};

static void action_terminal()    { gui_terminal_open();    startmenu_toggle(); }
static void action_filemanager() { gui_filemanager_open(); startmenu_toggle(); }
static void action_settings()    { gui_settings_open();    startmenu_toggle(); }
static void action_about()       { icon_about();           startmenu_toggle(); }
static void action_shutdown() {
    system_shutdown();
}
static void action_reboot() {
    system_reboot();
}

static MenuItem s_menu_items[] = {
    { "Terminal",      Color::Cyan,    action_terminal    },
    { "File Manager",  Color::Yellow,  action_filemanager },
    { "Settings",      Color::Orange,  action_settings    },
    { "About",   Color::Magenta, action_about       },
    { "---",           0,              nullptr            },
    { "Shutdown",      Color::Red,     action_shutdown    },
    { "Reboot",        Color::Orange,  action_reboot      },
};
static const int MENU_ITEM_COUNT = 7;
static int s_menu_hovered = -1;

static const int SM_W  = 200;
static const int SM_IH = 30;    // item height
static const int SM_HDR= 36;    // header height

void startmenu_toggle() { s_start_open = !s_start_open; s_menu_hovered = -1; }
bool startmenu_visible() { return s_start_open; }

void startmenu_draw() {
    if (!s_start_open) return;
    int bar_y = (int)FB_HEIGHT - TASKBAR_HEIGHT;
    int sm_h  = SM_HDR + MENU_ITEM_COUNT * SM_IH + 4;
    int sm_x  = SB_X;
    int sm_y  = bar_y - sm_h;

    // Shadow
    fb_fill_rect_blend(sm_x + 4, sm_y + 4, SM_W, sm_h, 0x000000, 120);

    // Panel
    fb_fill_gradient(sm_x, sm_y, SM_W, sm_h, 0x0F1F35, 0x0A1525);
    fb_draw_rect_thick(sm_x, sm_y, SM_W, sm_h, Color::WinBorder, 1);

    // Header
    fb_fill_gradient(sm_x, sm_y, SM_W, SM_HDR, Color::WinTitle, Color::DarkBlue);
    fb_draw_text(sm_x + 8, sm_y + 8,  "NodOS",  Color::White);
    fb_draw_text(sm_x + 8, sm_y + 20, "v4.0",   Color::TextDim);

    // Items
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {
        int iy = sm_y + SM_HDR + i * SM_IH;
        MenuItem& item = s_menu_items[i];

        if (k_strcmp(item.label, "---") == 0) {
            fb_fill_rect(sm_x + 4, iy + SM_IH/2, SM_W - 8, 1, Color::MidGrey);
            continue;
        }

        bool hov = (i == s_menu_hovered);
        if (hov) fb_fill_rect(sm_x + 1, iy, SM_W - 2, SM_IH, Color::Highlight);

        // Color dot
        fb_fill_circle(sm_x + 14, iy + SM_IH/2, 5, item.icon_color);
        fb_draw_text(sm_x + 26, iy + (SM_IH - 8) / 2, item.label,
                     hov ? Color::White : Color::TextNormal);
    }
}

void startmenu_handle_mouse(int mx, int my, bool clicked) {
    if (!s_start_open) return;
    int bar_y = (int)FB_HEIGHT - TASKBAR_HEIGHT;
    int sm_h  = SM_HDR + MENU_ITEM_COUNT * SM_IH + 4;
    int sm_x  = SB_X;
    int sm_y  = bar_y - sm_h;

    if (mx < sm_x || mx >= sm_x + SM_W || my < sm_y || my >= bar_y) {
        if (clicked) s_start_open = false;
        s_menu_hovered = -1;
        return;
    }

    int rel_y = my - (sm_y + SM_HDR);
    if (rel_y < 0) { s_menu_hovered = -1; return; }

    int idx = rel_y / SM_IH;
    if (idx >= MENU_ITEM_COUNT) { s_menu_hovered = -1; return; }
    s_menu_hovered = idx;

    if (clicked && s_menu_items[idx].action) {
        s_menu_items[idx].action();
    }
}

// Stubs for launch_ functions declared in header
void launch_terminal()    { gui_terminal_open(); }
void launch_filemanager() { gui_filemanager_open(); }
void launch_about()       { icon_about(); }