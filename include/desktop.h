#pragma once
#include <stdint.h>

// Desktop, icons, taskbar, Start menu
static const int TASKBAR_HEIGHT  = 36;
static const int ICON_SIZE       = 48;
static const int ICON_LABEL_H    = 14;
static const int DESKTOP_ICONS_X = 20;
static const int DESKTOP_ICONS_Y = 20;
static const int ICON_STRIDE_Y   = ICON_SIZE + ICON_LABEL_H + 16;

// Desktop icon descriptor
struct DesktopIcon {
    const char* label;
    uint32_t    color;     // icon background accent
    uint32_t    glyph;     // simple symbol index
    void (*on_click)();
    int  x, y;
    bool hovered;
};

void desktop_init();
void desktop_draw();                 // draw background + icons
void desktop_handle_mouse(int mx, int my, bool clicked);

// Taskbar
void taskbar_draw();
void taskbar_handle_mouse(int mx, int my, bool clicked);

// Start menu (popup)
void startmenu_toggle();
void startmenu_draw();
void startmenu_handle_mouse(int mx, int my, bool clicked);
bool startmenu_visible();

// Launch helpers (called by icon/menu clicks)
void launch_terminal();
void launch_filemanager();
void launch_about();