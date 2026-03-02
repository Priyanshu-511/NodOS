#pragma once

// NodOS Settings window: Wallpaper, Terminal theme, System prompt.
// Applies instantly and saves to /etc/settings.cfg.
void gui_settings_open();

// WM callbacks (registered internally — no need to call directly)
void gui_settings_draw (int wid, int cx, int cy, int cw, int ch, void* ud);
void gui_settings_key  (int wid, char key, void* ud);
void gui_settings_mouse(int wid, int mx, int my, bool left, bool right, void* ud);
void gui_settings_close(int wid, void* ud);