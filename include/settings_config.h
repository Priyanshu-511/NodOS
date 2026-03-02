#pragma once
#include <stdint.h>

// Runtime settings (saved to /etc/settings.cfg)

struct NodSettings {
    uint32_t wp_top;        // wallpaper gradient top
    uint32_t wp_bottom;     // wallpaper gradient bottom
    char     wp_name[64];   // wallpaper name

    uint32_t term_bg;       // terminal background
    uint32_t term_fg;       // terminal text color
    uint32_t term_cursor;   // cursor color

    char     hostname[32];  // shell/terminal prompt name
};

// Global settings instance
extern NodSettings g_settings;

void settings_init();  // load defaults or file
void settings_save();  // save to file
void settings_load();  // reload from file