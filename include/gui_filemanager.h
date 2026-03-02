#pragma once

// GUI file manager window interface

void gui_filemanager_open();  // create/open file manager window
void gui_filemanager_draw(int wid, int cx, int cy, int cw, int ch, void* ud); // render window
void gui_filemanager_key(int wid, char key, void* ud); // keyboard handler
void gui_filemanager_mouse(int wid, int cx, int cy, bool left, bool right, void* ud); // mouse handler
void gui_filemanager_close(int wid, void* ud); // close window